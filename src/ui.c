/*
** ui.c - everything around the view: messages, the party panel, and the inventory screen.
**
**   columns 0-23, rows 0-21   the 3D view (view.c), or the inventory and text boxes
**   columns 25-39, rows 0-21  the party
**   rows 22-24                messages
*/

#include <string.h>
#include <cbm.h>
#include <c64.h>
#include "dm.h"

#define PANEL_X 25
#define MSG_Y   22

unsigned char redraw;

/* Screen codes for text. The program's own upper-case strings arrive as PETSCII's shifted
** letters ($C1-$DA, as cc65 translates them); names and texts from the game as plain ASCII. */
unsigned char scr_code (unsigned char c)
{
    if (c >= 0xC1 && c <= 0xDA) {
        return c - 0xC0;
    }
    return c >= 64 && c < 96 ? c - 64 : c;
}

void ui_clear (unsigned char x, unsigned char y, unsigned char w, unsigned char h)
{
    unsigned int p = y * 40 + x;

    while (h--) {
        memset (SCREEN + p, ' ', w);
        p += 40;
    }
}

void ui_print (unsigned char x, unsigned char y, unsigned char col, const char* s)
{
    unsigned int p = y * 40 + x;

    while (*s && x < 40) {
        SCREEN[p] = scr_code (*s++);
        COLOUR[p++] = col;
        ++x;
    }
}

static char num_buf[6];

static const char* num (unsigned int n, unsigned char width)
{
    unsigned char i = width;

    num_buf[width] = 0;
    do {
        num_buf[--i] = '0' + n % 10;
        n /= 10;
    } while (n && i);
    while (i) {
        num_buf[--i] = ' ';
    }
    return num_buf;
}

unsigned char wait_key (void)
{
    unsigned char k;

    while ((k = cbm_k_getin ()) == 0) {
    }
    return k;
}

/* The screen is in the RAM under the KERNAL, where the CPU reads ROM, so the message lines are
** kept here too, to scroll. */
static char msg_lines[3][41];

void msg (const char* s)
{
    unsigned char i;

    memmove (msg_lines[0], msg_lines[1], sizeof msg_lines - sizeof msg_lines[0]);
    for (i = 0; i < 40 && s[i]; ++i) {
        msg_lines[2][i] = s[i];
    }
    msg_lines[2][i] = 0;
    ui_clear (0, MSG_Y, 40, 3);
    for (i = 0; i < 3; ++i) {
        ui_print (0, MSG_Y + i, i == 2 ? COLOR_YELLOW : COLOR_GRAY3, msg_lines[i]);
    }
}

/* A dungeon text, one message per line. */
void msg_text (const char* s)
{
    static char line[41];
    unsigned char n;

    while (*s) {
        for (n = 0; *s && *s != '\n' && n < 40; ++n) {
            line[n] = *s++;
        }
        line[n] = 0;
        if (*s == '\n') {
            ++s;
        }
        if (n) {
            msg (line);
        }
    }
}

/* A bar of 4 characters, 32 steps, for value out of max. */
static void bar (unsigned char x, unsigned char y, int value, int max, unsigned char col)
{
    unsigned char filled, i, n;
    unsigned int p = y * 40 + x;

    if (value < 0) value = 0;
    filled = max ? (unsigned char) (((unsigned long) value * 32) / max) : 0;
    if (filled > 32) filled = 32;
    for (i = 0; i < 4; ++i) {
        n = filled > 8 ? 8 : filled;
        filled -= n;
        SCREEN[p] = G_BAR0 + n;
        COLOUR[p++] = col;
    }
}

static const char dir_letter[4] = { 'N', 'E', 'S', 'W' };

void panel_draw (void)
{
    unsigned char i, y;
    champion_t* c;
    unsigned int t;
    char d[2];

    ui_clear (PANEL_X, 0, 15, VIEW_H);
    ui_print (PANEL_X, 0, COLOR_GRAY3, "LEVEL");
    ui_print (PANEL_X + 6, 0, COLOR_WHITE, num (levels[lev].depth + 1, 2));
    d[0] = dir_letter[pdir];
    d[1] = 0;
    ui_print (PANEL_X + 10, 0, COLOR_GRAY3, "FACE");
    ui_print (PANEL_X + 14, 0, COLOR_WHITE, d);

    for (i = 0; i < 4; ++i) {
        y = 2 + i * 4;
        if (i >= nchamps) {
            ui_print (PANEL_X, y, COLOR_GRAY1, "- EMPTY -");
            continue;
        }
        c = &champ[i];
        d[0] = '1' + i;
        ui_print (PANEL_X, y, COLOR_GRAY2, d);
        ui_print (PANEL_X + 2, y, c->hp > 0 ? (c->cooldown ? COLOR_GRAY2 : COLOR_WHITE) : COLOR_RED,
                  c->name);
        if (c->hp <= 0) {
            ui_print (PANEL_X + 10, y, COLOR_RED, "DEAD");
            continue;
        }
        bar (PANEL_X, y + 1, c->hp, c->maxhp, COLOR_LIGHTGREEN);
        bar (PANEL_X + 5, y + 1, c->sta, c->maxsta, COLOR_YELLOW);
        bar (PANEL_X + 10, y + 1, c->mana, c->maxmana, COLOR_LIGHTBLUE);
        t = c->slot[SL_ACTION];
        ui_print (PANEL_X, y + 2, COLOR_GRAY2, t == NONE ? "(EMPTY HAND)" : obj_name (t));
    }

    ui_print (PANEL_X, 18, COLOR_GRAY3, "HAND:");
    ui_print (PANEL_X, 19, COLOR_YELLOW, held == NONE ? "-" : obj_name (held));
    t = first_thing (px, py);
    for (i = 0; t != END; t = NEXT (t)) {
        if (TH_TYPE (t) >= T_WEAPON) {
            ++i;
        }
    }
    if (i) {
        ui_print (PANEL_X, 21, COLOR_GRAY2, num (i, 2));
        ui_print (PANEL_X + 3, 21, COLOR_GRAY2, "ON FLOOR");
    }
}

