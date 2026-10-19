/*
** sensor.c - how the dungeon reacts: sensors, doors, pits, stairs and teleporters, and moving
** the party through them.
**
** A sensor sits on a floor square (a pressure plate) or on one side of a wall (a switch,
** keyhole or mirror). When it's triggered it sends an effect - set, clear or toggle - to its
** target square, perhaps after a delay: set opens a door, pit or teleporter, or shows a text, and
** clear closes or hides it. A "hold" sensor sets while it's pressed and clears when let go.
*/

#include <stdlib.h>
#include "dm.h"

enum { EV_DOOR, EV_EFFECT };

event_t events[MAXEVENTS];
unsigned char nevents;
static unsigned char depth_guard;       /* stops teleporters that lead into each other */

static void schedule (unsigned char kind, unsigned char delay, unsigned char x, unsigned char y,
                      unsigned char cell, unsigned char effect)
{
    event_t* e;

    if (nevents == MAXEVENTS) {
        return;
    }
    e = &events[nevents++];
    e->time = game_time + delay;
    e->kind = kind;
    e->lev = lev;
    e->x = x;
    e->y = y;
    e->cell = cell;
    e->effect = effect;
}

static unsigned char apply_bit (unsigned char* s, unsigned char bit, unsigned char effect)
{
    switch (effect) {
    case EFF_SET:   *s |= bit;  break;
    case EFF_CLEAR: *s &= ~bit; break;
    default:        *s ^= bit;  break;
    }
    return *s & bit;
}

static void trigger (unsigned int* r, unsigned char pressed);

/* Effects reaching a wall square go to the texts and logic sensors on that side of it. */
static void wall_effect (unsigned char x, unsigned char y, unsigned char cell, unsigned char effect)
{
    unsigned int t, *r;
    unsigned char bit, type;

    for (t = first_thing (x, y); t != END; t = NEXT (t)) {
        if (TH_CELL (t) != cell) {
            continue;
        }
        r = thing_ptr (t);
        type = TH_TYPE (t);
        if (type == T_TEXT) {
            if (effect == EFF_TOGGLE) {
                r[1] ^= 1;
            } else {
                r[1] = (r[1] & ~1) | (effect == EFF_SET);
            }
        } else if (type == T_SENSOR) {
            switch (SENS_TYPE (r)) {
            case 5:
                /* AND/OR gate: data bits 0-3 are the inputs (one per cell), bits 4-7 the pattern
                ** that fires it. */
                bit = 1 << cell;
                if (effect == EFF_SET) {
                    r[1] |= bit << 7;
                } else if (effect == EFF_CLEAR) {
                    r[1] &= ~(bit << 7);
                } else {
                    r[1] ^= bit << 7;
                }
                trigger (r, (SENS_DATA (r) & 15) == ((SENS_DATA (r) >> 4) & 15));
                break;
            case 6:
                /* Countdown: fires when its count reaches zero. */
                if (effect != EFF_CLEAR && SENS_DATA (r)) {
                    r[1] -= 0x80;
                    if (!SENS_DATA (r)) {
                        trigger (r, 1);
                    }
                }
                break;
            }
        }
    }
}

static void floor_text_effect (unsigned char x, unsigned char y, unsigned char effect)
{
    unsigned int t, *r;

    for (t = first_thing (x, y); t != END; t = NEXT (t)) {
        if (TH_TYPE (t) == T_TEXT) {
            r = thing_ptr (t);
            if (effect == EFF_TOGGLE) {
                r[1] ^= 1;
            } else {
                r[1] = (r[1] & ~1) | (effect == EFF_SET);
            }
        }
    }
}

/* An effect arrives at square (x, y) of the current level. */
static void apply_effect (unsigned char x, unsigned char y, unsigned char cell, unsigned char effect)
{
    unsigned char* s;
    unsigned char state;

    if (x >= lw || y >= lh) {
        return;
    }
    s = sq_ptr (x, y);
    switch (SQ_TYPE (*s)) {
    case E_DOOR:
        state = *s & DOOR_STATE;
        if (state == 5) {
            break;
        }
        if (effect == EFF_TOGGLE) {
            effect = state == 0 ? EFF_CLEAR : EFF_SET;
        }
        schedule (EV_DOOR, 1, x, y, 0, effect);
        break;
    case E_PIT:
        apply_bit (s, PIT_OPEN, effect);
        break;
    case E_TELE:
        apply_bit (s, TELE_OPEN, effect);
        break;
    case E_FAKE:
        apply_bit (s, FAKE_OPEN, effect);
        break;
    case E_WALL:
        wall_effect (x, y, cell, effect);
        break;
    default:
        floor_text_effect (x, y, effect);
        break;
    }
    redraw |= REDRAW_VIEW;
}

