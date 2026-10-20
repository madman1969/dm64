"""Phase 0 of the bitmap view: Dungeon Master's own view, built the way DM builds it, and what it
would look like on a C64.

DM draws its 224x136 view by pasting pictures from GRAPHICS.DAT, far to near: the ceiling and
floor, then a wall piece for each wall in sight. For each distance the left, centre and right
views of a wall are one strip (D1 = item 86, D2 = 87, D3 = 88), and a "frame" says which slice
of the strip goes where (frames as in ScummVM's DM engine, DisplayMan::initConstants). Colour 10
is transparent.

    python dmview.py "Dungeon Master.stx" <outdir> [level x y dir ...]

writes, for each position, a PNG of DM's view beside the C64 version: 112 double-wide pixels by
136 (28 x 17 character cells), each 4x8 cell holding at most 3 colours plus the background. It
prints how many cells needed more colours than that (clash), and how many cells only partly
covered by a wall piece (the ones a DMA blitter couldn't copy whole, so the CPU would mask).
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dmdat    # noqa: E402
import dmgfx    # noqa: E402
import dmimg    # noqa: E402

VIEW_W, VIEW_H = 224, 136
FLOOR, CEILING = 75, 76
WALL_D0R, WALL_D0L, WALL_D1, WALL_D2, WALL_D3, WALL_D3_FAR = 84, 85, 86, 87, 88, 89
CLEAR = 10

# (strip item, dest x1, x2, y1, y2, source x, source y), back to front; key = (depth, lateral)
FRAMES = {
    (3, -2): (WALL_D3_FAR, 0, 15, 25, 73, 0, 0),        # mirrored, as D3R2
    (3, 2): (WALL_D3_FAR, 208, 223, 25, 73, 0, 0),
    (3, -1): (WALL_D3, 0, 83, 25, 75, 32, 0),
    (3, 1): (WALL_D3, 139, 223, 25, 75, 0, 0),
    (3, 0): (WALL_D3, 74, 149, 25, 75, 18, 0),
    (2, -1): (WALL_D2, 0, 74, 20, 90, 61, 0),
    (2, 1): (WALL_D2, 149, 223, 20, 90, 0, 0),
    (2, 0): (WALL_D2, 60, 163, 20, 90, 16, 0),
    (1, -1): (WALL_D1, 0, 63, 9, 119, 192, 0),
    (1, 1): (WALL_D1, 160, 223, 9, 119, 0, 0),
    (1, 0): (WALL_D1, 32, 191, 9, 119, 48, 0),
    (0, -1): (WALL_D0L, 0, 31, 0, 135, 0, 0),
    (0, 1): (WALL_D0R, 192, 223, 0, 135, 0, 0),
}
ORDER = [(3, -2), (3, 2), (3, -1), (3, 1), (3, 0), (2, -1), (2, 1), (2, 0),
         (1, -1), (1, 1), (1, 0), (0, -1), (0, 1)]
DX, DY = [0, 1, 0, -1], [-1, 0, 1, 0]


def compose(G, m, px, py, d):
    """DM's view from (px, py) facing d: 224x136 palette indices, and per-pixel which piece
    drew it (for counting partly covered cells)."""
    view = [0] * (VIEW_W * VIEW_H)
    owner = [None] * (VIEW_W * VIEW_H)
    for item, top in ((CEILING, 0), (FLOOR, VIEW_H - 70)):
        w, h, px_ = dmimg.decode(G.item(item))
        for y in range(h):
            view[(top + y) * VIEW_W:(top + y) * VIEW_W + w] = px_[y * w:(y + 1) * w]
    rx, ry = DX[(d + 1) % 4], DY[(d + 1) % 4]
    for key in ORDER:
        k, l = key
        x, y = px + k * DX[d] + l * rx, py + k * DY[d] + l * ry
        if not (0 <= x < m.w and 0 <= y < m.h):
            wall = True
        else:
            s = m.sq[y][x]
            wall = s >> 5 == 0 or (s >> 5 == 6 and not s & 4)
        if not wall:
            continue
        item, x1, x2, y1, y2, sx, sy = FRAMES[key]
        w, h, src = dmimg.decode(G.item(item))
        mirror = key == (3, -2)
        for yy in range(y1, y2 + 1):
            for xx in range(x1, x2 + 1):
                cx = sx + (xx - x1)
                cy = sy + (yy - y1)
                if mirror:
                    cx = w - 1 - cx
                if 0 <= cx < w and 0 <= cy < h:
                    c = src[cy * w + cx]
                    if c != CLEAR:
                        view[yy * VIEW_W + xx] = c
                        owner[yy * VIEW_W + xx] = key
    return view, owner


# DM's dungeon palette, colour by colour, as C64 colours: its five greys onto the C64's five
# (black 0, dark 11, mid 12, light 15, white 1), its browns, reds and blues onto the nearest.
#            0  333 444 310 066 420 040 060 700 750 643 770 222 555 007 777
DM_TO_C64 = [0, 11, 12, 9, 3, 8, 11, 5, 2, 8, 10, 7, 11, 15, 6, 1]
LUMA = [0, 3, 4, 2, 4, 3, 2, 3, 3, 5, 5, 6, 2, 5, 1, 7]     # DM colours' brightness, 0-7


BACKGROUND = 12     # the bitmap's shared colour: DM's walls are greys, so a grey one means each
                    # cell can still show the other four greys, and colours hardly ever clash


def to_c64(view, pal):
    """The view as a C64 multicolour bitmap: 112x136 double-wide pixels, each 4x8 cell at most
    3 colours plus the background. Returns (C64 colour per fat pixel, clashing cells)."""
    fw = VIEW_W // 2
    # Each fat pixel is two DM pixels. Where they differ sharply (a mortar line, an edge) keep
    # the darker, so thin lines survive; otherwise take them in turn like a dither, which keeps
    # the texture and brightness of DM's stone.
    fat = []
    for y in range(VIEW_H):
        for x in range(fw):
            a, b = view[y * VIEW_W + 2 * x], view[y * VIEW_W + 2 * x + 1]
            if abs(LUMA[a] - LUMA[b]) >= 2:
                c = a if LUMA[a] < LUMA[b] else b
            else:
                c = a if (x + y) & 1 else b
            fat.append(DM_TO_C64[c])
    clash = 0
    out = list(fat)
    for cy in range(0, VIEW_H, 8):
        for cx in range(0, fw, 4):
            cells = [(x, y) for y in range(cy, min(cy + 8, VIEW_H)) for x in range(cx, cx + 4)]
            counts = {}
            for x, y in cells:
                c = fat[y * fw + x]
                if c != BACKGROUND:
                    counts[c] = counts.get(c, 0) + 1
            keep = sorted(counts, key=lambda c: -counts[c])[:3]
            if len(counts) > 3:
                clash += 1
            allowed = [BACKGROUND] + keep
            for x, y in cells:
                c = fat[y * fw + x]
                if c not in allowed:
                    out[y * fw + x] = dmimg.nearest(dmimg.C64[c], allowed)
    return out, clash


def partly_covered(owner):
    """Cells (8x8 DM pixels) where a wall piece covers some pixels but not all."""
    n = 0
    for cy in range(0, VIEW_H, 8):
        for cx in range(0, VIEW_W, 8):
            keys = [owner[y * VIEW_W + x] for y in range(cy, min(cy + 8, VIEW_H))
                    for x in range(cx, cx + 8)]
            top = keys[-1]
            if any(k is not None for k in keys) and len(set(keys)) > 1:
                n += 1
    return n


def write_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\0' + b''.join(bytes(p) for p in row) for row in rows)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    stx, outdir = sys.argv[1:3]
    D, files = dmdat.load(stx)
    G = dmgfx.Graphics(files['GRAPHICS.DAT'])
    pal = dmimg.palette(G)
    args = [int(a) for a in sys.argv[3:]] or [0, 1, 4, 2, 1, 3, 0, 2]
    for i in range(0, len(args), 4):
        lv, x, y, d = args[i:i + 4]
        view, owner = compose(G, D.maps[lv], x, y, d)
        c64, clash = to_c64(view, pal)
        # Side by side, each doubled: DM's view, then the C64's.
        rows = []
        for yy in range(VIEW_H):
            left = [pal[view[yy * VIEW_W + xx]] for xx in range(VIEW_W)]
            right = [dmimg.C64[c64[yy * (VIEW_W // 2) + xx // 2]] for xx in range(VIEW_W)]
            row = [p for p in left for _ in (0, 1)] + [(40, 0, 40)] * 8 + \
                  [p for p in right for _ in (0, 1)]
            rows += [row, row]
        name = os.path.join(outdir, 'view_%d_%d_%d_%d.png' % (lv, x, y, d))
        write_png(name, rows)
        print('level %d (%d,%d) dir %d: %d of %d cells clash, %d cells partly covered -> %s' % (
            lv, x, y, d, clash, (VIEW_W // 8) * (VIEW_H // 8 + 1), partly_covered(owner), name))


if __name__ == '__main__':
    main()
