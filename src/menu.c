/* Menu tree layout and command dispatch from SpState. */
#include "menu.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int add_item(SpMenuLayout *l, int parent_idx, int id, SpMenuItemType type,
                    const char *label, int enabled, int checked)
{
    if (l->count >= SP_MENU_MAX_ITEMS) return -1;
    int idx = l->count++;
    SpMenuItem *it = &l->items[idx];
    memset(it, 0, sizeof *it);
    it->id = id;
    it->type = type;
    if (label) snprintf(it->label, sizeof it->label, "%s", label);
    it->enabled = enabled;
    it->checked = checked;

    if (parent_idx >= 0 && parent_idx < l->count) {
        SpMenuItem *p = &l->items[parent_idx];
        if (p->child_count < SP_MENU_MAX_CHILDREN) {
            p->children[p->child_count++] = idx;
        }
    }
    return idx;
}

void sp_menu_build(SpMenuLayout *l, const SpState *st)
{
    memset(l, 0, sizeof *l);

    /* Root node is always at index 0 with ID 0 */
    int root = add_item(l, -1, ID_ROOT, SP_ITEM_SUBMENU, "", 1, 0);

    if (!st->running) {
        add_item(l, root, ID_START_SERVICE, SP_ITEM_NORMAL, "Start Screen+", 1, 0);
        add_item(l, root, ID_SEP_STOPPED, SP_ITEM_SEPARATOR, "", 1, 0);
        add_item(l, root, ID_QUIT_TRAY, SP_ITEM_NORMAL, "Quit tray", 1, 0);
        return;
    }

    add_item(l, root, ID_SHOW, SP_ITEM_NORMAL, "Show Screen+", 1, 0);
    add_item(l, root, ID_SEP_1, SP_ITEM_SEPARATOR, "", 1, 0);

    add_item(l, root, ID_PAUSE, SP_ITEM_NORMAL,
             st->paused ? "Resume wallpapers" : "Pause wallpapers", 1, 0);
    add_item(l, root, ID_NEXT, SP_ITEM_NORMAL,
             "Next wallpaper", st->total_wallpapers >= 2, 0);

    /* Switch wallpaper > */
    int sw = add_item(l, root, ID_SWITCH_SUB, SP_ITEM_SUBMENU, "Switch wallpaper", 1, 0);
    if (st->wp_count == 0) {
        add_item(l, sw, ID_WP_EMPTY, SP_ITEM_NORMAL, "Library is empty", 0, 0);
    } else {
        for (int i = 0; i < st->wp_count; i++) {
            const SpWallpaper *w = &st->wallpapers[i];
            int is_cur = (st->has_current && !strcmp(w->id, st->current.id)) ||
                         (!st->has_current && w->on_screen);
            add_item(l, sw, ID_WP_BASE + i, SP_ITEM_RADIO,
                     w->name[0] ? w->name : w->id, !w->missing, is_cur);
        }
    }

    add_item(l, root, ID_FAVOURITE, SP_ITEM_CHECKBOX, "Favourite",
             st->has_current, st->has_current && st->current.favourite);

    add_item(l, root, ID_SEP_2, SP_ITEM_SEPARATOR, "", 1, 0);

    /* Sound & Volume */
    add_item(l, root, ID_AUDIO, SP_ITEM_CHECKBOX, "Wallpaper sound", 1, st->audio);
    int vol_sub = add_item(l, root, ID_VOLUME_SUB, SP_ITEM_SUBMENU, "Volume", 1, 0);
    static const int k_vols[5] = { 0, 25, 50, 75, 100 };
    static const char *const k_vol_labels[5] = { "0%", "25%", "50%", "75%", "100%" };
    for (int i = 0; i < 5; i++) {
        add_item(l, vol_sub, ID_VOL_0 + i, SP_ITEM_RADIO,
                 k_vol_labels[i], 1, st->volume == k_vols[i]);
    }

    add_item(l, root, ID_SEP_3, SP_ITEM_SEPARATOR, "", 1, 0);

    /* Slideshow */
    add_item(l, root, ID_SLIDESHOW_TOGGLE, SP_ITEM_CHECKBOX, "Slideshow", 1, st->slideshow);
    int ss_sub = add_item(l, root, ID_SLIDESHOW_SUB, SP_ITEM_SUBMENU, "Slideshow", 1, 0);
    static const int k_mins[4] = { 5, 15, 30, 60 };
    static const char *const k_min_labels[4] = {
        "Every 5 minutes", "Every 15 minutes", "Every 30 minutes", "Every 60 minutes"
    };
    for (int i = 0; i < 4; i++) {
        add_item(l, ss_sub, ID_SS_EVERY_5 + i, SP_ITEM_RADIO,
                 k_min_labels[i], 1, st->slideshow_minutes == k_mins[i]);
    }
    add_item(l, ss_sub, ID_SS_SEP_1, SP_ITEM_SEPARATOR, "", 1, 0);
    add_item(l, ss_sub, ID_SS_SHUFFLE, SP_ITEM_CHECKBOX, "Shuffle", 1, st->shuffle);
    add_item(l, ss_sub, ID_SS_SEP_2, SP_ITEM_SEPARATOR, "", 1, 0);

    int from_all = (st->slideshow_from[0] == '\0');
    int from_fav = (!strcmp(st->slideshow_from, "*"));
    add_item(l, ss_sub, ID_SS_SRC_ALL, SP_ITEM_RADIO, "Source: All", 1, from_all);
    add_item(l, ss_sub, ID_SS_SRC_FAV, SP_ITEM_RADIO, "Source: Favourites", 1, from_fav);
    for (int i = 0; i < st->collection_count; i++) {
        char lbl[128];
        snprintf(lbl, sizeof lbl, "Source: %s", st->collections[i]);
        int checked = !from_all && !from_fav && !strcasecmp(st->slideshow_from, st->collections[i]);
        add_item(l, ss_sub, ID_SS_SRC_COL_BASE + i, SP_ITEM_RADIO, lbl, 1, checked);
    }

    /* Day/night schedule & Display mode */
    add_item(l, root, ID_SCHEDULE, SP_ITEM_CHECKBOX, "Day/night schedule", 1, st->schedule);
    int mode_sub = add_item(l, root, ID_MODE_SUB, SP_ITEM_SUBMENU, "Display mode", 1, 0);
    add_item(l, mode_sub, ID_MODE_INDEPENDENT, SP_ITEM_RADIO, "Independent",
             1, !strcasecmp(st->display_mode, "independent"));
    add_item(l, mode_sub, ID_MODE_DUPLICATE, SP_ITEM_RADIO, "Duplicate",
             1, !strcasecmp(st->display_mode, "duplicate"));
    add_item(l, mode_sub, ID_MODE_SPAN, SP_ITEM_RADIO, "Span",
             1, !strcasecmp(st->display_mode, "span"));

    add_item(l, root, ID_SEP_4, SP_ITEM_SEPARATOR, "", 1, 0);

    /* Service / Lifecycle */
    add_item(l, root, ID_STARTUP, SP_ITEM_CHECKBOX, "Start at login", 1, st->start_with_linux);
    add_item(l, root, ID_RESTART, SP_ITEM_NORMAL, "Restart wallpapers", 1, 0);
    add_item(l, root, ID_STOP, SP_ITEM_NORMAL, "Stop Screen+", 1, 0);
}

