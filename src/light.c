/*
** light.c - light and darkness, by Dungeon Master's rules (as ScummVM's InventoryMan and
** Timeline have them).
**
** Every level but the first is dark: what light there is comes from torches in the champions'
** hands (each as bright as it has charges left: they burn one down every 512 ticks), an
** illumulet worn round the neck, and magic. The four brightest torches count, the brighter more,
** and a fifth a little. Spells of light add "magical light" that, once its time is up, fades
** away a step every 4 ticks; darkness takes some away for a while. The total picks one of DM's
** six palettes, 0 the brightest, which view.c shows (light_colours, from dmview.py).
*/

#include "dm.h"

static const unsigned char light_amount[16] = {
    0, 5, 12, 24, 33, 40, 46, 51, 59, 68, 76, 82, 89, 94, 97, 100
};
static const unsigned char palette_light[5] = { 99, 75, 50, 25, 1 };

#define TORCH        2          /* the weapon type */
#define ILLUMULET    12         /* its icons: 12 and 13 */

light_t light;
unsigned char light_level;

static unsigned char torch_charges (unsigned int t)
{
    unsigned int a;

    if (t == NONE || TH_TYPE (t) != T_WEAPON) {
        return 0;
    }
    a = thing_ptr (t)[1];
    return (a & 0x7F) == TORCH ? (a >> 10) & 15 : 0;
}

/* Magic: power p's light, given now (p below 0) and to fade once ticks are up, or taken away
** (p above 0) for that long. */
void light_spell (signed char p, unsigned int ticks)
{
    unsigned char i;

    light.magic += p < 0 ? light_amount[-p] : -light_amount[p];
    for (i = 0; i < 4 && light.power[i]; ++i) {
    }
    if (i < 4) {
        light.power[i] = p;
        light.ticks[i] = ticks;
    }
}

/* Which palette: how much light the party has. */
static void light_update (void)
{
    unsigned char charges[8], i, j, n = 0, shift = 6, level;
    int total;

    if (!levels[lev].difficulty) {
        level = 0;
    } else {
        total = light.magic;
        for (i = 0; i < nchamps; ++i) {
            charges[n++] = torch_charges (champ[i].slot[SL_ACTION]);
            charges[n++] = torch_charges (champ[i].slot[SL_READY]);
            j = obj_icon_of (champ[i].slot[SL_NECK]);
            if (j == ILLUMULET || j == ILLUMULET + 1) {
                total += light_amount[2];
            }
        }
        /* The brightest torch counts fully, the next half, and so on, for the five brightest. */
        for (i = 0; i < 5; ++i) {
            level = 0;
            for (j = 0; j < n; ++j) {
                if (charges[j] > charges[level]) {
                    level = j;
                }
            }
            if (n && charges[level]) {
                total += (light_amount[charges[level]] << shift) >> 6;
                --shift;
                charges[level] = 0;
            }
        }
        level = 5;
        if (total > 0) {
            for (level = 0; level < 5 && palette_light[level] > total; ++level) {
            }
        }
    }
    if (level != light_level) {
        light_level = level;
        redraw |= REDRAW_VIEW;
    }
}

/* Each tick: magic fades, torches burn down. */
void light_tick (void)
{
    unsigned char i, k;
    signed char p, weaker;
    unsigned int t;

    for (i = 0; i < 4; ++i) {
        p = light.power[i];
        if (p && !--light.ticks[i]) {
            k = p < 0 ? -p : p;
            weaker = k - 1;
            if (p < 0) {
                light.magic -= light_amount[k] - light_amount[weaker];
                weaker = -weaker;
            } else {
                light.magic += light_amount[k] - light_amount[weaker];
            }
            light.power[i] = weaker;
            light.ticks[i] = 4;
        }
    }
    if (!(game_time & 511)) {
        for (i = 0; i < nchamps; ++i) {
            for (k = SL_ACTION; k <= SL_READY; ++k) {
                t = champ[i].slot[k];
                if (torch_charges (t)) {
                    thing_ptr (t)[1] -= 1 << 10;
                    redraw |= REDRAW_PANEL;
                }
            }
        }
    }
    light_update ();
}
