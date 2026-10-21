/*
** view.c - the party's 3D view, built as Dungeon Master builds it: DM's own pictures of the
** ceiling and floor, then far to near each square's walls and their ornaments, doors, stairs,
** pits, floor ornaments, objects, spells in flight and teleporter fields (DMWAL, made by
** tools/dmview.py), drawn into the multicolour bitmap at the top left of the screen (bitmap.s).
** 28x17 cells, the size of DM's view (224x136), each cell's pixels in four colours: the
** background (grey), two greys every cell shares, and one of its own.
**
** Squares are seen at depths 0 (the party's own) to 3, and up to two squares to each side, drawn
** in DM's order. Creatures are sprites, made from DM's pictures.
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
static const unsigned char door_button[NSQUARES] = {      /* D3R, D3C, D2C, D1C */
    255, 255, 255, 0, 1, 255, 255, 2, 255, 255, 3, 255, 255, 255
};
static const unsigned char sq_vsq[NSQUARES] = {         /* view squares, as objects see them */
    255, 255, 1, 2, 0, 4, 5, 3, 7, 8, 6, 255, 255, 9
};
static const unsigned char sq_pos[NSQUARES] = {         /* floor places (dmview.py) */
    255, 255, 0, 2, 1, 3, 5, 4, 6, 8, 7, 9, 11, 10
};
static const unsigned char sq_stairs[NSQUARES] = {
    255, 255, 0, 2, 1, 3, 5, 4, 6, 8, 7, 9, 10, 255
};

/* Where things on a square's floor go, by depth 1-3 (index 0 unused): the cell row, the centre
** column and how far each square to the side is (where small creatures stand). */
static const unsigned char floor_row[4] = { 16, 15, 12, 10 };
static const unsigned char side_cols[4] = { 0, 12, 9, 6 };
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
static void piece (unsigned int id, unsigned char quarters)
{
    static unsigned int w;
    static unsigned char n;
    static unsigned long at;

    at = 0;
    far_wall (&at, 2 + 3 * id, 3);
    far_wall (&n, at++, 1);
    n = (unsigned char) (n * quarters + 3) >> 2;
    while (n--) {
        far_wall (&w, at, 2);
        far_wall (view_cols, at + 2, w);
        at += 2 + w;
        blit_row (view_cols);
    }
}

/* ---- objects: DM's own pictures (dmview.py's object_sprite), placed as DM places them ---- */

/* DMWAL's tables for placing objects (dmview.py's object_tables): each set's places, each
** aspect's set and flags, each object type's aspect. */
#define OT_PLACES     0
#define OT_ASPECT_SET 300
#define OT_ASPECT     386
#define AS_MIRROR     16        /* mirrored on the right */
#define AS_ALCOVE     32        /* has a picture of its own for an alcove */
#define CELL_ALCOVE   4
static unsigned long obj_tables;

/* How far objects in a pile shift from their place (ScummVM's G0217, G0223). */
static const signed char shift_sets[3][8] = {
    { 0, 1, 2, 3, 0, -3, -2, -1 }, { 0, 1, 1, 2, 0, -2, -1, -1 }, { 0, 1, 1, 1, 0, -1, -1, -1 }
};
static const unsigned char pile_shift[16] = {           /* x << 4 | y */
    0x25, 0x06, 0x57, 0x30, 0x71, 0x12, 0x63, 0x33, 0x55, 0x26, 0x77, 0x10, 0x31, 0x62, 0x13, 0x53
};
static unsigned char pile;      /* the next object's shift */

static void sprite (unsigned int id, int x, int top, unsigned char centred);

/* A spell in flight in view square vsq (DM draws them half-way up, in the square's front
** places). */
static void projectile (unsigned char p, unsigned char vsq)
{
    static const unsigned char kind[8] = { 0, 2, 1, 2, 2, 2, 3, 3 };
    static unsigned char place[4];
    int x;

    far_wall (place, obj_tables + OT_PLACES + vsq * 10 + 4, 4);    /* set 0, places 2 and 3 */
    x = !place[0] ? place[2] : !place[2] ? place[0] : (place[0] + place[2]) >> 1;
    if (x) {
        sprite (PIECE_PROJECTILE + kind[proj[p].type & 7] * 4 + vsq / 3, x, 47, 1);
    }
}

