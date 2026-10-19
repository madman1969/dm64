/*
** view.c - the party's 3D view, drawn in characters.
**
** The view is 24x22 characters, centred between columns 11 and 12 and rows 10 and 11. Squares
** are seen at depths 0 (the party's own) to 3, and up to two squares to each side. The near face
** of the squares at depth k lies on plane k, which reaches plane_w[k] columns and plane_h[k] rows
** either side of the centre. Each step in depth narrows a plane by as many columns as rows, so the
** walls' top and bottom edges run at exactly 45 degrees and the triangle glyphs fit them.
** Squares are drawn far to near, outside in, so nearer ones cover farther ones.
*/

#include <string.h>
#include <c64.h>
#include "dm.h"

#define CX 12
#define CY 11

static const unsigned char plane_w[5] = { 12, 8, 5, 3, 2 };
static const unsigned char plane_h[5] = { 11, 7, 4, 2, 1 };
static const unsigned char mid_w[4] = { 10, 6, 4, 2 };      /* half-way into each square */
static const unsigned char mid_h[4] = { 9, 5, 3, 1 };
static const unsigned char front_col[4] = { COLOR_WHITE, COLOR_GRAY3, COLOR_GRAY2, COLOR_GRAY1 };
static const unsigned char side_col[3] = { COLOR_GRAY2, COLOR_GRAY1, COLOR_GRAY1 };
static const signed char lateral_order[5] = { -2, 2, -1, 1, 0 };

static unsigned int row_offset[VIEW_H];

static unsigned char fc, fcol;      /* what fill and put draw */
static signed char sx, sy;          /* the square being drawn */
static unsigned char sprite_depth;  /* 1 or 2 when a creature is right ahead, else 0 */
static unsigned char sprite_type;

static void fill (signed char x0, signed char y0, signed char x1, signed char y1)
{
    unsigned char x, y;
    unsigned int p;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > VIEW_W) x1 = VIEW_W;
    if (y1 > VIEW_H) y1 = VIEW_H;
    for (y = y0; (signed char) y < y1; ++y) {
        p = row_offset[y] + x0;
        for (x = x0; (signed char) x < x1; ++x, ++p) {
            view_chars[p] = fc;
            view_cols[p] = fcol;
        }
    }
}

static void put (signed char x, signed char y, unsigned char ch, unsigned char col)
{
    unsigned int p;

    if (x < 0 || x >= VIEW_W || y < 0 || y >= VIEW_H) {
        return;
    }
    p = row_offset[y] + x;
    view_chars[p] = ch;
    view_cols[p] = col;
}

static void front_face (signed char k, signed char l)
{
    signed char w = plane_w[k], h = plane_h[k];

    fc = G_BRICK;
    fcol = front_col[k];
    fill (CX + (2 * l - 1) * w, CY - h, CX + (2 * l + 1) * w, CY + h);
}

static void side_face (signed char k, signed char l)
{
    signed char j, x, t, b, n;

    fc = G_SIDE;
    fcol = side_col[k];
    if (l == 2 || l == -2) {
        /* Only ever seen at depth 2, at the very edge of the view. */
        x = l > 0 ? CX + 3 * plane_w[3] : 0;
        fill (x, CY - plane_h[3] - 1, x + 3, CY + plane_h[3] + 1);
        return;
    }
    n = plane_w[k] - plane_w[k + 1];
    for (j = 0; j < n; ++j) {
        t = CY - plane_h[k + 1] - j;
        b = CY + plane_h[k + 1] + j;
        if (l > 0) {
            x = CX + plane_w[k + 1] + j;
            put (x, t - 1, G_TRI_BR, fcol);
            fill (x, t, x + 1, b);
            put (x, b, G_TRI_TR, fcol);
        } else {
            x = CX - plane_w[k + 1] - 1 - j;
            put (x, t - 1, G_TRI_BL, fcol);
            fill (x, t, x + 1, b);
            put (x, b, G_TRI_TL, fcol);
        }
    }
}

