/*
** save.c - saving and loading the game, in two slots on the game disk (DMSAVE1, DMSAVE2). An
** overlay (see overlay in dungeon.c).
**
** A save is a header, the champions, the party's objects and the waiting events, then every
** level image straight from far memory: the levels hold everything else that changes - doors,
** pits, sensors, texts shown, creatures and where every object lies. About 33K, or 133 blocks.
** Each part is read and written straight from where it lives, so saving needs no copy of it.
*/

#include <string.h>
#include <cbm.h>
#include <c64.h>
#include "dm.h"
#include "farmem.h"

#pragma code-name (push, "OVL4")
#pragma rodata-name (push, "OVL4")
#pragma data-name (push, "OVL4")

#define SAVE_VERSION 2
#define DEVICE (*(unsigned char*) 0x00BA < 8 ? 8 : *(unsigned char*) 0x00BA)

typedef struct {
    char magic[4];
    unsigned char version;
    unsigned long levels_total;         /* these levels must be the ones this game was built with */
    unsigned char lev, px, py, pdir, nchamps, nevents;
    unsigned int held, game_time;
} header_t;

static header_t h;
static char name[] = "dmsave1";
static const char magic[4] = { 'D', 'M', '6', '4' };

/* The parts after the header. */
static void* const part_ptr[3] = { champ, pool, events };
static const unsigned int part_size[3] = { sizeof champ, sizeof pool, sizeof events };

/* Disk transfers go through the view's colour buffer: the view is drawn again afterwards. */
#define BUF      view_cols
#define BUF_SIZE 256

/* ---- writing files --------------------------------------------------------------------------- */

static unsigned int put (const void* buf, unsigned int len)
{
    int n = cbm_write (2, buf, len);
    return n < 0 ? 0 : n;
}

/* Replaces a file: scratches the old one first, rather than trusting the 1541's "@0:" save-
** with-replace, then opens the new one for writing as a PRG with a dummy load address, so that
** hw_open reads it back. */
static unsigned char create (void)
{
    static char cmd[20] = "s0:";
    unsigned int load = 0;

    strcpy (cmd + 3, name);
    cbm_open (15, DEVICE, 15, cmd);
    cbm_close (15);
    if (cbm_open (2, DEVICE, 1, name) != 0) {
        cbm_close (2);
        return 0;
    }
    return put (&load, 2) == 2;
}

/* Closes the file and asks the drive how it went: its status starts "00" when all is well. */
static unsigned char close_check (void)
{
    char status[2] = { 0, 0 };

    cbm_close (2);
    if (cbm_open (15, DEVICE, 15, "") != 0) {
        cbm_close (15);
        return 0;
    }
    cbm_read (15, status, 2);
    cbm_close (15);
    return status[0] == '0' && status[1] == '0';
}

/* ---- saving and loading ---------------------------------------------------------------------- */

/* Asks for a slot; 0 if the player backs out. */
static unsigned char pick_slot (const char* what)
{
    unsigned char k;

    msg (what);
    msg ("SLOT 1 OR 2? (ANY OTHER KEY CANCELS)");
    zones_clear ();
    zone (4, 24, 3, 1, '1');
    zone (9, 24, 3, 1, '2');
    zone (0, 0, 40, 25, ' ');
    k = wait_key ();
    if (k != '1' && k != '2') {
        msg ("CANCELLED");
        return 0;
    }
    name[6] = k;
    return 1;
}

/* Copies a level's image between far memory and the open file, a buffer at a time. */
static unsigned char copy_level (unsigned char l, unsigned char saving)
{
    farptr p = level_far_addr (l);
    unsigned int left = level_size[l], n;

    for (; left; left -= n, p += n) {
        n = left > BUF_SIZE ? BUF_SIZE : left;
        if (saving) {
            far_read (BUF, p, n);
            if (put (BUF, n) != n) {
                return 0;
            }
        } else {
            if (hw_read (BUF, n) != n) {
                return 0;
            }
            far_write (p, BUF, n);
        }
    }
    return 1;
}

void ovl_save (void)
{
    unsigned char i, ok;

    if (!pick_slot ("SAVE THE GAME")) {
        return;
    }
    msg ("SAVING...");
    level_flush ();
    memcpy (h.magic, magic, sizeof magic);
    h.version = SAVE_VERSION;
    h.levels_total = LEVELS_TOTAL;
    h.lev = lev;
    h.px = px;
    h.py = py;
    h.pdir = pdir;
    h.nchamps = nchamps;
    h.nevents = nevents;
    h.held = held;
    h.game_time = game_time;

    ok = create () && put (&h, sizeof h) == sizeof h;
    for (i = 0; ok && i < 3; ++i) {
        ok = put (part_ptr[i], part_size[i]) == part_size[i];
    }
    for (i = 0; ok && i < NLEVELS; ++i) {
        ok = copy_level (i, 1);
    }
    ok = close_check () && ok;
    redraw |= REDRAW_VIEW;
    msg (ok ? "GAME SAVED" : "SAVE FAILED: IS THE DISK FULL OR PROTECTED?");
}

static void damaged (void)
{
    hw_close ();
    msg ("THE SAVED GAME IS DAMAGED. PRESS A KEY.");
    wait_key ();
    __asm__ ("jmp $FCE2");
}

unsigned char ovl_load (void)
{
    unsigned char i;

    if (!pick_slot ("LOAD A GAME")) {
        return 0;
    }
    msg ("LOADING...");
    if (!hw_open (name) || hw_read (&h, sizeof h) != sizeof h ||
        memcmp (h.magic, magic, sizeof magic) || h.version != SAVE_VERSION ||
        h.levels_total != LEVELS_TOTAL) {
        hw_close ();
        msg ("THERE'S NO SAVED GAME IN THAT SLOT");
        return 0;
    }
    /* From here on the game is being replaced: a failure leaves none to go back to. */
    for (i = 0; i < 3; ++i) {
        if (hw_read (part_ptr[i], part_size[i]) != part_size[i]) {
            damaged ();
        }
    }
    for (i = 0; i < NLEVELS; ++i) {
        if (!copy_level (i, 0)) {
            damaged ();
        }
    }
    hw_close ();

    px = h.px;
    py = h.py;
    pdir = h.pdir;
    nchamps = h.nchamps;
    nevents = h.nevents;
    held = h.held;
    game_time = h.game_time;
    nproj = 0;
    shield = 0;
    shield_ticks = invisible_ticks = 0;
    level_reload (h.lev);
    creatures_scan ();
    VIC.bordercolor = COLOR_BLACK;
    redraw = REDRAW_VIEW | REDRAW_PANEL;
    msg ("GAME LOADED");
    return 1;
}

#pragma data-name (pop)
#pragma rodata-name (pop)
#pragma code-name (pop)
