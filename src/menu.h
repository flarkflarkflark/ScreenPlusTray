/* Menu tree layout and command dispatch from SpState. */
#pragma once
#include "client.h"

typedef enum SpMenuItemType {
    SP_ITEM_NORMAL = 0,
    SP_ITEM_SEPARATOR,
    SP_ITEM_CHECKBOX,
    SP_ITEM_RADIO,
    SP_ITEM_SUBMENU
} SpMenuItemType;

#define SP_MENU_MAX_CHILDREN 80
#define SP_MENU_MAX_ITEMS    160

typedef struct SpMenuItem {
    int            id;
    SpMenuItemType type;
    char           label[192];
    int            enabled;
    int            checked;
    int            child_count;
    int            children[SP_MENU_MAX_CHILDREN]; /* indices into SpMenuLayout.items */
} SpMenuItem;

typedef struct SpMenuLayout {
    int        count;
    SpMenuItem items[SP_MENU_MAX_ITEMS]; /* items[0] is always the root (id 0) */
} SpMenuLayout;

/* Fixed item IDs so D-Bus menu IDs remain stable across refreshes. */
enum {
    ID_ROOT             = 0,
    ID_SHOW             = 1,
    ID_SEP_1            = 2,
    ID_PAUSE            = 3,
    ID_NEXT             = 4,
    ID_SWITCH_SUB       = 5,
    ID_FAVOURITE        = 6,
    ID_SEP_2            = 7,
    ID_AUDIO            = 8,
    ID_VOLUME_SUB       = 9,
    ID_SEP_3            = 10,
    ID_SLIDESHOW_TOGGLE = 11,
    ID_SLIDESHOW_SUB    = 12,
    ID_SCHEDULE         = 13,
    ID_MODE_SUB         = 14,
    ID_SEP_4            = 15,
    ID_STARTUP          = 16,
    ID_RESTART          = 17,
    ID_STOP             = 18,

    /* When Screen+ is not running */
    ID_START_SERVICE    = 20,
    ID_SEP_STOPPED      = 21,
    ID_QUIT_TRAY        = 22,

    /* Switch wallpaper submenu (100..125) */
    ID_WP_EMPTY         = 99,
    ID_WP_BASE          = 100,

    /* Volume submenu (200..204) */
    ID_VOL_0            = 200,
    ID_VOL_25           = 201,
    ID_VOL_50           = 202,
    ID_VOL_75           = 203,
    ID_VOL_100          = 204,

    /* Slideshow submenu */
    ID_SS_EVERY_5       = 300,
    ID_SS_EVERY_15      = 301,
    ID_SS_EVERY_30      = 302,
    ID_SS_EVERY_60      = 303,
    ID_SS_SEP_1         = 304,
    ID_SS_SHUFFLE       = 305,
    ID_SS_SEP_2         = 306,
    ID_SS_SRC_ALL       = 307,
    ID_SS_SRC_FAV       = 308,
    ID_SS_SRC_COL_BASE  = 310, /* 310 .. 310 + SP_MAX_COLLECTIONS - 1 */

    /* Display mode submenu */
    ID_MODE_INDEPENDENT = 400,
    ID_MODE_DUPLICATE   = 401,
    ID_MODE_SPAN        = 402
};

/* Build a complete menu layout from the given state. */
void sp_menu_build(SpMenuLayout *layout, const SpState *st);

/* Compare two layouts; returns 1 if identical, 0 if any item/property differs. */
int  sp_menu_equal(const SpMenuLayout *a, const SpMenuLayout *b);

/* Find an item by its D-Bus menu ID; returns NULL if not found. */
const SpMenuItem *sp_menu_find(const SpMenuLayout *layout, int id);

/* Toggle window (show / hide) using st->window_open tracking. */
void sp_menu_toggle_window(SpState *st);

/* Execute the action for a clicked menu item ID.
 * Sets *quit_out = 1 if "Quit tray" was clicked.
 * Returns 1 if an action was performed and state may have changed. */
int  sp_menu_activate(int id, SpState *st, int *quit_out);