static unsigned char item_glyph (unsigned int t)
{
    unsigned char oi = obj_info (t), f = obj_flags[oi];

    switch (TH_TYPE (t)) {
    case T_WEAPON:    return G_IT_WEAPON;
    case T_ARMOUR:    return G_IT_ARMOUR;
    case T_POTION:    return G_IT_POTION;
    case T_SCROLL:    return G_IT_SCROLL;
    case T_CONTAINER: return G_IT_CHEST;
    }
    if (oi >= 136 && oi <= 151) return G_IT_KEY;
    if (oi >= 133 && oi <= 135) return G_IT_COIN;
    return (f & OBJ_MOUTH) ? G_IT_FOOD : G_IT_JUNK;
}

static unsigned char item_colour (unsigned int t)
{
    switch (TH_TYPE (t)) {
    case T_WEAPON:  return COLOR_GRAY3;
    case T_ARMOUR:  return COLOR_LIGHTBLUE;
    case T_POTION:  return COLOR_LIGHTGREEN;
    case T_SCROLL:  return COLOR_WHITE;
    }
    return COLOR_YELLOW;
}

/* The objects on the floor of square (sx, sy): up to two, side by side. */
static void floor_items (signed char x, signed char y)
{
    unsigned int t = first_thing (sx, sy);
    unsigned char n = 0, type;

    while (t != END && n < 2) {
        type = TH_TYPE (t);
        if (type >= T_WEAPON) {
            put (x - n, y, item_glyph (t), item_colour (t));
            ++n;
        }
        t = NEXT (t);
    }
}

/* Writing on the wall right ahead, carved into it line by line as in Dungeon Master. */
static void wall_text (const char* s)
{
    unsigned char lines = 1, n, y;
    const char* p;

    for (p = s; *p; ++p) {
        lines += *p == '\n';
    }
    y = lines > 12 ? 5 : 11 - lines / 2;
    while (*s && y < 17) {
        for (n = 0; s[n] && s[n] != '\n'; ++n) {
        }
        p = s + n;
        if (n > 16) {
            n = 16;
        }
        for (fc = 0; fc < n; ++fc) {
            put (CX - n / 2 + fc, y, scr_code (s[fc]), COLOR_WHITE);
        }
        s = *p ? p + 1 : p;
        ++y;
    }
}

/* What's on the side of the wall at (sx, sy) that faces the party, drawn on its front face. */
static void ornament (signed char k)
{
    unsigned int t = first_thing (sx, sy), *r, item = NONE, text = NONE;
    unsigned char cell = face_cell (), type, kind = 0, orn;
    signed char y;
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
    y = k == 1 ? 0 : 1;
    switch (kind) {
    case K_ALCOVE:
        fc = ' ';
        if (k == 1) {
            fill (9, 9, 15, 13);
        } else {
            fill (11, 10, 13, 12);
        }
        if (item != NONE) {
            put (12, 12 - y, item_glyph (item), item_colour (item));
        }
        break;
    case K_MIRROR:
        if (k == 1) {
            fc = ' ';
            fill (9, 7, 15, 14);
            put (9, 7, 112, COLOR_LIGHTBLUE);
            put (14, 7, 110, COLOR_LIGHTBLUE);
            put (9, 13, 109, COLOR_LIGHTBLUE);
            put (14, 13, 125, COLOR_LIGHTBLUE);
            for (y = 10; y < 14; ++y) {
                put (y, 7, 64, COLOR_LIGHTBLUE);
                put (y, 13, 64, COLOR_LIGHTBLUE);
            }
            for (y = 8; y < 13; ++y) {
                put (9, y, 93, COLOR_LIGHTBLUE);
                put (14, y, 93, COLOR_LIGHTBLUE);
            }
            put (11, 10, G_FACE, COLOR_LIGHTRED);
            put (12, 10, G_FACE, COLOR_LIGHTRED);
        } else {
            put (12, 11, G_FACE, COLOR_LIGHTBLUE);
        }
        break;
    case K_BUTTON:
        put (12, 11, G_BUTTON, COLOR_YELLOW);
        break;
    case K_KEYHOLE:
        put (12, 11, G_KEYHOLE, COLOR_YELLOW);
        break;
    case K_TEXT:
        if (k == 1) {
            wall_text (text_decode (text));
        } else {
            put (12, 11, G_TEXT, COLOR_WHITE);
        }
        break;
    case K_TORCH:
        put (12, 10 + y, item == NONE ? G_TORCH : item_glyph (item),
             item == NONE ? COLOR_BROWN : (TH_TYPE (item) == T_WEAPON ? COLOR_ORANGE : item_colour (item)));
        break;
    }
}

