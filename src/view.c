/*
** view.c - the party's 3D view, built as Dungeon Master builds it: DM's own pictures of the
** ceiling and floor, then of walls, doors and stairs, far to near (DMWAL, made by
** tools/dmview.py), drawn into the multicolour bitmap at the top left of the screen (bitmap.s).
** 28x17 cells, the size of DM's view (224x136), each cell's pixels in four colours: the
** background (grey), two greys every cell shares, and one of its own.
**
** Squares are seen at depths 0 (the party's own) to 3, and up to two squares to each side, drawn
** in DM's order. What isn't a picture of DM's yet (objects, wall decorations, pits, teleporters,
** projectiles) is drawn over it as small icons; creatures are sprites, made from DM's pictures.
*/

#include <string.h>
#include <c64.h>
#include "dm.h"

/* The squares in the order they're drawn (dmview.py's ORDER, then the party's own), and each
** one's piece of each kind in DMWAL (255: none). */
#define NSQUARES 14
static const signed char sq_k[NSQUARES] = { 3, 3, 3, 3, 3, 2, 2, 2, 1, 1, 1, 0, 0, 0 };
static const signed char sq_l[NSQUARES] = { -2, 2, -1, 1, 0, -1, 1, 0, -1, 1, 0, -1, 1, 0 };
static const unsigned char sq_frame[NSQUARES] = {        /* door frames; panels the first 9 */
    255, 255, 0, 1, 2, 3, 4, 5, 6, 7, 8, 255, 255, 9
};
static const unsigned char sq_stairs[NSQUARES] = {
    255, 255, 0, 2, 1, 3, 5, 4, 6, 8, 7, 9, 10, 255
};

/* Where things on a square's floor go, by depth 1-3 (index 0 unused): the cell row, the centre
** column and how far each square to the side is; and the rows a pit or teleporter covers. */
static const unsigned char floor_row[4] = { 16, 15, 12, 10 };
static const unsigned char side_cols[4] = { 0, 12, 9, 6 };
static const unsigned char half_cols[4] = { 0, 5, 4, 2 };
static const unsigned char tele_top[4] = { 0, 2, 4, 5 };
static const unsigned char air_row[4] = { 0, 8, 7, 6 };
#define CENTRE 14

static unsigned char sight;         /* how far ahead the party can see: the first wall, or 3 */
static unsigned char sprite_depth;  /* 1 or 2 when a creature is right ahead, else 0 */
static unsigned char sprite_type;

/* Creature sprites, DM's own pictures (DMSPR, copied in from far memory as needed):
**   sprites 0-3  the creature right ahead, a 2x2 block; two frames, the second mirrored,
**                swapped now and then as DM does
**   sprites 4-6  up to three more, farther off or to the sides, small
**   sprite 7     the mouse pointer */
#define MAXSMALL 3
#define BIG_BLOCK  ((HI_SPRITES - 0xC000) / 64)
#define SMALL_BLOCK (BIG_BLOCK + 8)
static unsigned char nsmall, small_x[MAXSMALL], small_y[MAXSMALL], small_type[MAXSMALL];
static unsigned char big_loaded = 255, small_loaded[MAXSMALL] = { 255, 255, 255 };
unsigned char creature_flash;

/* Draws one of DMWAL's pieces, or the top quarters of it (a door part of the way open). */
static void piece (unsigned char id, unsigned char quarters)
{
    static unsigned int w;
    static unsigned char n;
    unsigned long at;

    far_wall (&w, 2 + 2 * id, 2);
    at = (unsigned long) w * 2;
    far_wall (&n, at++, 1);
    n = (unsigned char) (n * quarters + 3) >> 2;
    while (n--) {
        far_wall (&w, at, 2);
        far_wall (view_cols, at + 2, w);
        at += 2 + w;
        blit_row (view_cols);
    }
}

