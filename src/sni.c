/* org.kde.StatusNotifierItem D-Bus implementation.
 * Adapted from SDL3 (src/tray/unix/SDL_dbustray.c), zlib License. */
#include "sni.h"
#include "dbusmenu.h"
#include "menu.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define SNI_INTERFACE         "org.kde.StatusNotifierItem"
#define SNI_WATCHER_SERVICE   "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH      "/StatusNotifierWatcher"
#define SNI_WATCHER_INTERFACE "org.kde.StatusNotifierWatcher"

#define PLASMASHELL_SERVICE   "org.kde.plasmashell"
#define KDE_OSD_PATH          "/org/kde/osdService"
#define KDE_OSD_INTERFACE     "org.kde.osdService"

#define NOTIFY_SERVICE        "org.freedesktop.Notifications"
#define NOTIFY_PATH           "/org/freedesktop/Notifications"
#define NOTIFY_INTERFACE      "org.freedesktop.Notifications"

#define SCROLL_DEBOUNCE_MS    150

#ifndef SP_INSTALL_ICON_DIR
#define SP_INSTALL_ICON_DIR ""
#endif

static const char *k_sni_introspect =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"org.kde.StatusNotifierItem\">\n"
    "    <property name=\"Category\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Id\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Title\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Status\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"WindowId\" type=\"i\" access=\"read\"/>\n"
    "    <property name=\"IconThemePath\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Menu\" type=\"o\" access=\"read\"/>\n"
    "    <property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"IconName\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"
    "    <property name=\"OverlayIconName\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"OverlayIconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"
    "    <property name=\"AttentionIconName\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"AttentionIconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"
    "    <property name=\"AttentionMovieName\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>\n"
    "    <method name=\"ProvideXdgActivationToken\">\n"
    "      <arg name=\"token\" type=\"s\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"ContextMenu\">\n"
    "      <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"
    "      <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"Activate\">\n"
    "      <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"
    "      <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"SecondaryActivate\">\n"
    "      <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"
    "      <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"Scroll\">\n"
    "      <arg name=\"delta\" type=\"i\" direction=\"in\"/>\n"
    "      <arg name=\"orientation\" type=\"s\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <signal name=\"NewTitle\"/>\n"
    "    <signal name=\"NewIcon\"/>\n"
    "    <signal name=\"NewAttentionIcon\"/>\n"
    "    <signal name=\"NewOverlayIcon\"/>\n"
    "    <signal name=\"NewMenu\"/>\n"
    "    <signal name=\"NewToolTip\"/>\n"
    "    <signal name=\"NewStatus\">\n"
    "      <arg name=\"status\" type=\"s\"/>\n"
    "    </signal>\n"
    "  </interface>\n"
    "</node>\n";

static struct {
    SpState      *state;
    char          service_name[128];
    char          icon_theme_path[PATH_MAX];
    char          icon_name[64];
    char          tooltip_title[192];
    char          tooltip_body[384];
    const char   *menu_path;

    /* Scroll accumulation and debouncing */
    int           scroll_accum;
    int           scroll_pending;
    int64_t       scroll_due_ms;

    /* OSD bus name presence & freedesktop notification replacement ID */
    int           has_plasmashell;
    int           has_notifications;
    dbus_uint32_t notify_replaces_id;
} g_sni;

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

static int has_tray_icon(const char *dir)
{
    if (!dir || !dir[0]) return 0;
    char p[PATH_MAX + 64];
    snprintf(p, sizeof p, "%s/screenplus-tray.png", dir);
    if (access(p, R_OK) == 0) return 1;
    snprintf(p, sizeof p, "%s/hicolor/256x256/apps/screenplus-tray.png", dir);
    return access(p, R_OK) == 0;
}

