"""Builds the C64 port's data from the original Atari ST disk image.

    python dmconv.py "Dungeon Master.stx" <gen dir> <bin dir>

Writes:
  <gen>/dmdata.c, dmdata.h  the rules tables from GRAPHICS.DAT and each level's size and place
  <gen>/glyphs.h            the custom character codes (from art.py)
  <bin>/dmlev.bin           DMLEV: the 14 levels, each a self-contained image (below), which the
                            game keeps in far memory (an REU or SuperCPU, through FarMem) and
                            copies into ordinary memory when the party arrives on the level
  <bin>/dmhi.bin            DMHI: the custom characters and the mouse pointer, loaded into the
                            RAM under the C64's I/O area
  <bin>/dmspr.bin           DMSPR: the creatures' sprites, made from DM's own pictures (dmimg.py),
                            kept in far memory
  <bin>/dmtxt.bin           DMTXT: the dungeon's texts, object names and our own strings, kept in
                            far memory
Nothing from the game is kept in the source tree: it all comes from your copy of the disk.

A level image, all words little-endian:
  header   size, nfirsts, cap_firsts, then counts of doors, teleporters, texts, sensors,
           creatures and objects, then the byte offsets of squares, colidx, firsts, those six
           tables and the seen map
  squares  one byte each, column by column
  colidx   width + 1 words: where each column's object lists start in firsts
  firsts   the first thing of each square that has a list, with room for more
  tables   doors (2 words), teleporters (3), texts (2), sensors (4), creatures (8) and objects
           (2: next and one word of attributes; for a scroll, its text's offset; for a chest,
           its contents), each record starting with the next thing in its list
  seen     128 bytes, a bit for each square the party has seen (for the map), all clear here:
           square (x, y) is bit y & 7 of byte x * 4 + y / 8
Things in a level's lists are numbered within that level. Spare objects have 0xFFFF as next."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import art      # noqa: E402
import dmdat    # noqa: E402
import dmgfx    # noqa: E402
import dmimg    # noqa: E402
import text     # noqa: E402

SPARE_FIRSTS = 48           # room for objects dropped on squares that had none
SPARE_OBJECTS = 48          # room for objects dropped on the level
POOL_BASE = 960             # object numbers from here up are the party's (dungeon.c)
HEADER_WORDS = 19
SEEN_BYTES = 128            # a bit for each square of a 32x32 level: has the party seen it?
REC_WORDS = {0: 2, 1: 3, 2: 2, 3: 4, 4: 8}      # doors .. creatures; objects are 2
ITEM_TYPES = range(5, 11)

# Where things go in the video chip's bank 3 (see hw.c): the character set at $D000 (DMHI puts
# the glyphs after the C64's own characters), sprites from $D400, all in the RAM under the I/O
# area; the bitmap view's colour data at $D800 and bitmap at $E000.
HI_GLYPHS = 0xD000 + art.FIRST_GLYPH * 8
HI_SPRITES = 0xD400         # creature sprites, copied in from far memory
HI_POINTER = 0xD6C0         # the mouse pointer's sprite

# Food value of each junk type (by junk subtype); anything not listed isn't food.
FOOD = {29: 30, 30: 30, 31: 40, 32: 45, 33: 50, 34: 50, 35: 60, 36: 110}


def num(v):
    """A C constant: cc65's int is 16 bits, so larger values need a u."""
    return '%du' % v if v > 32767 else str(v)


def carray(name, ctype, values, per_line=16):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append('    ' + ', '.join(num(v) for v in values[i:i + per_line]) + ',')
    return 'const %s %s[%d] = {\n%s\n};\n' % (ctype, name, len(values), '\n'.join(lines))


