/*
** magic.c - casting spells, from Dungeon Master's own spell table (GRAPHICS.DAT item 560).
**
** A spell is a power rune (LO to MON) followed by one to three more, one from each of the
** element, form and class rows in turn. Each rune costs mana as it's chosen: the power rune its
** base cost, the others their base cost times the power's multiplier (in eighths). The spell
** needs a skill level of its own level plus the power, in priest or wizard skill; each level
** short is a chance of failing, less likely the wiser the caster.
**
** Projectiles fly a square a tick from the party and burst on the first creature, wall or door
** in their way. Potions are cast into an empty flask.
*/

#include <stdlib.h>
#include <string.h>
#include "dm.h"

proj_t proj[MAXPROJ];
unsigned char nproj;
unsigned int shield_ticks, invisible_ticks;
unsigned char shield;

static const char* const rune_names[24] = {
    "LO", "UM", "ON", "EE", "PAL", "MON",
    "YA", "VI", "OH", "FUL", "DES", "ZO",
    "VEN", "EW", "KATH", "IR", "BRO", "GOR",
    "KU", "ROS", "DAIN", "NETA", "RA", "SAR"
};
static const char* const row_names[4] = { "POWER", "ELEMENT", "FORM", "CLASS" };

/* Projectile types, as the spell table numbers them. */
enum { P_FIREBALL = 0, P_LIGHTNING = 2, P_HARM_NONMATERIAL = 3, P_OPEN_DOOR = 4,
       P_POISON_BOLT = 6, P_POISON_CLOUD = 7 };
/* Other spells. */
enum { S_LIGHT, S_DARKNESS, S_SEE_WALLS, S_INVISIBLE, S_SHIELD, S_TORCH, S_FOOTPRINTS,
       S_ZOKATHRA, S_FIRE_SHIELD };

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

    SND_HIT ();
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

static void launch (unsigned char type, unsigned char power, unsigned char strength)
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

/* ---- casting --------------------------------------------------------------------------------- */

static unsigned char runes[4], nrunes;
static unsigned char cost[4];
static char line[41];

static void add (const char* s)
{
    strncat (line, s, sizeof line - 1 - strlen (line));
}

static void show (champion_t* c)
{
    unsigned char i;
    char num[4];

    ui_clear (0, 22, 40, 3);
    line[0] = 0;
    add (c->name);
    add (":");
    for (i = 0; i < nrunes; ++i) {
        add (" ");
        add (rune_names[runes[i]]);
    }
    add ("  MANA ");
    num[0] = '0' + c->mana / 100 % 10;
    num[1] = '0' + c->mana / 10 % 10;
    num[2] = '0' + c->mana % 10;
    num[3] = 0;
    add (num[0] == '0' ? (num[1] == '0' ? num + 2 : num + 1) : num);
    ui_print (0, 22, 13, line);
    line[0] = 0;
    if (nrunes < 4) {
        add (row_names[nrunes]);
        add (":");
        for (i = 0; i < 6; ++i) {
            num[0] = ' ';
            num[1] = '1' + i;
            num[2] = 0;
            add (num);
            add (rune_names[nrunes * 6 + i]);
        }
    }
    ui_print (0, 23, 7, line);
    ui_print (0, 24, 15, nrunes > 1 ? "RETURN CAST  DEL BACK  SPACE CANCEL" : "DEL BACK  SPACE CANCEL");
}

static unsigned char skill_of (champion_t* c, unsigned char skill)
{
    return c->skill[(skill == 2 || (skill >= 12 && skill <= 15)) ? 2 : 3];
}