static void trigger (unsigned int* r, unsigned char pressed)
{
    unsigned char effect = SENS_EFFECT (r);

    if (effect == EFF_HOLD) {
        effect = pressed ? EFF_SET : EFF_CLEAR;
    } else if (!pressed) {
        return;
    }
    if (r[2] & SENS_REVERT) {
        if (effect == EFF_SET) {
            effect = EFF_CLEAR;
        } else if (effect == EFF_CLEAR) {
            effect = EFF_SET;
        }
    }
    if (r[2] & SENS_AUDIBLE) {
        SND_CLICK ();
    }
    if (!(r[2] & SENS_LOCAL)) {
        if (SENS_DELAY (r)) {
            schedule (EV_EFFECT, SENS_DELAY (r) * 2, SENS_X (r), SENS_Y (r), SENS_CELL (r), effect);
        } else {
            apply_effect (SENS_X (r), SENS_Y (r), SENS_CELL (r), effect);
        }
    }
    if (r[2] & SENS_ONCE) {
        r[1] &= 0xFF80;         /* now a disabled sensor */
    }
}

void events_tick (void)
{
    unsigned char i = 0, here = lev, state;
    unsigned char* s;
    event_t* e;

    while (i < nevents) {
        e = &events[i];
        if ((int) (game_time - e->time) < 0) {
            ++i;
            continue;
        }
        if (e->lev != here) {
            level_enter (e->lev);
        }
        if (e->kind == EV_EFFECT) {
            apply_effect (e->x, e->y, e->cell, e->effect);
            *e = events[--nevents];
        } else {
            /* Doors move a quarter of the way every two ticks. A door won't close on the party. */
            s = sq_ptr (e->x, e->y);
            state = *s & DOOR_STATE;
            if (e->effect == EFF_SET ? state == 0 :
                (state >= 4 || (e->lev == here && e->x == px && e->y == py))) {
                *e = events[--nevents];
            } else {
                *s = (*s & ~DOOR_STATE) | (e->effect == EFF_SET ? state - 1 : state + 1);
                e->time = game_time + 2;
                if (e->lev == here) {
                    redraw |= REDRAW_VIEW;
                    if ((*s & DOOR_STATE) == 2) {
                        SND_DOOR ();
                    }
                }
                ++i;
            }
        }
        if (lev != here) {
            level_enter (here);
        }
    }
}

void sensors_init (void)
{
    nevents = 0;
}

/* ---- the party moving ------------------------------------------------------------------------ */

static unsigned char find_level (unsigned char depth)
{
    unsigned char i;

    for (i = 0; i < NLEVELS; ++i) {
        if (levels[i].depth == depth) {
            return i;
        }
    }
    return 255;
}

/* Moves the party to the same place in the dungeon on another level. */
static unsigned char change_level (unsigned char l)
{
    signed int x = px + levels[lev].ox - levels[l].ox;
    signed int y = py + levels[lev].oy - levels[l].oy;

    if (x < 0 || y < 0 || x >= levels[l].w || y >= levels[l].h) {
        return 0;
    }
    level_enter (l);
    px = (unsigned char) x;
    py = (unsigned char) y;
    creatures_scan ();
    redraw = REDRAW_VIEW | REDRAW_PANEL;
    return 1;
}

static void stairs (void)
{
    unsigned char s = sq_at (px, py), d, l;

    l = find_level (levels[lev].depth + ((s & STAIRS_UP) ? -1 : 1));
    if (l == 255 || !change_level (l)) {
        return;
    }
    /* Step off the stairs at the other end, along the way they run. */
    s = sq_at (px, py);
    for (d = (s & STAIRS_NS) ? 0 : 1; d < 4; d += 2) {
        if (passable (px + dir_dx[d], py + dir_dy[d]) &&
            SQ_TYPE (sq_at (px + dir_dx[d], py + dir_dy[d])) != E_STAIRS) {
            pdir = d;
            px += dir_dx[d];
            py += dir_dy[d];
            break;
        }
    }
    msg ((sq_at (px - dir_dx[pdir], py - dir_dy[pdir]) & STAIRS_UP) ? "DOWN THE STAIRS" : "UP THE STAIRS");
    party_entered ();
}

