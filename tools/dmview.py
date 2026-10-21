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
# DMWAL: a word, the number of pieces; three bytes per piece, its offset in DMWAL; then each
# piece:
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


def build_pieces(G, used_orns=None, door_kinds=(), objinfo=(), level_orns=b''):
    """All the view's pieces, in the game's order (see view.c), as DMWAL. used_orns: the wall
    ornaments any level uses (the others are left empty); door_kinds: the doors it has, as
    (door set, ornament) pairs (DOOR_KIND_ below)."""
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
    for way in ('up', 'down'):
        for key in ST_KEYS:
            item, x1, x2, y1, y2, sx, mirror = STAIRS_FRONT[way][key]
            pieces.append(piece_bytes(piece_canvas(G, [(item, x1, x2, y1, y2, sx, 0, mirror)])))
    assert len(pieces) == PIECE_ORN
    used = set(range(WALL_ORNS)) if used_orns is None else set(used_orns)
    for orn in range(WALL_ORNS):
        for vw in range(13):
            pieces.append(piece_bytes(ornament_canvas(G, orn, vw)) if orn in used else b'\0')
    portraits = dmimg.decode(G.item(PORTRAITS))
    for champion in range(PORTRAIT_COUNT):
        pieces.append(piece_bytes(portrait_canvas(portraits, champion)))
    assert len(pieces) == PIECE_PIT
    for invisible in (False, True):
        for pos in range(12):
            visible = not invisible or pos in INVISIBLE_PIT
            pieces.append(piece_bytes(pit_canvas(G, pos, invisible)) if visible else b'\0')
    for pos in range(12):
        pieces.append(piece_bytes(field_canvas(G, pos)))
    for orn in range(FLOOR_ORNS):
        for pos in range(9):
            pieces.append(piece_bytes(floor_orn_canvas(G, orn, pos)))
    for pos in range(4):
        pieces.append(piece_bytes(door_button_canvas(G, pos)))
    assert len(pieces) == PIECE_OBJ
    pieces += object_pieces(G)
    pieces.append(object_tables(objinfo) + level_orns)
    pieces += projectile_pieces(G)
    assert len(pieces) == PIECE_DOOR
    # Doors only shut: a part-open door is drawn as the top rows of the shut one (view.c).
    for door_set, orn in door_kinds:
        for key in DP_KEYS:
            pieces.append(piece_bytes(door_canvas(G, door_set, orn, key)))
    offset = 2 + 3 * len(pieces)
    directory = bytearray(struct.pack('<H', len(pieces)))
    for p in pieces:
        directory += struct.pack('<I', offset)[:3]
        offset += len(p)
    return bytes(directory) + b''.join(pieces)