void ui_init (void)
{
    ui_clear (0, 0, 40, 25);
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}

/* ---- the inventory ------------------------------------------------------------------------- */

static const char* const slot_names[NSLOTS] = {
    "ACTION", "READY", "HEAD", "NECK", "TORSO", "LEGS", "FEET",
    "PACK", "PACK", "PACK", "PACK", "PACK", "PACK", "PACK", "PACK"
};
static const unsigned char slot_needs[7] = { 0, 0, OBJ_HEAD, OBJ_NECK, OBJ_TORSO, OBJ_LEGS, OBJ_FEET };
static const char* const stat_names[6] = { "STR", "DEX", "WIS", "VIT", "AM ", "AF " };
static const char* const class_names[4] = { "FIG", "NIN", "PRI", "WIZ" };

static void inventory_draw (unsigned char who)
{
    champion_t* c = &champ[who];
    unsigned char i, y;
    char letter[2];

    sprites_off ();
    ui_clear (0, 0, VIEW_W, VIEW_H);
    ui_print (0, 0, COLOR_WHITE, c->name);
    ui_print (8, 0, COLOR_GRAY3, c->title);
    ui_clear (VIEW_W, 0, 1, 1);
    ui_print (0, 1, COLOR_LIGHTGREEN, "HP");
    ui_print (3, 1, COLOR_WHITE, num (c->hp, 3));
    ui_print (6, 1, COLOR_GRAY2, "/");
    ui_print (7, 1, COLOR_WHITE, num (c->maxhp, 3));
    ui_print (12, 1, COLOR_YELLOW, "ST");
    ui_print (15, 1, COLOR_WHITE, num (c->sta, 3));
    ui_print (18, 1, COLOR_GRAY2, "/");
    ui_print (19, 1, COLOR_WHITE, num (c->maxsta, 3));
    ui_print (0, 2, COLOR_LIGHTBLUE, "MA");
    ui_print (3, 2, COLOR_WHITE, num (c->mana, 3));
    ui_print (6, 2, COLOR_GRAY2, "/");
    ui_print (7, 2, COLOR_WHITE, num (c->maxmana, 3));
    ui_print (12, 2, COLOR_ORANGE, "FOOD");
    ui_print (17, 2, COLOR_WHITE, num (c->food, 3));
    for (i = 0; i < 6; ++i) {
        ui_print ((i % 3) * 8, 3 + i / 3, COLOR_GRAY3, stat_names[i]);
        ui_print ((i % 3) * 8 + 4, 3 + i / 3, COLOR_WHITE, num (c->stat[i + 1], 3));
    }
    for (i = 0; i < 4; ++i) {
        ui_print (i * 6, 5, COLOR_GRAY3, class_names[i]);
        ui_print (i * 6 + 3, 5, COLOR_WHITE, num (c->skill[i], 2));
    }
    letter[1] = 0;
    for (i = 0; i < NSLOTS; ++i) {
        y = 7 + i;
        letter[0] = 'A' + i;
        ui_print (0, y, COLOR_YELLOW, letter);
        ui_print (2, y, COLOR_GRAY2, slot_names[i]);
        ui_print (9, y, COLOR_WHITE, c->slot[i] == NONE ? "-" : obj_name (c->slot[i]));
    }
    panel_draw ();
}

void inventory (void)
{
    unsigned char who = 0, k, s, f;
    unsigned int t;

    if (!nchamps) {
        msg ("NO CHAMPIONS YET");
        return;
    }
    msg ("A-O SWAP HAND  1-4 WHO  E EAT/DRINK/READ");
    msg ("I OR SPACE: BACK TO THE DUNGEON");
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
        } else if (k == 'i' || k == ' ' || k == 13) {
            break;
        }
    }
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}
