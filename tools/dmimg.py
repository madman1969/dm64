"""Dungeon Master's pictures, as the C64 port uses them: FTL's packed images in GRAPHICS.DAT,
decoded and turned into multicolour sprites.

The packing ("IMG2", as ScummVM's DM engine reads the big-endian versions) is a width and height
(big-endian words) and then bytes of `op << 4 | colour`:
  op 0-7   a run of op + 1 pixels of colour
  op 8     a run, count = next byte + 1          op C   the same with a word count
  op B     copy next byte + 1 pixels from the row above, then one pixel of colour
  op F     the same with a word count
  op 9     n = next byte; if n is odd n += 1, else first one pixel of colour; then n / 2 bytes
           of two pixels each, high nibble first
Colours are indices into DM's 16-colour palette, in item 562.

Item 558 describes each creature's pictures ("aspects", 12 bytes each from offset 832): the first
of its pictures relative to item 446 (a word), the front, side and attack views' sizes (width
in bytes, height), its transparent colour (low nibble of byte 10), and which replacement colour
sets recolour its palette entries 9 and 10 (byte 11's nibbles, 1-based, 0 for none). The 13 sets
are at offset 650: six shades each, one per light level, then two bytes.
"""
import struct

FIRST_CREATURE_ITEM = 446
ASPECTS_ITEM, ASPECTS_OFFSET, ASPECT_SIZE = 558, 832, 12
REPLACEMENTS_OFFSET, REPLACEMENT_SIZE = 650, 14

PALETTE_ITEM, PALETTE_OFFSET = 562, 1278

# The C64's colours (Pepto's measurements).
C64 = [(0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x68, 0x37, 0x2B), (0x70, 0xA4, 0xB2),
       (0x6F, 0x3D, 0x86), (0x58, 0x8D, 0x43), (0x35, 0x28, 0x79), (0xB8, 0xC7, 0x6F),
       (0x6F, 0x4F, 0x25), (0x43, 0x39, 0x00), (0x9A, 0x67, 0x59), (0x44, 0x44, 0x44),
       (0x6C, 0x6C, 0x6C), (0x9A, 0xD2, 0x84), (0x6C, 0x5E, 0xB5), (0x95, 0x95, 0x95)]
SHARED0, SHARED1 = 0, 15        # sprite multicolours shared by all creatures: black, light grey


