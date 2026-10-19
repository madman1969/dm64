/*
** creature.c - the creatures on the party's level.
**
** A creature thing is a group of one to four creatures of one type on one square, with a
** health for each. On entering a level its groups are listed in active[], with where they are,
** and they chase and attack the party at the speeds Dungeon Master's own creature table gives.
*/

#include <stdlib.h>
#include "dm.h"

active_t active[MAXACTIVE];
unsigned char nactive;

#define GROUP(a)   (th_creature[TH_INDEX (active[a].thing)])
#define CTYPE(r)   ((unsigned char) ((r)[2] >> 8))
#define COUNT(r)   ((unsigned char) (((r)[7] >> 5) & 3) + 1)

void creatures_scan (void)
{
    unsigned char x, y;
    unsigned int t;

    nactive = 0;
    for (x = 0; x < lw; ++x) {
        for (y = 0; y < lh; ++y) {
            if (!(*sq_ptr (x, y) & SQ_THINGS)) {
                continue;
            }
            for (t = first_thing (x, y); t != END; t = NEXT (t)) {
                if (TH_TYPE (t) == T_CREATURE && nactive < MAXACTIVE) {
                    active[nactive].thing = t;
                    active[nactive].x = x;
                    active[nactive].y = y;
                    active[nactive].timer = 1 + (rand () & 15);
                    active[nactive].atk = 1 + (rand () & 15);
                    ++nactive;
                }
            }
        }
    }
}

signed char creature_at (unsigned char x, unsigned char y)
{
    unsigned char a;

    for (a = 0; a < nactive; ++a) {
        if (active[a].x == x && active[a].y == y) {
            return a;
        }
    }
    return -1;
}

static void remove_active (unsigned char a)
{
    active[a] = active[--nactive];
}

/* A blow lands on the group: it hits the first creature, which dies at zero health. */
void hit_creature (signed char a, int dmg)
{
    unsigned int* r = GROUP (a);
    unsigned char n = COUNT (r), i, x, y;
    unsigned int t;

    if ((int) r[3] > dmg) {
        r[3] -= dmg;
        return;
    }
    msg (creature_names[CTYPE (r)]);
    msg ("IS SLAIN!");
    for (i = 0; i < 3; ++i) {
        r[3 + i] = r[4 + i];
    }
    if (--n) {
        r[7] = (r[7] & ~0x60) | ((n - 1) << 5);
        return;
    }
    /* The last of the group: it drops what it carried. */
    x = active[a].x;
    y = active[a].y;
    list_remove (x, y, active[a].thing);
    t = r[1];
    while (t != END && t != NONE) {
        unsigned int next = NEXT (t);
        list_add (x, y, t);
        t = next;
    }
    r[1] = END;
    remove_active (a);
    redraw |= REDRAW_VIEW;
}

static signed char sign (signed char v)
{
    return v > 0 ? 1 : v < 0 ? -1 : 0;
}

static unsigned char can_enter (unsigned char x, unsigned char y)
{
    unsigned char s = sq_at (x, y), t = SQ_TYPE (s);

    if (!passable (x, y) || t == E_STAIRS || (t == E_PIT && (s & PIT_OPEN))) {
        return 0;
    }
    return (x != px || y != py) && creature_at (x, y) < 0;
}

static void step (unsigned char a, signed char dx, signed char dy)
{
    unsigned char nx = active[a].x + dx, ny = active[a].y + dy;

    if (!can_enter (nx, ny)) {
        return;
    }
    list_remove (active[a].x, active[a].y, active[a].thing);
    list_add (nx, ny, active[a].thing);
    active[a].x = nx;
    active[a].y = ny;
    redraw |= REDRAW_VIEW;
}

void creatures_tick (void)
{
    unsigned char a, who;
    unsigned int* r;
    const creature_info_t* ci;
    signed char dx, dy, adx, ady;
    int dmg;

    for (a = 0; a < nactive; ++a) {
        r = GROUP (a);
        ci = &creature_info[CTYPE (r)];
        dx = px - active[a].x;
        dy = py - active[a].y;
        adx = dx < 0 ? -dx : dx;
        ady = dy < 0 ? -dy : dy;
        if (adx + ady == 1) {
            /* Next to the party: attack, more often for a bigger group. */
            if (active[a].atk > COUNT (r)) {
                active[a].atk -= COUNT (r);
                continue;
            }
            active[a].atk = ci->attack_ticks + 4;
            if (!party_alive ()) {
                continue;
            }
            do {
                who = rand () & 3;
            } while (who >= nchamps || champ[who].hp <= 0);
            dmg = ci->attack / 8 + (rand () % (ci->attack / 8 + 2)) + 1;
            damage_champion (who, dmg);
            continue;
        }
        if (adx > 7 || ady > 7 || ci->move_ticks == 255) {
            continue;
        }
        if (--active[a].timer) {
            continue;
        }
        active[a].timer = (ci->move_ticks >> 1) + 3;
        if (adx >= ady) {
            step (a, sign (dx), 0);
            if (active[a].x == px - dx) {
                step (a, 0, sign (dy));
            }
        } else {
            step (a, 0, sign (dy));
            if (active[a].y == py - dy) {
                step (a, sign (dx), 0);
            }
        }
    }
}
