/*
** screens.c - the inventory and help screens, drawn over the view. An overlay (see overlay in
** dungeon.c): they're only needed when asked for.
*/

#include <c64.h>
#include "dm.h"

#pragma code-name (push, "OVL2")
#pragma rodata-name (push, "OVL2")
#pragma data-name (push, "OVL2")

static const char* const slot_names[NSLOTS] = {
    "ACTION", "READY", "HEAD", "NECK", "TORSO", "LEGS", "FEET",
    "PACK", "PACK", "PACK", "PACK", "PACK", "PACK", "PACK", "PACK"
};
static const unsigned char slot_needs[7] = { 0, 0, OBJ_HEAD, OBJ_NECK, OBJ_TORSO, OBJ_LEGS, OBJ_FEET };
static const char* const stat_names[6] = { "STR", "DEX", "WIS", "VIT", "AM ", "AF " };
static const char* const class_names[4] = { "FIG", "NIN", "PRI", "WIZ" };

static void pair (unsigned char x, unsigned char y, unsigned char col, const char* label,
                  int value, int max)
{
    ui_print (x, y, col, label);
    ui_print (x + 3, y, COLOR_WHITE, ui_num (value, 3));
    ui_print (x + 6, y, COLOR_GRAY2, "/");
    ui_print (x + 7, y, COLOR_WHITE, ui_num (max, 3));
}

static void inventory_draw (unsigned char who)
{
    champion_t* c = &champ[who];
    unsigned char i;
    char letter[2];

    sprites_off ();
    ui_clear (0, 0, VIEW_W, VIEW_H);
    ui_print (0, 0, COLOR_WHITE, c->name);
    ui_print (8, 0, COLOR_GRAY3, c->title);
    pair (0, 1, COLOR_LIGHTGREEN, "HP", c->hp, c->maxhp);
    pair (12, 1, COLOR_YELLOW, "ST", c->sta, c->maxsta);
    pair (0, 2, COLOR_LIGHTBLUE, "MA", c->mana, c->maxmana);
    ui_print (12, 2, COLOR_ORANGE, "FOOD");
    ui_print (17, 2, COLOR_WHITE, ui_num (c->food, 3));
    for (i = 0; i < 6; ++i) {
        ui_print ((i % 3) * 8, 3 + i / 3, COLOR_GRAY3, stat_names[i]);
        ui_print ((i % 3) * 8 + 4, 3 + i / 3, COLOR_WHITE, ui_num (c->stat[i + 1], 3));
    }
    for (i = 0; i < 4; ++i) {
        ui_print (i * 6, 5, COLOR_GRAY3, class_names[i]);
        ui_print (i * 6 + 3, 5, COLOR_WHITE, ui_num (c->skill[i], 2));
    }
    ui_print (0, 6, COLOR_LIGHTGREEN, "[EAT/DRINK]");
    ui_print (16, 6, COLOR_LIGHTGREEN, "[BACK]");
    letter[1] = 0;
    for (i = 0; i < NSLOTS; ++i) {
        letter[0] = 'A' + i;
        ui_print (0, 7 + i, COLOR_YELLOW, letter);
        ui_print (2, 7 + i, COLOR_GRAY2, slot_names[i]);
        ui_print (9, 7 + i, COLOR_WHITE, c->slot[i] == NONE ? "-" : obj_name (c->slot[i]));
    }
    panel_draw ();
}

void ovl_inventory (unsigned char who)
{
    unsigned char k, s, f;
    unsigned int t;

    if (!nchamps) {
        msg ("NO CHAMPIONS YET");
        return;
    }
    if (who >= nchamps) {
        who = 0;
    }
    msg ("A-O SWAP HAND  1-4 WHO  E EAT/DRINK/READ");
    msg ("I OR SPACE: BACK TO THE DUNGEON");
    zones_clear ();
    for (s = 0; s < NSLOTS; ++s) {
        zone (0, 7 + s, VIEW_W, 1, 'a' + s);
    }
    zone (0, 6, 11, 1, 'e');
    zone (16, 6, 6, 1, 'i');
    zone_champions ('1');
    for (;;) {
        inventory_draw (who);
        k = wait_key ();
        if (k >= '1' && k < '1' + nchamps) {
            who = k - '1';
        } else if (k >= 'a' && k < 'a' + NSLOTS) {
            s = k - 'a';
            if (held != NONE && s < SL_PACK && slot_needs[s]) {
                f = obj_flags[obj_info (held)];
                if (!(f & slot_needs[s])) {
                    msg ("IT DOESN'T GO THERE");
                    continue;
                }
            }
            t = champ[who].slot[s];
            champ[who].slot[s] = held;
            held = t;
        } else if (k == 'e') {
            use_held (who);
        } else if (k == 'i' || k == ' ' || k == 13 || k == KEY_RIGHT) {
            break;
        }
    }
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}

/* The help text is in tools/text.py, kept in the RAM under the KERNAL. */
void ovl_help (void)
{
    unsigned char i;

    sprites_off ();
    ui_clear (0, 0, VIEW_W, VIEW_H);
    ui_print (2, 0, COLOR_WHITE, "DUNGEON MASTER 64");
    for (i = 0; i < NHELP; ++i) {
        ui_print (0, 2 + i, COLOR_GRAY3, hi_str (help_line_at[i]));
    }
    zones_clear ();
    zone (0, 0, 40, 25, ' ');
    wait_key ();
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}

#pragma data-name (pop)
#pragma rodata-name (pop)
#pragma code-name (pop)