# ---- wall ornaments ----------------------------------------------------------------------------
# DM's 60 wall ornaments (switches, locks, alcoves, torch holders, the mirrors...) are two pictures
# each from item 121: seen from the side, then from the front, both as at depth 1. Farther off
# they're shrunk, with darker colours. Where each goes, for each of 13 places a wall face can be
# seen, is one of 8 sets of boxes: (x1, x2, y1, y2, byte width, height); the width in pixels is
# twice the byte width. From ScummVM's DisplayMan (G0205, G0194; the Atari ST's D3 boxes).
FIRST_WALL_ORN, WALL_ORNS = 121, 60
# The 13 places (view.c's VW_): 0 D3L's right side, 1 D3R's left side, 2-4 the fronts of D3L,
# D3C, D3R, 5 D2L's right side, 6 D2R's left side, 7-9 the fronts of D2L, D2C, D2R, 10 D1L's
# right side, 11 D1R's left side, 12 D1C's front.
WALL_ORN_BOXES = [
    [(80, 83, 41, 45, 8, 5), (140, 143, 41, 45, 8, 5), (16, 29, 39, 50, 8, 12), (107, 120, 39, 50, 8, 12),
     (187, 200, 39, 50, 8, 12), (67, 77, 40, 49, 8, 10), (146, 156, 40, 49, 8, 10), (0, 17, 38, 55, 16, 18),
     (102, 123, 38, 55, 16, 18), (206, 223, 38, 55, 16, 18), (48, 63, 38, 56, 8, 19), (160, 175, 38, 56, 8, 19),
     (96, 127, 36, 63, 16, 28)],
    [(74, 82, 41, 60, 8, 20), (141, 149, 41, 60, 8, 20), (1, 47, 37, 63, 24, 27), (88, 134, 37, 63, 24, 27),
     (171, 217, 37, 63, 24, 27), (61, 76, 38, 67, 8, 30), (147, 162, 38, 67, 8, 30), (0, 43, 37, 73, 32, 37),
     (80, 143, 37, 73, 32, 37), (180, 223, 37, 73, 32, 37), (32, 63, 36, 83, 16, 48), (160, 191, 36, 83, 16, 48),
     (64, 159, 36, 91, 48, 56)],
    [(80, 83, 66, 70, 8, 5), (140, 143, 66, 70, 8, 5), (16, 29, 64, 75, 8, 12), (106, 119, 64, 75, 8, 12),
     (187, 200, 64, 75, 8, 12), (67, 77, 74, 83, 8, 10), (146, 156, 74, 83, 8, 10), (0, 17, 73, 90, 16, 18),
     (100, 121, 73, 90, 16, 18), (206, 223, 73, 90, 16, 18), (48, 63, 84, 102, 8, 19), (160, 175, 84, 102, 8, 19),
     (96, 127, 92, 119, 16, 28)],
    [(80, 83, 49, 53, 8, 5), (140, 143, 49, 53, 8, 5), (16, 29, 50, 61, 8, 12), (106, 119, 50, 61, 8, 12),
     (187, 200, 50, 61, 8, 12), (67, 77, 53, 62, 8, 10), (146, 156, 53, 62, 8, 10), (0, 17, 55, 72, 16, 18),
     (100, 121, 55, 72, 16, 18), (206, 223, 55, 72, 16, 18), (48, 63, 57, 75, 8, 19), (160, 175, 57, 75, 8, 19),
     (96, 127, 64, 91, 16, 28)],
    [(75, 90, 40, 44, 8, 5), (133, 148, 40, 44, 8, 5), (1, 48, 44, 49, 24, 6), (88, 135, 44, 49, 24, 6),
     (171, 218, 44, 49, 24, 6), (60, 77, 40, 46, 16, 7), (146, 163, 40, 46, 16, 7), (0, 35, 43, 50, 32, 8),
     (80, 143, 43, 50, 32, 8), (184, 223, 43, 50, 32, 8), (32, 63, 41, 52, 16, 12), (160, 191, 41, 52, 16, 12),
     (64, 159, 41, 52, 48, 12)],
    [(78, 85, 36, 51, 8, 16), (138, 145, 36, 51, 8, 16), (10, 41, 34, 53, 16, 20), (98, 129, 34, 53, 16, 20),
     (179, 210, 34, 53, 16, 20), (66, 75, 34, 56, 8, 23), (148, 157, 34, 56, 8, 23), (0, 26, 33, 61, 24, 29),
     (91, 133, 33, 61, 24, 29), (194, 223, 33, 61, 24, 29), (41, 56, 31, 65, 8, 35), (167, 182, 31, 65, 8, 35),
     (80, 143, 29, 71, 32, 43)],
    [(75, 82, 25, 75, 8, 51), (142, 149, 25, 75, 8, 51), (12, 60, 25, 75, 32, 51), (88, 136, 25, 75, 32, 51),
     (163, 211, 25, 75, 32, 51), (64, 73, 20, 90, 8, 71), (150, 159, 20, 90, 8, 71), (0, 38, 20, 90, 32, 71),
     (82, 142, 20, 90, 32, 71), (184, 223, 20, 90, 32, 71), (41, 56, 9, 119, 8, 111), (169, 184, 9, 119, 8, 111),
     (64, 159, 9, 119, 48, 111)],
    [(74, 85, 25, 75, 8, 51), (137, 149, 25, 75, 8, 51), (0, 83, 25, 75, 48, 51), (74, 149, 25, 75, 48, 51),
     (139, 223, 25, 75, 48, 51), (60, 77, 20, 90, 16, 71), (146, 163, 20, 90, 16, 71), (0, 74, 20, 90, 56, 71),
     (60, 163, 20, 90, 56, 71), (149, 223, 20, 90, 56, 71), (32, 63, 9, 119, 16, 111), (160, 191, 9, 119, 16, 111),
     (32, 191, 9, 119, 80, 111)],
]
WALL_ORN_SET = [1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 2, 2, 1, 1, 1, 1, 1, 4, 4, 4, 5, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 0, 2, 6, 6, 6, 7]
# DM's colours for wall ornaments and door buttons at depth 3 and 2 (ScummVM's G0198, G0199).
PAL_D3 = [0, 0, 12, 3, 4, 3, 0, 6, 3, 9, 10, 11, 0, 1, 0, 2]
PAL_D2 = [0, 12, 1, 3, 4, 3, 6, 7, 5, 9, 10, 11, 0, 2, 14, 13]
PORTRAITS, PORTRAIT_COUNT = 26, 24
PORTRAIT_BOX = (96, 127, 35, 63)        # in the mirror, at depth 1


