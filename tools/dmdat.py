"""Reads Dungeon Master's DUNGEON.DAT straight out of the original Atari ST .STX disk image."""
import struct

THING_SIZES = [4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4]
T_DOOR, T_TELE, T_TEXT, T_SENSOR, T_CREATURE, T_WEAPON, T_ARMOUR, T_SCROLL, T_POTION, \
    T_CONTAINER, T_JUNK = range(11)
END = 0xFFFE


def stx_to_sectors(data):
    """Pasti .STX -> flat list of 512-byte sectors (track-major, single or double sided)."""
    ntr = data[10]
    p, disk = 16, {}
    for _ in range(ntr):
        rs, fz, ns, fl, tl, tn, tt = struct.unpack_from('<IIHHHBB', data, p)
        base = p + 16
        if fl & 1:
            descs = [struct.unpack_from('<IHHBBBBHBB', data, base + i * 16) for i in range(ns)]
            tdata = base + ns * 16 + fz
            if fl & 0x40:  # track image header precedes sector data offsets? offsets are from tdata
                pass
            for off, bp, rt, it, ih, isec, isz, crc, ff, r in descs:
                disk[(tn & 0x7f, tn >> 7, isec)] = data[tdata + off:tdata + off + (128 << isz)]
        else:
            for i in range(ns):
                disk[(tn & 0x7f, tn >> 7, i + 1)] = data[base + i * 512:base + i * 512 + 512]
        p += rs
    sides = 2 if any(k[1] for k in disk) else 1
    spt = max(k[2] for k in disk if k[2] < 20)
    tracks = max(k[0] for k in disk) + 1
    out = bytearray()
    for t in range(tracks):
        for h in range(sides):
            for s in range(1, spt + 1):
                out += disk.get((t, h, s), b'\0' * 512)[:512].ljust(512, b'\0')
    return bytes(out)


def fat12_files(img):
    bps, spc, res, nf, nr, ts, md, spf = struct.unpack_from('<HBHBHHBH', img, 11)
    fat = img[res * bps:(res + spf) * bps]
    root = (res + nf * spf) * bps
    data0 = root + nr * 32
    csz = spc * bps

    def nxt(c):
        o = c * 3 // 2
        v = fat[o] | fat[o + 1] << 8
        return v >> 4 if c & 1 else v & 0xfff

    files = {}
    for i in range(nr):
        e = img[root + i * 32:root + i * 32 + 32]
        if e[0] in (0, 0xe5) or e[11] & 0x18:
            continue
        name = (e[:8].decode('latin1').strip() + '.' + e[8:11].decode('latin1').strip()).rstrip('.')
        cl, size = struct.unpack_from('<HI', e, 26)
        buf = bytearray()
        while 2 <= cl < 0xff0:
            o = data0 + (cl - 2) * csz
            buf += img[o:o + csz]
            cl = nxt(cl)
        files[name] = bytes(buf[:size])
    return files


class Map:
    pass


class Dungeon:
    def __init__(self, d):
        self.raw = d
        w = lambda o: struct.unpack_from('>H', d, o)[0]
        self.seed = w(0)
        mapsize = w(2)
        nmaps = d[4]
        textwords = w(6)
        start = w(8)
        self.start = (start & 31, (start >> 5) & 31, (start >> 10) & 3)
        nfirst = w(10)
        self.counts = [w(12 + i * 2) for i in range(16)]
        p = 44
        self.maps = []
        for i in range(nmaps):
            m = Map()
            m.offset = w(p)
            m.ox, m.oy = d[p + 6], d[p + 7]
            s = w(p + 8)
            m.h = (s >> 11) + 1
            m.w = ((s >> 6) & 31) + 1
            m.level = s & 63
            m.orn = w(p + 10)
            m.misc = w(p + 12)
            m.sets = w(p + 14)
            self.maps.append(m)
            p += 16
        ncols = sum(m.w for m in self.maps)
        self.colidx = [w(p + i * 2) for i in range(ncols)]
        p += ncols * 2
        self.first = [w(p + i * 2) for i in range(nfirst)]
        p += nfirst * 2
        self.text = [w(p + i * 2) for i in range(textwords)]
        p += textwords * 2
        self.things = []
        for t in range(16):
            sz = THING_SIZES[t]
            lst = []
            for i in range(self.counts[t]):
                lst.append([w(p + i * sz + j) for j in range(0, sz, 2)])
            p += self.counts[t] * sz
            self.things.append(lst)
        mapdata = d[p:p + mapsize]
        self.tail = d[p + mapsize:]
        col = 0
        for m in self.maps:
            md = mapdata[m.offset:]
            # column-major: x outer, y inner
            m.sq = [[md[x * m.h + y] for x in range(m.w)] for y in range(m.h)]
            m.col0 = col
            col += m.w
        # thing lists per square
        for mi, m in enumerate(self.maps):
            m.lists = {}
            for x in range(m.w):
                k = self.colidx[m.col0 + x]
                for y in range(m.h):
                    if m.sq[y][x] & 0x10:
                        m.lists[(x, y)] = self.chain(self.first[k])
                        k += 1

    def chain(self, t):
        out = []
        n = 0
        while t not in (0xFFFE, 0xFFFF) and n < 500:
            typ, idx = (t >> 10) & 15, t & 1023
            out.append((t >> 14, typ, idx))
            t = self.things[typ][idx][0]
            n += 1
        return out

    def decode_text(self, off):
        out = []
        i = off
        esc = 0
        while i < len(self.text):
            v = self.text[i]
            i += 1
            for c in ((v >> 10) & 31, (v >> 5) & 31, v & 31):
                if esc:
                    out.append('\x01%d' % c if esc == 30 else '\x02%d' % c)
                    esc = 0
                elif c < 26:
                    out.append(chr(65 + c))
                elif c == 26:
                    out.append(' ')
                elif c == 27:
                    out.append('.')
                elif c == 28:
                    out.append('\n')
                elif c in (29, 30):
                    esc = c
                else:
                    return ''.join(out)
        return ''.join(out)


def load(stx_path):
    img = stx_to_sectors(open(stx_path, 'rb').read())
    files = fat12_files(img)
    return Dungeon(files['DUNGEON.DAT']), files
