/*
** screens.c - the map screen, and choosing the mouse. An overlay (see overlay in dungeon.c):
** they're only needed when asked for.
*/

#include <c64.h>
#include <mouse.h>
#include "dm.h"

extern const struct mouse_callbacks mouse_callbacks;   /* mouse.s */

#pragma code-name (push, "OVL2")
#pragma rodata-name (push, "OVL2")
#pragma data-name (push, "OVL2")

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