/* Object t in view square vsq (D3C, D3L, D3R, D2C, D2L, D2R, D1C, D1L, D1R, D0C) at place cell
** (0-3 as DM numbers them from the party's view, 4 an alcove). */
static void object (unsigned int t, unsigned char vsq, unsigned char cell)
{
    static unsigned char place[2];
    unsigned char oi = obj_info (t), aspect, info, size, variant = 0, lane = vsq % 3;
    int x, top;

    if (oi >= 180) {
        return;
    }
    far_wall (&aspect, obj_tables + OT_ASPECT + oi, 1);
    far_wall (&info, obj_tables + OT_ASPECT_SET + aspect, 1);
    far_wall (place, obj_tables + OT_PLACES + ((info & 15) * 10 + vsq) * 10 + cell * 2, 2);
    if (!place[1]) {
        return;                                 /* out of sight */
    }
    if (cell == CELL_ALCOVE && (info & AS_ALCOVE) && lane == 0) {
        variant = 2;
    } else if ((info & AS_MIRROR) && (lane == 2 || (lane == 0 && (cell == 1 || cell == 2)))) {
        variant = 1;
    }
    /* As at depth 1 when near, smaller farther off. */
    size = vsq == 9 || (vsq >= 6 && cell >= 2) ? 0 : vsq >= 6 || (vsq >= 3 && cell >= 2) ? 1 : 2;
    x = place[0] + shift_sets[size][pile_shift[pile] >> 4];
    top = place[1] + shift_sets[size][pile_shift[pile] & 15];
    ++pile;
    pile = cell == CELL_ALCOVE ? (pile >= 14 ? 2 : pile) : pile & 15;

    sprite (PIECE_OBJ + (aspect * 3 + size) * 3 + variant, x, top, 0);
}

/* Draws one of DMWAL's object sprites (dmview.py's object_sprite) with the middle of its bottom
** at (x, y) in DM's pixels, or if centred its middle at (x, y). */
static void sprite (unsigned int id, int x, int top, unsigned char centred)
{
    static unsigned char hdr[3];
    static unsigned long at;
    unsigned char r, n, h;

    at = 0;
    far_wall (&at, 2 + 3 * id, 3);
    far_wall (hdr, at, 3);                      /* width in double pixels, height, colour */
    at += 3;
    h = hdr[1];
    top -= centred ? h >> 1 : h - 1;
    x = (x - hdr[0] + 1) >> 1;                  /* the left edge, in double pixels */
    n = ((hdr[0] + 3) >> 2) * 2;                /* bytes a row */
    ob_shift = x & 3;
    ob_colour = hdr[2];
    for (r = 0; r < h; ++r, ++top, at += n) {
        if (top < 0) {
            continue;
        }
        if (top >= VIEW_H * 8) {
            break;
        }
        far_wall (view_cols, at, n);
        ob_col = x >> 2;
        ob_cells = n >> 1;
        ob_y = top;
        object_row (view_cols);
    }
}

/* The objects in each place of square (sx, sy), seen as view square vsq, far places first. */
static void objects (unsigned char sx, unsigned char sy, unsigned char vsq)
{
    unsigned char c, abs;
    unsigned int t;

    for (c = 0; c < 4; ++c) {
        abs = (c + pdir) & 3;
        pile = (abs & 1) << 3;
        for (t = first_thing (sx, sy); t != END; t = NEXT (t)) {
            if (TH_CELL (t) == abs && TH_TYPE (t) >= T_WEAPON) {
                object (t, vsq, c);
            }
        }
    }
}

/* The objects in an alcove on face `face` of wall square (x, y), seen as view square vsq. */
static void alcove_objects (unsigned char x, unsigned char y, unsigned char face, unsigned char vsq)
{
    unsigned int t;

    pile = 2 + ((face & 1) << 3);
    for (t = first_thing (x, y); t != END; t = NEXT (t)) {
        if (TH_CELL (t) == face && TH_TYPE (t) >= T_WEAPON) {
            object (t, vsq, CELL_ALCOVE);
        }
    }
}

/* Writing on the wall right ahead, in DM's inscription font (dmview.py), centred, its lines at
** the heights DM puts them (to the nearest cell). */
static void wall_text (const char* s)
{
    static const unsigned char line_row[4] = { 5, 7, 9, 10 };
    unsigned char line = 0, n, i, c;
    const char* p;

    while (*s && line < 4) {
        for (n = 0; s[n] && s[n] != '\n'; ++n) {
        }
        p = s + n;
        if (n > VIEW_W) {
            n = VIEW_W;
        }
        bm_row = line_row[line++];
        for (i = 0; i < n; ++i) {
            /* The dungeon's texts are ASCII (and cc65 would make 'A' PETSCII). */
            c = (s[i] & 0x7F) | 0x20;           /* lower case */
            if (c >= 0x61 && c <= 0x7A) {
                c -= 0x61;                      /* the letters come first */
            } else if (c == 0x2E) {
                c = 27;                         /* a full stop */
            } else {
                continue;                       /* a space, or one it doesn't have */
            }
            bm_col = CENTRE - n / 2 + i;
            stamp_cell (c);
        }
        s = *p ? p + 1 : p;
    }
}

