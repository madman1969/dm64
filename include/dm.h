/*
** dm.h - Dungeon Master for the Commodore 64: everything the modules share.
**
** The dungeon itself (dmdata.h) is Dungeon Master's own DUNGEON.DAT, converted by
** tools/dmconv.py: levels of squares stored column by column, each square's objects in a linked
** list of "things", and things kept in one table per type. A thing is a 16-bit word:
**   bits 15-14 the cell (corner of the square, or side of a wall) it's in
**   bits 13-10 its type (T_ below)
**   bits  9-0  its index in that type's table
** and each thing's record starts with the word of the next thing in its list (END at the end).
*/

#ifndef DM_H
#define DM_H

#include "dmdata.h"
#include "glyphs.h"

#define END   0xFFFE    /* end of a thing list */
#define NONE  0xFFFF    /* no thing (an empty hand, an empty slot) */

#define TH_TYPE(t)   ((unsigned char) ((t) >> 10) & 15)
#define TH_INDEX(t)  ((t) & 1023)
#define TH_CELL(t)   ((unsigned char) ((t) >> 14))
#define TH_MAKE(type, index, cell) (((unsigned int) (cell) << 14) | ((unsigned int) (type) << 10) | (index))

enum { T_DOOR, T_TELE, T_TEXT, T_SENSOR, T_CREATURE, T_WEAPON, T_ARMOUR, T_SCROLL,
       T_POTION, T_CONTAINER, T_JUNK };

/* A square byte: its element in bits 7-5, whether it has things in bit 4, and element flags. */
enum { E_WALL, E_FLOOR, E_PIT, E_STAIRS, E_DOOR, E_TELE, E_FAKE };
#define SQ_TYPE(s)    ((unsigned char) (s) >> 5)
#define SQ_THINGS     0x10
#define PIT_OPEN      0x08
#define PIT_INVISIBLE 0x04
#define PIT_IMAGINARY 0x01
#define STAIRS_NS     0x08      /* runs north-south (else west-east) */
#define STAIRS_UP     0x04
#define DOOR_NS       0x08      /* passable north-south */
#define DOOR_STATE    0x07      /* 0 open .. 4 closed, 5 destroyed */
#define TELE_OPEN     0x08
#define TELE_VISIBLE  0x04
#define FAKE_OPEN     0x04
#define FAKE_IMAGINARY 0x01

/* Sensor fields (a sensor's words: next, type+data, attributes, action). */
#define SENS_TYPE(s)     ((unsigned char) (s)[1] & 0x7F)
#define SENS_DATA(s)     ((s)[1] >> 7)
#define SENS_ONCE        0x0004
#define SENS_EFFECT(s)   ((unsigned char) ((s)[2] >> 3) & 3)
#define SENS_REVERT      0x0020
#define SENS_AUDIBLE     0x0040
#define SENS_DELAY(s)    ((unsigned char) ((s)[2] >> 7) & 15)
#define SENS_LOCAL       0x0800
#define SENS_ORNAMENT(s) ((unsigned char) ((s)[2] >> 12))
#define SENS_X(s)        ((unsigned char) ((s)[3] >> 6) & 31)
#define SENS_Y(s)        ((unsigned char) ((s)[3] >> 11))
#define SENS_CELL(s)     ((unsigned char) ((s)[3] >> 4) & 3)
enum { EFF_SET, EFF_CLEAR, EFF_TOGGLE, EFF_HOLD };

/* Screen layout: the 3D view on the left, the party on the right, messages below. */
#define VIEW_W   24
#define VIEW_H   22
#define SCREEN   ((unsigned char*) 0xE000)
#define COLOUR   ((unsigned char*) 0xD800)