static void cast (champion_t* c)
{
    const spell_t* sp;
    unsigned char i, power = runes[0], missing, need, wis;
    unsigned int* r;

    for (i = 0, sp = spells; i < NSPELLS; ++i, ++sp) {
        if (sp->rune[0] == runes[1] &&
            sp->rune[1] == (nrunes > 2 ? runes[2] : 255) &&
            sp->rune[2] == (nrunes > 3 ? runes[3] : 255)) {
            break;
        }
    }
    c->cooldown = 10;
    if (i == NSPELLS) {
        msg ("THE WORDS MEAN NOTHING");
        return;
    }
    need = sp->level + power;
    missing = need > skill_of (c, sp->skill) ? need - skill_of (c, sp->skill) : 0;
    wis = c->stat[3] + 15 > 115 ? 115 : c->stat[3] + 15;
    while (missing--) {
        if ((rand () & 127) > wis) {
            msg ("THE SPELL FIZZLES. MORE PRACTICE NEEDED");
            return;
        }
    }

    switch (sp->kind) {
    case SPELL_POTION:
        if (held == NONE || TH_TYPE (held) != T_POTION || ((thing_ptr (held)[1] >> 8) & 0x7F) != 20) {
            msg ("THAT NEEDS AN EMPTY FLASK IN HAND");
            return;
        }
        r = thing_ptr (held);
        r[1] = ((unsigned int) sp->type << 8) | (40 * (power + 1) + (rand () & 15));
        msg ("THE FLASK FILLS");
        msg (obj_name (held));
        redraw |= REDRAW_PANEL;
        break;
    case SPELL_PROJECTILE:
        launch (sp->type, power, sp->strength);
        break;
    default:
        switch (sp->type) {
        case S_SHIELD:
        case S_FIRE_SHIELD:
            shield = (sp->strength * (power + 1)) >> 3;
            shield_ticks = 150 * (power + 1);
            msg ("A MAGICAL SHIELD SURROUNDS THE PARTY");
            break;
        case S_INVISIBLE:
            invisible_ticks = 100 * (power + 1);
            msg ("THE PARTY FADES FROM SIGHT");
            break;
        case S_ZOKATHRA:
            if (held == NONE) {
                held = pool_new (T_JUNK, 51);
                redraw |= REDRAW_PANEL;
            }
            msg (held == NONE ? "NOTHING APPEARS" : "SOMETHING APPEARS IN YOUR HAND");
            break;
        case S_LIGHT:
        case S_TORCH:
        case S_DARKNESS:
            msg ("THE DUNGEON IS ALWAYS LIT IN THIS VERSION");
            break;
        default:
            msg ("THAT SPELL HAS NO EFFECT YET");
            break;
        }
    }
}

void cast_spell (void)
{
    unsigned char who = 0, k, n;
    champion_t* c;

    if (!nchamps) {
        msg ("NO CHAMPIONS YET");
        return;
    }
    if (nchamps > 1) {
        msg ("WHO CASTS? 1-4");
        k = wait_key ();
        if (k < '1' || k >= '1' + nchamps) {
            return;
        }
        who = k - '1';
    }
    c = &champ[who];
    if (c->hp <= 0 || c->cooldown) {
        msg (c->hp <= 0 ? "THE DEAD CAST NO SPELLS" : "NOT READY YET");
        return;
    }
    nrunes = 0;
    for (;;) {
        show (c);
        k = wait_key ();
        if (k >= '1' && k <= '6' && nrunes < 4) {
            n = nrunes * 6 + k - '1';
            cost[nrunes] = nrunes ? (mana_cost[nrunes][k - '1'] * mana_multiplier[runes[0]]) >> 3
                                  : mana_cost[0][k - '1'];
            if (c->mana < cost[nrunes]) {
                continue;
            }
            c->mana -= cost[nrunes];
            runes[nrunes++] = n;
        } else if (k == 20 && nrunes) {             /* DEL */
            c->mana += cost[--nrunes];
        } else if (k == 13 && nrunes > 1) {
            msg ("");
            cast (c);
            break;
        } else if (k == ' ') {
            while (nrunes) {
                c->mana += cost[--nrunes];
            }
            msg ("");
            break;
        }
    }
    redraw |= REDRAW_PANEL;
}