static void resolve_icon_theme_path(char *out, size_t cap)
{
    /* 1. Installed share dir (<prefix>/share/screenplus-tray/icons) first */
    if (has_tray_icon(SP_INSTALL_ICON_DIR)) {
        snprintf(out, cap, "%s", SP_INSTALL_ICON_DIR);
        return;
    }

    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            char cand[PATH_MAX];
            char real[PATH_MAX];

            /* Relative to installed binary: <prefix>/bin/screenplus-tray -> ../share/screenplus-tray/icons */
            int w = snprintf(cand, sizeof cand, "%s/../share/screenplus-tray/icons", exe);
            if (w > 0 && (size_t)w < sizeof cand && realpath(cand, real) && has_tray_icon(real)) {
                snprintf(out, cap, "%s", real);
                return;
            }

            /* 2. Development fallback: <build>/screenplus-tray -> ../data/icons */
            w = snprintf(cand, sizeof cand, "%s/../data/icons", exe);
            if (w > 0 && (size_t)w < sizeof cand && realpath(cand, real) && has_tray_icon(real)) {
                snprintf(out, cap, "%s", real);
                return;
            }
        }
    }

    snprintf(out, cap, "%s", SP_INSTALL_ICON_DIR);
}

static void compute_icon_and_tooltip(const SpState *st,
                                     char *icon_out, size_t icon_cap,
                                     char *title_out, size_t title_cap,
                                     char *body_out, size_t body_cap)
{
    snprintf(title_out, title_cap, "Screen+");

    if (!st->running) {
        snprintf(icon_out, icon_cap, "screenplus-tray-stopped");
        snprintf(body_out, body_cap,
                 "Not running\n"
                 "\n"
                 "Right click: options");
        return;
    }

    if (st->paused) {
        snprintf(icon_out, icon_cap, "screenplus-tray-paused");
    } else {
        snprintf(icon_out, icon_cap, "screenplus-tray");
    }

    const char *status_word = st->paused ? "Paused" : "Playing";
    const char *wp_name = (st->has_current && st->current.name[0]) ? st->current.name : "Screen+";
    snprintf(body_out, body_cap,
             "%s — %s · Volume %d%%\n"
             "\n"
             "Left click: show / hide window\n"
             "Middle click: pause / resume\n"
             "Scroll: volume\n"
             "Right click: options",
             wp_name, status_word, st->volume);
}

/* ------------------------------------------------------------------ OSD (fire-and-forget) */

static void on_notify_pending_reply(DBusPendingCall *pending, void *user_data)
{
    (void)user_data;
    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    if (reply) {
        if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
            dbus_uint32_t id = 0;
            if (dbus_message_get_args(reply, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID) && id != 0) {
                g_sni.notify_replaces_id = id;
            }
        }
        dbus_message_unref(reply);
    }
    dbus_pending_call_unref(pending);
}

static void send_freedesktop_notification(DBusConnection *conn,
                                          const char *summary,
                                          const char *body,
                                          int has_value,
                                          int value_percent)
{
    if (!conn || !g_sni.has_notifications) return;

    DBusMessage *msg = dbus_message_new_method_call(NOTIFY_SERVICE,
                                                    NOTIFY_PATH,
                                                    NOTIFY_INTERFACE,
                                                    "Notify");
    if (!msg) return;

    const char *app_name = "Screen+";
    const char *app_icon = "screenplus-tray";
    dbus_uint32_t replaces_id = g_sni.notify_replaces_id;
    dbus_int32_t expire_timeout = 1500;

    DBusMessageIter iter, actions, hints, entry, variant;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &app_name);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &replaces_id);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &app_icon);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &summary);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &body);

    /* actions: empty as */
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &actions);
    dbus_message_iter_close_container(&iter, &actions);

    /* hints: a{sv} */
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &hints);

    if (has_value) {
        const char *k_val = "value";
        dbus_int32_t v = (dbus_int32_t)value_percent;
        dbus_message_iter_open_container(&hints, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_val);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &v);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&hints, &entry);
    }

    const char *k_sync = "x-canonical-private-synchronous";
    const char *v_sync = "screenplus-tray";
    dbus_message_iter_open_container(&hints, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_sync);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &v_sync);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&hints, &entry);

    const char *k_urg = "urgency";
    unsigned char v_urg = 0; /* low */
    dbus_message_iter_open_container(&hints, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_urg);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "y", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BYTE, &v_urg);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&hints, &entry);

    dbus_message_iter_close_container(&iter, &hints);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &expire_timeout);

    DBusPendingCall *pending = NULL;
    if (dbus_connection_send_with_reply(conn, msg, &pending, 1000) && pending) {
        if (!dbus_pending_call_set_notify(pending, on_notify_pending_reply, NULL, NULL)) {
            dbus_pending_call_unref(pending);
        }
    }
    dbus_message_unref(msg);
    dbus_connection_flush(conn);
}