/* An icon or letter (art.py) in cell (x, y), in a colour (255: the cell's own stays). */
static void icon (unsigned char x, unsigned char y, unsigned char glyph, unsigned char colour)
{
    if (x < VIEW_W && y < VIEW_H) {
        bm_col = x;
        bm_row = y;
        bm_colour = colour;
        stamp (glyph);
    }
}

static void block (unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1,
                   unsigned char glyph, unsigned char colour)
{
    unsigned char x;

    for (; y0 <= y1; ++y0) {
        for (x = x0; x <= x1; ++x) {
            icon (x, y0, glyph, colour);
        }
    }
}

static unsigned char item_glyph (unsigned int t)
{
    unsigned char oi = obj_info (t), f = obj_flags[oi];

    switch (TH_TYPE (t)) {
    case T_WEAPON:    return ICON_IT_WEAPON;
    case T_ARMOUR:    return ICON_IT_ARMOUR;
    case T_POTION:    return ICON_IT_POTION;
    case T_SCROLL:    return ICON_IT_SCROLL;
    case T_CONTAINER: return ICON_IT_CHEST;
    }
    if (oi >= 136 && oi <= 151) return ICON_IT_KEY;
    if (oi >= 133 && oi <= 135) return ICON_IT_COIN;
    return (f & OBJ_MOUTH) ? ICON_IT_FOOD : ICON_IT_JUNK;
}

static unsigned char item_colour (unsigned int t)
{
    switch (TH_TYPE (t)) {
    case T_WEAPON:  return COLOR_ORANGE;
    case T_ARMOUR:  return COLOR_LIGHTBLUE;
    case T_POTION:  return COLOR_LIGHTGREEN;
    case T_SCROLL:  return COLOR_WHITE;
    }
    return COLOR_YELLOW;
}

static void item_icon (unsigned char x, unsigned char y, unsigned int t)
{
    icon (x, y, item_glyph (t), item_colour (t));
}

/* A character's glyph in the tiny font (art.py: characters 32-95). */
static unsigned char font_glyph (unsigned char c)
{
    c &= 0x7F;
    if (c >= 96) {
        c -= 32;
    }
    return c < 32 ? 0 : c - 32;
}

/* Writing on the wall right ahead, in the tiny font, line by line as in Dungeon Master. */
static void wall_text (const char* s)
{
    unsigned char lines = 1, n, i, y;
    const char* p;

    for (p = s; *p; ++p) {
        lines += *p == '\n';
    }
    y = lines > 10 ? 2 : 7 - lines / 2;
    bm_ink = 0x55;                  /* the dark grey, carved into the light stone */
    while (*s && y < 14) {
        for (n = 0; s[n] && s[n] != '\n'; ++n) {
        }
        p = s + n;
        if (n > 18) {
            n = 18;
        }
        for (i = 0; i < n; ++i) {
            icon (CENTRE - n / 2 + i, y, font_glyph (s[i]), 255);
        }
        s = *p ? p + 1 : p;
        ++y;
    }
    bm_ink = 0xFF;
}