def shrink(src, sw, sh, dw, dh, pal):
    """DM's shrinking (nearest pixel) with a colour change: ScummVM's blitToBitmapShrinkWithPalChange."""
    sx_step, sy_step = (32768 * sw) // dw, (32768 * sh) // dh
    out = []
    for y in range(dh):
        row = ((y * sy_step) // 32768) * sw
        for x in range(dw):
            out.append(pal[src[row + (x * sx_step) // 32768]])
    return out


def mirrored(px, w, h):
    return [px[y * w + w - 1 - x] for y in range(h) for x in range(w)]


def boxed_canvas(px, w, h, box, src_x, src_y=0, clear=CLEAR):
    """A canvas with picture px (w x h) shown in box (x1, x2, y1, y2) from (src_x, src_y)."""
    canvas = [-1] * (VIEW_W * VIEW_H)
    x1, x2, y1, y2 = box[:4]
    for y in range(y1, y2 + 1):
        for x in range(x1, x2 + 1):
            sx, sy = src_x + x - x1, src_y + y - y1
            if 0 <= sx < w and 0 <= sy < h and 0 <= x < VIEW_W and 0 <= y < VIEW_H:
                c = px[sy * w + sx]
                if c != clear:
                    canvas[y * VIEW_W + x] = c
    return canvas


def ornament_canvas(G, orn, vw):
    """Wall ornament orn at place vw, as DM draws it (ScummVM's isDrawnWallOrnAnAlcove)."""
    boxes = WALL_ORN_BOXES[WALL_ORN_SET[orn]]
    box = boxes[vw]
    front = vw in (2, 3, 4, 7, 8, 9, 12)
    w, h, px = dmimg.decode(G.item(FIRST_WALL_ORN + orn * 2 + front))
    if vw >= 10:
        if vw == 11:
            px = mirrored(px, w, h)
        return boxed_canvas(px, w, h, box, 0)
    flip = vw in (1, 6)
    ref = boxes[12] if front else boxes[11] if flip else boxes[10]
    assert (w, h) == (ref[4] * 2, ref[5]), (orn, vw, w, h, ref)
    coords = boxes[vw + (1 if vw == 7 else -1 if vw == 9 else 0)]
    dw, dh = box[4] * 2, box[5]
    px = shrink(px, w, h, dw, dh, PAL_D3 if vw <= 4 else PAL_D2)
    src_x = coords[1] - coords[0]
    if flip:
        px = mirrored(px, dw, dh)
        src_x = 15 - (src_x & 15)
    elif vw == 7:
        src_x -= box[1] - box[0]
    else:
        src_x = 0
    return boxed_canvas(px, dw, dh, box, src_x)


# ---- floors: pits, teleporter fields, floor ornaments ---------------------------------------------
# The 12 floor places (view.c's sq_pos): D3L, D3C, D3R, D2L, D2C, D2R, D1L, D1C, D1R, D0L, D0C,
# D0R. A right-hand place shows the left one's picture mirrored.
# Pits: (item, x1, x2, y1, y2), from ScummVM's G0140-G0151; invisible pits (only seen from depth 2
# in) have pictures of their own.
PITS = [(49, 0, 79, 66, 73), (50, 64, 159, 66, 73), (49, 144, 223, 66, 73),
        (51, 0, 79, 77, 88), (52, 64, 159, 77, 88), (51, 144, 223, 77, 88),
        (53, 0, 63, 93, 116), (54, 32, 191, 93, 116), (53, 160, 223, 93, 116),
        (55, 0, 31, 124, 135), (56, 16, 207, 124, 135), (55, 192, 223, 124, 135)]
INVISIBLE_PIT = {3: 57, 4: 58, 5: 57, 6: 59, 7: 60, 8: 59, 9: 61, 10: 62, 11: 61}
# Teleporter fields: the sparkles of item 73 over each place's wall box, cut to shape by a mask
# (items 69-72, mirrored on the right) for the places to the side: (x1, x2, y1, y2, mask, flip,
# mask x), from ScummVM's G0163 and G0188.
FIELD, FIELD_MASK = 73, 69
FIELDS = [(0, 83, 25, 75, 0, False, 11), (74, 149, 25, 75, None, False, 0), (139, 223, 25, 75, 0, True, 0),
          (0, 74, 20, 90, 1, False, 5), (60, 163, 20, 90, None, False, 0), (149, 223, 20, 90, 1, True, 0),
          (0, 63, 9, 119, 2, False, 0), (32, 191, 9, 119, None, False, 0), (160, 223, 9, 119, 2, True, 0),
          (0, 31, 0, 135, 3, False, 0), (0, 223, 0, 135, None, False, 0), (192, 223, 0, 135, 3, True, 0)]
# Floor ornaments (pressure pads, grates, moss, puddles...): 6 pictures each from item 247 (D3L,
# D3C, D2L, D2C, D1L, D1C), in one of 3 sets of boxes for D3L-D1R (ScummVM's G0206, G0195).
FIRST_FLOOR_ORN, FLOOR_ORNS = 247, 9
FLOOR_ORN_SET = [0, 0, 0, 0, 2, 0, 0, 2, 0]
FLOOR_ORN_BOXES = [
    [(32, 79, 66, 71), (96, 127, 66, 71), (144, 191, 66, 71), (0, 63, 77, 87), (80, 143, 77, 87),
     (160, 223, 77, 87), (0, 31, 92, 116), (80, 143, 92, 116), (192, 223, 92, 116)],
    [(0, 95, 66, 74), (64, 159, 66, 74), (128, 223, 66, 74), (0, 79, 75, 89), (56, 167, 75, 89),
     (144, 223, 75, 89), (0, 63, 90, 118), (32, 191, 90, 118), (160, 223, 90, 118)],
    [(42, 57, 68, 72), (104, 119, 68, 72), (166, 181, 68, 72), (9, 40, 80, 85), (96, 127, 80, 85),
     (183, 214, 80, 85), (0, 15, 97, 108), (96, 127, 97, 108), (208, 223, 97, 108)],
]


def picture_canvas(G, item, box, flip):
    w, h, px = dmimg.decode(G.item(item))
    if flip:
        px = mirrored(px, w, h)
    return boxed_canvas(px, w, h, box, 0)


def pit_canvas(G, pos, invisible):
    item, x1, x2, y1, y2 = PITS[pos]
    if invisible:
        item = INVISIBLE_PIT[pos]
    return picture_canvas(G, item, (x1, x2, y1, y2), pos % 3 == 2)


def field_canvas(G, pos):
    x1, x2, y1, y2, mask, flip, mx = FIELDS[pos]
    tw, th, tile = dmimg.decode(G.item(FIELD))
    if mask is not None:
        mw, mh, mpx = dmimg.decode(G.item(FIELD_MASK + mask))
        if flip:
            mpx = mirrored(mpx, mw, mh)
    canvas = [-1] * (VIEW_W * VIEW_H)
    for y in range(y1, y2 + 1):
        for x in range(x1, x2 + 1):
            if mask is not None:
                u, v = mx + x - x1, y - y1
                if not (0 <= u < mw and v < mh) or not mpx[v * mw + u]:
                    continue
            c = tile[(y % th) * tw + x % tw]
            if c != CLEAR:
                canvas[y * VIEW_W + x] = c
    return canvas


def floor_orn_canvas(G, orn, pos):
    box = FLOOR_ORN_BOXES[FLOOR_ORN_SET[orn]][pos]
    item = FIRST_FLOOR_ORN + orn * 6 + [0, 1, 0, 2, 3, 2, 4, 5, 4][pos]
    return picture_canvas(G, item, box, pos % 3 == 2)


# ---- doors' ornaments and buttons -------------------------------------------------------------
# A door's ornament (iron bars, locks, jewels...) is drawn onto its picture before it's shown,
# so its see-through parts make holes in the door. One picture each from item 303, as at depth
# 1; farther off shrunk, with other colours. Boxes on the door's picture (48x41, 64x61, 96x88)
# for depth 3, 2, 1 in one of 4 sets (ScummVM's G0207, G0196). DM's "ornament" 12 here is the
# broken door (item 301), drawn over doors that have been bashed down.
FIRST_DOOR_ORN, DOOR_ORN_BROKEN, DOOR_ORN_NONE = 303, 12, 15
DOOR_BROKEN_PICTURE = 301
DOOR_ORN_SET = [0, 1, 1, 1, 0, 2, 3, 1, 2, 2, 1, 1, 1]
DOOR_ORN_BOXES = [
    [(17, 31, 8, 17, 10), (22, 42, 11, 23, 13), (32, 63, 13, 31, 19)],
    [(0, 47, 0, 40, 41), (0, 63, 0, 60, 61), (0, 95, 0, 87, 88)],
    [(17, 31, 15, 24, 10), (22, 42, 22, 34, 13), (32, 63, 31, 49, 19)],
    [(23, 35, 31, 39, 9), (30, 48, 41, 52, 12), (44, 75, 61, 79, 19)],
]
PAL_DOOR_ORN_D3 = [0, 12, 1, 3, 4, 3, 0, 6, 3, 9, 10, 11, 0, 2, 0, 13]
GOLD = 9                                # an ornament's see-through colour
# Door buttons: item 315 (16x9) at D1C, shrunk farther off: (x1, x2, y1, y2, height) at D3R, D3C,
# D2C, D1C (ScummVM's G0208).
DOOR_BUTTON = 315
DOOR_BUTTONS = [(199, 204, 41, 44, 4), (136, 141, 41, 44, 4), (144, 155, 42, 47, 6), (160, 175, 44, 52, 9)]


def door_canvas(G, door_set, orn, key):
    """A shut door of door_set, with ornament orn (or DOOR_ORN_NONE), at place key."""
    depth, slices = DOOR_PANELS[key]
    w, h, px = dmimg.decode(G.item(FIRST_DOOR_SET + door_set * 3 + depth))
    px = list(px)
    if orn != DOOR_ORN_NONE:
        item = DOOR_BROKEN_PICTURE if orn == DOOR_ORN_BROKEN else FIRST_DOOR_ORN + orn
        ow, oh, opx = dmimg.decode(G.item(item))
        x1, x2, y1, y2, oh2 = DOOR_ORN_BOXES[DOOR_ORN_SET[orn]][depth]
        if depth < 2:
            ow2 = x2 - x1 + 1
            opx = shrink(opx, ow, oh, ow2, oh2, PAL_DOOR_ORN_D3 if depth == 0 else list(range(16)))
            ow, oh = ow2, oh2
        for y in range(y1, y2 + 1):
            for x in range(x1, x2 + 1):
                if x - x1 < ow and y - y1 < oh and x < w and y < h:
                    c = opx[(y - y1) * ow + x - x1]
                    if c != GOLD:
                        px[y * w + x] = c
    x1, x2, y1, y2, sx, sy = slices[0]
    return boxed_canvas(px, w, h, (x1, x2, y1, y2), sx, sy)


def door_button_canvas(G, pos):
    w, h, px = dmimg.decode(G.item(DOOR_BUTTON))
    x1, x2, y1, y2, bh = DOOR_BUTTONS[pos]
    if pos < 3:
        bw = (x2 - x1 + 2) & ~1
        px = shrink(px, w, h, bw, bh, PAL_D3 if pos < 2 else PAL_D2)
        w, h = bw, bh
    return boxed_canvas(px, w, h, (x1, x2, y1, y2), 0)


# ---- objects ----------------------------------------------------------------------------------
# Objects lying in the view are DM's own pictures (from item 360), placed by view.c at run time
# (bitmap.s's object_row shifts them into place), so each is stored once per size: as at depth 1,
# and shrunk to 20/32 and 16/32 with darker colours (ScummVM's G0209 object aspects, G0213/4).
# Per aspect: (first picture, byte width, height, flags, set of places); flags 1: shown mirrored
# on the right, 0x10: a picture of its own in an alcove (the next one).
FIRST_OBJECT = 360
OBJECT_ASPECTS = [
    (0, 24, 27, 0x11, 0), (2, 24, 8, 0, 1), (3, 8, 18, 0, 1), (4, 8, 8, 0, 1), (5, 8, 4, 0, 1),
    (6, 16, 11, 0, 1), (7, 24, 13, 0, 0), (8, 32, 16, 0, 0), (9, 40, 24, 0, 0), (10, 16, 20, 0, 1),
    (11, 40, 20, 0, 0), (12, 32, 4, 0, 1), (13, 40, 8, 0, 1), (14, 32, 17, 0, 0), (15, 40, 17, 0, 2),
    (16, 16, 9, 0, 1), (17, 24, 5, 0, 1), (18, 16, 9, 0, 0), (19, 8, 4, 0, 1), (20, 32, 21, 0, 2),
    (21, 32, 25, 0, 2), (22, 32, 14, 0, 1), (23, 32, 26, 0, 2), (24, 32, 16, 0, 0), (25, 32, 16, 0, 0),
    (26, 16, 16, 0, 1), (27, 16, 15, 0, 1), (28, 16, 13, 0, 1), (29, 16, 10, 0, 1), (30, 40, 24, 0, 0),
    (31, 40, 9, 0, 1), (32, 16, 3, 0, 1), (33, 32, 5, 0, 1), (34, 40, 16, 0, 0), (35, 8, 7, 0, 1),
    (36, 32, 7, 0, 1), (37, 24, 14, 0, 0), (38, 16, 8, 0, 0), (39, 8, 3, 0, 1), (40, 40, 9, 0, 1),
    (41, 24, 14, 0, 0), (42, 40, 20, 0, 0), (43, 40, 15, 0, 1), (44, 32, 10, 0, 1), (45, 32, 19, 0, 0),
    (46, 40, 25, 0, 2), (47, 24, 7, 0, 1), (48, 8, 7, 0, 1), (49, 16, 5, 0, 1), (50, 8, 9, 0, 1),
    (51, 32, 11, 0, 1), (52, 32, 14, 0, 0), (53, 24, 20, 0, 0), (54, 16, 14, 0, 1), (55, 32, 23, 0, 0),
    (56, 24, 16, 0, 0), (57, 32, 25, 0, 0), (58, 24, 25, 0, 0), (59, 8, 8, 0, 1), (60, 8, 7, 0, 1),
    (61, 8, 8, 0, 1), (62, 8, 8, 0, 1), (63, 8, 5, 0, 1), (64, 8, 13, 1, 1), (65, 16, 13, 0, 1),
    (66, 16, 14, 0, 0), (67, 16, 10, 0, 1), (68, 8, 18, 0, 1), (69, 8, 17, 0, 1), (70, 32, 18, 0, 0),
    (71, 16, 23, 0, 0), (72, 16, 24, 0, 0), (73, 16, 15, 0, 0), (74, 8, 7, 0, 1), (75, 8, 15, 0, 1),
    (76, 8, 9, 0, 1), (77, 16, 14, 0, 0), (78, 8, 8, 0, 1), (79, 16, 9, 0, 1), (80, 8, 13, 1, 1),
    (81, 8, 18, 0, 1), (82, 24, 28, 0, 0), (83, 40, 13, 0, 1), (84, 8, 4, 0, 1), (85, 32, 17, 0, 0),
]
OBJECT_SCALES = [32, 20, 16]
PAL_FLOOR_D3 = [0, 12, 1, 3, 4, 3, 0, 6, 3, 9, 10, 11, 0, 2, 14, 13]
# Where objects lie: for each set, each view square (D3C, D3L, D3R, D2C, D2L, D2R, D1C, D1L, D1R,
# D0C) and each place in it (4 cells, then an alcove), the middle of the bottom (ScummVM's G0218).
OBJECT_PLACES = [
    [[(0, 0), (0, 0), (125, 72), (95, 72), (112, 64)], [(0, 0), (0, 0), (62, 72), (25, 72), (24, 64)],
     [(0, 0), (0, 0), (200, 72), (162, 72), (194, 64)], [(92, 78), (132, 78), (136, 86), (88, 86), (112, 74)],
     [(10, 78), (53, 78), (41, 86), (0, 0), (3, 74)], [(171, 78), (218, 78), (0, 0), (183, 86), (219, 74)],
     [(83, 96), (141, 96), (148, 111), (76, 111), (112, 94)], [(0, 0), (26, 96), (5, 111), (0, 0), (0, 0)],
     [(197, 96), (0, 0), (0, 0), (220, 111), (0, 0)], [(66, 131), (158, 131), (0, 0), (0, 0), (0, 0)]],
    [[(0, 0), (0, 0), (125, 72), (95, 72), (112, 63)], [(0, 0), (0, 0), (62, 72), (25, 72), (24, 63)],
     [(0, 0), (0, 0), (200, 72), (162, 72), (194, 63)], [(92, 78), (132, 78), (136, 86), (88, 86), (112, 73)],
     [(10, 78), (53, 78), (41, 86), (0, 0), (3, 73)], [(171, 78), (218, 78), (0, 0), (183, 86), (219, 73)],
     [(83, 96), (141, 96), (148, 111), (76, 111), (112, 89)], [(0, 0), (26, 96), (5, 111), (0, 0), (0, 0)],
     [(197, 96), (0, 0), (0, 0), (220, 111), (0, 0)], [(66, 131), (158, 131), (0, 0), (0, 0), (0, 0)]],
    [[(0, 0), (0, 0), (125, 75), (95, 75), (112, 65)], [(0, 0), (0, 0), (62, 75), (25, 75), (24, 65)],
     [(0, 0), (0, 0), (200, 75), (162, 75), (194, 65)], [(92, 81), (132, 81), (136, 88), (88, 88), (112, 76)],
     [(10, 81), (53, 81), (41, 88), (0, 0), (3, 76)], [(171, 81), (218, 81), (0, 0), (183, 86), (219, 76)],
     [(83, 98), (141, 98), (148, 115), (76, 115), (112, 98)], [(0, 0), (26, 98), (5, 115), (0, 0), (0, 0)],
     [(197, 98), (0, 0), (0, 0), (220, 115), (0, 0)], [(66, 135), (158, 135), (0, 0), (0, 0), (0, 0)]],
]


def object_sprite(px, w, h):
    """A picture (w x h, colour 10 see-through) as an object sprite: its width in double pixels,
    height, own colour, then each row: for each 4 double pixels, a byte of pattern and one of
    mask. Every cell it covers can show the background, the two greys and one colour of its own:
    the one it uses most."""
    fw = w // 2
    fat = []
    for y in range(h):
        for x in range(fw):
            a, b = px[y * w + 2 * x], px[y * w + 2 * x + 1]
            if a == CLEAR and b == CLEAR:
                fat.append(None)
            elif a == CLEAR or b == CLEAR:
                fat.append(c64_colour(b if a == CLEAR else a, x, y))
            elif abs(LUMA[a] - LUMA[b]) >= 2:
                fat.append(c64_colour(a if LUMA[a] < LUMA[b] else b, x, y, True))
            else:
                fat.append(c64_colour(a if (x + y) & 1 else b, x, y))
    counts = {}
    for c in fat:
        if c is not None and c not in FIXED_CODES:
            counts[c] = counts.get(c, 0) + 1
    own = max(counts, key=lambda c: counts[c]) if counts else None
    allowed = [12, 11, 15] + ([own] if own is not None else [])
    cells = (fw + 3) // 4
    out = bytearray([fw, h, own if own is not None else 255])
    for y in range(h):
        for cx in range(cells):
            pattern = mask = 0
            for i in range(4):
                x = cx * 4 + i
                c = fat[y * fw + x] if x < fw else None
                if c is None:
                    continue
                if c not in allowed:
                    c = dmimg.nearest(dmimg.C64[c], allowed)
                pattern |= FIXED_CODES.get(c, 3) << (6 - 2 * i)
                mask |= 3 << (6 - 2 * i)
            out += bytes([pattern, mask])
    return bytes(out)


def object_pieces(G):
    """Each object aspect at each size, plain, mirrored (if it ever is) and in an alcove (if it
    has a picture for that): 9 pieces each."""
    pieces = []
    for rel, bw, h, flags, _ in OBJECT_ASPECTS:
        for scale in OBJECT_SCALES:
            for variant in range(3):
                if (variant == 1 and not flags & 1) or (variant == 2 and not flags & 0x10):
                    pieces.append(b'\0')
                    continue
                w, hh, px = dmimg.decode(G.item(FIRST_OBJECT + rel + (variant == 2)))
                if scale != 32:
                    sw = (bw * scale + scale // 2) // 32
                    sh = (h * scale + scale // 2) // 32
                    px = shrink(px, w, hh, sw * 2, sh, PAL_FLOOR_D3 if scale == 16 else list(range(16)))
                    w, hh = sw * 2, sh
                if variant == 1:
                    px = mirrored(px, w, hh)
                pieces.append(object_sprite(px, w, hh))
    return pieces


# Spells in flight: DM's pictures from item 316 for a fireball, lightning, anything else, and
# poison (ScummVM's G0210 aspects 10, 3, 11, 13), at depth 3, 2, 1 and 0 (G0215's scales; the
# colours darker at depth 3).
FIRST_PROJECTILE = 316
PROJECTILES = [(28, 16, 28), (9, 32, 15), (29, 16, 11), (31, 16, 24)]
PROJECTILE_SCALES = [16, 22, 28, 32]


def projectile_pieces(G):
    pieces = []
    for rel, bw, h in PROJECTILES:
        w, hh, native = dmimg.decode(G.item(FIRST_PROJECTILE + rel))
        for scale in PROJECTILE_SCALES:
            px = native
            sw, sh = w, hh
            if scale != 32:
                sw = (bw * scale + scale // 2) // 32 * 2
                sh = (h * scale + scale // 2) // 32
                px = shrink(native, w, hh, sw, sh, PAL_FLOOR_D3 if scale == 16 else list(range(16)))
            pieces.append(object_sprite(px, sw, sh))
    return pieces


def object_tables(objinfo):
    """What view.c needs to place objects, kept in DMWAL: each set's places (3 x 10 x 5 x X, Y),
    each aspect's set, flags (16: mirrored on the right, 32: an alcove picture), and each object
    type's aspect."""
    out = bytearray()
    for places in OBJECT_PLACES:
        for square in places:
            for x, y in square:
                out += bytes([x, y])
    out += bytes(cs | (flags & 1) << 4 | (flags & 0x10) << 1 for _, _, _, flags, cs in OBJECT_ASPECTS)
    out += bytes(o[1] for o in objinfo)
    return bytes(out)


# ---- light and darkness ---------------------------------------------------------------------
# DM darkens the view with five more palettes, each darker than the last (GRAPHICS.DAT item 562).
# On the C64 each of the view's colours becomes another (view.c): for each light level and C64
# colour, the C64 colour nearest to what the DM colour it stands for becomes at that level.
# (Colours no DM colour maps to are darkened like the greys.)
LIGHT_REPRESENTS = {0: 0, 11: 12, 12: 1, 15: 13, 1: 15, 9: 3, 3: 4, 8: 5, 5: 7, 2: 8, 10: 10,
                    7: 11, 6: 14}


def light_colours(G):
    g = G.item(dmimg.PALETTE_ITEM)
    pals = [[dmimg.st_rgb(w) for w in struct.unpack_from('>16H', g, dmimg.PALETTE_OFFSET + 32 * level)]
            for level in range(6)]
    out = []
    for level, pal in enumerate(pals):
        row = []
        for c in range(16):
            if level == 0:
                row.append(c)
            elif c in LIGHT_REPRESENTS:
                rep = LIGHT_REPRESENTS[c]
                greys = rep in (0, 1, 2, 12, 13, 15)        # greys stay greys
                row.append(dmimg.nearest(pal[rep], (0, 11, 12, 15, 1) if greys else range(16)))
            else:
                f = sum(pal[15]) / max(1, sum(pals[0][15]))
                row.append(dmimg.nearest(tuple(int(v * f) for v in dmimg.C64[c]), range(16)))
        out.append(row)
    return out


# ---- writing on walls -----------------------------------------------------------------------------
# DM's inscription font (item 120): 36 shaded letters, 8x8 each, side by side: A-Z, space, full
# stop, then symbols. Each becomes one cell of the bitmap: 8 bytes of pattern, 8 of mask (view.c
# draws it with bitmap.s's stamp_cell, the cell's own colour white).
INSCRIPTION_FONT, INSCRIPTION_CHARS = 120, 36


def inscription_cells(G):
    w, h, px = dmimg.decode(G.item(INSCRIPTION_FONT))
    out = bytearray()
    for c in range(INSCRIPTION_CHARS):
        canvas = [-1] * (VIEW_W * VIEW_H)
        for y in range(8):
            for x in range(8):
                v = px[y * w + c * 8 + x]
                if v != CLEAR:
                    canvas[y * VIEW_W + x] = v
        data = piece_bytes(canvas)
        if len(data) < 7:
            out += bytes(16)                    # nothing: a space
            continue
        # One row of one cell: rows, row length, cell row, column, cells, then the cell.
        flag = data[6]
        pattern = data[7:15]
        mask = data[15:23] if flag & 2 else b'\xff' * 8
        out += pattern + mask
    return bytes(out)


def portrait_canvas(portraits, champion):
    """Champion's portrait in a mirror right ahead (32x29 pixels each, eight to a row)."""
    w, h, px = portraits
    return boxed_canvas(px, w, h, PORTRAIT_BOX, (champion & 7) * 32, (champion >> 3) * 29, clear=1)


# Where each kind of piece starts in DMWAL (view.c).
PIECE_WALL = 1
PIECE_DOOR_FRAME = PIECE_WALL + len(ORDER)
PIECE_STAIRS = PIECE_DOOR_FRAME + len(DF_KEYS)
PIECE_ORN = PIECE_STAIRS + 2 * len(ST_KEYS)
PIECE_PORTRAIT = PIECE_ORN + WALL_ORNS * 13
PIECE_PIT = PIECE_PORTRAIT + PORTRAIT_COUNT       # + place, + 12 for an invisible pit
PIECE_FIELD = PIECE_PIT + 24                        # + place
PIECE_FLOOR_ORN = PIECE_FIELD + 12                  # + ornament * 9 + place
PIECE_DOOR_BUTTON = PIECE_FLOOR_ORN + FLOOR_ORNS * 9  # + D3R, D3C, D2C, D1C
PIECE_OBJ = PIECE_DOOR_BUTTON + 4                   # + (aspect * 3 + size) * 3 + variant
PIECE_OBJ_TABLES = PIECE_OBJ + len(OBJECT_ASPECTS) * 9
PIECE_PROJECTILE = PIECE_OBJ_TABLES + 1            # + kind * 4 + depth (3 to 0)
PIECE_DOOR = PIECE_PROJECTILE + 16                  # + door kind * 9 + place (DP_KEYS); last

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