static void show_volume_osd(DBusConnection *conn, int percent)
{
    if (!conn) return;
    if (g_sni.has_plasmashell) {
        DBusMessage *msg = dbus_message_new_method_call(PLASMASHELL_SERVICE,
                                                        KDE_OSD_PATH,
                                                        KDE_OSD_INTERFACE,
                                                        "mediaPlayerVolumeChanged");
        if (!msg) return;
        dbus_int32_t pct = (dbus_int32_t)percent;
        const char *player = "Screen+";
        const char *icon = "screenplus-tray";
        dbus_message_append_args(msg,
                                 DBUS_TYPE_INT32, &pct,
                                 DBUS_TYPE_STRING, &player,
                                 DBUS_TYPE_STRING, &icon,
                                 DBUS_TYPE_INVALID);
        dbus_connection_send(conn, msg, NULL);
        dbus_message_unref(msg);
        dbus_connection_flush(conn);
    } else if (g_sni.has_notifications) {
        char body[32];
        snprintf(body, sizeof body, "%d%%", percent);
        send_freedesktop_notification(conn, "Screen+ volume", body, 1, percent);
    }
}

static void show_pause_osd(DBusConnection *conn, int paused)
{
    if (!conn) return;
    if (g_sni.has_plasmashell) {
        DBusMessage *msg = dbus_message_new_method_call(PLASMASHELL_SERVICE,
                                                        KDE_OSD_PATH,
                                                        KDE_OSD_INTERFACE,
                                                        "showText");
        if (!msg) return;
        const char *icon = paused ? "media-playback-pause" : "media-playback-start";
        const char *text = paused ? "Screen+ paused" : "Screen+ playing";
        dbus_message_append_args(msg,
                                 DBUS_TYPE_STRING, &icon,
                                 DBUS_TYPE_STRING, &text,
                                 DBUS_TYPE_INVALID);
        dbus_connection_send(conn, msg, NULL);
        dbus_message_unref(msg);
        dbus_connection_flush(conn);
    } else if (g_sni.has_notifications) {
        send_freedesktop_notification(conn, "Screen+", paused ? "Paused" : "Playing", 0, 0);
    }
}

void sp_sni_on_name_owner_changed(const char *name, const char *new_owner)
{
    if (!name) return;
    int has = (new_owner && new_owner[0] != '\0');
    if (!strcmp(name, PLASMASHELL_SERVICE)) {
        g_sni.has_plasmashell = has;
    } else if (!strcmp(name, NOTIFY_SERVICE)) {
        g_sni.has_notifications = has;
        if (!has) g_sni.notify_replaces_id = 0;
    }
}

/* ------------------------------------------------------------------ D-Bus properties & handlers */

static void append_dict_string(DBusMessageIter *dict, const char *key, const char *val)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_dict_objpath(DBusMessageIter *dict, const char *key, const char *val)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &val);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_dict_bool(DBusMessageIter *dict, const char *key, dbus_bool_t val)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_dict_int32(DBusMessageIter *dict, const char *key, dbus_int32_t val)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &val);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_empty_pixmap_variant(DBusMessageIter *parent)
{
    DBusMessageIter variant, array;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "a(iiay)", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(iiay)", &array);
    dbus_message_iter_close_container(&variant, &array);
    dbus_message_iter_close_container(parent, &variant);
}