static void square (signed char k, signed char l)
{
    unsigned char s, type, state, rows;
    signed char x0, x1, y0, y1, w, h;
    signed char a;
    unsigned int t;

    sx = px + k * dir_dx[pdir] + l * dir_dx[(pdir + 1) & 3];
    sy = py + k * dir_dy[pdir] + l * dir_dy[(pdir + 1) & 3];
    s = sq_at (sx, sy);
    type = SQ_TYPE (s);

    if (type == E_WALL || (type == E_FAKE && !(s & FAKE_OPEN))) {
        if (k) {
            front_face (k, l);
        }
        if (l && k < 3) {
            side_face (k, l);
        }
        if (!l && (k == 1 || k == 2)) {
            ornament (k);
        }
        return;
    }
    if (k == 0) {
        return;
    }

    w = mid_w[k];
    h = mid_h[k];
    x0 = CX + (2 * l - 1) * w;
    x1 = CX + (2 * l + 1) * w;
    y0 = CY - h;
    y1 = CY + h;

    switch (type) {
    case E_PIT:
        if ((s & (PIT_OPEN | PIT_INVISIBLE)) == PIT_OPEN) {
            fc = ' ';
            fill (CX + (2 * l - 1) * plane_w[k + 1], CY + plane_h[k + 1],
                  CX + (2 * l + 1) * plane_w[k + 1], CY + plane_h[k]);
        }
        break;
    case E_STAIRS:
        fc = G_STAIRS;
        fcol = (s & STAIRS_UP) ? COLOR_WHITE : COLOR_GRAY2;
        fill (x0, y0 + h / 2, x1, y1);
        if (k == 1 && !l) {
            put (CX, y0, (s & STAIRS_UP) ? G_ARROW_U : G_ARROW_D, COLOR_YELLOW);
        }
        break;
    case E_TELE:
        if ((s & (TELE_OPEN | TELE_VISIBLE)) == (TELE_OPEN | TELE_VISIBLE)) {
            fc = G_TELE;
            fcol = COLOR_LIGHTBLUE;
            fill (x0, y0, x1, y1);
        }
        break;
    case E_DOOR:
        if (((pdir & 1) == 0) != ((s & DOOR_NS) != 0)) {
            break;
        }
        fc = G_SOLID;
        fcol = front_col[k];
        fill (x0, y0, x0 + 1, y1);
        fill (x1 - 1, y0, x1, y1);
        fill (x0, y0, x1, y0 + 1);
        state = s & DOOR_STATE;
        if (state && state < 5) {
            rows = (unsigned char) (((y1 - y0 - 1) * state) >> 2);
            fc = G_DOOR;
            fcol = COLOR_BROWN;
            fill (x0 + 1, y0 + 1, x1 - 1, y0 + 1 + rows);
        }
        for (t = first_thing (sx, sy); t != END; t = NEXT (t)) {
            if (TH_TYPE (t) == T_DOOR && (thing_ptr (t)[1] & 0x40)) {
                put (x1 - 1, CY, G_BUTTON, COLOR_YELLOW);
            }
        }
        break;
    }

    /* Objects lie on the floor half-way into the square; creatures stand there. */
    x0 = CX + l * (plane_w[k] + plane_w[k + 1]);
    y0 = (2 * CY + plane_h[k] + plane_h[k + 1]) / 2;
    floor_items (x0, y0);
    a = creature_at (sx, sy);
    if (a >= 0) {
        if (!l && k < 3 && (!sprite_depth || k < sprite_depth)) {
            sprite_depth = k;
            sprite_type = th_creature[TH_INDEX (active[a].thing)][2] >> 8;
        } else {
            put (x0, y0 - 1, G_MONSTER,
                 creature_info[th_creature[TH_INDEX (active[a].thing)][2] >> 8].colour);
        }
    }
}

