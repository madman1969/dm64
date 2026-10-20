/*
** dungeon.c - the dungeon's levels, their squares, and their lists of things.
**
** All 14 levels live in far memory (FarMem: an REU or a SuperCPU), each a self-contained image
** made by tools/dmconv.py. The party's level is copied into level_buf, and copied back when
** they leave it, so the rest of the game works on it with ordinary pointers.
**
** As in Dungeon Master, only squares with bit 4 set have a list, and the first thing of each
** list is kept in firsts[], in column order: colidx[x] is where column x's lists start. Adding a
** list to a square that had none (dropping an object on it, say) inserts into firsts[] and moves
** every later column's start along by one.
**
** Objects the party carries are kept apart from the levels, in pool[], so they can travel from
** one level to another: picking an object up moves its record there, dropping it moves it back.
*/

#include <string.h>
#include "dm.h"
#include "farmem.h"

typedef struct {
    unsigned int size, nfirsts, cap_firsts;
    unsigned int count[6];              /* doors, teleporters, texts, sensors, creatures, objects */
    unsigned int off_sq, off_colidx, off_firsts, off_table[6], off_seen;
} image_t;

static unsigned char level_buf[LEVEL_MAX];
#define IMG ((image_t*) level_buf)
static farptr level_far[NLEVELS];

unsigned char lev = 255, lw, lh;
unsigned int levels_visited;        /* a bit for each level the party has been on */
unsigned char px, py, pdir;
const signed char dir_dx[4] = { 0, 1, 0, -1 };
const signed char dir_dy[4] = { -1, 0, 1, 0 };

static unsigned char* lsq;
static unsigned char* lseen;
static unsigned int* lcolidx;
static unsigned int* lfirsts;
unsigned int (*th_door)[2];
unsigned int (*th_tele)[3];
unsigned int (*th_text)[2];
unsigned int (*th_sensor)[4];
unsigned int (*th_creature)[8];
unsigned int (*th_obj)[2];
unsigned int pool[POOL_SIZE][2];

/* The overlays: code needed only now and then (overlay.c's list), linked to run in one area of
** ordinary memory and kept in far memory, each copied in when it's called. The linker's config
** (c64-dm64.cfg) defines the area; the build puts them in the DMOVL file, each padded to its
** size. */
#define NOVERLAYS 4
extern unsigned char _OVL1_START__[], _OVL1_SIZE__[];
#define OVL_START ((void*) _OVL1_START__)
#define OVL_SIZE ((unsigned int) _OVL1_SIZE__)
static farptr overlay_far, sprites_far;
static unsigned char overlay_in = 255;

void overlay (unsigned char n)
{
    if (n != overlay_in) {
        far_read (OVL_START, overlay_far + (unsigned long) n * OVL_SIZE, OVL_SIZE);
        overlay_in = n;
    }
}

/* Copies a data file into far memory at p, through level_buf; returns where it ended, or 0. */
static farptr load_far (const char* name, farptr p, unsigned long size)
{
    unsigned int n;

    if (!hw_open (name)) {
        return 0;
    }
    for (; size; size -= n, p += n) {
        n = size > LEVEL_MAX ? LEVEL_MAX : (unsigned int) size;
        if (hw_read (level_buf, n) != n) {
            hw_close ();
            return 0;
        }
        far_write (p, level_buf, n);
    }
    hw_close ();
    return p;
}