static void append_tooltip_variant(DBusMessageIter *parent)
{
    DBusMessageIter variant, st, array;
    const char *icon = g_sni.icon_name;
    const char *title = g_sni.tooltip_title;
    const char *body = g_sni.tooltip_body;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "(sa(iiay)ss)", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &icon);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "(iiay)", &array);
    dbus_message_iter_close_container(&st, &array);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &title);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &body);
    dbus_message_iter_close_container(&variant, &st);
    dbus_message_iter_close_container(parent, &variant);
}

static DBusHandlerResult handle_get_prop(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter args, iter, variant;
    const char *iface = NULL, *prop = NULL;

    if (!dbus_message_iter_init(msg, &args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &iface);
    if (!dbus_message_iter_next(&args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &prop);

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    dbus_message_iter_init_append(reply, &iter);

    const char *val = "";
    if (!strcmp(prop, "Category")) {
        val = "ApplicationStatus";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "Id")) {
        val = "screenplus-tray";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "Title")) {
        val = "Screen+";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "Status")) {
        val = "Active";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "IconName")) {
        val = g_sni.icon_name;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "IconThemePath")) {
        val = g_sni.icon_theme_path;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "OverlayIconName") || !strcmp(prop, "AttentionIconName") || !strcmp(prop, "AttentionMovieName")) {
        val = "";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "IconPixmap") || !strcmp(prop, "OverlayIconPixmap") || !strcmp(prop, "AttentionIconPixmap")) {
        append_empty_pixmap_variant(&iter);
    } else if (!strcmp(prop, "WindowId")) {
        dbus_int32_t wid = 0;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "i", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &wid);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "ItemIsMenu")) {
        dbus_bool_t b = FALSE; /* FALSE so left-click triggers Activate */
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &b);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "Menu")) {
        val = g_sni.menu_path ? g_sni.menu_path : "/NO_DBUSMENU";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "o", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &val);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "ToolTip")) {
        append_tooltip_variant(&iter);
    } else {
        dbus_message_unref(reply);
        reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property");
    }

    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_all_props(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter iter, dict, entry;
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

    append_dict_string(&dict, "Category", "ApplicationStatus");
    append_dict_string(&dict, "Id", "screenplus-tray");
    append_dict_string(&dict, "Title", "Screen+");
    append_dict_string(&dict, "Status", "Active");
    append_dict_int32(&dict, "WindowId", 0);
    append_dict_string(&dict, "IconThemePath", g_sni.icon_theme_path);
    append_dict_string(&dict, "IconName", g_sni.icon_name);
    append_dict_string(&dict, "OverlayIconName", "");
    append_dict_string(&dict, "AttentionIconName", "");
    append_dict_string(&dict, "AttentionMovieName", "");
    append_dict_bool(&dict, "ItemIsMenu", FALSE);
    append_dict_objpath(&dict, "Menu", g_sni.menu_path ? g_sni.menu_path : "/NO_DBUSMENU");

    const char *k_pix = "IconPixmap";
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_pix);
    append_empty_pixmap_variant(&entry);
    dbus_message_iter_close_container(&dict, &entry);

    const char *k_tt = "ToolTip";
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_tt);
    append_tooltip_variant(&entry);
    dbus_message_iter_close_container(&dict, &entry);

    dbus_message_iter_close_container(&iter, &dict);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static void on_activate(void)
{
    if (!g_sni.state || !g_sni.state->running) return;
    sp_menu_toggle_window(g_sni.state);
}

static void on_secondary_activate(DBusConnection *conn)
{
    if (!g_sni.state || !g_sni.state->running) return;
    const char *argv[] = { "toggle", NULL };
    JVal *root = NULL;
    if (sp_client_command(argv, &root, NULL)) {
        const JVal *d = json_get(root, "data");
        if (d) g_sni.state->paused = json_bool(json_get(d, "paused"), !g_sni.state->paused);
        json_free(root);
        sp_sni_sync(conn, g_sni.state, 0);
        sp_dbusmenu_sync(conn, g_sni.state, 0);
        show_pause_osd(conn, g_sni.state->paused);
    }
}

