/*
** party.c - the champions: their health, their possessions, fighting, eating and drinking.
** Recruiting them is in recruit.c.
*/

#include <stdlib.h>
#include <string.h>
#include <c64.h>
#include "dm.h"

champion_t champ[4];
unsigned char nchamps;
unsigned int held = NONE;

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
