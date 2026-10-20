/*
** ui.c - everything around the view: messages, the party panel, and input from the keyboard and
** a mouse (or a joystick moving a pointer).
**
**   rows 0-16    a bitmap (irq.s): the 3D view in columns 0-27 (view.c), and beside it
**                buttons and the movement pad (drawn by art.py); or, all text, the inventory,
**                the map, help and recruiting
**   rows 17-19   the party, ten columns each: name, bars, what's in hand
**   row 20       the level, the way the party faces, what's in hand, what's on the floor
**   rows 22-24   messages
**
** Clicks become keys: each screen lists the zones that can be clicked and the key each stands
** for, so the code reading keys works the same with a mouse.
*/

#include <string.h>
#include <cbm.h>
#include <c64.h>
#include <mouse.h>
#include "dm.h"

#define PARTY_Y  17
#define STATUS_Y 20
#define MSG_Y    22

unsigned char redraw;
unsigned char ui_limit = 40;

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

    while (*s && x < ui_limit) {
        SCREEN[p] = scr_code (*s++);
        COLOUR[p++] = col;
        ++x;
    }
}

void ui_glyph (unsigned char x, unsigned char y, unsigned char ch, unsigned char col)
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

/* Each champion, as keys first, first + 1... */
void zone_champions (unsigned char first)
{
    unsigned char i;

    for (i = 0; i < nchamps; ++i) {
        zone (i * 10, PARTY_Y, 10, 3, first + i);
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

/* The dungeon's own zones: the view, the buttons beside it (art.py's PANEL_BUTTONS), the
** champions and the status line. */
void main_zones (void)
{
    static const unsigned char buttons[11][4] = {
        { 28, 0, 12, 'c' }, { 28, 2, 6, 133 }, { 34, 2, 6, 134 }, { 28, 4, 6, 'h' },
        { 34, 4, 6, 'p' }, { 28, 13, 4, 'q' }, { 32, 13, 4, 'w' }, { 36, 13, 4, 'e' },
        { 28, 15, 4, 'a' }, { 32, 15, 4, 's' }, { 36, 15, 4, 'd' }
    };
    unsigned char i;

    zones_clear ();
    zone (0, 11, VIEW_W, 6, KEY_FLOOR);        /* the floor ahead: take or put down */
    zone (0, 0, VIEW_W, 11, ' ');              /* the wall or door ahead: press it */
    for (i = 0; i < 11; ++i) {
        zone (buttons[i][0], buttons[i][1], buttons[i][2], 2, buttons[i][3]);
    }
    for (i = 0; i < nchamps; ++i) {
        zone (i * 10, PARTY_Y, 10, 2, KEY_INVENTORY + i);
        zone (i * 10, PARTY_Y + 2, 10, 1, '1' + i);     /* their hand: attack */
    }
    zone (10, STATUS_Y, 30, 1, KEY_FLOOR);
}

/* The split screen: the top 17 rows' memory is the bitmap's colours (two greys in every cell;
** the panel's own colour white, the view's set as it's drawn). */
void screen_view (void)
{
    unsigned char y;

    memset (SCREEN, 0xBF, VIEW_H * 40);
    for (y = 0; y < VIEW_H; ++y) {
        memset (COLOUR + y * 40 + VIEW_W, COLOR_WHITE, 40 - VIEW_W);
    }
    split_on = 1;
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}

void screen_text (void)
{
    split_on = 0;
    sprites_off ();
    ui_clear (0, 0, 40, VIEW_H);
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

/* A bar of 3 characters, 24 steps, for value out of max. */
static void bar (unsigned char x, unsigned char y, int value, int max, unsigned char col)
{
    unsigned char filled, i, n;

    if (value < 0) value = 0;
    filled = max ? (unsigned char) (((unsigned long) value * 24) / max) : 0;
    if (filled > 24) filled = 24;
    for (i = 0; i < 3; ++i) {
        n = filled > 8 ? 8 : filled;
        filled -= n;
        ui_glyph (x + i, y, G_BAR0 + n, col);
    }
}

static const char dir_letter[4] = { 'N', 'E', 'S', 'W' };

void panel_draw (void)
{
    unsigned char i, x;
    champion_t* c;
    unsigned int t;
    char d[2];

    ui_clear (0, PARTY_Y, 40, 4);
    d[1] = 0;
    for (i = 0; i < 4; ++i) {
        x = i * 10;
        if (i >= nchamps) {
            ui_print (x, PARTY_Y, COLOR_GRAY1, "- EMPTY -");
            continue;
        }
        c = &champ[i];
        d[0] = '1' + i;
        ui_print (x, PARTY_Y, COLOR_GRAY2, d);
        ui_limit = x + 9;
        ui_print (x + 2, PARTY_Y, c->hp > 0 ? (c->cooldown ? COLOR_GRAY2 : COLOR_WHITE) : COLOR_RED,
                  c->name);
        if (c->hp <= 0) {
            ui_print (x, PARTY_Y + 1, COLOR_RED, "DEAD");
        } else {
            bar (x, PARTY_Y + 1, c->hp, c->maxhp, COLOR_LIGHTGREEN);
            bar (x + 3, PARTY_Y + 1, c->sta, c->maxsta, COLOR_YELLOW);
            bar (x + 6, PARTY_Y + 1, c->mana, c->maxmana, COLOR_LIGHTBLUE);
            t = c->slot[SL_ACTION];
            ui_print (x, PARTY_Y + 2, COLOR_GRAY2, t == NONE ? "-" : obj_name (t));
        }
        ui_limit = 40;
    }

    ui_print (0, STATUS_Y, COLOR_GRAY3, "LEVEL");
    ui_print (6, STATUS_Y, COLOR_WHITE, ui_num (levels[lev].depth + 1, 2));
    d[0] = dir_letter[pdir];
    ui_print (9, STATUS_Y, COLOR_WHITE, d);
    ui_print (11, STATUS_Y, COLOR_GRAY3, "HAND");
    ui_limit = 30;
    ui_print (16, STATUS_Y, COLOR_YELLOW, held == NONE ? "-" : obj_name (held));
    ui_limit = 40;
    t = first_thing (px, py);
    for (i = 0; t != END; t = NEXT (t)) {
        if (TH_TYPE (t) >= T_WEAPON) {
            ++i;
        }
    }
    if (i) {
        ui_print (29, STATUS_Y, COLOR_GRAY2, ui_num (i, 2));
        ui_print (31, STATUS_Y, COLOR_GRAY2, " ON FLOOR");
    }
}

void ui_init (void)
{
    ui_clear (0, 0, 40, 25);
    screen_view ();
    main_zones ();
}
