"""Reads the parts of Dungeon Master's GRAPHICS.DAT the C64 port uses: object names (item 556)
and the game's rules tables (item 559: creature, weapon, armour and object info)."""
import struct


def lzw_expand(data):
    """FTL's GRAPHICS.DAT packing: Unix-compress LZW (9-12 bit codes, LSB first, 256 = clear)."""
    total = len(data) * 8
    bitpos, nbits, maxcode, free = 0, 9, 511, 257
    dic = {i: bytes([i]) for i in range(256)}
    out, prev = bytearray(), None
    while bitpos + nbits <= total:
        c = 0
        for i in range(nbits):
            b = bitpos + i
            c |= ((data[b >> 3] >> (b & 7)) & 1) << i
        bitpos += nbits
        if c == 256:
            dic = {i: bytes([i]) for i in range(256)}
            free, nbits, maxcode, prev = 257, 9, 511, None
            continue
        if c in dic:
            e = dic[c]
        elif c == free and prev is not None:
            e = prev + prev[:1]
        else:
            break
        out += e
        if prev is not None and free < 4096:
            dic[free] = prev + e[:1]
            free += 1
            if free > maxcode and nbits < 12:
                nbits += 1
                maxcode = (1 << nbits) - 1
        prev = e
    return bytes(out)


def rle_expand(b):
    """...followed by a run-length pass: 0x90 n repeats the previous byte n-1 more times."""
    out, i = bytearray(), 0
    while i < len(b):
        c = b[i]
        i += 1
        if c == 0x90:
            n = b[i]
            i += 1
            if n == 0:
                out.append(0x90)
            else:
                out += bytes([out[-1]]) * (n - 1)
        else:
            out.append(c)
    return bytes(out)


class Graphics:
    def __init__(self, d):
        self.d = d
        n = struct.unpack_from('>H', d, 0)[0]
        self.cs = struct.unpack_from('>%dH' % n, d, 2)
        self.us = struct.unpack_from('>%dH' % n, d, 2 + 2 * n)
        p = 2 + 4 * n
        self.offs = []
        for i in range(n):
            self.offs.append(p)
            p += self.cs[i]

    def item(self, i):
        raw = self.d[self.offs[i]:self.offs[i] + self.cs[i]]
        if self.cs[i] == self.us[i]:
            return raw
        out = rle_expand(lzw_expand(raw))
        assert len(out) == self.us[i], 'item %d unpacked to %d, not %d' % (i, len(out), self.us[i])
        return out

    def names(self):
        """Object names by icon index: each ends with a character that has bit 7 set."""
        names, cur = [], ''
        for c in self.item(556):
            if c & 0x80:
                names.append(cur + chr(c & 0x7f))
                cur = ''
            else:
                cur += chr(c)
        return names

    def tables(self):
        g = self.item(559)
        # Offsets of each table in this (FTL Atari ST) version of item 559, checked below.
        CREATURES, ARMOUR, WEAPONS, OBJINFO = 676, 1450, 1682, 1958
        t = {}
        t['objinfo'] = [struct.unpack_from('>HBBH', g, OBJINFO + i * 6) for i in range(180)]
        t['weapons'] = [tuple(g[WEAPONS + i * 6:WEAPONS + i * 6 + 4]) for i in range(46)]
        t['armour'] = [tuple(g[ARMOUR + i * 4:ARMOUR + i * 4 + 2]) for i in range(58)]
        cr = []
        for i in range(27):
            r = g[CREATURES + i * 26:CREATURES + i * 26 + 26]
            cr.append(dict(aspect=r[0], attributes=struct.unpack_from('>H', r, 2)[0],
                           move=r[6], attack_ticks=r[7], defense=r[8], health=r[9],
                           attack=r[10], poison=r[11], dex=r[12]))
        t['creatures'] = cr
        # Sanity checks against values known from the game: keys' icons, the dagger, the cape.
        assert t['objinfo'][136][0] == 176, 'object info table not where expected'
        assert t['weapons'][8][2] == 10 and t['armour'][0] == (3, 5), 'weapon/armour tables moved'
        assert all(c['aspect'] == i for i, c in enumerate(cr)), 'creature table not where expected'
        return t
