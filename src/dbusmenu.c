/* com.canonical.dbusmenu D-Bus interface implementation.
 * Adapted from SDL3 (src/core/linux/SDL_dbus.c), zlib License. */
#include "dbusmenu.h"
#include "menu.h"
#include "sni.h"
#include <stdio.h>
#include <string.h>

#define DBUSMENU_INTERFACE "com.canonical.dbusmenu"

static const char *k_dbusmenu_introspect =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"com.canonical.dbusmenu\">\n"
    "    <property name=\"Version\" type=\"u\" access=\"read\"/>\n"
    "    <property name=\"TextDirection\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Status\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"IconThemePath\" type=\"as\" access=\"read\"/>\n"
    "    <method name=\"GetLayout\">\n"
    "      <arg type=\"i\" name=\"parentId\" direction=\"in\"/>\n"
    "      <arg type=\"i\" name=\"recursionDepth\" direction=\"in\"/>\n"
    "      <arg type=\"as\" name=\"propertyNames\" direction=\"in\"/>\n"
    "      <arg type=\"u\" name=\"revision\" direction=\"out\"/>\n"
    "      <arg type=\"(ia{sv}av)\" name=\"layout\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"GetGroupProperties\">\n"
    "      <arg type=\"ai\" name=\"ids\" direction=\"in\"/>\n"
    "      <arg type=\"as\" name=\"propertyNames\" direction=\"in\"/>\n"
    "      <arg type=\"a(ia{sv})\" name=\"properties\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"GetProperty\">\n"
    "      <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"name\" direction=\"in\"/>\n"
    "      <arg type=\"v\" name=\"value\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"Event\">\n"
    "      <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"eventId\" direction=\"in\"/>\n"
    "      <arg type=\"v\" name=\"data\" direction=\"in\"/>\n"
    "      <arg type=\"u\" name=\"timestamp\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"EventGroup\">\n"
    "      <arg type=\"a(isvu)\" name=\"events\" direction=\"in\"/>\n"
    "      <arg type=\"ai\" name=\"idErrors\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"AboutToShow\">\n"
    "      <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"
    "      <arg type=\"b\" name=\"needUpdate\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"AboutToShowGroup\">\n"
    "      <arg type=\"ai\" name=\"ids\" direction=\"in\"/>\n"
    "      <arg type=\"ai\" name=\"updatesNeeded\" direction=\"out\"/>\n"
    "      <arg type=\"ai\" name=\"idErrors\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <signal name=\"ItemsPropertiesUpdated\">\n"
    "      <arg type=\"a(ia{sv})\" name=\"updatedProps\"/>\n"
    "      <arg type=\"a(ias)\" name=\"removedProps\"/>\n"
    "    </signal>\n"
    "    <signal name=\"LayoutUpdated\">\n"
    "      <arg type=\"u\" name=\"revision\"/>\n"
    "      <arg type=\"i\" name=\"parent\"/>\n"
    "    </signal>\n"
    "    <signal name=\"ItemActivationRequested\">\n"
    "      <arg type=\"i\" name=\"id\"/>\n"
    "      <arg type=\"u\" name=\"timestamp\"/>\n"
    "    </signal>\n"
    "  </interface>\n"
    "</node>\n";

static struct {
    SpState      *state;
    int          *quit_flag;
    SpMenuLayout  layout;
    dbus_uint32_t revision;
} g_menu;

static void append_prop_str(DBusMessageIter *dict, const char *key, const char *val)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_prop_bool(DBusMessageIter *dict, const char *key, dbus_bool_t val)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_prop_int32(DBusMessageIter *dict, const char *key, dbus_int32_t val)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &val);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_item_properties(const SpMenuItem *it, DBusMessageIter *dict)
{
    if (it->id == ID_ROOT) {
        append_prop_str(dict, "children-display", "submenu");
        return;
    }

    if (it->type == SP_ITEM_SEPARATOR) {
        append_prop_str(dict, "type", "separator");
        append_prop_bool(dict, "visible", TRUE);
        return;
    }

    append_prop_str(dict, "type", "standard");
    append_prop_str(dict, "label", it->label);
    append_prop_bool(dict, "enabled", it->enabled ? TRUE : FALSE);
    append_prop_bool(dict, "visible", TRUE);

    if (it->type == SP_ITEM_CHECKBOX) {
        append_prop_str(dict, "toggle-type", "checkmark");
        append_prop_int32(dict, "toggle-state", it->checked ? 1 : 0);
    } else if (it->type == SP_ITEM_RADIO) {
        append_prop_str(dict, "toggle-type", "radio");
        append_prop_int32(dict, "toggle-state", it->checked ? 1 : 0);
    }

    if (it->type == SP_ITEM_SUBMENU) {
        append_prop_str(dict, "children-display", "submenu");
    }
}