/* ---- hw.c ---- */
extern unsigned char view_chars[VIEW_W * VIEW_H];   /* the view is drawn here, then shown */
extern unsigned char view_cols[VIEW_W * VIEW_H];
unsigned char hw_load_data (void);
unsigned char hw_open (const char* name);
unsigned int hw_read (void* buf, unsigned int len);
void hw_close (void);
void hw_init (void);
void hi_read (void* dst, unsigned src, unsigned len);
const char* hi_str (unsigned int addr);
void sound (unsigned char pitch, unsigned char wave, unsigned char len);
void sound_tick (void);
#define SND_CLICK()  sound (40, 0x81, 2)
#define SND_HIT()    sound (12, 0x81, 4)
#define SND_HURT()   sound (6, 0x21, 6)
#define SND_DOOR()   sound (3, 0x41, 8)
#define SND_STEP()   sound (2, 0x81, 1)

/* ---- dungeon.c ---- */
/* The party's level, copied in from far memory: its squares, object lists and things. */
extern unsigned int (*th_door)[2];
extern unsigned int (*th_tele)[3];
extern unsigned int (*th_text)[2];
extern unsigned int (*th_sensor)[4];
extern unsigned int (*th_creature)[8];
extern unsigned int (*th_obj)[2];   /* weapons, armour, scrolls, potions, chests and junk */
/* Objects the party carries live outside the levels, numbered from POOL_BASE. */
#define POOL_SIZE 64
extern unsigned int pool[POOL_SIZE][2];
unsigned char dungeon_init (void);
void overlay (unsigned char n);             /* copy overlay n in from far memory (overlay.c) */
void level_flush (void);                    /* the party's level back to far memory */
void level_reload (unsigned char l);        /* read level l afresh from far memory */
unsigned long level_far_addr (unsigned char l);
unsigned int to_party (unsigned int t);     /* a level's object -> the party's */
unsigned int to_level (unsigned int t);     /* and back, NONE if the level is full */
void pool_free (unsigned int t);
unsigned int pool_new (unsigned char type, unsigned int attributes);   /* NONE if full */
extern unsigned char lev, lw, lh;   /* the party's level and its size */
extern unsigned char px, py, pdir;  /* the party's square and direction (0 N, 1 E, 2 S, 3 W) */
extern const signed char dir_dx[4], dir_dy[4];
void level_enter (unsigned char l);
unsigned char* sq_ptr (unsigned char x, unsigned char y);
unsigned char sq_at (unsigned char x, unsigned char y);        /* walls outside the map */
void mark_seen (unsigned char x, unsigned char y);             /* for the map */
unsigned char was_seen (unsigned char x, unsigned char y);
unsigned int first_thing (unsigned char x, unsigned char y);
unsigned int* thing_ptr (unsigned int t);
#define NEXT(t) (*thing_ptr (t))
void list_add (unsigned char x, unsigned char y, unsigned int t);
void list_remove (unsigned char x, unsigned char y, unsigned int t);
unsigned char obj_info (unsigned int t);    /* object info index, 255 if not an object */
unsigned char obj_icon_of (unsigned int t);
const char* obj_name (unsigned int t);
const char* text_decode (unsigned int offset);  /* a dungeon text, '\n' between lines */
unsigned char passable (unsigned char x, unsigned char y);
unsigned char face_cell (void);             /* the side of the wall ahead that faces us */

/* ---- view.c ---- */
void view_draw (void);
void sprites_off (void);

/* ---- ui.c ---- */
void ui_init (void);
unsigned char scr_code (unsigned char c);
void msg (const char* s);
void msg_text (const char* s);              /* a multi-line dungeon text */
void panel_draw (void);
void ui_print (unsigned char x, unsigned char y, unsigned char col, const char* s);
void ui_clear (unsigned char x, unsigned char y, unsigned char w, unsigned char h);
unsigned char wait_key (void);             /* a key, or a click on a zone */
unsigned char get_input (void);            /* the same, or 0 if there's none */
const char* ui_num (unsigned int n, unsigned char width);
void ui_glyph (unsigned char x, unsigned char y, unsigned char ch, unsigned char col);
void zones_clear (void);
void zone (unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char key);
void zone_champions (unsigned char first);
void main_zones (void);
void mouse_cycle (void);
/* Keys only clicks make. */
#define KEY_RIGHT     3         /* the right mouse button: back out */
#define KEY_INVENTORY 0xF0      /* +n: champion n's inventory */
#define KEY_FLOOR     0xF8      /* the floor: take, or put down */
extern unsigned char redraw;                /* REDRAW_ bits: what needs drawing again */
#define REDRAW_VIEW  1
#define REDRAW_PANEL 2

