/*
** hw.c - the C64 itself: the video bank, the RAM under the ROMs, the data file, and sound.
**
** Memory map while the game runs (the video chip uses bank 3, $C000-$FFFF):
**   $0400-$07FF  buffers (the default screen's RAM, free once the screen moves)
**   $0801-$CBFF  the program, the party's level, overlays and the C stack
**   $CC00-$CFFF  the screen; its top 17 rows are the bitmap's colours (irq.s)
**   $D000-$D3FF  the character set   } in the RAM under the I/O area, which the video chip
**   $D400-$D6FF  sprites             } always sees; the CPU writes there with I/O switched
**   $D800-$DA87  the bitmap's font and icons   } out (hi_read)
**   $E000-$FF3F  the bitmap (rows 0-16 used), in the RAM under the KERNAL
** The dungeon's texts, object names and our own strings are in far memory (DMTXT).
*/

#include <string.h>
#include <cbm.h>
#include <c64.h>
#include "dm.h"

#pragma bss-name (push, "LOWBSS")
unsigned char view_chars[VIEW_BUF];
#pragma bss-name (pop)
unsigned char view_cols[VIEW_BUF];

#define CPU_PORT   (*(unsigned char*) 0x0001)
#define CHARSET    ((unsigned char*) 0xD000)
#define HIBASE     (*(unsigned char*) 0x0288)
#define CASE_LOCK  (*(unsigned char*) 0x0291)
#define LAST_DEVICE (*(unsigned char*) 0x00BA)

/* Copies with every ROM and the I/O area switched out, so all 64K is RAM (interrupts go on:
** irq.s). Writes need no switching (they always reach the RAM under a ROM), except under the
** I/O area: hi_read is used for those too. */
void hi_read (void* dst, unsigned src, unsigned len)
{
    unsigned char port = CPU_PORT;

    CPU_PORT = 0x34;
    memcpy (dst, (void*) src, len);
    CPU_PORT = port;
}

/* One of our own strings (creature names, help) from DMTXT, until the next. */
const char* hi_str (unsigned int offset)
{
    static char buf[41];

    far_text (buf, offset, 40);
    buf[40] = 0;
    return buf;
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