def level_image(D, m):
    """One level as an image (see the top of this file), and how many objects it holds."""
    local = {}                      # (type, global index) -> local index
    order = {t: [] for t in range(11)}

    def table(t):
        return 5 if t in ITEM_TYPES else t

    def visit(thing):
        """Numbers a list of things, and the lists inside creatures and chests."""
        while thing not in (0xFFFE, 0xFFFF):
            t, i = (thing >> 10) & 15, thing & 1023
            if (t, i) in local:
                break
            local[(t, i)] = len(order[table(t)])
            order[table(t)].append((t, i))
            rec = D.things[t][i]
            if t == 4 or t == 9:
                visit(rec[1])
            thing = rec[0]

    for x in range(m.w):
        for y in range(m.h):
            if (x, y) in m.lists:
                visit(D.first[m.firstk[(x, y)]])

    def remap(thing):
        if thing in (0xFFFE, 0xFFFF):
            return thing
        t, i = (thing >> 10) & 15, thing & 1023
        return (thing & 0xFC00) | local[(t, i)]

    words = []
    squares = bytearray()
    colidx, firsts = [], []
    for x in range(m.w):
        colidx.append(len(firsts))
        for y in range(m.h):
            squares.append(m.sq[y][x])
            if (x, y) in m.lists:
                firsts.append(remap(D.first[m.firstk[(x, y)]]))
    colidx.append(len(firsts))
    if len(squares) & 1:
        squares.append(0)
    nfirsts = len(firsts)
    firsts += [0] * SPARE_FIRSTS

    tables = []
    for tab in range(6):
        recs = []
        for t, i in order[tab]:
            r = list(D.things[t][i])
            r[0] = remap(r[0])
            if t == 4:                              # creature: its possessions
                r[1] = remap(r[1])
            elif t == 9:                            # chest: its contents
                r = [r[0], remap(r[1])]
            elif t == 7:                            # scroll: straight to its text
                r = [r[0], D.things[2][r[1] & 0x3FF][1] >> 3]
            elif t in ITEM_TYPES:
                r = r[:2]
            recs.append(r)
        if tab == 5:
            recs += [[0xFFFF, 0]] * SPARE_OBJECTS
        tables.append(recs)
    assert len(tables[5]) < POOL_BASE

    # Lay it out: header, squares, colidx, firsts, then the tables.
    offs = []
    pos = HEADER_WORDS * 2
    for part in (len(squares), len(colidx) * 2, len(firsts) * 2):
        offs.append(pos)
        pos += part
    for tab in range(6):
        offs.append(pos)
        pos += len(tables[tab]) * REC_WORDS.get(tab, 2) * 2
    offs.append(pos)                                # the seen map
    pos += SEEN_BYTES
    header = [pos, nfirsts, len(firsts)] + [len(tables[t]) for t in range(6)] + offs
    assert len(header) == HEADER_WORDS
    img = struct.pack('<%dH' % HEADER_WORDS, *header) + bytes(squares)
    img += struct.pack('<%dH' % len(colidx), *colidx) + struct.pack('<%dH' % len(firsts), *firsts)
    for tab in range(6):
        for r in tables[tab]:
            assert len(r) == REC_WORDS.get(tab, 2)
            img += struct.pack('<%dH' % len(r), *r)
    img += bytes(SEEN_BYTES)
    assert len(img) == pos
    return img


