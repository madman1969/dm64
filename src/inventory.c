/*
** inventory.c - a champion's inventory, as Dungeon Master shows it: its own picture in the view's
** place, with each object's icon in its slot (positions from ScummVM's ObjectMan), the food bar
** in the panel, and the champion's numbers in the text below. An overlay (overlay.c).
**
** Slots are chosen with A-O or by clicking them: the object there swaps with the one in hand.
*/

#include <c64.h>
#include <string.h>
#include "dm.h"

#pragma code-name (push, "OVL6")
#pragma rodata-name (push, "OVL6")
#pragma data-name (push, "OVL6")
#pragma bss-name (push, "OVL6")

/* Where each slot is in DM's picture: action hand, ready hand, head, neck, torso, legs, feet,
** then the first eight places in the backpack. */
static const unsigned char slot_x[NSLOTS] = {
    62, 6, 34, 6, 34, 34, 34, 66, 83, 100, 117, 134, 151, 168, 185
};
static const unsigned char slot_y[NSLOTS] = {
    53, 53, 26, 33, 46, 66, 86, 33, 33, 33, 33, 33, 33, 33, 33
};
static const unsigned char slot_needs[7] = { 0, 0, OBJ_HEAD, OBJ_NECK, OBJ_TORSO, OBJ_LEGS, OBJ_FEET };
static const char* const stat_names[6] = { "STR", "DEX", "WIS", "VIT", "AM", "AF" };
static const unsigned char stat_x[6] = { 0, 9, 18, 27, 0, 8 };
static const char* const skill_names[4] = { "FIG", "NIN", "PRI", "WIZ" };

#define FOOD_X 113              /* DM's food bar, in the panel */
#define FOOD_Y 69

/* A filled box, x and width in DM's pixels (even), in a colour (code 3), or code 0 if colour
** is 255. */
static void bar (unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char colour)
{
    unsigned char fat = w >> 1, i, n = (fat + 3) >> 2, ink = colour == 255 ? 0 : 0xFF;

    for (i = 0; i < n; ++i) {
        view_cols[2 * i] = ink;
        view_cols[2 * i + 1] = 0xFF;
    }
    if (fat & 3) {
        view_cols[2 * n - 1] = 0xFF << (8 - 2 * (fat & 3));
        view_cols[2 * n - 2] &= view_cols[2 * n - 1];
    }
    ob_colour = colour;
    for (; h; --h, ++y) {
        ob_col = x >> 3;
        ob_shift = (x >> 1) & 3;
        ob_cells = n;
        ob_y = y;
        object_row (view_cols);
    }
}

static void number (unsigned char x, unsigned char y, int n, unsigned char width)
{
    ui_print (x, y, COLOR_WHITE, ui_num (n < 0 ? 0 : n, width));
}

static void draw (unsigned char who)
{
    champion_t* c = &champ[who];
    unsigned char i, w;
    unsigned int t;

    /* The picture: the view's colours as in full light. */
    memset (SCREEN, 0xBF, VIEW_H * 40);
    split_bg = 12;
    piece (PIECE_INVENTORY, 4);
    for (i = 0; i < NSLOTS; ++i) {
        t = c->slot[i];
        if (t != NONE && obj_icon_of (t) < 224) {
            sprite (PIECE_ICON + obj_icon_of (t), slot_x[i] + 7, slot_y[i] + 15, 0);
        }
    }
    /* The champion's portrait, in the middle of the panel beside the view. */
    ob_right = 40;
    sprite (PIECE_FACE + c->portrait, 271, 84, 0);
    ob_right = VIEW_W;
    w = (unsigned char) (((unsigned int) c->food * 96) >> 8) & 0xFE;
    if (w) {
        bar (FOOD_X + 2, FOOD_Y + 2, w, 6, COLOR_BLACK);
        bar (FOOD_X, FOOD_Y, w, 6, c->food < 32 ? COLOR_RED : COLOR_ORANGE);
    }

    /* The numbers (messages stay below). */
    ui_clear (0, 17, 40, 5);
    ui_print (0, 17, COLOR_WHITE, c->name);
    ui_print (8, 17, COLOR_GRAY3, c->title);
    ui_print (0, 18, COLOR_LIGHTGREEN, "HP");
    number (3, 18, c->hp, 3);
    ui_print (6, 18, COLOR_GRAY2, "/");
    number (7, 18, c->maxhp, 3);
    ui_print (11, 18, COLOR_YELLOW, "ST");
    number (14, 18, c->sta, 3);
    ui_print (17, 18, COLOR_GRAY2, "/");
    number (18, 18, c->maxsta, 3);
    ui_print (22, 18, COLOR_LIGHTBLUE, "MA");
    number (25, 18, c->mana, 3);
    ui_print (28, 18, COLOR_GRAY2, "/");
    number (29, 18, c->maxmana, 3);
    ui_print (33, 18, COLOR_ORANGE, "FOOD");
    number (37, 18, c->food, 3);
    /* Statistics, four and two, then skills. */
    for (i = 0; i < 6; ++i) {
        ui_print (stat_x[i], 19 + (i >> 2), COLOR_GRAY3, stat_names[i]);
        number (stat_x[i] + 4, 19 + (i >> 2), c->stat[i + 1], 3);
    }
    for (i = 0; i < 4; ++i) {
        ui_print (16 + i * 6, 20, COLOR_GRAY3, skill_names[i]);
        number (16 + i * 6 + 3, 20, c->skill[i], 2);
    }
    ui_print (26, 17, COLOR_GRAY3, "HAND");
    ui_print (31, 17, COLOR_YELLOW, held == NONE ? "-" : obj_name (held));
    ui_print (0, 21, COLOR_GRAY2, "A-B HANDS C HEAD D NECK E BODY F LEGS");
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
    sprites_off ();
    msg ("G FEET  H-O PACK  1-4 WHO  X EAT/DRINK");
    msg ("I OR SPACE: BACK TO THE DUNGEON");
    zones_clear ();
    for (s = 0; s < NSLOTS; ++s) {
        zone ((slot_x[s] + 4) >> 3, (slot_y[s] + 4) >> 3, 2, 2, 'a' + s);
    }
    for (;;) {
        draw (who);
        k = wait_key ();
        if (k >= '1' && k < '1' + nchamps) {
            who = k - '1';
        } else if (k >= 'a' && k < 'a' + NSLOTS) {
            s = k - 'a';
            if (held != NONE && s < SL_PACK && slot_needs[s]) {
                f = text_byte (TXT_OBJ_FLAGS + obj_info (held));
                if (!(f & slot_needs[s])) {
                    msg ("IT DOESN'T GO THERE");
                    continue;
                }
            }
            t = champ[who].slot[s];
            champ[who].slot[s] = held;
            held = t;
        } else if (k == 'x') {
            use_held (who);
        } else if (k == 'i' || k == ' ' || k == 13 || k == KEY_RIGHT) {
            break;
        }
    }
    ui_clear (0, 17, 40, 5);
    piece (PIECE_PANEL, 4);         /* the buttons again, where the portrait was */
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}

/* No pops here: cc65 writes string literals at the end of the file, into the segments then in
** force, and they belong in the overlay. */
