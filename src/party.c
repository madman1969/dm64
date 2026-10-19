/*
** party.c - the champions: recruiting them from the Hall of Champions' mirrors, their health,
** their possessions, fighting, eating and drinking.
**
** Each mirror is a sensor on a wall; the champion's details are a text on the floor in front of
** it, and their possessions lie on the mirror's side of the wall. The text is the champion's
** name, title, a blank line, gender, then health, stamina (in tenths) and mana as three 4-digit
** numbers, the seven statistics as 2-digit numbers, and sixteen skill levels as digits - all in
** hexadecimal, written with A for 0 to P for 15.
*/

#include <stdlib.h>
#include <string.h>
#include <c64.h>
#include "dm.h"

champion_t champ[4];
unsigned char nchamps;
unsigned int held = NONE;

static const char* line_ptr[8];

static unsigned int hex (const char* s, unsigned char n)
{
    unsigned int v = 0;

    while (n--) {
        v = (v << 4) | ((*s++ - 'A') & 15);
    }
    return v;
}

static unsigned char split (const char* s)
{
    unsigned char n = 0;

    line_ptr[n++] = s;
    while (*s && n < 8) {
        if (*s++ == '\n') {
            line_ptr[n++] = s;
        }
    }
    return n;
}

static void copy_line (char* dst, const char* src, unsigned char max)
{
    while (*src && *src != '\n' && --max) {
        *dst++ = *src++;
    }
    *dst = 0;
}

unsigned char party_alive (void)
{
    unsigned char i;

    for (i = 0; i < nchamps; ++i) {
        if (champ[i].hp > 0) {
            return 1;
        }
    }
    return 0;
}

static unsigned char place (champion_t* c, unsigned int t)
{
    unsigned char oi = obj_info (t), f = obj_flags[oi], s;

    if (TH_TYPE (t) == T_WEAPON && c->slot[SL_ACTION] == NONE) {
        s = SL_ACTION;
    } else if ((f & OBJ_HEAD) && c->slot[SL_HEAD] == NONE) {
        s = SL_HEAD;
    } else if ((f & OBJ_NECK) && c->slot[SL_NECK] == NONE) {
        s = SL_NECK;
    } else if ((f & OBJ_TORSO) && c->slot[SL_TORSO] == NONE) {
        s = SL_TORSO;
    } else if ((f & OBJ_LEGS) && c->slot[SL_LEGS] == NONE) {
        s = SL_LEGS;
    } else if ((f & OBJ_FEET) && c->slot[SL_FEET] == NONE) {
        s = SL_FEET;
    } else {
        for (s = SL_READY; s < NSLOTS && c->slot[s] != NONE; ++s) {
            if (s == SL_READY) {
                s = SL_PACK - 1;
            }
        }
        if (s == NSLOTS) {
            return 0;
        }
    }
    c->slot[s] = t;
    return 1;
}

