"""Dungeon Master's own view, built the way DM builds it, for the C64's bitmap view: dmconv.py
uses build_pieces and lz_pack to make DMWAL, the pictures view.c draws from.

DM draws its 224x136 view by pasting pictures from GRAPHICS.DAT, far to near: the ceiling and
floor, then a wall piece for each wall in sight. For each distance the left, centre and right
views of a wall are one strip (D1 = item 86, D2 = 87, D3 = 88), and a "frame" says which slice
of the strip goes where (frames as in ScummVM's DM engine, DisplayMan::initConstants). Colour 10
is transparent.

    python dmview.py "Dungeon Master.stx" <outdir> [level x y dir ...]

on its own writes mockups: for each position, a PNG of DM's view beside the C64 version: 112 double-wide pixels by
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

# The rest of the wall set (items 77-83): door frames, and where they go, from ScummVM. A
# "right" frame is the left one's picture mirrored.
DOOR_FRAME_D0C, DOOR_FRAME_D1C, DOOR_FRAME_D2C, DOOR_FRAME_D3C, DOOR_FRAME_D3L = 77, 78, 79, 80, 81
DOOR_FRAME_TOP_D1, DOOR_FRAME_TOP_D2 = 82, 83
# (item, x1, x2, y1, y2, source x, source y, mirrored) by (depth, lateral)
DOOR_FRAMES = {
    (3, -1): [(DOOR_FRAME_D3L, 0, 31, 28, 70, 0, 0, False)],
    (3, 1): [(DOOR_FRAME_D3L, 192, 223, 28, 70, 0, 0, True)],
    (3, 0): [(DOOR_FRAME_D3C, 64, 95, 27, 70, 0, 0, False),
             (DOOR_FRAME_D3C, 128, 159, 27, 70, 0, 0, True)],
    (2, -1): [(DOOR_FRAME_TOP_D2, 0, 59, 22, 24, 16, 0, False)],
    (2, 1): [(DOOR_FRAME_TOP_D2, 164, 223, 22, 24, 16, 0, False)],
    (2, 0): [(DOOR_FRAME_D2C, 48, 95, 22, 86, 0, 0, False),
             (DOOR_FRAME_D2C, 128, 175, 22, 86, 0, 0, True),
             (DOOR_FRAME_TOP_D2, 64, 159, 22, 24, 0, 0, False)],
    (1, -1): [(DOOR_FRAME_TOP_D1, 0, 31, 14, 17, 16, 0, False)],
    (1, 1): [(DOOR_FRAME_TOP_D1, 192, 223, 14, 17, 16, 0, False)],
    (1, 0): [(DOOR_FRAME_D1C, 43, 74, 14, 107, 0, 0, False),
             (DOOR_FRAME_D1C, 149, 180, 14, 107, 0, 0, True),
             (DOOR_FRAME_TOP_D1, 48, 175, 14, 17, 0, 0, False)],
    (0, 0): [(DOOR_FRAME_D0C, 96, 127, 0, 122, 0, 0, False)],
}

# Doors: each door set is three pictures from item 108 (far, middle and near, each the left,
# centre and right views side by side as with walls). For each place and opening state (0 shut,
# 1-3 a quarter to three quarters open, sliding up), the slice that shows: (x1, x2, y1, y2,
# source x, source y). From ScummVM's DoorFrames tables (D1C's follows the same pattern).
FIRST_DOOR_SET = 108
DOOR_PANELS = {
    (3, -1): (0, [(24, 71, 28, 67, 0, 0), (24, 71, 28, 38, 0, 30), (24, 71, 28, 48, 0, 20), (24, 71, 28, 58, 0, 10)]),
    (3, 1): (0, [(150, 197, 28, 67, 0, 0), (150, 197, 28, 38, 0, 30), (150, 197, 28, 48, 0, 20), (150, 197, 28, 58, 0, 10)]),
    (3, 0): (0, [(88, 135, 28, 67, 0, 0), (88, 135, 28, 38, 0, 30), (88, 135, 28, 48, 0, 20), (88, 135, 28, 58, 0, 10)]),
    (2, -1): (1, [(0, 63, 24, 82, 0, 0), (0, 63, 24, 39, 0, 45), (0, 63, 24, 54, 0, 30), (0, 63, 24, 69, 0, 15)]),
    (2, 1): (1, [(160, 223, 24, 82, 0, 0), (160, 223, 24, 39, 0, 45), (160, 223, 24, 54, 0, 30), (160, 223, 24, 69, 0, 15)]),
    (2, 0): (1, [(80, 143, 24, 82, 0, 0), (80, 143, 24, 39, 0, 45), (80, 143, 24, 54, 0, 30), (80, 143, 24, 69, 0, 15)]),
    (1, -1): (2, [(0, 31, 17, 102, 64, 0), (0, 31, 17, 38, 64, 66), (0, 31, 17, 60, 64, 44), (0, 31, 17, 82, 64, 22)]),
    (1, 1): (2, [(192, 223, 17, 102, 0, 0), (192, 223, 17, 38, 0, 66), (192, 223, 17, 60, 0, 44), (192, 223, 17, 82, 0, 22)]),
    (1, 0): (2, [(64, 159, 17, 102, 0, 0), (64, 159, 17, 38, 0, 66), (64, 159, 17, 60, 0, 44), (64, 159, 17, 82, 0, 22)]),
}

# Stairs (items 90-107): (item, x1, x2, y1, y2, source x, mirrored) for up and down stairs seen
# from the front, by place. Right-hand views are the left ones mirrored.
FIRST_STAIRS = 90
STAIRS_FRONT = {
    'up': {(3, -1): (90, 0, 79, 25, 70, 0, False), (3, 0): (91, 64, 159, 25, 70, 0, False),
           (3, 1): (90, 149, 223, 25, 70, 5, True), (2, -1): (92, 0, 63, 22, 83, 0, False),
           (2, 0): (93, 64, 159, 22, 83, 0, False), (2, 1): (92, 160, 223, 22, 83, 0, True),
           (1, -1): (94, 0, 31, 9, 108, 0, False), (1, 0): (95, 32, 191, 9, 108, 0, False),
           (1, 1): (94, 192, 223, 9, 108, 0, True), (0, -1): (96, 0, 31, 58, 101, 0, False),
           (0, 1): (96, 192, 223, 58, 101, 0, True)},
    'down': {(3, -1): (97, 0, 79, 28, 68, 0, False), (3, 0): (98, 64, 159, 28, 70, 0, False),
             (3, 1): (97, 149, 223, 28, 68, 5, True), (2, -1): (99, 0, 63, 24, 85, 0, False),
             (2, 0): (100, 64, 159, 24, 85, 0, False), (2, 1): (99, 160, 223, 24, 85, 0, True),
             (1, -1): (101, 0, 31, 18, 108, 0, False), (1, 0): (102, 32, 191, 18, 108, 0, False),
             (1, 1): (101, 192, 223, 18, 108, 0, True), (0, -1): (103, 0, 31, 76, 135, 0, False),
             (0, 1): (103, 192, 223, 76, 135, 0, True)},
}


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
    door_set = (m.sets >> 8) & 15

    def blit(item, x1, x2, y1, y2, sx, sy, mirror, key):
        w, h, src = dmimg.decode(G.item(item))
        for yy in range(y1, y2 + 1):
            for xx in range(x1, x2 + 1):
                cx, cy = sx + (xx - x1), sy + (yy - y1)
                if mirror:
                    cx = w - 1 - cx
                if 0 <= cx < w and 0 <= cy < h:
                    c = src[cy * w + cx]
                    if c != CLEAR:
                        view[yy * VIEW_W + xx] = c
                        owner[yy * VIEW_W + xx] = key

    for key in ORDER + [(0, 0)]:
        k, l = key
        x, y = px + k * DX[d] + l * rx, py + k * DY[d] + l * ry
        s = m.sq[y][x] if 0 <= x < m.w and 0 <= y < m.h else 0
        kind = s >> 5
        if kind == 0 or (kind == 6 and not s & 4):
            if key in FRAMES:
                item, x1, x2, y1, y2, sx, sy = FRAMES[key]
                blit(item, x1, x2, y1, y2, sx, sy, key == (3, -2), key)
        elif kind == 4:
            # A door, seen face on when its passable way runs along the party's view.
            if bool(s & 8) != (d % 2 == 0):
                continue
            for item, x1, x2, y1, y2, sx, sy, mirror in DOOR_FRAMES.get(key, []):
                blit(item, x1, x2, y1, y2, sx, sy, mirror, key)
            state = s & 7
            if key in DOOR_PANELS and 1 <= state <= 4:
                depth, slices = DOOR_PANELS[key]
                x1, x2, y1, y2, sx, sy = slices[0 if state == 4 else state]
                blit(FIRST_DOOR_SET + door_set * 3 + depth, x1, x2, y1, y2, sx, sy, False, key)
        elif kind == 3:
            # Stairs, seen from the front when they run along the party's view.
            if bool(s & 8) == (d % 2 == 0):
                table = STAIRS_FRONT['up' if s & 4 else 'down']
                if key in table:
                    item, x1, x2, y1, y2, sx, mirror = table[key]
                    blit(item, x1, x2, y1, y2, sx, 0, mirror, key)
    return view, owner


# DM's dungeon palette, colour by colour, as C64 colours: its greys onto the C64's by
# brightness (222 dark 11, 333 mid 12, 444 between mid and light so the two in turn, 555 light
# 15), its browns, reds and blues onto the nearest.
#            0  333 444       310 066 420 040 060 700 750 643 770 222 555 007 777
DM_TO_C64 = [0, 12, (12, 15), 9, 3, 8, 11, 5, 2, 8, 10, 7, 11, 15, 6, 1]


# Where two DM pixels differ sharply, the darker one is kept as a line: in the darker C64 grey,
# so mortar and the floor's cracks stand out.
EDGE_C64 = {1: 11, 2: 12}


def c64_colour(c, x, y, edge=False):
    """DM colour c as a C64 colour at fat pixel (x, y): a pair is a dither."""
    m = EDGE_C64.get(c, DM_TO_C64[c]) if edge else DM_TO_C64[c]
    return m[(x + y) & 1] if isinstance(m, tuple) else m
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
            edge = abs(LUMA[a] - LUMA[b]) >= 2
            if edge:
                c = a if LUMA[a] < LUMA[b] else b
            else:
                c = a if (x + y) & 1 else b
            fat.append(c64_colour(c, x, y, edge))
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


# ---- runtime pieces --------------------------------------------------------------------------
#
# What the game draws its view from: every piece (the ceiling and floor, each wall, door frame,
# door at each opening state, stairs) already converted to C64 bitmap cells at the place it
# goes. The bitmap's colours are fixed: pixel value 0 the background (mid grey), 1 and 2 the
# screen matrix's dark and light grey (the same in every cell), 3 the cell's own colour from
# colour RAM (black, white or a door's brown).
#
# Pieces, in order: 0 the base (ceiling and floor); 1-13 walls (ORDER); 14-23 door frames
# (DF_KEYS); 24-59 shut doors (door set * 9 + DP_KEYS); 60-81 stairs (up then down, ST_KEYS).
#
# DMWAL: a word, the number of pieces; a word per piece, its offset in DMWAL in words; then each
# piece (padded to an even length):
# a byte, its number of rows of cells, then each row: a word, the row's length after it; the
# cell row (0-16), the first cell's column (0-27), the number of cells; then each cell: a flag
# byte (bit 0 drawn, bit 1 only partly covered, bit 2 uses colour 3, bits 4-7 that colour),
# and if drawn 8 bitmap bytes, and if partly covered 8 mask bytes (bits set where it covers).

FIXED_CODES = {12: 0, 11: 1, 15: 2}
DOOR_SETS = 4
DF_KEYS = [(3, -1), (3, 1), (3, 0), (2, -1), (2, 1), (2, 0), (1, -1), (1, 1), (1, 0), (0, 0)]
DP_KEYS = [(3, -1), (3, 1), (3, 0), (2, -1), (2, 1), (2, 0), (1, -1), (1, 1), (1, 0)]
ST_KEYS = [(3, -1), (3, 0), (3, 1), (2, -1), (2, 0), (2, 1), (1, -1), (1, 0), (1, 1), (0, -1), (0, 1)]


def piece_canvas(G, ops):
    """A 224x136 canvas with the blits in ops (item, x1, x2, y1, y2, sx, sy, mirror) on it;
    -1 where nothing was drawn."""
    canvas = [-1] * (VIEW_W * VIEW_H)
    for item, x1, x2, y1, y2, sx, sy, mirror in ops:
        w, h, src = dmimg.decode(G.item(item))
        for yy in range(y1, y2 + 1):
            for xx in range(x1, x2 + 1):
                cx, cy = sx + (xx - x1), sy + (yy - y1)
                if mirror:
                    cx = w - 1 - cx
                if 0 <= cx < w and 0 <= cy < h and src[cy * w + cx] != CLEAR:
                    canvas[yy * VIEW_W + xx] = src[cy * w + cx]
    return canvas


def piece_bytes(canvas):
    """A canvas as a DMWAL piece (see above)."""
    rows = []
    for cy in range(VIEW_H // 8 + 1):
        cells = []
        for cx in range(VIEW_W // 8):
            fat, opaque = [], []
            for y in range(cy * 8, cy * 8 + 8):
                for x in range(cx * 4, cx * 4 + 4):
                    if y >= VIEW_H:
                        fat.append(None)
                        continue
                    a, b = canvas[y * VIEW_W + 2 * x], canvas[y * VIEW_W + 2 * x + 1]
                    if a < 0 and b < 0:
                        fat.append(None)
                    elif a < 0 or b < 0:
                        fat.append(c64_colour(max(a, b), x, y))
                    elif abs(LUMA[a] - LUMA[b]) >= 2:
                        fat.append(c64_colour(a if LUMA[a] < LUMA[b] else b, x, y, True))
                    else:
                        fat.append(c64_colour(a if (x + y) & 1 else b, x, y))
            if all(c is None for c in fat):
                cells.append(None)
                continue
            others = {}
            for c in fat:
                if c is not None and c not in FIXED_CODES:
                    others[c] = others.get(c, 0) + 1
            own = max(others, key=lambda c: others[c]) if others else None
            allowed = [12, 11, 15] + ([own] if own is not None else [])
            pattern, mask = bytearray(8), bytearray(8)
            for i, c in enumerate(fat):
                row, col = i // 4, i % 4
                if c is None:
                    continue
                if c not in allowed:
                    c = dmimg.nearest(dmimg.C64[c], allowed)
                code = FIXED_CODES.get(c, 3)
                shift = 6 - col * 2
                pattern[row] |= code << shift
                mask[row] |= 3 << shift
            partial = any(c is None for c in fat)
            flag = 1 | (2 if partial else 0) | ((4 | own << 4) if own is not None else 0)
            cells.append(bytes([flag]) + bytes(pattern) + (bytes(mask) if partial else b''))
        if any(cells):
            first = next(i for i, c in enumerate(cells) if c)
            last = max(i for i, c in enumerate(cells) if c)
            body = bytes([cy, first, last - first + 1]) + b''.join(
                c if c else b'\0' for c in cells[first:last + 1])
            rows.append(struct.pack('<H', len(body)) + body)
    return bytes([len(rows)]) + b''.join(rows)


def build_pieces(G):
    """All the view's pieces, in the game's order (see view.c), as DMWAL."""
    pieces = []
    base = [0] * (VIEW_W * VIEW_H)
    for item, top in ((CEILING, 0), (FLOOR, VIEW_H - 70)):
        w, h, px_ = dmimg.decode(G.item(item))
        for y in range(h):
            base[(top + y) * VIEW_W:(top + y) * VIEW_W + w] = px_[y * w:(y + 1) * w]
    pieces.append(piece_bytes(base))
    for key in ORDER:
        item, x1, x2, y1, y2, sx, sy = FRAMES[key]
        pieces.append(piece_bytes(piece_canvas(G, [(item, x1, x2, y1, y2, sx, sy, key == (3, -2))])))
    for key in DF_KEYS:
        pieces.append(piece_bytes(piece_canvas(G, DOOR_FRAMES[key])))
    # Doors only shut: a part-open door is drawn as the top rows of the shut one (view.c).
    for door_set in range(DOOR_SETS):
        for key in DP_KEYS:
            depth, slices = DOOR_PANELS[key]
            x1, x2, y1, y2, sx, sy = slices[0]
            item = FIRST_DOOR_SET + door_set * 3 + depth
            pieces.append(piece_bytes(piece_canvas(G, [(item, x1, x2, y1, y2, sx, sy, False)])))
    for way in ('up', 'down'):
        for key in ST_KEYS:
            item, x1, x2, y1, y2, sx, mirror = STAIRS_FRONT[way][key]
            pieces.append(piece_bytes(piece_canvas(G, [(item, x1, x2, y1, y2, sx, 0, mirror)])))
    # Offsets are in words (each piece padded to an even length), so DMWAL can pass 64K.
    pieces = [p + b'\0' if len(p) & 1 else p for p in pieces]
    offset = 2 + 2 * len(pieces)
    directory = bytearray(struct.pack('<H', len(pieces)))
    for p in pieces:
        directory += struct.pack('<H', offset // 2)
        offset += len(p)
    return bytes(directory) + b''.join(pieces)


# Where each kind of piece starts in DMWAL (view.c).
PIECE_WALL = 1
PIECE_DOOR_FRAME = PIECE_WALL + len(ORDER)
PIECE_DOOR = PIECE_DOOR_FRAME + len(DF_KEYS)
PIECE_STAIRS = PIECE_DOOR + DOOR_SETS * len(DP_KEYS)
PIECES = PIECE_STAIRS + 2 * len(ST_KEYS)

# DMWAL is packed on the disk, and unpacked into far memory as it loads (walls.c). A byte below
# $80 is followed by that many plus one bytes to copy; otherwise its bits 2-6 plus 3 are a
# length, and its bits 0-1 with the next byte, plus 1, how far back to copy from: never more
# than 1024 bytes, so the game unpacks through a 1K ring.
LZ_WINDOW, LZ_LONGEST = 1024, 34


def lz_pack(data):
    out, lit, heads, i, n = bytearray(), bytearray(), {}, 0, len(data)

    def flush():
        while lit:
            out.append(min(len(lit), 128) - 1)
            out.extend(lit[:128])
            del lit[:128]
    while i < n:
        best, back = 0, 0
        for j in reversed(heads.get(data[i:i + 3], [])):
            if i - j > LZ_WINDOW:
                break
            k = 0
            while k < LZ_LONGEST and i + k < n and data[j + k] == data[i + k]:
                k += 1
            if k > best:
                best, back = k, i - j
                if k == LZ_LONGEST:
                    break
        step = best if best >= 3 else 1
        for k in range(i, i + step):
            heads.setdefault(data[k:k + 3], []).append(k)
        if best >= 3:
            flush()
            out += bytes([0x80 | (best - 3) << 2 | (back - 1) >> 8, (back - 1) & 255])
        else:
            lit.append(data[i])
        i += step
    flush()
    return bytes(out)


def lz_unpack(packed, size):
    out, i = bytearray(), 0
    while len(out) < size:
        t = packed[i]
        if t < 0x80:
            out += packed[i + 1:i + 2 + t]
            i += 2 + t
        else:
            back = ((t & 3) << 8 | packed[i + 1]) + 1
            for _ in range(((t >> 2) & 31) + 3):
                out.append(out[-back])
            i += 2
    return bytes(out)


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