static void append_layout_node(const SpMenuLayout *l, const SpMenuItem *it,
                               DBusMessageIter *parent_iter, int depth)
{
    DBusMessageIter st, dict, children;
    dbus_int32_t id = it->id;

    dbus_message_iter_open_container(parent_iter, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &id);

    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "{sv}", &dict);
    append_item_properties(it, &dict);
    dbus_message_iter_close_container(&st, &dict);

    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "v", &children);
    if (depth > 0 && it->child_count > 0) {
        for (int i = 0; i < it->child_count; i++) {
            const SpMenuItem *child = &l->items[it->children[i]];
            DBusMessageIter child_var;
            dbus_message_iter_open_container(&children, DBUS_TYPE_VARIANT, "(ia{sv}av)", &child_var);
            append_layout_node(l, child, &child_var, depth - 1);
            dbus_message_iter_close_container(&children, &child_var);
        }
    }
    dbus_message_iter_close_container(&st, &children);
    dbus_message_iter_close_container(parent_iter, &st);
}

static void emit_layout_updated(DBusConnection *conn)
{
    if (!conn) return;
    DBusMessage *sig = dbus_message_new_signal(DBUSMENU_OBJECT_PATH, DBUSMENU_INTERFACE, "LayoutUpdated");
    if (!sig) return;
    dbus_int32_t parent = 0;
    dbus_message_append_args(sig,
                             DBUS_TYPE_UINT32, &g_menu.revision,
                             DBUS_TYPE_INT32, &parent,
                             DBUS_TYPE_INVALID);
    dbus_connection_send(conn, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(conn);
}

int sp_dbusmenu_sync(DBusConnection *conn, const SpState *state, int force_emit)
{
    SpMenuLayout next;
    sp_menu_build(&next, state);

    int changed = !sp_menu_equal(&g_menu.layout, &next);
    if (changed) {
        g_menu.layout = next;
        g_menu.revision++;
    }
    if ((changed || force_emit) && conn) {
        if (!changed) g_menu.revision++;
        emit_layout_updated(conn);
    }
    return changed;
}

static DBusHandlerResult handle_get_layout(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter args, iter;
    dbus_int32_t parent_id = 0, depth = -1;

    if (!dbus_message_iter_init(msg, &args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_INT32) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &parent_id);
    if (!dbus_message_iter_next(&args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_INT32) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &depth);

    if (depth < 0) depth = 100;

    const SpMenuItem *target = sp_menu_find(&g_menu.layout, parent_id);
    if (!target) target = &g_menu.layout.items[0];

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &g_menu.revision);
    append_layout_node(&g_menu.layout, target, &iter, depth);

    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_group_properties(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter args, array_iter, iter, reply_array;
    dbus_int32_t *ids = NULL;
    int ids_count = 0;

    if (!dbus_message_iter_init(msg, &args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    dbus_message_iter_recurse(&args, &array_iter);
    if (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_INT32) {
        dbus_message_iter_get_fixed_array(&array_iter, &ids, &ids_count);
    }

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(ia{sv})", &reply_array);

    for (int i = 0; i < ids_count; i++) {
        const SpMenuItem *it = sp_menu_find(&g_menu.layout, ids[i]);
        if (!it) continue;
        DBusMessageIter st, dict;
        dbus_int32_t id = it->id;
        dbus_message_iter_open_container(&reply_array, DBUS_TYPE_STRUCT, NULL, &st);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &id);
        dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "{sv}", &dict);
        append_item_properties(it, &dict);
        dbus_message_iter_close_container(&st, &dict);
        dbus_message_iter_close_container(&reply_array, &st);
    }

    dbus_message_iter_close_container(&iter, &reply_array);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_property(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter args, iter, variant;
    dbus_int32_t id = 0;
    const char *prop = NULL;

    if (!dbus_message_iter_init(msg, &args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_INT32) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &id);
    if (!dbus_message_iter_next(&args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &prop);

    const SpMenuItem *it = sp_menu_find(&g_menu.layout, id);
    if (!it) {
        DBusMessage *err = dbus_message_new_error(msg, "com.canonical.dbusmenu.Error", "Item not found");
        if (err) {
            dbus_connection_send(conn, err, NULL);
            dbus_message_unref(err);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    dbus_message_iter_init_append(reply, &iter);

    const char *sval = "";
    dbus_bool_t bval = TRUE;
    dbus_int32_t ival = 0;

    if (!strcmp(prop, "label")) {
        sval = it->label;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &sval);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "type")) {
        sval = (it->type == SP_ITEM_SEPARATOR) ? "separator" : "standard";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &sval);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "enabled")) {
        bval = it->enabled ? TRUE : FALSE;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &bval);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "visible")) {
        bval = TRUE;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &bval);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "toggle-type")) {
        sval = (it->type == SP_ITEM_CHECKBOX) ? "checkmark" :
               (it->type == SP_ITEM_RADIO) ? "radio" : "";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &sval);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "toggle-state")) {
        ival = it->checked ? 1 : 0;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "i", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &ival);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "children-display")) {
        sval = (it->type == SP_ITEM_SUBMENU) ? "submenu" : "";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &sval);
        dbus_message_iter_close_container(&iter, &variant);
    } else {
        sval = "";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &sval);
        dbus_message_iter_close_container(&iter, &variant);
    }

    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static void dispatch_click_id(DBusConnection *conn, int id)
{
    int quit = 0;
    if (sp_menu_activate(id, g_menu.state, &quit)) {
        sp_sni_sync(conn, g_menu.state, 0);
        sp_dbusmenu_sync(conn, g_menu.state, 0);
    }
    if (quit && g_menu.quit_flag) {
        *g_menu.quit_flag = 1;
    }
}

