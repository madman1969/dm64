/*
** main.c - Dungeon Master for the Commodore 64: start-up and the main loop.
**
** The game runs in real time, like the original: ten game ticks a second, timed from the
** KERNAL's jiffy clock, move doors and creatures whether or not a key is pressed.
*/

#include <stdlib.h>
#include <cbm.h>
#include <c64.h>
#include "dm.h"

unsigned int game_time;

#define JIFFY (*(volatile unsigned char*) 0x00A2)
#define RETURN 13

static void say (const char* s)
{
    /* cc65 turns upper-case literals into PETSCII's shifted letters; the KERNAL's upper-case
    ** set wants them unshifted. */
    while (*s) {
        cbm_k_bsout (*s >= 0xC1 && *s <= 0xDA ? *s - 0x80 : *s);
        ++s;
    }
    cbm_k_bsout (RETURN);
}

static void key (unsigned char k)
{
    switch (k) {
    case 'w': case 145: move_party (1, 0); break;
    case 's': case 17:  move_party (-1, 0); break;
    case 'a':           move_party (0, -1); break;
    case 'd':           move_party (0, 1); break;
    case 'q': case 157: turn_party (-1); break;
    case 'e': case 29:  turn_party (1); break;
    case ' ':           click_wall (); break;
    case 't':           take_object (); break;
    case 'g':           drop_object (); break;
    case 'i':           inventory (0); break;
    case 'm':           mouse_cycle (); break;
    case KEY_FLOOR:     if (held == NONE) take_object (); else drop_object (); break;
    case 'x':           use_held (0); break;
    case 'c':           cast_spell (); break;
    case 'h':           help (); break;
    case 'p':           map (); break;
    case 133:           save_game (); break;        /* F1 */
    case 134:           load_game (); break;        /* F3 */
    case '1': case '2': case '3': case '4':
        attack (k - '1');
        break;
    default:
        if (k >= KEY_INVENTORY && k < KEY_INVENTORY + 4) {
            inventory (k - KEY_INVENTORY);
        }
    }
}

int main (void)
{
    unsigned char last, k;

    cbm_k_bsout (147);
    say ("DUNGEON MASTER 64");
    say ("");
    say ("LOADING...");
    if (!hw_load_data ()) {
        say ("CAN'T LOAD DMHI");
        return 1;
    }
    if (!dungeon_init ()) {
        say ("THIS GAME NEEDS A RAM EXPANSION:");
        say ("AN REU (128K OR MORE) OR A SUPERCPU.");
        return 1;
    }
    hw_init ();
    ui_init ();
    srand (JIFFY);

    level_enter (0);
    px = START_X;
    py = START_Y;
    pdir = START_DIR;
    sensors_init ();
    creatures_scan ();
    msg ("WELCOME TO THE DUNGEON.");
    msg ("PRESS H FOR HELP.");

    last = JIFFY;
    for (;;) {
        k = get_input ();
        if (k) {
            key (k);
        }
        while ((unsigned char) (JIFFY - last) >= 6) {
            last += 6;
            ++game_time;
            events_tick ();
            creatures_tick ();
            party_tick ();
            magic_tick ();
            sound_tick ();
        }
        if (redraw & REDRAW_VIEW) {
            view_draw ();
        }
        if (redraw & REDRAW_PANEL) {
            panel_draw ();
        }
        redraw = 0;
        if (nchamps && !party_alive ()) {
            msg ("THE PARTY HAS PERISHED.");
            msg ("F3: LOAD A GAME  ANY OTHER KEY: QUIT");
            zones_clear ();
            zone (0, 24, 15, 1, 134);
            zone (0, 0, 40, 25, ' ');
            if (wait_key () != 134 || !load_game ()) {
                __asm__ ("jmp $FCE2");
            }
        }
    }
}
