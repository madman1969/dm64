/*
** magic.c - what spells leave behind them: projectiles flying through the dungeon, and the
** shield and invisibility running down. Casting them is in cast.c.
**
** Projectiles fly a square a tick from the party and burst on the first creature, wall or door
** in their way.
*/

#include <stdlib.h>
#include "dm.h"

proj_t proj[MAXPROJ];
unsigned char nproj;
unsigned int shield_ticks, invisible_ticks;
unsigned char shield;

/* Projectile types, as the spell table numbers them. */
enum { P_FIREBALL = 0, P_LIGHTNING = 2, P_HARM_NONMATERIAL = 3, P_OPEN_DOOR = 4,
       P_POISON_BOLT = 6, P_POISON_CLOUD = 7 };

static const unsigned char proj_colours[8] = { 8, 8, 7, 1, 14, 8, 5, 13 };

unsigned char projectile_colour (unsigned char p)
{
    return proj_colours[proj[p].type & 7];
}

signed char projectile_at (unsigned char x, unsigned char y)
{
    unsigned char p;

    for (p = 0; p < nproj; ++p) {
        if (proj[p].lev == lev && proj[p].x == x && proj[p].y == y) {
            return p;
        }
    }
    return -1;
}

/* ---- projectiles ----------------------------------------------------------------------------- */

static void burst (unsigned char p, unsigned char x, unsigned char y)
{
    proj_t* q = &proj[p];
    signed char a = creature_at (x, y);
    unsigned char s = sq_at (x, y), type = q->type, nonmaterial;
    unsigned int t;
    int dmg = ((q->strength * (q->power + 2)) >> 2) + (rand () & 7);

    sfx (SFX_EXPLOSION);
    if (a >= 0) {
        nonmaterial = creature_info[th_creature[TH_INDEX (active[a].thing)][2] >> 8].flags
                      & CR_NON_MATERIAL;
        if (type == P_OPEN_DOOR || (type == P_HARM_NONMATERIAL && !nonmaterial) ||
            ((type == P_POISON_BOLT || type == P_POISON_CLOUD) && nonmaterial)) {
            msg ("THE SPELL HAS NO EFFECT");
        } else {
            msg ("THE SPELL STRIKES!");
            hit_creature (a, dmg);
        }
    } else if (SQ_TYPE (s) == E_DOOR) {
        if (type == P_OPEN_DOOR) {
            effect_at (x, y, 0, EFF_SET);
        } else if (type == P_FIREBALL) {
            for (t = first_thing (x, y); t != END; t = NEXT (t)) {
                if (TH_TYPE (t) == T_DOOR && (thing_ptr (t)[1] & 0x80)) {
                    *sq_ptr (x, y) = (s & ~DOOR_STATE) | 5;
                    msg ("THE DOOR IS BLASTED APART!");
                }
            }
        }
    }
    proj[p] = proj[--nproj];
    redraw |= REDRAW_VIEW;
}

/* Moves projectile p on a square; 0 if it's gone. */
static unsigned char fly (unsigned char p)
{
    proj_t* q = &proj[p];
    unsigned char nx = q->x + dir_dx[q->dir], ny = q->y + dir_dy[q->dir];

    if (creature_at (nx, ny) >= 0 || !passable (nx, ny)) {
        burst (p, nx, ny);
        return 0;
    }
    q->x = nx;
    q->y = ny;
    if (!--q->range) {
        proj[p] = proj[--nproj];
        return 0;
    }
    return 1;
}

void launch (unsigned char type, unsigned char power, unsigned char strength)
{
    proj_t* q;

    if (nproj == MAXPROJ) {
        return;
    }
    q = &proj[nproj++];
    q->lev = lev;
    q->x = px;
    q->y = py;
    q->dir = pdir;
    q->type = type;
    q->power = power;
    q->strength = strength;
    q->range = 4 + power * 2;
    sfx (type == P_FIREBALL ? SFX_FIREBALL : SFX_SPELL);
    fly (nproj - 1);
    redraw |= REDRAW_VIEW;
}

void magic_tick (void)
{
    unsigned char p = 0;

    if (shield_ticks && !--shield_ticks) {
        shield = 0;
        msg ("THE SHIELD FADES");
    }
    if (invisible_ticks && !--invisible_ticks) {
        msg ("THE PARTY IS VISIBLE AGAIN");
    }
    while (p < nproj) {
        if (proj[p].lev != lev) {
            proj[p] = proj[--nproj];        /* left behind on another level */
        } else if (fly (p)) {
            ++p;
        }
        redraw |= REDRAW_VIEW;
    }
}
