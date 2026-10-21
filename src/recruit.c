/*
** recruit.c - taking on champions from the Hall of Champions' mirrors. An overlay (see
** overlay.c): it's only needed in front of a mirror.
**
** Each mirror is a sensor on a wall; the champion's details are a text on the floor in front of
** it, and their possessions lie on the mirror's side of the wall. The text is the champion's
** name, title, a blank line, gender, then health, stamina (in tenths) and mana as three 4-digit
** numbers, the seven statistics as 2-digit numbers, and sixteen skill levels as digits - all in
** hexadecimal, written with A for 0 to P for 15.
*/

#include <string.h>
#include <c64.h>
#include "dm.h"

#pragma code-name (push, "OVL1")
#pragma rodata-name (push, "OVL1")
#pragma data-name (push, "OVL1")

static const char* line_ptr[8];

static unsigned int hex (const char* s, unsigned char n)
{
    unsigned int v = 0;

    while (n--) {
        v = (v << 4) | ((*s++ - 'A') & 15);
    }
    return v;
}

static unsigned char split (const char* s)
{
    unsigned char n = 0;

    line_ptr[n++] = s;
    while (*s && n < 8) {
        if (*s++ == '\n') {
            line_ptr[n++] = s;
        }
    }
    return n;
}

static void copy_line (char* dst, const char* src, unsigned char max)
{
    while (*src && *src != '\n' && --max) {
        *dst++ = *src++;
    }
    *dst = 0;
}

static unsigned char place (champion_t* c, unsigned int t)
{
    unsigned char oi = obj_info (t), f = obj_flags[oi], s;

    if (TH_TYPE (t) == T_WEAPON && c->slot[SL_ACTION] == NONE) {
        s = SL_ACTION;
    } else if ((f & OBJ_HEAD) && c->slot[SL_HEAD] == NONE) {
        s = SL_HEAD;
    } else if ((f & OBJ_NECK) && c->slot[SL_NECK] == NONE) {
        s = SL_NECK;
    } else if ((f & OBJ_TORSO) && c->slot[SL_TORSO] == NONE) {
        s = SL_TORSO;
    } else if ((f & OBJ_LEGS) && c->slot[SL_LEGS] == NONE) {
        s = SL_LEGS;
    } else if ((f & OBJ_FEET) && c->slot[SL_FEET] == NONE) {
        s = SL_FEET;
    } else {
        for (s = SL_READY; s < NSLOTS && c->slot[s] != NONE; ++s) {
            if (s == SL_READY) {
                s = SL_PACK - 1;
            }
        }
        if (s == NSLOTS) {
            return 0;
        }
    }
    c->slot[s] = t;
    return 1;
}

static const char* floor_text (void)
{
    unsigned int t;
    const char* txt = 0;

    for (t = first_thing (px, py); t != END; t = NEXT (t)) {
        if (TH_TYPE (t) == T_TEXT) {
            txt = text_decode (thing_ptr (t)[1] >> 3);
        }
    }
    return txt;
}