def decode(g):
    """One packed picture -> (width, height, pixels), or None if it isn't one."""
    w, h = struct.unpack_from('>HH', g, 0)
    if not (0 < w <= 320 and 0 < h <= 200):
        return None
    n_px, px, i = w * h, [], 4
    try:
        while len(px) < n_px:
            b = g[i]
            i += 1
            op, c = b >> 4, b & 15
            if op <= 7:
                px += [c] * (op + 1)
            elif op == 8:
                px += [c] * (g[i] + 1)
                i += 1
            elif op == 0xC:
                px += [c] * (((g[i] << 8) | g[i + 1]) + 1)
                i += 2
            elif op in (0xB, 0xF):
                if op == 0xB:
                    n = g[i] + 1
                    i += 1
                else:
                    n = ((g[i] << 8) | g[i + 1]) + 1
                    i += 2
                for _ in range(n):
                    k = len(px)
                    px.append(px[k - w] if k >= w else 0)
                px.append(c)
            elif op == 9:
                n = g[i]
                i += 1
                if n % 2:
                    n += 1
                else:
                    px.append(c)
                for _ in range(n // 2):
                    d = g[i]
                    i += 1
                    px += [d >> 4, d & 15]
            else:
                return None
    except IndexError:
        return None
    return w, h, px[:n_px]


def st_rgb(w):
    """An Atari ST colour word 0x0RGB (3 bits a component) as RGB."""
    return (((w >> 8) & 7) * 255 // 7, ((w >> 4) & 7) * 255 // 7, (w & 7) * 255 // 7)


def palette(graphics):
    """DM's dungeon-view palette (the brightest of its six) as RGB."""
    g = graphics.item(PALETTE_ITEM)
    words = struct.unpack_from('>16H', g, PALETTE_OFFSET)
    assert words[0] == 0 and words[15] == 0x777, 'palette not where expected'
    return [st_rgb(w) for w in words]


def creature_aspects(graphics):
    """For each creature type: (front view item, transparent colour, its palette)."""
    g = graphics.item(ASPECTS_ITEM)
    base = palette(graphics)
    sets = [st_rgb(struct.unpack_from('>H', g, REPLACEMENTS_OFFSET + i * REPLACEMENT_SIZE)[0])
            for i in range(13)]
    out = []
    for t in range(27):
        r = g[ASPECTS_OFFSET + t * ASPECT_SIZE:ASPECTS_OFFSET + (t + 1) * ASPECT_SIZE]
        pal = list(base)
        if r[11] >> 4:
            pal[9] = sets[(r[11] >> 4) - 1]
        if r[11] & 15:
            pal[10] = sets[(r[11] & 15) - 1]
        out.append((FIRST_CREATURE_ITEM + struct.unpack_from('>H', r, 0)[0], r[10] & 15, pal))
    assert out[0][0] == 446 and out[0][1] == 13 and out[1][0] == 450, 'aspects not where expected'
    return out


def nearest(rgb, choices):
    return min(choices, key=lambda c: sum((a - b) ** 2 for a, b in zip(rgb, C64[c])))


def to_sprite(pic, pal, clear, box_w, box_h):
    """A picture as multicolour sprite cells: box_w x box_h cells of 2x1 pixels each, values
    0 clear, 1 SHARED0, 2 the creature's own colour, 3 SHARED1. Returns (cells, colour)."""
    w, h, px = pic
    # The creature's own colour: its commonest C64 colour other than the shared ones.
    counts = {}
    for c in px:
        if c != clear:
            k = nearest(pal[c], range(16))
            counts[k] = counts.get(k, 0) + 1
    own = max((k for k in counts if k not in (SHARED0, SHARED1)), key=lambda k: counts[k],
              default=12)
    if counts.get(own, 0) * 4 < sum(counts.values()):
        own = 12            # no strong colour of its own: a grey creature

    choices = {SHARED0: 1, own: 2, SHARED1: 3}
    scale = min(box_w * 2 / w, box_h / h)
    tw, th = max(1, round(w * scale / 2)), max(1, round(h * scale))
    cells = [[0] * box_w for _ in range(box_h)]
    left, top = (box_w - tw) // 2, box_h - th
    for ty in range(th):
        y0, y1 = int(ty * h / th), max(int(ty * h / th) + 1, int((ty + 1) * h / th))
        for tx in range(tw):
            x0, x1 = int(tx * w / tw), max(int(tx * w / tw) + 1, int((tx + 1) * w / tw))
            opaque = [px[y * w + x] for y in range(y0, y1) for x in range(x0, x1)
                      if px[y * w + x] != clear]
            if len(opaque) * 2 < (y1 - y0) * (x1 - x0):
                continue
            rgb = tuple(sum(pal[c][i] for c in opaque) // len(opaque) for i in range(3))
            cells[top + ty][left + tx] = choices[nearest(rgb, choices)]
    return cells, own


def sprite_blocks(cells):
    """Cells -> 64-byte sprite blocks: one for 12x21 cells, four (TL, TR, BL, BR) for 24x42."""
    out = bytearray()
    for by in range(len(cells) // 21):
        for bx in range(len(cells[0]) // 12):
            block = bytearray()
            for y in range(21):
                bits = 0
                for v in cells[by * 21 + y][bx * 12:bx * 12 + 12]:
                    bits = (bits << 2) | v
                block += bits.to_bytes(3, 'big')
            out += block + b'\0'
    return bytes(out)


def creature_sprites(graphics):
    """For each creature type: its big sprite (4 blocks) and a small one (1 block) - 320 bytes
    - and its own colour. (The game mirrors the big one itself, for its second frame.)"""
    data, colours = bytearray(), []
    for item, clear, pal in creature_aspects(graphics):
        pic = decode(graphics.item(item))
        big, own = to_sprite(pic, pal, clear, 24, 42)
        small, _ = to_sprite(pic, pal, clear, 12, 21)
        data += sprite_blocks(big) + sprite_blocks(small)
        colours.append(own)
    return bytes(data), colours
