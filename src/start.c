/*
** start.c - what the game does once, as it starts, after DMOVL is in far memory (dungeon.c):
** the rest of its data, and setting up the video chip. In overlay 4 with the unpacker, out of
** the way of the game's own memory.
*/

#include <string.h>
#include <cbm.h>
#include <c64.h>
#include "dm.h"

#pragma code-name (push, "OVL5")
#pragma rodata-name (push, "OVL5")
#pragma data-name (push, "OVL5")
#pragma bss-name (push, "OVL5")

#define CPU_PORT   (*(unsigned char*) 0x0001)
#define CHARSET    ((unsigned char*) 0xD000)
#define HIBASE     (*(unsigned char*) 0x0288)
#define CASE_LOCK  (*(unsigned char*) 0x0291)

/* Loads DMHI: chunks of (address, length, bytes) into the RAM under the ROMs. The bytes come
** through a buffer, as the KERNAL can't write under the I/O area itself. */
unsigned char hw_load_data (void)
{
    static unsigned char buf[128];
    unsigned int hdr[2], n, len, addr;

    if (!hw_open ("dmhi")) {
        return 0;
    }
    while (hw_read (hdr, 4) == 4 && hdr[0]) {
        addr = hdr[0];
        len = hdr[1];
        while (len) {
            n = len > sizeof buf ? sizeof buf : len;
            if (hw_read (buf, n) != n) {
                hw_close ();
                return 0;
            }
            hi_read ((void*) addr, (unsigned) buf, n);
            addr += n;
            len -= n;
        }
    }
    hw_close ();
    return 1;
}

void hw_init (void)
{
    unsigned char port = CPU_PORT;

    /* The C64's own letters, digits and punctuation (its first 64 characters), for text: the
    ** next 64 are our own glyphs, loaded with DMHI. With I/O switched out the character ROM is
    ** at $D000 for reading, and writing there reaches the RAM underneath: so it copies onto
    ** itself. */
    __asm__ ("sei");
    CPU_PORT = port & ~0x04;
    memcpy (CHARSET, CHARSET, 64 * 8);
    CPU_PORT = port;
    __asm__ ("cli");

    memset (SCREEN, ' ', 1000);
    memset (COLOUR, COLOR_WHITE, 1000);
    HIBASE = (unsigned) SCREEN >> 8;
    CASE_LOCK = 0x80;
    VIC.bordercolor = COLOR_BLACK;
    VIC.bgcolor0 = COLOR_BLACK;
    /* Video bank 3: screen at $CC00 (offset $0C00, in 1K steps), characters at $D000 (offset
    ** $1000, in 2K steps). */
    VIC.addr = (3 << 4) | (2 << 1);
    CIA2.pra &= 0xFC;

    /* Sprites 0-3 are creatures, in multicolour; sprite 7 is the mouse pointer (mouse.s), whose
    ** image DMHI puts at HI_POINTER. */
    VIC.spr_mcolor = 0x7F;
    VIC.spr_mcolor0 = COLOR_BLACK;          /* shared by every creature: dmimg.py's SHARED0, 1 */
    VIC.spr_mcolor1 = COLOR_GRAY3;
    VIC.spr_ena = 0;
    VIC.spr7_color = COLOR_WHITE;
    SCREEN[0x3FF] = (HI_POINTER - 0xC000) / 64;
    irq_init ();

    sound_init ();
}

/* Everything at once: 0 if DMHI can't be loaded. */
unsigned char ovl_start (void)
{
    if (!hw_load_data ()) {
        return 0;
    }
    hw_init ();
    return 1;
}

/* No pops here: cc65 writes string literals at the end of the file, into the segments then in
** force, and they belong in the overlay. */