int sp_menu_equal(const SpMenuLayout *a, const SpMenuLayout *b)
{
    if (a->count != b->count) return 0;
    for (int i = 0; i < a->count; i++) {
        const SpMenuItem *x = &a->items[i];
        const SpMenuItem *y = &b->items[i];
        if (x->id != y->id || x->type != y->type ||
            x->enabled != y->enabled || x->checked != y->checked ||
            x->child_count != y->child_count ||
            strcmp(x->label, y->label) != 0) {
            return 0;
        }
        for (int c = 0; c < x->child_count; c++) {
            if (x->children[c] != y->children[c]) return 0;
        }
    }
    return 1;
}

const SpMenuItem *sp_menu_find(const SpMenuLayout *layout, int id)
{
    for (int i = 0; i < layout->count; i++) {
        if (layout->items[i].id == id) return &layout->items[i];
    }
    return NULL;
}

void sp_menu_toggle_window(SpState *st)
{
    if (!st || !st->running) return;
    const char *cmd = st->window_open ? "hide" : "show";
    const char *argv[] = { cmd, NULL };
    if (sp_client_command(argv, NULL, NULL)) {
        st->window_open = !st->window_open;
    }
}

int sp_menu_activate(int id, SpState *st, int *quit_out)
{
    if (quit_out) *quit_out = 0;

    if (id == ID_QUIT_TRAY) {
        if (quit_out) *quit_out = 1;
        return 0;
    }

    if (id == ID_START_SERVICE) {
        const char *argv[] = { "screenplus", "start", "--quiet", NULL };
        sp_client_exec_binary(argv, 1);
        st->window_open = 0;
        sp_client_fetch_full(st);
        return 1;
    }

    if (!st->running) return 0;

    switch (id) {
    case ID_SHOW:
        sp_menu_toggle_window(st);
        return 0;

    case ID_PAUSE: {
        const char *argv[] = { "toggle", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_NEXT: {
        const char *argv[] = { "next", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_FAVOURITE: {
        if (!st->has_current || !st->current.id[0]) return 0;
        const char *argv[] = { "favourite", st->current.id, "toggle", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_AUDIO: {
        const char *argv[] = { "audio", st->audio ? "off" : "on", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_VOL_0:
    case ID_VOL_25:
    case ID_VOL_50:
    case ID_VOL_75:
    case ID_VOL_100: {
        static const char *const k_vstr[5] = { "0", "25", "50", "75", "100" };
        const char *argv[] = { "volume", k_vstr[id - ID_VOL_0], NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_SLIDESHOW_TOGGLE: {
        const char *argv[] = { "slideshow", st->slideshow ? "off" : "on", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_SS_EVERY_5:
    case ID_SS_EVERY_15:
    case ID_SS_EVERY_30:
    case ID_SS_EVERY_60: {
        static const char *const k_mstr[4] = { "5", "15", "30", "60" };
        const char *argv[] = { "slideshow", "every", k_mstr[id - ID_SS_EVERY_5], NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_SS_SHUFFLE: {
        const char *argv[] = { "slideshow", "shuffle", st->shuffle ? "off" : "on", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_SS_SRC_ALL: {
        const char *argv[] = { "slideshow", "from", "all", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_SS_SRC_FAV: {
        const char *argv[] = { "slideshow", "from", "favourites", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_SCHEDULE: {
        const char *argv[] = { "schedule", st->schedule ? "off" : "on", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_MODE_INDEPENDENT:
    case ID_MODE_DUPLICATE:
    case ID_MODE_SPAN: {
        static const char *const k_modes[3] = { "independent", "duplicate", "span" };
        const char *argv[] = { "mode", k_modes[id - ID_MODE_INDEPENDENT], NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_STARTUP: {
        const char *argv[] = { "settings", "set", "start_with_linux", st->start_with_linux ? "off" : "on", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_RESTART: {
        const char *argv[] = { "restart", NULL };
        sp_client_command(argv, NULL, NULL);
        sp_client_fetch_full(st);
        return 1;
    }
    case ID_STOP: {
        const char *argv[] = { "stop", NULL };
        sp_client_command(argv, NULL, NULL);
        st->running = 0;
        st->window_open = 0;
        st->has_current = 0;
        st->have_list = 0;
        st->have_settings = 0;
        return 1;
    }
    default:
        break;
    }

    if (id >= ID_WP_BASE && id < ID_WP_BASE + st->wp_count) {
        int idx = id - ID_WP_BASE;
        const char *wp_id = st->wallpapers[idx].id;
        if (wp_id[0]) {
            const char *argv[] = { "apply", wp_id, NULL };
            sp_client_command(argv, NULL, NULL);
            sp_client_fetch_full(st);
            return 1;
        }
    }

    if (id >= ID_SS_SRC_COL_BASE && id < ID_SS_SRC_COL_BASE + st->collection_count) {
        int idx = id - ID_SS_SRC_COL_BASE;
        const char *col = st->collections[idx];
        if (col[0]) {
            const char *argv[] = { "slideshow", "from", col, NULL };
            sp_client_command(argv, NULL, NULL);
            sp_client_fetch_full(st);
            return 1;
        }
    }

    return 0;
}