/* Called with the party facing a mirror: shows the champion, and takes them on if asked. */
void ovl_recruit (void)
{
    unsigned int t, *sensor = 0;
    unsigned char ax = px + dir_dx[pdir], ay = py + dir_dy[pdir], cell = face_cell ();
    unsigned char i, k, n;
    const char* txt;
    champion_t* c;
    static char buf[24];
    unsigned int v;

    for (t = first_thing (ax, ay); t != END; t = NEXT (t)) {
        if (TH_CELL (t) == cell && TH_TYPE (t) == T_SENSOR && SENS_TYPE (thing_ptr (t)) == 127) {
            sensor = thing_ptr (t);
        }
    }
    txt = floor_text ();
    if (!sensor || !txt || split (txt) < 7) {
        return;
    }
    sprites_off ();
    ui_clear (0, 0, VIEW_W, VIEW_H);
    for (i = 0; i < 2; ++i) {
        copy_line (buf, line_ptr[i], sizeof buf);
        ui_print (2, 3 + i * 2, i ? 7 : 1, buf);
    }
    ui_print (2, 8, 15, "HEALTH");
    ui_print (2, 9, 15, "STAMINA");
    ui_print (2, 10, 15, "MANA");
    for (i = 0; i < 3; ++i) {
        v = hex (line_ptr[4] + i * 4, 4);
        if (i == 1) {
            v /= 10;
        }
        buf[3] = 0;
        buf[2] = '0' + v % 10;
        v /= 10;
        buf[1] = v ? '0' + v % 10 : ' ';
        v /= 10;
        buf[0] = v ? '0' + v % 10 : ' ';
        ui_print (11, 8 + i, 1, buf);
    }
    zones_clear ();
    zone (0, 0, VIEW_W, VIEW_H, 'n');
    if (nchamps == 4) {
        ui_print (2, 14, 10, "THE PARTY IS FULL");
        wait_key ();
        return;
    }
    ui_print (2, 13, 13, "RECRUIT? (Y/N)");
    ui_print (2, 15, 1, "[YES]");
    ui_print (9, 15, 1, "[NO]");
    zones_clear ();
    zone (2, 15, 5, 1, 'y');
    zone (9, 15, 4, 1, 'n');
    k = wait_key ();
    if (k != 'y') {
        return;
    }

    /* text_decode's buffer has been reused by now; decode the text again. */
    split (floor_text ());
    c = &champ[nchamps++];
    memset (c, 0, sizeof *c);
    copy_line (c->name, line_ptr[0], sizeof c->name);
    copy_line (c->title, line_ptr[1], sizeof c->title);
    c->hp = c->maxhp = hex (line_ptr[4], 4);
    c->sta = c->maxsta = hex (line_ptr[4] + 4, 4) / 10;
    c->mana = c->maxmana = hex (line_ptr[4] + 8, 4);
    for (i = 0; i < 7; ++i) {
        c->stat[i] = (unsigned char) hex (line_ptr[5] + i * 2, 2);
    }
    for (i = 0; i < 16; ++i) {
        n = (unsigned char) hex (line_ptr[6] + i, 1);
        if (n > c->skill[i >> 2]) {
            c->skill[i >> 2] = n;
        }
    }
    c->food = 200;
    for (i = 0; i < NSLOTS; ++i) {
        c->slot[i] = NONE;
    }

    /* Their possessions, from the mirror's side of the wall. */
    t = first_thing (ax, ay);
    while (t != END) {
        unsigned int next = NEXT (t), mine;
        if (TH_CELL (t) == cell && TH_TYPE (t) >= T_WEAPON) {
            list_remove (ax, ay, t);
            mine = to_party (t);
            if (mine == NONE || !place (c, mine)) {
                if (mine != NONE) {
                    t = to_level (mine);
                }
                list_add (px, py, t);
            }
        }
        t = next;
    }
    sensor[1] &= 0xFF80;            /* an empty mirror now */
    msg (c->name);
    msg ("JOINS THE PARTY");
}

/* The help screen, here as there's room (overlay.c). The help text is in tools/text.py, kept in the RAM under the KERNAL. */
void ovl_help (void)
{
    unsigned char i;

    ui_clear (0, 0, 40, 22);
    ui_print (2, 0, COLOR_WHITE, "DUNGEON MASTER 64");
    for (i = 0; i < NHELP; ++i) {
        ui_print (0, 2 + i, COLOR_GRAY3, hi_str (text_at (TXT_HELP + i * 2)));
    }
    zones_clear ();
    zone (0, 0, 40, 25, ' ');
    wait_key ();
    redraw = REDRAW_VIEW | REDRAW_PANEL;
}

/* No pops here: cc65 writes string literals at the end of the file, into the segments then
** in force, and they belong in the overlay. */