/* ---- party.c ---- */
#define NSLOTS 15
enum { SL_ACTION, SL_READY, SL_HEAD, SL_NECK, SL_TORSO, SL_LEGS, SL_FEET, SL_PACK };
typedef struct {
    char name[8];
    char title[17];
    int hp, maxhp, sta, maxsta, mana, maxmana;
    unsigned char stat[7];      /* luck, strength, dexterity, wisdom, vitality, anti-magic, anti-fire */
    unsigned char skill[4];     /* fighter, ninja, priest, wizard */
    unsigned char food;
    unsigned char cooldown;
    unsigned int slot[NSLOTS];
} champion_t;
extern champion_t champ[4];
extern unsigned char nchamps;
extern unsigned int held;               /* the object in the party's hand (the mouse pointer in DM) */
unsigned char party_alive (void);
void party_tick (void);
void attack (unsigned char who);
void damage_champion (unsigned char who, int amount);
void use_held (unsigned char who);
unsigned char party_has_icon (unsigned char icon);
unsigned char armour_total (unsigned char who);

/* ---- sensor.c ---- */
/* Things waiting to happen: doors moving, and sensors' delayed effects. */
#define MAXEVENTS 16
typedef struct {
    unsigned int time;
    unsigned char kind, lev, x, y, cell, effect;
} event_t;
extern event_t events[MAXEVENTS];
extern unsigned char nevents;
void sensors_init (void);
void events_tick (void);
void party_entered (void);
void party_left (unsigned char x, unsigned char y);
void floor_object (unsigned char x, unsigned char y, unsigned int t, unsigned char added);
void click_wall (void);
void click_door (void);
void effect_at (unsigned char x, unsigned char y, unsigned char cell, unsigned char effect);
void move_party (signed char fwd, signed char side);
void turn_party (signed char by);
void take_object (void);
void drop_object (void);

/* ---- creature.c ---- */
#define MAXACTIVE 24
typedef struct { unsigned int thing; unsigned char x, y, timer, atk; } active_t;
extern active_t active[MAXACTIVE];
extern unsigned char nactive;
void creatures_scan (void);
void creatures_tick (void);
signed char creature_at (unsigned char x, unsigned char y);   /* active[] index or -1 */
void hit_creature (signed char a, int dmg);

/* ---- magic.c ---- */
#define MAXPROJ 4
typedef struct { unsigned char lev, x, y, dir, type, power, strength, range; } proj_t;
extern proj_t proj[MAXPROJ];
extern unsigned char nproj;
extern unsigned int shield_ticks, invisible_ticks;
extern unsigned char shield;
void magic_tick (void);
void launch (unsigned char type, unsigned char power, unsigned char strength);
signed char projectile_at (unsigned char x, unsigned char y);   /* proj[] index or -1 */
unsigned char projectile_colour (unsigned char p);

/* ---- overlay.c: the overlays, and the way into each ---- */
void recruit (void);
void inventory (unsigned char who);
void help (void);
void map (void);
void cast_spell (void);
void save_game (void);
unsigned char load_game (void);             /* 1 if a game was loaded */
void ovl_recruit (void);
void ovl_inventory (unsigned char who);
void ovl_help (void);
void ovl_map (void);
void ovl_cast (void);
void ovl_save (void);
unsigned char ovl_load (void);

extern unsigned int game_time;

#endif