/* What's on the side of the wall ahead (depth k, 1 or 2) that faces the party. */
static void ornament (signed char sx, signed char sy, unsigned char k)
{
    unsigned int t = first_thing (sx, sy), *r, item = NONE, text = NONE;
    unsigned char cell = face_cell (), type, kind = 0, orn, y = k == 1 ? 7 : 6;
    enum { K_NONE, K_BUTTON, K_KEYHOLE, K_TEXT, K_TORCH, K_ALCOVE, K_MIRROR };

    for (; t != END; t = NEXT (t)) {
        if (TH_CELL (t) != cell) {
            continue;
        }
        r = thing_ptr (t);
        type = TH_TYPE (t);
        if (type == T_SENSOR) {
            orn = SENS_ORNAMENT (r);
            if (orn && wall_ornaments[lev][orn - 1] <= 3 && wall_ornaments[lev][orn - 1] >= 1) {
                kind = K_ALCOVE;
            }
            switch (SENS_TYPE (r)) {
            case 127: kind = K_MIRROR; break;
            case 1: if (!kind) kind = K_BUTTON; break;
            case 2: case 3: case 4: case 12: case 16: if (!kind) kind = K_KEYHOLE; break;
            case 13: if (!kind) kind = K_TORCH; break;
            }
        } else if (type == T_TEXT && (r[1] & 1)) {
            if (!kind) kind = K_TEXT;
            text = r[1] >> 3;
        } else if (type >= T_WEAPON && item == NONE) {
            item = t;
            if (!kind) kind = K_TORCH;
        }
    }
    switch (kind) {
    case K_ALCOVE:
        if (k == 1) {
            block (11, 7, 16, 9, ICON_SOLID, COLOR_BLACK);
        } else {
            block (12, 6, 15, 7, ICON_SOLID, COLOR_BLACK);
        }
        if (item != NONE) {
            item_icon (CENTRE, k == 1 ? 9 : 7, item);
        }
        break;
    case K_MIRROR:
        if (k == 1) {
            block (10, 3, 17, 10, ICON_SOLID, COLOR_LIGHTBLUE);
            block (11, 4, 16, 9, ICON_SOLID, COLOR_BLACK);
            icon (13, 6, ICON_FACE, COLOR_LIGHTRED);
            icon (14, 6, ICON_FACE, COLOR_LIGHTRED);
        } else {
            icon (CENTRE, y, ICON_FACE, COLOR_LIGHTBLUE);
        }
        break;
    case K_BUTTON:
        icon (CENTRE, y, ICON_BUTTON, COLOR_YELLOW);
        break;
    case K_KEYHOLE:
        icon (CENTRE, y, ICON_KEYHOLE, COLOR_YELLOW);
        break;
    case K_TEXT:
        if (k == 1) {
            wall_text (text_decode (text));
        } else {
            icon (CENTRE, y, ICON_TEXT, COLOR_WHITE);
        }
        break;
    case K_TORCH:
        if (item == NONE) {
            icon (CENTRE, y, ICON_TORCH, COLOR_BROWN);
        } else {
            item_icon (CENTRE, y, item);
        }
        break;
    }
}

static unsigned char is_wall (unsigned char s)
{
    return SQ_TYPE (s) == E_WALL || (SQ_TYPE (s) == E_FAKE && !(s & FAKE_OPEN));
}

static unsigned char square_at (signed char k, signed char l)
{
    return sq_at (px + k * dir_dx[pdir] + l * dir_dx[(pdir + 1) & 3],
                  py + k * dir_dy[pdir] + l * dir_dy[(pdir + 1) & 3]);
}

/* Is square (k, l) in sight: nothing nearer, ahead or on its side, that blocks the view? */
static unsigned char in_sight (unsigned char k, signed char l)
{
    unsigned char j, s;

    for (j = 1; j < k; ++j) {
        s = square_at (j, 0);
        if (is_wall (s) || (SQ_TYPE (s) == E_DOOR && (s & DOOR_STATE) && (s & DOOR_STATE) < 5)) {
            return 0;
        }
        if (l && is_wall (square_at (j, l))) {
            return 0;
        }
    }
    return 1;
}