static void on_scroll(DBusConnection *conn, dbus_int32_t delta, const char *orientation)
{
    if (!g_sni.state || !g_sni.state->running || delta == 0) return;
    if (orientation && orientation[0] && strcasecmp(orientation, "vertical") != 0) return;

    /* Ensure initial volume was fetched at least once, then update locally without re-querying. */
    if (!g_sni.state->have_settings) {
        if (!sp_client_ensure_volume(g_sni.state)) return;
    }

    /* Reset accumulator if scroll direction reversed */
    if ((delta > 0 && g_sni.scroll_accum < 0) || (delta < 0 && g_sni.scroll_accum > 0)) {
        g_sni.scroll_accum = 0;
    }
    g_sni.scroll_accum += delta;

    int steps = g_sni.scroll_accum / 120;
    if (steps == 0) return;
    g_sni.scroll_accum %= 120;

    int nv = g_sni.state->volume + steps * 5;
    if (nv < 0) nv = 0;
    if (nv > 100) nv = 100;
    int changed = (nv != g_sni.state->volume);

    g_sni.state->volume = nv;
    /* Update tooltip immediately (emits NewToolTip right away) and show volume OSD */
    sp_sni_sync(conn, g_sni.state, 0);
    show_volume_osd(conn, nv);

    if (changed || g_sni.scroll_pending) {
        g_sni.scroll_pending = 1;
        g_sni.scroll_due_ms = monotonic_ms() + SCROLL_DEBOUNCE_MS;
    }
}

int64_t sp_sni_tick_scroll(DBusConnection *conn, int64_t now_ms)
{
    if (!g_sni.scroll_pending) return 0;
    if (now_ms < g_sni.scroll_due_ms) return g_sni.scroll_due_ms;

    g_sni.scroll_pending = 0;
    g_sni.scroll_due_ms = 0;
    if (!g_sni.state || !g_sni.state->running) return 0;

    char num[16];
    snprintf(num, sizeof num, "%d", g_sni.state->volume);
    const char *argv[] = { "volume", num, NULL };
    if (sp_client_command(argv, NULL, NULL)) {
        sp_dbusmenu_sync(conn, g_sni.state, 0);
    }
    return 0;
}