/* Puts the level images (DMLEV), overlays (DMOVL) and creature sprites (DMSPR) in far memory,
** one after another, past the few bytes FarMem's allocator keeps at the start (unused here).
** Returns 0 if they don't fit, or can't be read. */
unsigned char dungeon_init (void)
{
    unsigned char l;
    farptr p = 16;

    for (l = 0; l < POOL_SIZE; ++l) {
        pool[l][0] = NONE;
    }
    if (far_init () <= FAR_NEAR ||
        far_size () < p + LEVELS_TOTAL + (unsigned long) NOVERLAYS * OVL_SIZE + SPRITES_TOTAL) {
        return 0;
    }
    for (l = 0; l < NLEVELS; ++l) {
        level_far[l] = p;
        p += level_size[l];
    }
    overlay_far = load_far ("dmlev", 16, LEVELS_TOTAL);
    if (!overlay_far) {
        return 0;
    }
    sprites_far = load_far ("dmovl", overlay_far, (unsigned long) NOVERLAYS * OVL_SIZE);
    return sprites_far && load_far ("dmspr", sprites_far, SPRITES_TOTAL);
}

/* Copies part of a creature's sprites (DMSPR: big, mirrored big, small) to where the video
** chip will show them, in the RAM under the KERNAL: a far copy writes straight there. */
void sprite_fetch (unsigned char type, unsigned int offset, void* dst, unsigned int len)
{
    far_read (dst, sprites_far + (unsigned long) type * SPRITE_SET + offset, len);
}

void level_flush (void)
{
    if (lev != 255) {
        far_write (level_far[lev], level_buf, level_size[lev]);
    }
}

void level_reload (unsigned char l)
{
    lev = 255;
    level_enter (l);
}

unsigned long level_far_addr (unsigned char l)
{
    return level_far[l];
}

/* Puts every level back as the disk has it, for loading a saved game over. */
unsigned char levels_reload (void)
{
    lev = 255;
    return load_far ("dmlev", 16, LEVELS_TOTAL) != 0;
}

void level_enter (unsigned char l)
{
    if (l == lev) {
        return;
    }
    level_flush ();
    far_read (level_buf, level_far[l], level_size[l]);
    lev = l;
    levels_visited |= 1 << l;
    lw = levels[l].w;
    lh = levels[l].h;
    lsq = level_buf + IMG->off_sq;
    lseen = level_buf + IMG->off_seen;
    lcolidx = (unsigned int*) (level_buf + IMG->off_colidx);
    lfirsts = (unsigned int*) (level_buf + IMG->off_firsts);
    th_door = (void*) (level_buf + IMG->off_table[0]);
    th_tele = (void*) (level_buf + IMG->off_table[1]);
    th_text = (void*) (level_buf + IMG->off_table[2]);
    th_sensor = (void*) (level_buf + IMG->off_table[3]);
    th_creature = (void*) (level_buf + IMG->off_table[4]);
    th_obj = (void*) (level_buf + IMG->off_table[5]);
}

unsigned char* sq_ptr (unsigned char x, unsigned char y)
{
    return lsq + (unsigned int) x * lh + y;
}

