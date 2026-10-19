/*
** save.c - saving and loading the game, in two slots on the game disk (DMSAVE1, DMSAVE2).
**
** A save is the party's state, then every level image straight from far memory: the levels hold
** everything else that changes - doors, pits, sensors, texts shown, creatures and where every
** object lies. About 33K, or 133 blocks.
*/

#include <string.h>
#include <c64.h>
#include "dm.h"
#include "farmem.h"

#define SAVE_VERSION 1

typedef struct {
    char magic[4];
    unsigned char version;
    unsigned long levels_total;         /* these levels must be the ones this game was built with */
    unsigned char lev, px, py, pdir, nchamps, nevents;
    unsigned int held, game_time;
    champion_t champ[4];
    unsigned int pool[POOL_SIZE][2];
    event_t events[MAXEVENTS];
} save_t;

static save_t s;
static unsigned char buf[256];
static char name[] = "dmsave1";

static const char magic[4] = { 'D', 'M', '6', '4' };

/* Asks for a slot; 0 if the player backs out. */
static unsigned char pick_slot (const char* what)
{
    unsigned char k;

    msg (what);
    msg ("SLOT 1 OR 2? (ANY OTHER KEY CANCELS)");
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
        n = left > sizeof buf ? sizeof buf : left;
        if (saving) {
            far_read (buf, p, n);
            if (hw_write (buf, n) != n) {
                return 0;
            }
        } else {
            if (hw_read (buf, n) != n) {
                return 0;
            }
            far_write (p, buf, n);
        }
    }
    return 1;
}

void save_game (void)
{
    unsigned char l, ok;

    if (!pick_slot ("SAVE THE GAME")) {
        return;
    }
    msg ("SAVING...");
    level_flush ();
    memcpy (s.magic, magic, sizeof magic);
    s.version = SAVE_VERSION;
    s.levels_total = LEVELS_TOTAL;
    s.lev = lev;
    s.px = px;
    s.py = py;
    s.pdir = pdir;
    s.nchamps = nchamps;
    s.nevents = nevents;
    s.held = held;
    s.game_time = game_time;
    memcpy (s.champ, champ, sizeof champ);
    memcpy (s.pool, pool, sizeof pool);
    memcpy (s.events, events, sizeof events);

    ok = hw_create (name) && hw_write (&s, sizeof s) == sizeof s;
    for (l = 0; ok && l < NLEVELS; ++l) {
        ok = copy_level (l, 1);
    }
    ok = hw_close_check () && ok;
    msg (ok ? "GAME SAVED" : "SAVE FAILED: IS THE DISK FULL OR PROTECTED?");
}

unsigned char load_game (void)
{
    unsigned char l;

    if (!pick_slot ("LOAD A GAME")) {
        return 0;
    }
    msg ("LOADING...");
    if (!hw_open (name) || hw_read (&s, sizeof s) != sizeof s ||
        memcmp (s.magic, magic, sizeof magic) || s.version != SAVE_VERSION ||
        s.levels_total != LEVELS_TOTAL) {
        hw_close ();
        msg ("THERE'S NO SAVED GAME IN THAT SLOT");
        return 0;
    }
    /* From here on the levels are being replaced: a failure leaves no game to go back to. */
    for (l = 0; l < NLEVELS; ++l) {
        if (!copy_level (l, 0)) {
            hw_close ();
            msg ("THE SAVED GAME IS DAMAGED. PRESS A KEY.");
            wait_key ();
            __asm__ ("jmp $FCE2");
        }
    }
    hw_close ();

    px = s.px;
    py = s.py;
    pdir = s.pdir;
    nchamps = s.nchamps;
    nevents = s.nevents;
    held = s.held;
    game_time = s.game_time;
    memcpy (champ, s.champ, sizeof champ);
    memcpy (pool, s.pool, sizeof pool);
    memcpy (events, s.events, sizeof events);
    level_reload (s.lev);
    creatures_scan ();
    VIC.bordercolor = COLOR_BLACK;
    redraw = REDRAW_VIEW | REDRAW_PANEL;
    msg ("GAME LOADED");
    return 1;
}