/* The place (dmview.py's 13) where each square's side and front faces are seen, if they are. */
static const unsigned char vw_side[NSQUARES] = {
    255, 255, 0, 1, 255, 5, 6, 255, 10, 11, 255, 255, 255, 255
};
static const unsigned char vw_front[NSQUARES] = {
    255, 255, 2, 4, 3, 7, 9, 8, 255, 255, 12, 255, 255, 255
};
#define VW_D1C 12
static const unsigned char vw_vsq[13] = {               /* fronts: their view square */
    255, 255, 1, 0, 2, 255, 255, 4, 3, 5, 255, 255, 6
};

/* The ornament on side `face` of wall square (x, y), by DM's rule: walls that allow it get one
** of the level's first few ornaments at random (always the same for that side), unless a sensor
** or writing on that side says otherwise. Returns the ornament's number in DM's 60 (alcoves,
** switches, locks...), or 255 for none; writing is DM's ornament 0, the unreadable inscription.
** It notes the writing and a mirror's champion. */
static unsigned int orn_text;
static unsigned char orn_champion;

static unsigned char wall_orn (unsigned char x, unsigned char y, unsigned char face, unsigned char s)
{
    unsigned char ord = 0, n = levels[lev].orns, type, fake = SQ_TYPE (s) == E_FAKE;
    unsigned int r, t, *p;

    if (s & (fake ? 8 : 8 >> face)) {
        r = (((((x * 32 + 2000 + (y + 1) * (face + 1)) * 31417u) >> 1) +
              (3000 + lev * 64 + lw + lh) * 11 + ORN_SEED) >> 2) % 30;
        if (r < (n >> 4)) {
            ord = r + 1;
            if (fake && WALL_ORN (r) >= 1 && WALL_ORN (r) <= 3) {
                ord = 0;                        /* no alcoves in fake walls */
            }
        }
    }
    orn_champion = 0;
    for (t = first_thing (x, y); t != END; t = NEXT (t)) {
        if (TH_CELL (t) != face) {
            continue;
        }
        p = thing_ptr (t);
        type = TH_TYPE (t);
        if (type == T_TEXT && (p[1] & 1)) {
            ord = 255;
            orn_text = p[1] >> 3;
        } else if (type == T_SENSOR) {
            ord = SENS_ORNAMENT (p);
            if (SENS_TYPE (p) == 127) {
                orn_champion = (unsigned char) SENS_DATA (p) + 1;
            }
        }
    }
    if (ord == 255) {
        return 0;
    }
    return ord && ord <= (n & 15) ? WALL_ORN (ord - 1) : 255;
}

