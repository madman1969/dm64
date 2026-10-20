/*
** unpack.c - unpacks a data file packed by tools/dmview.py (lz_pack) into far memory as it loads:
** DMWAL, the view's pieces, which wouldn't fit on the disk otherwise. In overlay 2, with casting,
** as it's only needed once, while the game starts.
**
** A byte below $80 is followed by that many plus one bytes to copy; otherwise its bits 2-6 plus
** 3 are a length, and its bits 0-1 with the next byte, plus 1, how far back to copy from - never
** more than 1024 bytes, so the bytes go through a 1K ring on their way to far memory. The work is
** done by unpackasm.s, which calls back here for more input and to empty the ring.
*/

#include "dm.h"
#include "farmem.h"

#pragma code-name (push, "OVL3")
#pragma rodata-name (push, "OVL3")
#pragma data-name (push, "OVL3")
#pragma bss-name (push, "OVL3")

#define RING 1024

unsigned int __fastcall__ unpack_run (unsigned char* ring);

static unsigned char* ring;
static unsigned long at;

unsigned char unpack_refill (void)
{
    return hw_read (ring + RING, 255);
}

void unpack_flush (void)
{
    far_write (at, ring, RING);
    at += RING;
}

/* Unpacks the file into far memory at dst, size bytes, using buf (at least RING + 510 bytes) on
** the way; returns 0 if it couldn't be read. */
unsigned char ovl_unpack (const char* name, unsigned long dst, unsigned long size, unsigned char* buf)
{
    unsigned int n;

    if (!hw_open (name)) {
        return 0;
    }
    ring = (unsigned char*) (((unsigned int) buf + 255) & 0xFF00);
    at = dst;
    n = unpack_run (ring);
    far_write (at, ring, n);
    hw_close ();
    return at + n - dst == size;
}
