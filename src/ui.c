/*
** ui.c - everything around the view: messages, the party panel, and input from the keyboard and
** a mouse (or a joystick moving a pointer).
**
**   columns 0-23, rows 0-21   the 3D view (view.c), or the inventory and text boxes
**   columns 25-39, rows 0-21  the party, what's in hand, and buttons
**   rows 22-24                messages
**
** Clicks become keys: each screen lists the zones that can be clicked and the key each stands
** for, so the code reading keys works the same with a mouse.
*/

#include <string.h>
#include <cbm.h>
#include <c64.h>
#include <mouse.h>
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

static void put_glyph (unsigned char x, unsigned char y, unsigned char ch, unsigned char col)
{
    unsigned int p = y * 40 + x;

    SCREEN[p] = ch;
    COLOUR[p] = col;
}

static char num_buf[6];

const char* ui_num (unsigned int n, unsigned char width)
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

/* ---- input ------------------------------------------------------------------------------------ */

#define MAXZONES 24
static unsigned char zx[MAXZONES], zy[MAXZONES], zw[MAXZONES], zh[MAXZONES], zkey[MAXZONES];
static unsigned char nzones;
static unsigned char mouse_mode, last_buttons;     /* mouse_mode: 0 off, 1 mouse, 2 joystick */
extern const struct mouse_callbacks mouse_callbacks;   /* mouse.s */

void zones_clear (void)
{
    nzones = 0;
}

void zone (unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char key)
{
    if (nzones < MAXZONES) {
        zx[nzones] = x;
        zy[nzones] = y;
        zw[nzones] = w;
        zh[nzones] = h;
        zkey[nzones++] = key;
    }
}

/* Each champion's name and bars in the panel, as keys first, first + 1... */
void zone_champions (unsigned char first)
{
    unsigned char i;

    for (i = 0; i < nchamps; ++i) {
        zone (PANEL_X, 2 + i * 4, 15, 2, first + i);
    }
}

#define KEYS_WAITING (*(unsigned char*) 0x00C6)     /* KERNAL: keys in its buffer */

/* A key, a click turned into a key, or 0. The right button is KEY_RIGHT, which backs out. */
unsigned char get_input (void)
{
    unsigned char k, i, x, y;
    struct mouse_info mi;

    if (mouse_mode) {
        mouse_info (&mi);
        /* The mouse's buttons share lines with the keyboard, so the KERNAL sees phantom keys
        ** while one is down: they're thrown away. */
        if (mi.buttons | last_buttons) {
            KEYS_WAITING = 0;
        }
        k = mi.buttons & ~last_buttons;
        last_buttons = mi.buttons;
        if (k & MOUSE_BTN_RIGHT) {
            return KEY_RIGHT;
        }
        if (k & MOUSE_BTN_LEFT) {
            x = mi.pos.x >> 3;
            y = mi.pos.y >> 3;
            for (i = 0; i < nzones; ++i) {
                if (x >= zx[i] && x < zx[i] + zw[i] && y >= zy[i] && y < zy[i] + zh[i]) {
                    return zkey[i];
                }
            }
            return 0;
        }
    }
    return cbm_k_getin ();
}

unsigned char wait_key (void)
{
    unsigned char k;

    while ((k = get_input ()) == 0) {
    }
    return k;
}

/* M: no pointer, a 1351 mouse in port 1, or a pointer moved by the joystick in port 2. */
void mouse_cycle (void)
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
    msg (mouse_mode == 0 ? "MOUSE OFF" : mouse_mode == 1 ? "MOUSE IN PORT 1" : "JOYSTICK POINTER IN PORT 2");
}

/* The dungeon's own zones: the view, the champions, and the buttons under them. */
void main_zones (void)
{
    static const unsigned char buttons[10][3] = {
        { 25, 19, 'q' }, { 28, 19, 'w' }, { 31, 19, 'e' },
        { 25, 20, 'a' }, { 28, 20, 's' }, { 31, 20, 'd' },
        { 35, 19, 'c' }, { 35, 20, 133 }, { 35, 21, 134 }, { 25, 21, 'h' }
    };
    unsigned char i;

    zones_clear ();
    zone (0, 17, VIEW_W, 5, KEY_FLOOR);        /* the floor ahead: take or put down */
    zone (0, 0, VIEW_W, 17, ' ');              /* the wall or door ahead: press it */
    zone_champions (KEY_INVENTORY);
    for (i = 0; i < nchamps; ++i) {
        zone (PANEL_X, 4 + i * 4, 15, 1, '1' + i);  /* their hand: attack */
    }
    zone (PANEL_X, 17, 15, 2, KEY_FLOOR);
    for (i = 0; i < 10; ++i) {
        zone (buttons[i][0], buttons[i][1], i < 6 ? 3 : 4, 1, buttons[i][2]);
    }
}

/* ---- messages --------------------------------------------------------------------------------- */

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

/* ---- the party panel ------------------------------------------------------------------------- */

/* A bar of 4 characters, 32 steps, for value out of max. */
static void bar (unsigned char x, unsigned char y, int value, int max, unsigned char col)
{
    unsigned char filled, i, n;

    if (value < 0) value = 0;
    filled = max ? (unsigned char) (((unsigned long) value * 32) / max) : 0;
    if (filled > 32) filled = 32;
    for (i = 0; i < 4; ++i) {
        n = filled > 8 ? 8 : filled;
        filled -= n;
        put_glyph (x + i, y, G_BAR0 + n, col);
    }
}

static const char dir_letter[4] = { 'N', 'E', 'S', 'W' };
static const unsigned char pad_glyph[6] = {
    G_TURN_L, G_ARROW_U, G_TURN_R, G_ARROW_L, G_ARROW_D, G_ARROW_R
};

void panel_draw (void)
{
    unsigned char i, y;
    champion_t* c;
    unsigned int t;
    char d[2];

    ui_clear (PANEL_X, 0, 15, VIEW_H);
    ui_print (PANEL_X, 0, COLOR_GRAY3, "LEVEL");
    ui_print (PANEL_X + 6, 0, COLOR_WHITE, ui_num (levels[lev].depth + 1, 2));
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

    ui_print (PANEL_X, 17, COLOR_GRAY3, "HAND");
    ui_print (PANEL_X + 5, 17, COLOR_YELLOW, held == NONE ? "-" : obj_name (held));
    t = first_thing (px, py);
    for (i = 0; t != END; t = NEXT (t)) {
        if (TH_TYPE (t) >= T_WEAPON) {
            ++i;
        }
    }
    if (i) {
        ui_print (PANEL_X, 18, COLOR_GRAY2, ui_num (i, 2));
        ui_print (PANEL_X + 3, 18, COLOR_GRAY2, "ON FLOOR");
    }

    /* The movement pad (turn, forward, turn / left, back, right) and buttons. */
    for (i = 0; i < 6; ++i) {
        put_glyph (PANEL_X + 1 + (i % 3) * 3, 19 + i / 3, pad_glyph[i], COLOR_WHITE);
    }
    ui_print (35, 19, COLOR_LIGHTBLUE, "CAST");
    ui_print (35, 20, COLOR_LIGHTGREEN, "SAVE");
    ui_print (35, 21, COLOR_LIGHTGREEN, "LOAD");
    ui_print (PANEL_X, 21, COLOR_GRAY2, "HELP");
}

void ui_init (void)
{
    ui_clear (0, 0, 40, 25);
    main_zones ();
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}