void party_entered (void)
{
    unsigned int t, *r;
    unsigned char s, i, type, l;

    if (++depth_guard > 4) {
        --depth_guard;
        return;
    }
    redraw = REDRAW_VIEW | REDRAW_PANEL;
    for (t = first_thing (px, py); t != END; t = NEXT (t)) {
        r = thing_ptr (t);
        type = TH_TYPE (t);
        if (type == T_SENSOR) {
            switch (SENS_TYPE (r)) {
            case 3:
                if (SENS_DATA (r) && SENS_DATA (r) - 1 != pdir) {
                    break;
                }
                /* fall through */
            case 1: case 2:
                trigger (r, 1);
                break;
            case 8:
                if (party_has_icon ((unsigned char) SENS_DATA (r))) {
                    trigger (r, 1);
                }
                break;
            }
        } else if (type == T_TEXT && (r[1] & 1)) {
            msg_text (text_decode (r[1] >> 3));
        }
    }

    s = sq_at (px, py);
    switch (SQ_TYPE (s)) {
    case E_PIT:
        if ((s & PIT_OPEN) && !(s & PIT_IMAGINARY)) {
            l = find_level (levels[lev].depth + 1);
            if (l != 255 && change_level (l)) {
                msg ("THE PARTY FALLS!");
                SND_HURT ();
                for (i = 0; i < nchamps; ++i) {
                    damage_champion (i, 2 + (rand () & 7));
                }
                party_entered ();
            }
        }
        break;
    case E_STAIRS:
        stairs ();
        break;
    case E_TELE:
        if (!(s & TELE_OPEN)) {
            break;
        }
        for (t = first_thing (px, py); t != END; t = NEXT (t)) {
            if (TH_TYPE (t) != T_TELE) {
                continue;
            }
            r = thing_ptr (t);
            if (!(r[1] & 0x4000)) {             /* not for the party */
                break;
            }
            l = r[2] >> 8;
            if (l != lev) {
                level_enter (l);
                creatures_scan ();
            }
            px = r[1] & 31;
            py = (r[1] >> 5) & 31;
            pdir = (r[1] & 0x1000) ? (r[1] >> 10) & 3 : (pdir + (r[1] >> 10)) & 3;
            if (r[1] & 0x8000) {
                SND_DOOR ();
            }
            party_entered ();
            break;
        }
        break;
    }
    --depth_guard;
}

void party_left (unsigned char x, unsigned char y)
{
    unsigned int t, *r;
    unsigned char st;

    for (t = first_thing (x, y); t != END; t = NEXT (t)) {
        if (TH_TYPE (t) == T_SENSOR) {
            r = thing_ptr (t);
            st = SENS_TYPE (r);
            if (st >= 1 && st <= 3) {
                trigger (r, 0);
            }
        }
    }
}

void floor_object (unsigned char x, unsigned char y, unsigned int obj, unsigned char added)
{
    unsigned int t, *r;
    unsigned char st;

    for (t = first_thing (x, y); t != END; t = NEXT (t)) {
        if (TH_TYPE (t) == T_SENSOR) {
            r = thing_ptr (t);
            st = SENS_TYPE (r);
            if (st == 1 || (st == 4 && SENS_DATA (r) == obj_icon_of (obj))) {
                trigger (r, added);
            }
        }
    }
}

void move_party (signed char fwd, signed char side)
{
    unsigned char d, nx, ny, ox = px, oy = py;

    d = fwd > 0 ? pdir : fwd < 0 ? (pdir + 2) & 3 : side > 0 ? (pdir + 1) & 3 : (pdir + 3) & 3;
    nx = px + dir_dx[d];
    ny = py + dir_dy[d];
    if (!passable (nx, ny) || creature_at (nx, ny) >= 0) {
        SND_STEP ();
        return;
    }
    px = nx;
    py = ny;
    party_left (ox, oy);
    party_entered ();
}

void turn_party (signed char by)
{
    pdir = (pdir + by) & 3;
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}

/* ---- clicking ------------------------------------------------------------------------------- */

static unsigned char ahead_x, ahead_y;

static void look_ahead (void)
{
    ahead_x = px + dir_dx[pdir];
    ahead_y = py + dir_dy[pdir];
}