static void square (unsigned char i)
{
    signed char k = sq_k[i], l = sq_l[i], sx, sy, a;
    unsigned char s, type, state, x, y, n;
    unsigned int t, *r;

    sx = px + k * dir_dx[pdir] + l * dir_dx[(pdir + 1) & 3];
    sy = py + k * dir_dy[pdir] + l * dir_dy[(pdir + 1) & 3];
    s = sq_at (sx, sy);
    if (k <= sight) {
        mark_seen (sx, sy);         /* for the map */
    }
    type = SQ_TYPE (s);

    if (is_wall (s)) {
        if (i < 13) {
            piece (PIECE_WALL + i, 4);
        }
        if (!l && (k == 1 || k == 2)) {
            ornament (sx, sy, k);
        }
        return;
    }
    x = CENTRE + l * side_cols[k];
    y = floor_row[k];

    switch (type) {
    case E_DOOR:
        if (((pdir & 1) == 0) != ((s & DOOR_NS) != 0) || sq_frame[i] == 255) {
            break;
        }
        piece (PIECE_DOOR_FRAME + sq_frame[i], 4);
        state = s & DOOR_STATE;
        for (t = first_thing (sx, sy); t != END && TH_TYPE (t) != T_DOOR; t = NEXT (t)) {
        }
        r = t == END ? 0 : thing_ptr (t);
        if (state && state < 5 && sq_frame[i] < 9) {
            /* Shut, or part of the way: the top of the door shows. */
            n = levels[lev].doors;
            if (r && (r[1] & 1)) {
                n >>= 4;
            }
            piece (PIECE_DOOR + (n & 15) * 9 + sq_frame[i], state);
        }
        if (r && (r[1] & 0x40) && !l) {
            icon (CENTRE + 7 - k * 2, y - 4 - k, ICON_BUTTON, COLOR_YELLOW);
        }
        break;
    case E_STAIRS:
        if (((pdir & 1) == 0) == ((s & STAIRS_NS) != 0) && sq_stairs[i] != 255) {
            piece (PIECE_STAIRS + ((s & STAIRS_UP) ? 0 : 11) + sq_stairs[i], 4);
        }
        break;
    case E_PIT:
        if ((s & (PIT_OPEN | PIT_INVISIBLE)) == PIT_OPEN && l > -2 && l < 2) {
            block (x - half_cols[k], y - 1, x + half_cols[k] - 1, y, ICON_SOLID, COLOR_BLACK);
        }
        break;
    case E_TELE:
        if ((s & (TELE_OPEN | TELE_VISIBLE)) == (TELE_OPEN | TELE_VISIBLE) && l > -2 && l < 2) {
            block (x - half_cols[k], tele_top[k], x + half_cols[k] - 1, y, ICON_TELE,
                   COLOR_LIGHTBLUE);
        }
        break;
    }
    if (k == 0 || l < -1 || l > 1) {
        return;
    }

    /* Objects lie on the floor half-way into the square; creatures stand there. */
    n = 0;
    for (t = first_thing (sx, sy); t != END && n < 2; t = NEXT (t)) {
        if (TH_TYPE (t) >= T_WEAPON) {
            item_icon (x - n, y, t);
            ++n;
        }
    }
    a = creature_at (sx, sy);
    if (a >= 0 && in_sight (k, l)) {
        type = th_creature[TH_INDEX (active[a].thing)][2] >> 8;
        if (!l && k < 3 && (!sprite_depth || k < sprite_depth)) {
            sprite_depth = k;
            sprite_type = type;
        } else if (nsmall < MAXSMALL) {
            small_x[nsmall] = x * 8 + 16;           /* centred on the cell, standing on it */
            small_y[nsmall] = y * 8 + 37;
            small_type[nsmall++] = type;
        }
    }
    a = projectile_at (sx, sy);
    if (a >= 0) {
        icon (x, air_row[k], ICON_BALL, projectile_colour (a));
    }
}

/* Sprite pointers and colours: the screen's own (the bitmap's colours share its memory). */
#define SPRITE_PTRS (SCREEN + 0x3F8)

void sprites_off (void)
{
    VIC.spr_ena &= 0x80;            /* the mouse pointer stays */
}

/* Each tick: the creature ahead shifts between its two frames, and shows white for a moment
** when it's hit. */
void sprites_animate (void)
{
    unsigned char q, frame, col;

    if (!(VIC.spr_ena & 0x0F)) {
        return;
    }
    frame = (game_time & 8) ? 4 : 0;
    col = creature_info[sprite_type].colour;
    if (creature_flash) {
        --creature_flash;
        col = COLOR_WHITE;
    }
    for (q = 0; q < 4; ++q) {
        SPRITE_PTRS[q] = BIG_BLOCK + frame + q;
        (&VIC.spr0_color)[q] = col;
    }
}

