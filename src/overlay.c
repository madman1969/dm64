/*
** overlay.c - the way into the overlays, which stay in ordinary memory: each copies its overlay
** in (dungeon.c: overlay), calls it, and puts the dungeon's mouse zones back afterwards.
**
**   overlay 0  recruit.c   champions from the mirrors, and help
**   overlay 1  screens.c   the map; the mouse
**   overlay 2  cast.c      casting spells
**   overlay 3  save.c      saving and loading
**   overlay 4  unpack.c    unpacking DMWAL as the game starts
**   overlay 5  inventory.c a champion's inventory
**
** An overlay may call anything here in ordinary memory, but nothing in another overlay. The
** screens that cover the view switch the whole screen to text while they're up.
*/

#include "dm.h"

void recruit (void)
{
    overlay (0);
    screen_text ();
    ovl_recruit ();
    screen_view ();
    main_zones ();
}

void inventory (unsigned char who)
{
    overlay (5);
    ovl_inventory (who);
    screen_view ();
    main_zones ();
}

void help (void)
{
    overlay (0);
    screen_text ();
    ovl_help ();
    screen_view ();
    main_zones ();
}

void mouse_cycle (void)
{
    overlay (1);
    ovl_mouse ();
}

void map (void)
{
    overlay (1);
    screen_text ();
    ovl_map ();
    screen_view ();
    main_zones ();
}

void cast_spell (void)
{
    overlay (2);
    ovl_cast ();
    main_zones ();
}

void save_game (void)
{
    overlay (3);
    ovl_save ();
    main_zones ();
}

unsigned char load_game (void)
{
    unsigned char loaded;

    overlay (3);
    loaded = ovl_load ();
    main_zones ();
    return loaded;
}