/* Is the side of the wall ahead an alcove, that objects can be put in? */
static unsigned char is_alcove (void)
{
    unsigned int t, *r;
    unsigned char orn;

    for (t = first_thing (ahead_x, ahead_y); t != END; t = NEXT (t)) {
        if (TH_CELL (t) == face_cell () && TH_TYPE (t) == T_SENSOR) {
            r = thing_ptr (t);
            orn = SENS_ORNAMENT (r);
            if (orn && wall_ornaments[lev][orn - 1] >= 1 && wall_ornaments[lev][orn - 1] <= 3) {
                return 1;
            }
        }
    }
    return 0;
}

static unsigned int wall_object (void)
{
    unsigned int t;

    for (t = first_thing (ahead_x, ahead_y); t != END; t = NEXT (t)) {
        if (TH_CELL (t) == face_cell () && TH_TYPE (t) >= T_WEAPON) {
            return t;
        }
    }
    return NONE;
}

static unsigned char wall_ahead (void)
{
    unsigned char s;

    look_ahead ();
    s = sq_at (ahead_x, ahead_y);
    return SQ_TYPE (s) == E_WALL || (SQ_TYPE (s) == E_FAKE && !(s & FAKE_OPEN));
}

void click_wall (void)
{
    unsigned int t, *r;
    unsigned char st, icon, done = 0;

    if (!wall_ahead ()) {
        click_door ();
        return;
    }
    icon = obj_icon_of (held);
    for (t = first_thing (ahead_x, ahead_y); t != END; t = NEXT (t)) {
        if (TH_CELL (t) != face_cell () || TH_TYPE (t) != T_SENSOR) {
            continue;
        }
        r = thing_ptr (t);
        st = SENS_TYPE (r);
        switch (st) {
        case 127:
            recruit ();
            return;
        case 1:
            trigger (r, 1);
            done = 1;
            break;
        case 2:
            if (held != NONE) {
                trigger (r, 1);
                done = 1;
            }
            break;
        case 3:
            if (held != NONE && SENS_DATA (r) == icon) {
                trigger (r, 1);
                done = 1;
            }
            break;
        case 4: case 12: case 16:
            if (held != NONE && SENS_DATA (r) == icon) {
                pool_free (held);               /* the lock keeps the key */
                held = NONE;
                trigger (r, 1);
                if (st == 16) {
                    r[1] &= 0xFF80;
                }
                done = 1;
                redraw |= REDRAW_PANEL;
            }
            break;
        case 17:
            msg ("THE DUNGEON IS AT PEACE. YOU HAVE WON!");
            done = 1;
            break;
        }
    }
    if (done) {
        SND_CLICK ();
        return;
    }
    if (held == NONE) {
        take_object ();
    } else {
        drop_object ();
    }
}

void click_door (void)
{
    unsigned int t;
    unsigned char s;

    look_ahead ();
    s = sq_at (ahead_x, ahead_y);
    if (SQ_TYPE (s) != E_DOOR) {
        return;
    }
    for (t = first_thing (ahead_x, ahead_y); t != END; t = NEXT (t)) {
        if (TH_TYPE (t) == T_DOOR && (thing_ptr (t)[1] & 0x40)) {
            SND_CLICK ();
            apply_effect (ahead_x, ahead_y, 0, EFF_TOGGLE);
            return;
        }
    }
    msg ("THIS DOOR HAS NO BUTTON");
}

void take_object (void)
{
    unsigned int t;

    if (held != NONE) {
        msg ("YOUR HAND IS FULL");
        return;
    }
    if (wall_ahead () && (t = wall_object ()) != NONE) {
        list_remove (ahead_x, ahead_y, t);
    } else {
        for (t = first_thing (px, py); t != END && TH_TYPE (t) < T_WEAPON; t = NEXT (t)) {
        }
        if (t == END) {
            msg ("NOTHING TO TAKE");
            return;
        }
        list_remove (px, py, t);
        floor_object (px, py, t, 0);
    }
    held = to_party (t);
    if (held == NONE) {
        msg ("YOU CAN'T CARRY ANY MORE");
        list_add (px, py, t);
        return;
    }
    msg (obj_name (held));
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}

void drop_object (void)
{
    unsigned int t;

    if (held == NONE) {
        msg ("YOUR HAND IS EMPTY");
        return;
    }
    t = to_level (held);
    if (t == NONE) {
        msg ("THERE'S NO ROOM HERE");
        return;
    }
    if (wall_ahead () && is_alcove ()) {
        t |= (unsigned int) face_cell () << 14;
        list_add (ahead_x, ahead_y, t);
    } else {
        t |= (unsigned int) pdir << 14;
        list_add (px, py, t);
        floor_object (px, py, t, 1);
    }
    held = NONE;
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}