/* Called with the party facing a mirror: shows the champion, and takes them on if asked. */
void recruit (void)
{
    unsigned int t, *sensor = 0;
    unsigned char ax = px + dir_dx[pdir], ay = py + dir_dy[pdir], cell = face_cell ();
    unsigned char i, k, n;
    const char* txt = 0;
    champion_t* c;

    for (t = first_thing (ax, ay); t != END; t = NEXT (t)) {
        if (TH_CELL (t) == cell && TH_TYPE (t) == T_SENSOR && SENS_TYPE (thing_ptr (t)) == 127) {
            sensor = thing_ptr (t);
        }
    }
    for (t = first_thing (px, py); t != END; t = NEXT (t)) {
        if (TH_TYPE (t) == T_TEXT) {
            txt = text_decode (thing_ptr (t)[1] >> 3);
        }
    }
    if (!sensor || !txt || split (txt) < 7) {
        return;
    }
    sprites_off ();
    ui_clear (0, 0, VIEW_W, VIEW_H);
    for (i = 0; i < 2; ++i) {
        static char buf[24];
        copy_line (buf, line_ptr[i], sizeof buf);
        ui_print (2, 3 + i * 2, i ? 7 : 1, buf);
    }
    ui_print (2, 8, 15, "HEALTH");
    ui_print (2, 9, 15, "STAMINA");
    ui_print (2, 10, 15, "MANA");
    {
        static char nb[6];
        unsigned int v[3];
        v[0] = hex (line_ptr[4], 4);
        v[1] = hex (line_ptr[4] + 4, 4) / 10;
        v[2] = hex (line_ptr[4] + 8, 4);
        for (i = 0; i < 3; ++i) {
            unsigned int x = v[i];
            nb[3] = 0;
            nb[2] = '0' + x % 10; x /= 10;
            nb[1] = x ? '0' + x % 10 : ' '; x /= 10;
            nb[0] = x ? '0' + x % 10 : ' ';
            ui_print (11, 8 + i, 1, nb);
        }
    }
    if (nchamps == 4) {
        ui_print (2, 14, 10, "THE PARTY IS FULL");
        wait_key ();
        redraw = REDRAW_VIEW | REDRAW_PANEL;
        return;
    }
    ui_print (2, 14, 13, "RECRUIT? (Y/N)");
    k = wait_key ();
    redraw = REDRAW_VIEW | REDRAW_PANEL;
    if (k != 'y') {
        return;
    }

    /* text_decode's buffer has been reused by now; decode the text again. */
    for (t = first_thing (px, py); t != END; t = NEXT (t)) {
        if (TH_TYPE (t) == T_TEXT) {
            txt = text_decode (thing_ptr (t)[1] >> 3);
        }
    }
    split (txt);
    c = &champ[nchamps++];
    memset (c, 0, sizeof *c);
    copy_line (c->name, line_ptr[0], sizeof c->name);
    copy_line (c->title, line_ptr[1], sizeof c->title);
    c->hp = c->maxhp = hex (line_ptr[4], 4);
    c->sta = c->maxsta = hex (line_ptr[4] + 4, 4) / 10;
    c->mana = c->maxmana = hex (line_ptr[4] + 8, 4);
    for (i = 0; i < 7; ++i) {
        c->stat[i] = (unsigned char) hex (line_ptr[5] + i * 2, 2);
    }
    for (i = 0; i < 16; ++i) {
        n = (unsigned char) hex (line_ptr[6] + i, 1);
        if (n > c->skill[i >> 2]) {
            c->skill[i >> 2] = n;
        }
    }
    c->food = 200;
    for (i = 0; i < NSLOTS; ++i) {
        c->slot[i] = NONE;
    }

    /* Their possessions, from the mirror's side of the wall. */
    t = first_thing (ax, ay);
    while (t != END) {
        unsigned int next = NEXT (t);
        if (TH_CELL (t) == cell && TH_TYPE (t) >= T_WEAPON) {
            unsigned int mine;
            list_remove (ax, ay, t);
            mine = to_party (t);
            if (mine == NONE || !place (c, mine)) {
                if (mine != NONE) {
                    t = to_level (mine);
                }
                list_add (px, py, t);
            }
        }
        t = next;
    }
    sensor[1] &= 0xFF80;            /* an empty mirror now */
    msg (c->name);
    msg ("JOINS THE PARTY");
}

unsigned char party_has_icon (unsigned char icon)
{
    unsigned char i, s;

    if (obj_icon_of (held) == icon) {
        return 1;
    }
    for (i = 0; i < nchamps; ++i) {
        for (s = 0; s < NSLOTS; ++s) {
            if (champ[i].slot[s] != NONE && obj_icon_of (champ[i].slot[s]) == icon) {
                return 1;
            }
        }
    }
    return 0;
}

unsigned char armour_total (unsigned char who)
{
    unsigned char s, total = 0;
    unsigned int t;

    for (s = SL_READY; s <= SL_FEET; ++s) {
        t = champ[who].slot[s];
        if (t != NONE && TH_TYPE (t) == T_ARMOUR) {
            total += armour_info[thing_ptr (t)[1] & 0x7F][1];
        }
    }
    return total;
}

void damage_champion (unsigned char who, int amount)
{
    champion_t* c = &champ[who];

    if (c->hp <= 0) {
        return;
    }
    amount -= (armour_total (who) >> 4) + shield;
    if (amount < 1) {
        amount = 1;
    }
    c->hp -= amount;
    SND_HURT ();
    VIC.bordercolor = COLOR_RED;
    if (c->hp <= 0) {
        c->hp = 0;
        msg (c->name);
        msg ("HAS DIED");
    }
    redraw |= REDRAW_PANEL;
}