def main():
    stx, gendir, bindir = sys.argv[1:4]
    D, files = dmdat.load(stx)
    G = dmgfx.Graphics(files['GRAPHICS.DAT'])
    names = G.names()
    T = G.tables()
    os.makedirs(gendir, exist_ok=True)
    os.makedirs(bindir, exist_ok=True)

    # Where each square's first thing is in DUNGEON.DAT's list of firsts.
    for m in D.maps:
        m.firstk = {}
        for x in range(m.w):
            k = D.colidx[m.col0 + x]
            for y in range(m.h):
                if m.sq[y][x] & 0x10:
                    m.firstk[(x, y)] = k
                    k += 1

    raw = D.raw
    mapsize = struct.unpack_from('>H', raw, 2)[0]
    mstart = len(raw) - mapsize
    images, wallorn = [], []
    for m in D.maps:
        images.append(level_image(D, m))
        nwall, ncr = m.orn & 15, (m.misc >> 4) & 15
        p = mstart + m.offset + m.w * m.h
        wallorn.append((list(raw[p + ncr:p + ncr + nwall]) + [0] * 16)[:16])
    level_max = max(len(i) for i in images)
    sprites, sprite_colours = dmimg.creature_sprites(G)

    h, c = [], []
    h.append('/* Generated by tools/dmconv.py from the Dungeon Master disk image: do not edit. */')
    h.append('#ifndef DMDATA_H\n#define DMDATA_H\n')
    h.append('#define NLEVELS %d' % len(D.maps))
    h.append('#define LEVEL_MAX %d        /* the largest level image */' % level_max)
    h.append('#define LEVELS_TOTAL %dUL' % sum(len(i) for i in images))
    h.append('#define POOL_BASE %d' % POOL_BASE)
    h.append('#define START_X %d\n#define START_Y %d\n#define START_DIR %d' % D.start)
    h.append('#define NNAMES %d' % len(names))
    h.append('#define HI_SPRITES 0x%04X      /* where creature sprites are copied to */' % HI_SPRITES)
    h.append('#define SPRITE_SET 320          /* bytes of sprites per creature in DMSPR */')
    h.append('#define SPRITES_TOTAL %dUL' % len(sprites))
    h.append('#define HI_POINTER 0x%04X' % HI_POINTER)
    h.append('#define NHELP %d' % len(text.HELP))
    h.append('''
typedef struct {
    unsigned char w, h;         /* size */
    unsigned char ox, oy;       /* position in the whole dungeon, to line up stairs and pits */
    unsigned char depth;        /* the level's depth (0 = the Hall of Champions) */
    unsigned char difficulty;
} level_t;

extern const level_t levels[NLEVELS];
extern const unsigned char wall_ornaments[NLEVELS][16];
extern const unsigned int level_size[NLEVELS];

extern const unsigned int name_offset[NNAMES];
extern const unsigned char obj_icon[180];       /* object info index -> icon (and name) */
extern const unsigned char obj_flags[180];      /* OBJ_ bits below */
extern const unsigned char weapon_info[46][3];  /* weight, damage, kinetic energy */
extern const unsigned char armour_info[58][2];  /* weight, defence */
extern const unsigned char food_value[64];      /* by junk type */
typedef struct {
    unsigned char move_ticks, attack_ticks, defense, attack, poison, dex, colour, flags;
} creature_info_t;
extern const creature_info_t creature_info[27];
extern const unsigned int creature_name_at[27];    /* in the RAM under the KERNAL: hi_str */
extern const unsigned int help_line_at[NHELP];
#define CR_NON_MATERIAL 0x40    /* only magic hurts it */

/* Spells: the runes after the power rune (row * 6 + column, 255 for none), the skill level and
** skill they need (2 or 12-15 priest, 3 or 16-19 wizard), what they are (SPELL_ below), which
** potion, projectile or effect, and how strong. */
typedef struct {
    unsigned char rune[3], level, skill, kind, type, strength;
} spell_t;
#define NSPELLS 25
#define SPELL_POTION     1
#define SPELL_PROJECTILE 2
#define SPELL_OTHER      3
extern const spell_t spells[NSPELLS];
extern const unsigned char mana_cost[4][6];     /* each rune's base cost, by row */
extern const unsigned char mana_multiplier[6];  /* in eighths, by power */

#define OBJ_MOUTH  0x01
#define OBJ_HEAD   0x02
#define OBJ_NECK   0x04
#define OBJ_TORSO  0x08
#define OBJ_LEGS   0x10
#define OBJ_FEET   0x20
#define OBJ_QUIVER 0x40
''')
    h.append('#endif')

    c.append('/* Generated by tools/dmconv.py from the Dungeon Master disk image: do not edit. */')
    c.append('#include "dmdata.h"\n')
    c.append('const level_t levels[NLEVELS] = {')
    for m in D.maps:
        c.append('    { %d, %d, %d, %d, %d, %d },' % (m.w, m.h, m.ox, m.oy, m.level,
                                                     (m.misc >> 12) & 15))
    c.append('};\n')
    c.append('const unsigned char wall_ornaments[NLEVELS][16] = {')
    for wl in wallorn:
        c.append('    { %s },' % ', '.join(map(str, wl)))
    c.append('};\n')
    c.append(carray('level_size', 'unsigned int', [len(i) for i in images]))

    # DMTXT, kept in far memory: the dungeon's texts, then object names, then our own strings.
    textdata = b"".join(struct.pack("<H", w) for w in D.text)
    blob, offs = bytearray(), []
    for nm in names:
        offs.append(len(textdata) + len(blob))
        b = nm.encode('latin1')
        blob += b[:-1] + bytes([b[-1] | 0x80])
    c.append(carray('name_offset', 'unsigned int', offs, 12))
    oi = T['objinfo']
    c.append(carray('obj_icon', 'unsigned char', [o[0] for o in oi], 20))
    c.append(carray('obj_flags', 'unsigned char', [o[3] & 0x7F for o in oi], 20))
    c.append('const unsigned char weapon_info[46][3] = {\n%s\n};\n' % '\n'.join(
        '    { %d, %d, %d },' % (w[0], w[2], w[3]) for w in T['weapons']))
    c.append('const unsigned char armour_info[58][2] = {\n%s\n};\n' % '\n'.join(
        '    { %d, %d },' % a for a in T['armour']))
    c.append(carray('food_value', 'unsigned char', [FOOD.get(i, 0) for i in range(64)]))
    c.append('const creature_info_t creature_info[27] = {')
    for i, cr in enumerate(T['creatures']):
        c.append('    { %d, %d, %d, %d, %d, %d, %d, %d },  /* %s */' % (
            cr['move'], cr['attack_ticks'], cr['defense'], cr['attack'], cr['poison'], cr['dex'],
            sprite_colours[i], cr['attributes'] & 0x40, art.CREATURE_NAMES[i]))
    c.append('};\n')
    c.append('const spell_t spells[NSPELLS] = {')
    for sp in T['spells']:
        c.append('    { { %d, %d, %d }, %d, %d, %d, %d, %d },' % tuple(
            sp['runes'] + [sp['level'], sp['skill'], sp['kind'], sp['type'], sp['strength']]))
    c.append('};\n')
    c.append('const unsigned char mana_cost[4][6] = {\n%s\n};\n' % '\n'.join(
        '    { %s },' % ', '.join(map(str, row)) for row in T['mana_cost']))
    c.append(carray('mana_multiplier', 'unsigned char', T['mana_multiplier']))

    # Words of our own: zero-terminated, at the offsets listed.
    strings, at = bytearray(), []
    for s in art.CREATURE_NAMES + text.HELP:
        at.append(len(textdata) + len(blob) + len(strings))
        strings += s.encode('ascii') + b'\0'
    txt = textdata + bytes(blob) + bytes(strings)
    h.insert(-1, '#define TXT_TOTAL %dUL       /* DMTXT: texts, names, strings */\n' % len(txt))
    c.append(carray('creature_name_at', 'unsigned int', at[:27], 9))
    c.append(carray('help_line_at', 'unsigned int', at[27:], 9))

    with open(os.path.join(gendir, 'dmdata.h'), 'w') as f:
        f.write('\n'.join(h) + '\n')
    with open(os.path.join(gendir, 'dmdata.c'), 'w') as f:
        f.write('\n'.join(c) + '\n')
    with open(os.path.join(gendir, 'glyphs.h'), 'w') as f:
        f.write('/* Generated by tools/dmconv.py from tools/art.py: do not edit. */\n')
        f.write('#ifndef GLYPHS_H\n#define GLYPHS_H\n%s\n#endif\n' % '\n'.join(art.glyph_defines()))

    # DMSPR: each creature's sprites (dmimg.py), kept in far memory.
    with open(os.path.join(bindir, 'dmspr.bin'), 'wb') as f:
        f.write(struct.pack('<H', 0) + sprites)

    # DMLEV: the level images one after another (each starts with its size).
    with open(os.path.join(bindir, 'dmlev.bin'), 'wb') as f:
        f.write(struct.pack('<H', 0) + b''.join(images))

    with open(os.path.join(bindir, 'dmtxt.bin'), 'wb') as f:
        f.write(struct.pack('<H', 0) + txt)

    # DMHI: chunks of (address, length, bytes), ended by address 0.
    assert HI_GLYPHS + len(art.glyph_bytes()) <= 0xD400, 'too many glyphs for 128 characters'
    hi = bytearray()
    for addr, data in ((HI_GLYPHS, art.glyph_bytes()), (HI_POINTER, art.pointer_bytes())):
        hi += struct.pack('<HH', addr, len(data)) + data
    hi += b'\0\0'
    with open(os.path.join(bindir, 'dmhi.bin'), 'wb') as f:
        f.write(struct.pack('<H', 0) + hi)          # a dummy load address
    print('dmconv: %d levels, %d bytes in all (largest %d), %d bytes of text, DMHI %d bytes' % (
        len(D.maps), sum(len(i) for i in images), level_max, len(textdata), len(hi)))


if __name__ == '__main__':
    main()