static DBusHandlerResult handle_event(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter args;
    dbus_int32_t id = 0;
    const char *event_id = NULL;

    if (!dbus_message_iter_init(msg, &args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_INT32) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &id);
    if (!dbus_message_iter_next(&args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &event_id);

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (reply) {
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    }

    if (event_id && !strcmp(event_id, "clicked")) {
        dispatch_click_id(conn, id);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_event_group(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter args, array_iter, iter, err_iter;
    int clicked_ids[32];
    int clicked_count = 0;

    if (dbus_message_iter_init(msg, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&args, &array_iter);
        while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRUCT) {
            DBusMessageIter st;
            dbus_int32_t id = 0;
            const char *event_id = NULL;
            dbus_message_iter_recurse(&array_iter, &st);
            if (dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_INT32) {
                dbus_message_iter_get_basic(&st, &id);
                if (dbus_message_iter_next(&st) &&
                    dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_STRING) {
                    dbus_message_iter_get_basic(&st, &event_id);
                    if (event_id && !strcmp(event_id, "clicked") && clicked_count < 32) {
                        clicked_ids[clicked_count++] = id;
                    }
                }
            }
            dbus_message_iter_next(&array_iter);
        }
    }

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (reply) {
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &err_iter);
        dbus_message_iter_close_container(&iter, &err_iter);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    }

    for (int i = 0; i < clicked_count; i++) {
        dispatch_click_id(conn, clicked_ids[i]);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

static int refresh_for_about_to_show(DBusConnection *conn)
{
    sp_client_fetch_full(g_menu.state);
    sp_sni_sync(conn, g_menu.state, 0);
    return sp_dbusmenu_sync(conn, g_menu.state, 0);
}

static DBusHandlerResult handle_about_to_show(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter args;
    dbus_int32_t id = 0;
    if (dbus_message_iter_init(msg, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_INT32) {
        dbus_message_iter_get_basic(&args, &id);
    }

    int changed = 0;
    /* Only fetch from service when the root menu (0) is about to open. */
    if (id == 0) {
        changed = refresh_for_about_to_show(conn);
    }

    dbus_bool_t need_update = changed ? TRUE : FALSE;
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &need_update, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_about_to_show_group(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter args, array_iter, iter, updates_iter, err_iter;
    dbus_int32_t *ids = NULL;
    int ids_count = 0;
    int has_root = 0;

    if (dbus_message_iter_init(msg, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&args, &array_iter);
        if (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_INT32) {
            dbus_message_iter_get_fixed_array(&array_iter, &ids, &ids_count);
            for (int i = 0; i < ids_count; i++) {
                if (ids[i] == 0) has_root = 1;
            }
        }
    }

    int changed = 0;
    if (has_root || ids_count == 0) {
        changed = refresh_for_about_to_show(conn);
    }

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &updates_iter);
    if (changed) {
        dbus_int32_t root_id = 0;
        dbus_message_iter_append_basic(&updates_iter, DBUS_TYPE_INT32, &root_id);
    }
    dbus_message_iter_close_container(&iter, &updates_iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &err_iter);
    dbus_message_iter_close_container(&iter, &err_iter);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_props_get(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter args, iter, variant, array_iter;
    const char *iface = NULL, *prop = NULL;

    if (!dbus_message_iter_init(msg, &args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &iface);
    if (!dbus_message_iter_next(&args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &prop);

    if (strcmp(iface, DBUSMENU_INTERFACE) != 0) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    dbus_message_iter_init_append(reply, &iter);

    if (!strcmp(prop, "Version")) {
        dbus_uint32_t ver = 3;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "u", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &ver);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "Status")) {
        const char *s = "normal";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &s);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "TextDirection")) {
        const char *s = "ltr";
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &s);
        dbus_message_iter_close_container(&iter, &variant);
    } else if (!strcmp(prop, "IconThemePath")) {
        dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "as", &variant);
        dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &array_iter);
        dbus_message_iter_close_container(&variant, &array_iter);
        dbus_message_iter_close_container(&iter, &variant);
    } else {
        dbus_message_unref(reply);
        reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property");
    }

    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_props_get_all(DBusConnection *conn, DBusMessage *msg)
{
    DBusMessageIter args, iter, dict, entry, variant, array_iter;
    const char *iface = NULL;

    if (!dbus_message_iter_init(msg, &args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_message_iter_get_basic(&args, &iface);

    if (strcmp(iface, DBUSMENU_INTERFACE) != 0) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

    const char *k_ver = "Version";
    dbus_uint32_t ver = 3;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_ver);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &ver);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&dict, &entry);

    append_prop_str(&dict, "Status", "normal");
    append_prop_str(&dict, "TextDirection", "ltr");

    const char *k_itp = "IconThemePath";
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_itp);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &array_iter);
    dbus_message_iter_close_container(&variant, &array_iter);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&dict, &entry);

    dbus_message_iter_close_container(&iter, &dict);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult dbusmenu_message_handler(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    (void)user_data;

    if (dbus_message_is_method_call(msg, DBUSMENU_INTERFACE, "GetLayout")) {
        return handle_get_layout(conn, msg);
    }
    if (dbus_message_is_method_call(msg, DBUSMENU_INTERFACE, "GetGroupProperties")) {
        return handle_get_group_properties(conn, msg);
    }
    if (dbus_message_is_method_call(msg, DBUSMENU_INTERFACE, "GetProperty")) {
        return handle_get_property(conn, msg);
    }
    if (dbus_message_is_method_call(msg, DBUSMENU_INTERFACE, "Event")) {
        return handle_event(conn, msg);
    }
    if (dbus_message_is_method_call(msg, DBUSMENU_INTERFACE, "EventGroup")) {
        return handle_event_group(conn, msg);
    }
    if (dbus_message_is_method_call(msg, DBUSMENU_INTERFACE, "AboutToShow")) {
        return handle_about_to_show(conn, msg);
    }
    if (dbus_message_is_method_call(msg, DBUSMENU_INTERFACE, "AboutToShowGroup")) {
        return handle_about_to_show_group(conn, msg);
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get")) {
        return handle_props_get(conn, msg);
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        return handle_props_get_all(conn, msg);
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &k_dbusmenu_introspect, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int sp_dbusmenu_init(DBusConnection *conn, SpState *state, int *quit_flag)
{
    memset(&g_menu, 0, sizeof g_menu);
    g_menu.state = state;
    g_menu.quit_flag = quit_flag;
    g_menu.revision = 1;
    sp_menu_build(&g_menu.layout, state);

    DBusObjectPathVTable vtable;
    memset(&vtable, 0, sizeof vtable);
    vtable.message_function = dbusmenu_message_handler;

    DBusError err;
    dbus_error_init(&err);
    if (!dbus_connection_try_register_object_path(conn, DBUSMENU_OBJECT_PATH, &vtable, NULL, &err)) {
        fprintf(stderr, "screenplus-tray: failed to register %s: %s\n",
                DBUSMENU_OBJECT_PATH, dbus_error_is_set(&err) ? err.message : "unknown error");
        dbus_error_free(&err);
        return 0;
    }
    return 1;
}

void sp_dbusmenu_shutdown(DBusConnection *conn)
{
    if (!conn) return;
    dbus_connection_unregister_object_path(conn, DBUSMENU_OBJECT_PATH);
}