void attack (unsigned char who)
{
    champion_t* c = &champ[who];
    unsigned int w = c->slot[SL_ACTION];
    unsigned char dmg = 2, weight = 2, wi, chance;
    signed char a;
    int hit;
    const creature_info_t* ci;

    if (who >= nchamps || c->hp <= 0 || c->cooldown) {
        return;
    }
    if (w != NONE && TH_TYPE (w) == T_WEAPON) {
        wi = thing_ptr (w)[1] & 0x7F;
        weight = weapon_info[wi][0];
        dmg = weapon_info[wi][1];
    }
    c->cooldown = 4 + (weight >> 3);
    if (c->sta > 2) {
        c->sta -= 1 + (weight >> 4);
    }
    redraw |= REDRAW_PANEL;
    a = creature_at (px + dir_dx[pdir], py + dir_dy[pdir]);
    if (a < 0) {
        SND_STEP ();
        return;
    }
    ci = &creature_info[th_creature[TH_INDEX (active[a].thing)][2] >> 8];
    if ((ci->flags & CR_NON_MATERIAL) && !(w != NONE && TH_TYPE (w) == T_WEAPON && wi == 16)) {
        msg ("THE BLOW PASSES RIGHT THROUGH IT!");      /* only magic, or a Vorpal Blade */
        return;
    }
    chance = 50 + (c->stat[2] >> 1) + c->skill[0] * 4;
    chance = chance > (ci->dex >> 1) + 5 ? chance - (ci->dex >> 1) : 5;
    if ((rand () % 100) >= chance) {
        msg ("MISSED!");
        return;
    }
    hit = dmg + (c->stat[1] >> 2) + rand () % (dmg / 2 + 1) - (ci->defense >> 3);
    if (hit < 1) {
        hit = 1;
    }
    SND_HIT ();
    {
        static char line[24];
        unsigned char n = 0, d = (unsigned char) (hit > 99 ? 99 : hit);
        const char* s = c->name;
        while (*s) line[n++] = *s++;
        s = " HITS FOR ";
        while (*s) line[n++] = *s++;
        if (d > 9) line[n++] = '0' + d / 10;
        line[n++] = '0' + d % 10;
        line[n] = 0;
        msg (line);
    }
    hit_creature (a, hit);
}

/* Eat, drink or read what's in the hand. */
void use_held (unsigned char who)
{
    champion_t* c = &champ[who];
    unsigned int* r;
    unsigned char oi, type, power, v;
    unsigned int t;

    if (held == NONE || who >= nchamps || c->hp <= 0) {
        return;
    }
    r = thing_ptr (held);
    oi = obj_info (held);
    switch (TH_TYPE (held)) {
    case T_JUNK:
        v = food_value[r[1] & 0x3F];
        if (!v) {
            break;
        }
        c->food = c->food + v > 255 ? 255 : c->food + v;
        pool_free (held);
        held = NONE;
        msg ("YUM");
        redraw |= REDRAW_PANEL;
        return;
    case T_POTION:
        type = (r[1] >> 8) & 0x7F;
        power = (unsigned char) r[1];
        v = 10 + (power >> 3);
        switch (type) {
        case 0: case 11: c->sta += v; break;            /* MON */
        case 13: c->mana += v; break;                   /* EE */
        case 14: c->hp += v; break;                     /* VI */
        case 6: ++c->stat[2]; break;                    /* ROS */
        case 7: ++c->stat[1]; break;                    /* KU */
        case 8: ++c->stat[3]; break;                    /* DANE */
        case 9: ++c->stat[4]; break;                    /* NETA */
        case 15: c->food += c->food < 235 ? 20 : 0; break;  /* water */
        case 20: msg ("IT'S EMPTY"); return;
        default: msg ("NOTHING HAPPENS"); break;
        }
        if (c->hp > c->maxhp) c->hp = c->maxhp;
        if (c->sta > c->maxsta) c->sta = c->maxsta;
        if (c->mana > c->maxmana) c->mana = c->maxmana;
        r[1] = 20 << 8;                                 /* an empty flask now */
        msg ("GULP");
        redraw |= REDRAW_PANEL;
        return;
    case T_SCROLL:
        msg_text (text_decode (r[1]));
        return;
    case T_CONTAINER:
        t = r[1];
        while (t != END && t != NONE) {
            unsigned int next = NEXT (t), out = to_level (t);
            if (out != NONE) {
                list_add (px, py, out | ((unsigned int) pdir << 14));
            }
            t = next;
        }
        r[1] = END;
        msg ("YOU EMPTY IT ONTO THE FLOOR");
        redraw = REDRAW_VIEW | REDRAW_PANEL;
        return;
    }
    (void) oi;
    msg ("NOTHING HAPPENS");
}

/* Ten times a second: recovery, hunger and the time between blows. */
void party_tick (void)
{
    static unsigned char slow;
    unsigned char i;
    champion_t* c;

    ++slow;
    VIC.bordercolor = COLOR_BLACK;
    for (i = 0; i < nchamps; ++i) {
        c = &champ[i];
        if (c->hp <= 0) {
            continue;
        }
        if (c->cooldown && !--c->cooldown) {
            redraw |= REDRAW_PANEL;
        }
        if ((slow & 31) == 0) {
            if (c->food) {
                if (c->hp < c->maxhp) ++c->hp;
                if (c->sta < c->maxsta) ++c->sta;
                if (c->mana < c->maxmana) ++c->mana;
            } else {
                damage_champion (i, 1);
            }
            redraw |= REDRAW_PANEL;
        }
        if (slow == 0 && c->food) {
            --c->food;
        }
    }
}