static DBusHandlerResult sni_message_handler(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    (void)user_data;

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get")) {
        return handle_get_prop(conn, msg);
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        return handle_get_all_props(conn, msg);
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &k_sni_introspect, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, SNI_INTERFACE, "Activate")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (reply) {
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
        }
        on_activate();
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, SNI_INTERFACE, "SecondaryActivate")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (reply) {
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
        }
        on_secondary_activate(conn);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, SNI_INTERFACE, "ContextMenu") ||
        dbus_message_is_method_call(msg, SNI_INTERFACE, "ProvideXdgActivationToken")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (reply) {
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, SNI_INTERFACE, "Scroll")) {
        DBusError err;
        dbus_error_init(&err);
        dbus_int32_t delta = 0;
        const char *orientation = "vertical";
        dbus_message_get_args(msg, &err,
                              DBUS_TYPE_INT32, &delta,
                              DBUS_TYPE_STRING, &orientation,
                              DBUS_TYPE_INVALID);
        int ok = !dbus_error_is_set(&err);
        dbus_error_free(&err);

        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (reply) {
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
        }
        if (ok) on_scroll(conn, delta, orientation);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void emit_sni_signal(DBusConnection *conn, const char *name)
{
    DBusMessage *sig = dbus_message_new_signal(SNI_OBJECT_PATH, SNI_INTERFACE, name);
    if (!sig) return;
    dbus_connection_send(conn, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(conn);
}

void sp_sni_register_watcher(DBusConnection *conn)
{
    if (!conn || !g_sni.service_name[0]) return;
    DBusMessage *msg = dbus_message_new_method_call(SNI_WATCHER_SERVICE,
                                                    SNI_WATCHER_PATH,
                                                    SNI_WATCHER_INTERFACE,
                                                    "RegisterStatusNotifierItem");
    if (!msg) return;
    const char *arg = g_sni.service_name;
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, msg, NULL);
    dbus_message_unref(msg);
    dbus_connection_flush(conn);
}

void sp_sni_sync(DBusConnection *conn, const SpState *state, int force_emit)
{
    char new_icon[64];
    char new_title[192];
    char new_body[384];

    compute_icon_and_tooltip(state,
                             new_icon, sizeof new_icon,
                             new_title, sizeof new_title,
                             new_body, sizeof new_body);

    int icon_changed = strcmp(new_icon, g_sni.icon_name) != 0;
    int tip_changed = strcmp(new_title, g_sni.tooltip_title) != 0 ||
                      strcmp(new_body, g_sni.tooltip_body) != 0;

    if (icon_changed) {
        snprintf(g_sni.icon_name, sizeof g_sni.icon_name, "%s", new_icon);
    }
    if (tip_changed) {
        snprintf(g_sni.tooltip_title, sizeof g_sni.tooltip_title, "%s", new_title);
        snprintf(g_sni.tooltip_body, sizeof g_sni.tooltip_body, "%s", new_body);
    }

    if (conn) {
        if (icon_changed || force_emit) emit_sni_signal(conn, "NewIcon");
        if (tip_changed || force_emit)  emit_sni_signal(conn, "NewToolTip");
    }
}

int sp_sni_init(DBusConnection *conn, SpState *state, const char *menu_path)
{
    memset(&g_sni, 0, sizeof g_sni);
    g_sni.state = state;
    g_sni.menu_path = menu_path;
    resolve_icon_theme_path(g_sni.icon_theme_path, sizeof g_sni.icon_theme_path);
    compute_icon_and_tooltip(state,
                             g_sni.icon_name, sizeof g_sni.icon_name,
                             g_sni.tooltip_title, sizeof g_sni.tooltip_title,
                             g_sni.tooltip_body, sizeof g_sni.tooltip_body);

    g_sni.has_plasmashell = dbus_bus_name_has_owner(conn, PLASMASHELL_SERVICE, NULL) ? 1 : 0;
    g_sni.has_notifications = dbus_bus_name_has_owner(conn, NOTIFY_SERVICE, NULL) ? 1 : 0;

    snprintf(g_sni.service_name, sizeof g_sni.service_name,
             "org.kde.StatusNotifierItem-%d-1", (int)getpid());

    DBusError err;
    dbus_error_init(&err);
    int ret = dbus_bus_request_name(conn, g_sni.service_name, DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (dbus_error_is_set(&err) || ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "screenplus-tray: failed to request SNI bus name: %s\n",
                dbus_error_is_set(&err) ? err.message : "not primary owner");
        dbus_error_free(&err);
        return 0;
    }

    DBusObjectPathVTable vtable;
    memset(&vtable, 0, sizeof vtable);
    vtable.message_function = sni_message_handler;

    if (!dbus_connection_try_register_object_path(conn, SNI_OBJECT_PATH, &vtable, NULL, &err)) {
        fprintf(stderr, "screenplus-tray: failed to register %s: %s\n",
                SNI_OBJECT_PATH, dbus_error_is_set(&err) ? err.message : "unknown error");
        dbus_error_free(&err);
        return 0;
    }

    sp_sni_register_watcher(conn);
    return 1;
}

void sp_sni_shutdown(DBusConnection *conn)
{
    if (!conn) return;
    dbus_connection_unregister_object_path(conn, SNI_OBJECT_PATH);
    if (g_sni.service_name[0]) {
        DBusError err;
        dbus_error_init(&err);
        dbus_bus_release_name(conn, g_sni.service_name, &err);
        dbus_error_free(&err);
    }
}
