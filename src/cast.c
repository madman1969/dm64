/*
** cast.c - casting spells, from Dungeon Master's own spell table (GRAPHICS.DAT item 560). An
** overlay (see overlay in dungeon.c).
**
** A spell is a power rune (LO to MON) followed by one to three more, one from each of the
** element, form and class rows in turn. Each rune costs mana as it's chosen: the power rune its
** base cost, the others their base cost times the power's multiplier (in eighths). The spell
** needs a skill level of its own level plus the power, in priest or wizard skill; each level
** short is a chance of failing, less likely the wiser the caster. Potions are cast into an
** empty flask; projectiles are launched into magic.c's care.
*/

#include <stdlib.h>
#include <string.h>
#include "dm.h"

#pragma code-name (push, "OVL3")
#pragma rodata-name (push, "OVL3")
#pragma data-name (push, "OVL3")

static const char* const rune_names[24] = {
    "LO", "UM", "ON", "EE", "PAL", "MON",
    "YA", "VI", "OH", "FUL", "DES", "ZO",
    "VEN", "EW", "KATH", "IR", "BRO", "GOR",
    "KU", "ROS", "DAIN", "NETA", "RA", "SAR"
};
static const char* const row_names[4] = { "POWER", "ELEMENT", "FORM", "CLASS" };

/* Other spells, as the spell table numbers them. */
enum { S_LIGHT, S_DARKNESS, S_SEE_WALLS, S_INVISIBLE, S_SHIELD, S_TORCH, S_FOOTPRINTS,
       S_ZOKATHRA, S_FIRE_SHIELD };

static unsigned char runes[4], nrunes;
static unsigned char cost[4];
static char line[41];

static void add (const char* s)
{
    strncat (line, s, sizeof line - 1 - strlen (line));
}

/* The spell so far, the next row of runes (each clickable), and what else can be done. */
static void show (champion_t* c)
{
    unsigned char i, x;
    char num[3];

    ui_clear (0, 22, 40, 3);
    line[0] = 0;
    add (c->name);
    add (":");
    for (i = 0; i < nrunes; ++i) {
        add (" ");
        add (rune_names[runes[i]]);
    }
    add ("  MANA ");
    add (ui_num (c->mana, 3));
    ui_print (0, 22, 13, line);

    zones_clear ();
    line[0] = 0;
    if (nrunes < 4) {
        add (row_names[nrunes]);
        add (":");
        for (i = 0; i < 6; ++i) {
            add (" ");
            x = strlen (line);
            num[0] = '1' + i;
            num[1] = 0;
            add (num);
            add (rune_names[nrunes * 6 + i]);
            zone (x, 23, strlen (line) - x, 1, '1' + i);
        }
    }
    ui_print (0, 23, 7, line);
    ui_print (0, 24, 15, "RETURN:CAST  DEL:BACK  SPACE:CANCEL");
    zone (0, 24, 11, 1, 13);
    zone (13, 24, 8, 1, 20);
    zone (23, 24, 12, 1, ' ');
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

void ovl_cast (void)
{
    unsigned char who = 0, k, n;
    champion_t* c;

    if (!nchamps) {
        msg ("NO CHAMPIONS YET");
        return;
    }
    if (nchamps > 1) {
        msg ("WHO CASTS? 1-4");
        zones_clear ();
        zone_champions ('1');
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
        } else if (k == ' ' || k == KEY_RIGHT) {
            while (nrunes) {
                c->mana += cost[--nrunes];
            }
            msg ("");
            break;
        }
    }
    redraw |= REDRAW_PANEL;
}

/* No pops here: cc65 writes string literals at the end of the file, into the segments then
** in force, and they belong in the overlay. */
