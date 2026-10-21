/*
** screens.c - the inventory and map screens, and choosing the mouse, drawn over the view. An overlay (see overlay in
** dungeon.c): they're only needed when asked for.
*/

#include <c64.h>
#include <mouse.h>
#include "dm.h"

extern const struct mouse_callbacks mouse_callbacks;   /* mouse.s */

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

/* The slots in two columns: the body's, then the pack. */
#define SLOT_X(s) ((s) < SL_PACK ? 0 : 20)
#define SLOT_Y(s) (8 + ((s) < SL_PACK ? (s) : (s) - SL_PACK))

static void inventory_draw (unsigned char who)
{
    champion_t* c = &champ[who];
    unsigned char i, x, y;
    char letter[2];

    ui_clear (0, 0, 40, VIEW_H);
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
        x = SLOT_X (i);
        y = SLOT_Y (i);
        letter[0] = 'A' + i;
        ui_print (x, y, COLOR_YELLOW, letter);
        ui_print (x + 2, y, COLOR_GRAY2, slot_names[i]);
        ui_limit = x + 20;
        ui_print (x + 9, y, COLOR_WHITE, c->slot[i] == NONE ? "-" : obj_name (c->slot[i]));
        ui_limit = 40;
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
        zone (SLOT_X (s), SLOT_Y (s), 20, 1, 'a' + s);
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

/* The map of the party's level: every square they've seen, one character each, across the
** whole screen, scrolled up or down to keep them on it. */
void ovl_map (void)
{
    static const unsigned char party_glyph[4] = { G_ARROW_U, G_ARROW_R, G_ARROW_D, G_ARROW_L };
    unsigned char x, y, top, left, s, ch, col;
    unsigned int t;

    ui_clear (0, 0, 40, VIEW_H);
    left = (40 - lw) / 2;
    top = 0;
    if (lh > VIEW_H) {
        top = py < VIEW_H / 2 ? 0 : py - VIEW_H / 2;
        if (top > lh - VIEW_H) {
            top = lh - VIEW_H;
        }
    }
    for (y = top; y < lh && y < top + VIEW_H; ++y) {
        for (x = 0; x < lw; ++x) {
            if (!was_seen (x, y)) {
                continue;
            }
            s = sq_at (x, y);
            ch = '.';
            col = COLOR_GRAY1;
            switch (SQ_TYPE (s)) {
            case E_WALL:
                ch = G_SOLID;
                col = COLOR_GRAY2;
                break;
            case E_FAKE:
                if (!(s & FAKE_OPEN)) {
                    ch = G_SOLID;
                    col = COLOR_GRAY2;
                }
                break;
            case E_DOOR:
                ch = (s & DOOR_STATE) && (s & DOOR_STATE) < 5 ? G_DOOR : '+';
                col = COLOR_BROWN;
                break;
            case E_STAIRS:
                ch = G_STAIRS;
                col = (s & STAIRS_UP) ? COLOR_WHITE : COLOR_LIGHTBLUE;
                break;
            case E_PIT:
                if ((s & (PIT_OPEN | PIT_INVISIBLE)) == PIT_OPEN) {
                    ch = 'O';
                    col = COLOR_RED;
                }
                break;
            case E_TELE:
                if ((s & (TELE_OPEN | TELE_VISIBLE)) == (TELE_OPEN | TELE_VISIBLE)) {
                    ch = G_TELE;
                    col = COLOR_CYAN;
                }
                break;
            }
            if (ch == '.' && (s & SQ_THINGS)) {
                for (t = first_thing (x, y); t != END; t = NEXT (t)) {
                    if (TH_TYPE (t) >= T_WEAPON) {
                        ch = G_IT_JUNK;
                        col = COLOR_YELLOW;
                        break;
                    }
                }
            }
            ui_glyph (left + x, y - top, ch, col);
        }
    }
    if (py >= top && py < top + VIEW_H) {
        ui_glyph (left + px, py - top, party_glyph[pdir], COLOR_YELLOW);
    }
    msg ("THE MAP: YOU ARE THE ARROW");
    msg ("ANY KEY OR CLICK TO GO BACK");
    zones_clear ();
    zone (0, 0, 40, 25, ' ');
    wait_key ();
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}

/* M (here as it's seldom wanted: overlay.c): no pointer, a 1351 mouse in port 1, or a pointer moved by the joystick in port 2. */
void ovl_mouse (void)
{
    if (mouse_mode) {
        mouse_uninstall ();
    }
    mouse_mode = mouse_mode == 2 ? 0 : mouse_mode + 1;
    if (mouse_mode && mouse_install (&mouse_callbacks,
                                     mouse_mode == 1 ? c64_1351_mou : c64_joy_mou) != MOUSE_ERR_OK) {
        mouse_mode = 0;
    }
    if (mouse_mode) {
        mouse_show ();
    }
    last_buttons = 0;
    msg (mouse_mode == 0 ? "MOUSE OFF" : mouse_mode == 1 ? "MOUSE IN PORT 1" : "JOYSTICK IN PORT 2");
}

/* No pops here: cc65 writes string literals at the end of the file, into the segments then
** in force, and they belong in the overlay. */