/* Draws what's on one face of a wall square, at place vw. */
static void ornament (unsigned char x, unsigned char y, unsigned char face, unsigned char vw,
                      unsigned char s)
{
    unsigned char orn = wall_orn (x, y, face, s);

    if (orn == 255) {
        return;
    }
    if (vw == VW_D1C && orn == 0) {
        wall_text (text_decode (orn_text));
        return;
    }
    piece (PIECE_ORN + orn * 13 + vw, 4);
    if (vw == VW_D1C && orn_champion) {
        piece (PIECE_PORTRAIT + orn_champion - 1, 4);
    }
    if (orn >= 1 && orn <= 3 && vw_vsq[vw] != 255) {
        alcove_objects (x, y, face, vw_vsq[vw]);
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

/* The floor ornament of floor square (x, y) (grates, pressure pads, moss...), by DM's rule:
** corridors that allow it get one of the level's first few at random, unless a sensor there
** says otherwise. Returns its number in DM's floor ornaments, or 255. */
static unsigned char floor_orn (unsigned char x, unsigned char y, unsigned char s)
{
    unsigned char ord = 0, n = levels[lev].floor_orns;
    unsigned int r, t;

    if (SQ_TYPE (s) == E_FLOOR && (s & 8)) {
        r = (((((x * 32 + 2000 + y) * 31417u) >> 1) +
              (3000 + lev * 64 + lw + lh) * 11 + ORN_SEED) >> 2) % 30;
        if (r < (n >> 4)) {
            ord = r + 1;
        }
    }
    for (t = first_thing (x, y); t != END; t = NEXT (t)) {
        if (TH_TYPE (t) == T_SENSOR) {
            ord = SENS_ORNAMENT (thing_ptr (t));
        }
    }
    return ord && ord <= (n & 15) ? FLOOR_ORN (ord - 1) : 255;
}

/* A teleporter's sparkling field, drawn over what's in the square. */
static void field (unsigned char s, unsigned char pos)
{
    if (SQ_TYPE (s) == E_TELE && (s & (TELE_OPEN | TELE_VISIBLE)) == (TELE_OPEN | TELE_VISIBLE) &&
        pos != 255) {
        piece (PIECE_FIELD + pos, 4);
    }
}

/* Which of DMWAL's doors a door is (dmconv.py's door_kinds): its level's door set for its type,
** and its ornament, or the broken door. DOOR_KINDS if none is. */
static unsigned char door_kind (unsigned int* r, unsigned char broken)
{
    unsigned char set = levels[lev].doors, orn = DOOR_ORN_NONE, kind, i;

    if (r) {
        if (r[1] & 1) {
            set >>= 4;
        }
        i = (r[1] >> 1) & 15;
        if (i) {
            orn = DOOR_ORN (i - 1);
        }
    }
    if (broken) {
        orn = DOOR_ORN_BROKEN;
    }
    kind = (set & 15) | orn << 4;
    for (i = 0; i < DOOR_KINDS && door_kinds[i] != kind; ++i) {
    }
    return i;
}

static void square (unsigned char i)
{
    signed char k = sq_k[i], l = sq_l[i], sx, sy, a;
    unsigned char s, type, state, x, y, n, pos = sq_pos[i];
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
            if (vw_side[i] != 255) {
                ornament (sx, sy, (pdir + (l < 0 ? 1 : 3)) & 3, vw_side[i], s);
            }
            if (vw_front[i] != 255) {
                ornament (sx, sy, (pdir + 2) & 3, vw_front[i], s);
            }
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
        if (state && sq_frame[i] < 9) {
            /* Shut, or part of the way: the top of the door shows. Bashed down, it shows
            ** broken. */
            n = door_kind (r, state == 5);
            if (n < DOOR_KINDS) {
                piece (PIECE_DOOR + n * 9 + sq_frame[i], state == 5 ? 4 : state);
            }
        }
        if (r && (r[1] & 0x40) && door_button[i] != 255) {
            piece (PIECE_DOOR_BUTTON + door_button[i], 4);
        }
        break;
    case E_STAIRS:
        if (((pdir & 1) == 0) == ((s & STAIRS_NS) != 0) && sq_stairs[i] != 255) {
            piece (PIECE_STAIRS + ((s & STAIRS_UP) ? 0 : 11) + sq_stairs[i], 4);
        }
        break;
    case E_PIT:
        if ((s & PIT_OPEN) && pos != 255) {
            if (!(s & PIT_INVISIBLE)) {
                piece (PIECE_PIT + pos, 4);
            } else if (pos >= 3) {
                piece (PIECE_PIT + 12 + pos, 4);    /* only seen from nearer */
            }
        }
        /* fall through */
    case E_FLOOR:
    case E_TELE:
    case E_FAKE:
        if (pos < 9) {
            n = floor_orn (sx, sy, s);
            if (n < 9) {
                piece (PIECE_FLOOR_ORN + n * 9 + pos, 4);
            }
        }
        break;
    }
    if (sq_vsq[i] != 255) {
        objects (sx, sy, sq_vsq[i]);
    }
    if (k == 0 || l < -1 || l > 1) {
        field (s, pos);
        return;
    }

    /* Creatures stand half-way into the square. */
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
    if (a >= 0 && sq_vsq[i] != 255) {
        projectile (a, sq_vsq[i]);
    }
    field (s, pos);
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
    col = light_colours[light_level][creature_info[sprite_type].colour];
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
        (&VIC.spr0_color)[4 + i] = light_colours[light_level][creature_info[small_type[i]].colour];
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

/* In the dark (light.c) the view's colours become the light level's (dmview.py's light_colours):
** the bitmap's shared ones, and each cell's own. The buttons beside it keep theirs. */
static void light_show (void)
{
    const unsigned char* m = light_colours[light_level];
    unsigned char x, y, *c;

    split_bg = m[12];
    VIC.spr_mcolor1 = m[15];
    for (y = 0; y < VIEW_H; ++y) {
        memset (SCREEN + y * 40, m[11] << 4 | m[15], VIEW_W);
        if (light_level) {
            c = COLOUR + y * 40;
            for (x = 0; x < VIEW_W; ++x) {
                c[x] = m[c[x] & 15];
            }
        }
    }
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
    if (!obj_tables) {
        far_wall (&obj_tables, 2 + 3 * PIECE_OBJ_TABLES, 3);
    }
    piece (PIECE_BASE, 4);
    for (i = 0; i < NSQUARES; ++i) {
        square (i);
    }
    light_show ();
    place_sprites (place_small ());
}
