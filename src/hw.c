/*
** hw.c - the C64 itself: the video bank, the RAM under the ROMs, the data file, and sound.
**
** Memory map while the game runs:
**   $0400-$07FF  buffers (the default screen's RAM, free once the screen moves)
**   $0801-$CFFF  the program and the dungeon
**   $D000-$DFFF  the dungeon's texts, in the RAM under the I/O area
**   $E000-$E3FF  the screen         } in the RAM under the KERNAL, which the video chip
**   $E800-$EFFF  the character set  } always sees: video bank 3
**   $F000-$F7FF  creature sprites   }
**   $F800-$FFF9  object names       }
** The texts and names are read with the ROMs and I/O switched out (hi_read).
*/

#include <string.h>
#include <cbm.h>
#include <c64.h>
#include "dm.h"

#pragma bss-name (push, "LOWBSS")
unsigned char view_chars[VIEW_W * VIEW_H];
#pragma bss-name (pop)
unsigned char view_cols[VIEW_W * VIEW_H];

#define CPU_PORT   (*(unsigned char*) 0x0001)
#define CHARSET    ((unsigned char*) 0xE800)
#define CHAR_ROM   ((unsigned char*) 0xD000)
#define HIBASE     (*(unsigned char*) 0x0288)
#define CASE_LOCK  (*(unsigned char*) 0x0291)
#define LAST_DEVICE (*(unsigned char*) 0x00BA)

/* Copies with every ROM and the I/O area switched out, so all 64K is RAM; interrupts are off
** meanwhile, as their handler needs the KERNAL. Writes need no switching (they always reach the
** RAM under a ROM), except under the I/O area: hi_read is used for those too. */
void hi_read (void* dst, unsigned src, unsigned len)
{
    unsigned char port = CPU_PORT;

    __asm__ ("sei");
    CPU_PORT = 0x34;
    memcpy (dst, (void*) src, len);
    CPU_PORT = port;
    __asm__ ("cli");
}

/* The data files, read from the drive the game was loaded from, skipping their load address. */
unsigned char hw_open (const char* name)
{
    unsigned int skip;

    if (cbm_open (2, LAST_DEVICE < 8 ? 8 : LAST_DEVICE, 0, name) != 0) {
        cbm_close (2);
        return 0;
    }
    return cbm_read (2, &skip, 2) == 2;
}

unsigned int hw_read (void* buf, unsigned int len)
{
    int n = cbm_read (2, buf, len);
    return n < 0 ? 0 : n;
}

void hw_close (void)
{
    cbm_close (2);
}

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

    /* The upper-case half of the C64's character set, for text: the rest is our own glyphs,
    ** loaded with DMHI. The character ROM is only visible to the CPU with I/O switched out. */
    __asm__ ("sei");
    CPU_PORT = port & ~0x04;
    memcpy (CHARSET, CHAR_ROM, 0x400);
    CPU_PORT = port;
    __asm__ ("cli");

    memset (SCREEN, ' ', 1000);
    memset (COLOUR, COLOR_WHITE, 1000);
    HIBASE = 0xE0;
    CASE_LOCK = 0x80;
    VIC.bordercolor = COLOR_BLACK;
    VIC.bgcolor0 = COLOR_BLACK;
    /* Video bank 3: screen at $E000 (offset $2000), characters at $E800 (offset $2800). */
    VIC.addr = (8 << 4) | (5 << 1);
    CIA2.pra &= 0xFC;

    VIC.spr_mcolor = 0xFF;
    VIC.spr_mcolor0 = COLOR_GRAY1;
    VIC.spr_mcolor1 = COLOR_WHITE;
    VIC.spr_ena = 0;

    SID.amp = 0x0F;
    SID.v1.ad = 0x09;
    SID.v1.sr = 0x00;
}

static unsigned char sound_left;

void sound (unsigned char pitch, unsigned char wave, unsigned char len)
{
    SID.v1.ctrl = 0;
    SID.v1.freq = (unsigned int) pitch << 8;
    SID.v1.pw = 0x0800;
    SID.v1.ctrl = wave;
    sound_left = len;
}

void sound_tick (void)
{
    if (sound_left && --sound_left == 0) {
        SID.v1.ctrl &= 0xFE;
    }
}