/* The map: which squares the party has seen, a bit each, kept with the level. */
static const unsigned char bit[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

void mark_seen (unsigned char x, unsigned char y)
{
    if (x < lw && y < lh) {
        lseen[(x << 2) | (y >> 3)] |= bit[y & 7];
    }
}

unsigned char was_seen (unsigned char x, unsigned char y)
{
    return lseen[(x << 2) | (y >> 3)] & bit[y & 7];
}

unsigned char sq_at (unsigned char x, unsigned char y)
{
    /* Coordinates arrive as unsigned, so one step off the top or left edge is 255. */
    if (x >= lw || y >= lh) {
        return 0;
    }
    return *sq_ptr (x, y);
}

/* Where square (x, y)'s first thing is in firsts[]: its column's start, plus one for each square
** above it in the column that also has a list. */
static unsigned int first_index (unsigned char x, unsigned char y)
{
    unsigned char* col = lsq + (unsigned int) x * lh;
    unsigned int k = lcolidx[x];
    unsigned char i;

    for (i = 0; i < y; ++i) {
        if (col[i] & SQ_THINGS) {
            ++k;
        }
    }
    return k;
}

unsigned int first_thing (unsigned char x, unsigned char y)
{
    if (!(sq_at (x, y) & SQ_THINGS)) {
        return END;
    }
    return lfirsts[first_index (x, y)];
}

static void set_first (unsigned char x, unsigned char y, unsigned int t)
{
    unsigned char* sq = sq_ptr (x, y);
    unsigned int k = first_index (x, y), n = IMG->nfirsts;
    unsigned char c;

    if (*sq & SQ_THINGS) {
        if (t != END) {
            lfirsts[k] = t;
            return;
        }
        memmove (&lfirsts[k], &lfirsts[k + 1], (n - k - 1) * 2);
        --IMG->nfirsts;
        for (c = x + 1; c <= lw; ++c) {
            --lcolidx[c];
        }
        *sq &= ~SQ_THINGS;
    } else if (t != END && n < IMG->cap_firsts) {
        memmove (&lfirsts[k + 1], &lfirsts[k], (n - k) * 2);
        lfirsts[k] = t;
        ++IMG->nfirsts;
        for (c = x + 1; c <= lw; ++c) {
            ++lcolidx[c];
        }
        *sq |= SQ_THINGS;
    }
}

unsigned int* thing_ptr (unsigned int t)
{
    unsigned int i = TH_INDEX (t);

    switch (TH_TYPE (t)) {
    case T_DOOR:     return th_door[i];
    case T_TELE:     return th_tele[i];
    case T_TEXT:     return th_text[i];
    case T_SENSOR:   return th_sensor[i];
    case T_CREATURE: return th_creature[i];
    }
    return i >= POOL_BASE ? pool[i - POOL_BASE] : th_obj[i];
}

void list_add (unsigned char x, unsigned char y, unsigned int t)
{
    unsigned int cur = first_thing (x, y), next;

    NEXT (t) = END;
    if (cur == END) {
        set_first (x, y, t);
        return;
    }
    while ((next = NEXT (cur)) != END) {
        cur = next;
    }
    NEXT (cur) = t;
}

void list_remove (unsigned char x, unsigned char y, unsigned int t)
{
    unsigned int cur = first_thing (x, y), prev = NONE;

    while (cur != END) {
        if (cur == t) {
            if (prev == NONE) {
                set_first (x, y, NEXT (cur));
            } else {
                NEXT (prev) = NEXT (cur);
            }
            NEXT (t) = END;
            return;
        }
        prev = cur;
        cur = NEXT (cur);
    }
}

/* ---- objects moving between a level and the party ------------------------------------------- */

/* Moves object t's record (and a chest's contents) from where it is to a free record in dst,
** which has room for n; returns its new thing, base being dst's first object number. */
static unsigned int move_object (unsigned int t, unsigned int (*dst)[2], unsigned int n,
                                 unsigned int base)
{
    unsigned int i, *from = thing_ptr (t), inside, moved, *last = 0, c;

    for (i = 0; i < n && dst[i][0] != NONE; ++i) {
    }
    if (i == n) {
        return NONE;
    }
    dst[i][0] = END;
    dst[i][1] = from[1];
    from[0] = NONE;                     /* its old record is free */
    moved = TH_MAKE (TH_TYPE (t), base + i, 0);
    if (TH_TYPE (t) == T_CONTAINER) {
        inside = dst[i][1];
        dst[i][1] = END;
        while (inside != END && inside != NONE) {
            c = NEXT (inside);
            t = move_object (inside, dst, n, base);
            if (t != NONE) {
                if (last) {
                    *last = t;
                } else {
                    thing_ptr (moved)[1] = t;
                }
                last = thing_ptr (t);
            }
            inside = c;
        }
    }
    return moved;
}

unsigned int to_party (unsigned int t)
{
    return move_object (t, pool, POOL_SIZE, POOL_BASE);
}

unsigned int to_level (unsigned int t)
{
    return move_object (t, th_obj, IMG->count[5], 0);
}

/* A new object for the party, conjured by a spell. */
unsigned int pool_new (unsigned char type, unsigned int attributes)
{
    unsigned char i;

    for (i = 0; i < POOL_SIZE; ++i) {
        if (pool[i][0] == NONE) {
            pool[i][0] = END;
            pool[i][1] = attributes;
            return TH_MAKE (type, POOL_BASE + i, 0);
        }
    }
    return NONE;
}

void pool_free (unsigned int t)
{
    unsigned int* r = thing_ptr (t), inside;

    if (TH_TYPE (t) == T_CONTAINER) {
        for (inside = r[1]; inside != END && inside != NONE; inside = NEXT (inside)) {
            thing_ptr (inside)[0] = NONE;
        }
    }
    r[0] = NONE;
}

/* ---- objects ---------------------------------------------------------------------------------- */

unsigned char obj_info (unsigned int t)
{
    unsigned int w;

    if (t >= END) {
        return 255;
    }
    w = thing_ptr (t)[1];
    switch (TH_TYPE (t)) {
    case T_SCROLL:    return 0;
    case T_CONTAINER: return 1;
    case T_POTION:    return 2 + ((w >> 8) & 0x7F);
    case T_WEAPON:    return 23 + (w & 0x7F);
    case T_ARMOUR:    return 69 + (w & 0x7F);
    case T_JUNK:      return 127 + (w & 0x7F);
    }
    return 255;
}

unsigned char obj_icon_of (unsigned int t)
{
    unsigned char i = obj_info (t);
    return i == 255 ? 255 : obj_icon[i];
}

#pragma bss-name (push, "LOWBSS")
static char text_buf[256];
static unsigned int text_words[96];
#pragma bss-name (pop)

const char* obj_name (unsigned int t)
{
    unsigned char icon = obj_icon_of (t), i;

    text_buf[0] = 0;
    if (icon != 255) {
        hi_read (text_buf, name_offset[icon], 16);
        for (i = 0; i < 16; ++i) {
            if (text_buf[i] & 0x80) {
                text_buf[i] &= 0x7F;
                text_buf[i + 1] = 0;
                break;
            }
        }
    }
    return text_buf;
}

/* Dungeon texts pack three 5-bit codes to a word: 0-25 letters, 26 space, 27 '.', 28 a new
** line, 30 then a code for a common word, 31 the end. */
static const char* const escapes[4] = { "?", "!", "THE ", "YOU " };

const char* text_decode (unsigned int offset)
{
    unsigned char n = 0, w = 0, shift, code, esc = 0;
    const char* e;

    hi_read (text_words, HI_TEXT + offset * 2, sizeof text_words);
    while (w < 96 && n < 250) {
        for (shift = 10; shift != 0xFB; shift -= 5) {
            code = (unsigned char) (text_words[w] >> shift) & 31;
            if (esc) {
                esc = 0;
                if (code < 4) {
                    for (e = escapes[code]; *e; ++e) {
                        text_buf[n++] = *e;
                    }
                }
            } else if (code < 26) {
                text_buf[n++] = 'A' + code;
            } else if (code == 26) {
                text_buf[n++] = ' ';
            } else if (code == 27) {
                text_buf[n++] = '.';
            } else if (code == 28) {
                text_buf[n++] = '\n';
            } else if (code == 31) {
                goto done;
            } else {
                esc = 1;
            }
        }
        ++w;
    }
done:
    text_buf[n] = 0;
    return text_buf;
}

/* Can the party (or a creature) stand on (x, y)? */
unsigned char passable (unsigned char x, unsigned char y)
{
    unsigned char s = sq_at (x, y);

    switch (SQ_TYPE (s)) {
    case E_WALL:
        return 0;
    case E_DOOR:
        s &= DOOR_STATE;
        return s == 0 || s == 5;
    case E_FAKE:
        return (s & (FAKE_OPEN | FAKE_IMAGINARY)) != 0;
    }
    return 1;
}

unsigned char face_cell (void)
{
    return (pdir + 2) & 3;
}