void sprites_off (void)
{
    VIC.spr_ena = 0;
}

/* Is the way ahead clear to depth k (no wall or closed door in between)? */
static unsigned char clear_to (unsigned char k)
{
    unsigned char i, s;

    for (i = 1; i <= k; ++i) {
        s = sq_at (px + i * dir_dx[pdir], py + i * dir_dy[pdir]);
        if (SQ_TYPE (s) == E_WALL || (SQ_TYPE (s) == E_FAKE && !(s & FAKE_OPEN))) {
            return 0;
        }
        if (i < k && SQ_TYPE (s) == E_DOOR && (s & DOOR_STATE) && (s & DOOR_STATE) < 5) {
            return 0;
        }
    }
    return 1;
}

static void place_sprites (void)
{
    unsigned char shape, col, q, x, y, big;
    unsigned char* ptrs = SCREEN + 0x3F8;

    if (!sprite_depth || !clear_to (sprite_depth)) {
        VIC.spr_ena = 0;
        return;
    }
    shape = creature_info[sprite_type].shape;
    col = creature_info[sprite_type].colour;
    big = sprite_depth == 1;
    x = big ? 72 : 96;
    y = big ? 98 : 124;
    for (q = 0; q < 4; ++q) {
        ptrs[q] = (HI_SPRITES - 0xC000) / 64 + shape * 4 + q;
        (&VIC.spr0_color)[q] = col;
        VIC.spr_pos[q].x = x + ((q & 1) ? (big ? 48 : 24) : 0);
        VIC.spr_pos[q].y = y + ((q & 2) ? (big ? 42 : 21) : 0);
    }
    VIC.spr_hi_x = 0;
    VIC.spr_exp_x = big ? 0x0F : 0;
    VIC.spr_exp_y = big ? 0x0F : 0;
    VIC.spr_ena = 0x0F;
}

void view_draw (void)
{
    signed char k, i;
    unsigned char y;
    unsigned char* scr = SCREEN;
    unsigned char* col = COLOUR;
    static unsigned char inited;

    if (!inited) {
        inited = 1;
        for (y = 0; y < VIEW_H; ++y) {
            row_offset[y] = y * VIEW_W;
        }
    }

    memset (view_chars, ' ', CY * VIEW_W);
    memset (view_cols, COLOR_BLACK, CY * VIEW_W);
    memset (view_chars + CY * VIEW_W, G_FLOOR, (VIEW_H - CY) * VIEW_W);
    memset (view_cols + CY * VIEW_W, COLOR_GRAY1, (VIEW_H - CY) * VIEW_W);

    sprite_depth = 0;
    for (k = 3; k >= 0; --k) {
        for (i = 0; i < 5; ++i) {
            if ((lateral_order[i] == 2 || lateral_order[i] == -2) && k < 2) {
                continue;
            }
            square (k, lateral_order[i]);
        }
    }

    for (y = 0; y < VIEW_H; ++y) {
        memcpy (scr, view_chars + row_offset[y], VIEW_W);
        memcpy (col, view_cols + row_offset[y], VIEW_W);
        scr += 40;
        col += 40;
    }
    place_sprites ();
}