/* The small creatures become sprites; returns their sprite-enable bits. */
static unsigned char place_small (void)
{
    unsigned char i, bits = 0;

    for (i = 0; i < nsmall; ++i) {
        if (small_loaded[i] != small_type[i]) {
            sprite_fetch (small_type[i], 256, view_cols, 64);
            hi_read ((void*) (HI_SPRITES + 512 + i * 64), (unsigned) view_cols, 64);
            small_loaded[i] = small_type[i];
        }
        SPRITE_PTRS[4 + i] = SMALL_BLOCK + i;
        (&VIC.spr0_color)[4 + i] = creature_info[small_type[i]].colour;
        VIC.spr_pos[4 + i].x = small_x[i];
        VIC.spr_pos[4 + i].y = small_y[i];
        bits |= 0x10 << i;
    }
    return bits;
}

/* A multicolour sprite byte's four pixels, in the opposite order. */
static unsigned char mirror (unsigned char b)
{
    return (b << 6) | ((b << 2) & 0x30) | ((b >> 2) & 0x0C) | (b >> 6);
}

/* Copies the creature ahead's sprite in from far memory, and makes its second frame by
** mirroring it: each row of the right-hand blocks, reversed, becomes the left-hand one's. The
** copy comes through view_cols, as the sprite area under the I/O area can't be written directly. */
static void load_big (void)
{
    unsigned char* src = view_cols;
    unsigned char* back = view_chars;
    unsigned char half, row, b;
    unsigned int l, r;

    sprite_fetch (sprite_type, 0, src, 256);
    for (half = 0; half < 2; ++half) {          /* top blocks, then bottom */
        for (row = 0; row < 21; ++row) {
            l = half * 128 + row * 3;           /* left block's row; the right's is 64 on */
            r = l + 64;
            for (b = 0; b < 3; ++b) {
                back[l + b] = mirror (src[r + 2 - b]);
                back[r + b] = mirror (src[l + 2 - b]);
            }
        }
    }
    hi_read ((void*) HI_SPRITES, (unsigned) src, 256);
    hi_read ((void*) (HI_SPRITES + 256), (unsigned) back, 256);
    big_loaded = sprite_type;
}

static void place_sprites (unsigned char small_bits)
{
    unsigned char q, x, y, big;

    if (!sprite_depth) {
        VIC.spr_ena = (VIC.spr_ena & 0x80) | small_bits;
        return;
    }
    if (big_loaded != sprite_type) {
        load_big ();
    }
    big = sprite_depth == 1;
    x = big ? 88 : 112;             /* centred, standing on the floor half-way into the square */
    y = big ? 92 : 110;
    for (q = 0; q < 4; ++q) {
        VIC.spr_pos[q].x = x + ((q & 1) ? (big ? 48 : 24) : 0);
        VIC.spr_pos[q].y = y + ((q & 2) ? (big ? 42 : 21) : 0);
    }
    VIC.spr_hi_x &= 0x80;
    VIC.spr_exp_x = big ? 0x0F : 0;
    VIC.spr_exp_y = big ? 0x0F : 0;
    VIC.spr_ena = (VIC.spr_ena & 0x80) | small_bits | 0x0F;
    sprites_animate ();
}

void view_draw (void)
{
    unsigned char i, s;

    sprite_depth = 0;
    nsmall = 0;
    for (sight = 1; sight < 3; ++sight) {
        s = square_at (sight, 0);
        if (is_wall (s) || (SQ_TYPE (s) == E_DOOR && (s & DOOR_STATE) && (s & DOOR_STATE) < 5)) {
            break;
        }
    }
    piece (PIECE_BASE, 4);
    for (i = 0; i < NSQUARES; ++i) {
        square (i);
    }
    place_sprites (place_small ());
}
