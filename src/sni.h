/* org.kde.StatusNotifierItem D-Bus implementation.
 * Adapted from SDL3 (src/tray/unix/SDL_dbustray.c), zlib License. */
#pragma once
#include "client.h"
#include <dbus/dbus.h>
#include <stdint.h>

#define SNI_OBJECT_PATH   "/StatusNotifierItem"
#define MENU_OBJECT_PATH  "/MenuBar"

/* Initialize the StatusNotifierItem on the session bus and register with StatusNotifierWatcher.
 * Returns 1 on success, 0 on error. */
int  sp_sni_init(DBusConnection *conn, SpState *state, const char *menu_path);

/* Re-register with org.kde.StatusNotifierWatcher (called at startup and on NameOwnerChanged). */
void sp_sni_register_watcher(DBusConnection *conn);

/* Update cached owner presence for org.kde.plasmashell and org.freedesktop.Notifications. */
void sp_sni_on_name_owner_changed(const char *name, const char *new_owner);

/* Sync IconName and ToolTip from `state`. Emits NewIcon / NewToolTip ONLY when changed.
 * If force_emit is 1, emits both signals unconditionally. */
void sp_sni_sync(DBusConnection *conn, const SpState *state, int force_emit);

/* Flush any debounced scroll-to-volume update when its 150 ms deadline arrives.
 * Returns the next deadline in monotonic ms, or 0 if no volume update is pending. */
int64_t sp_sni_tick_scroll(DBusConnection *conn, int64_t now_ms);

/* Clean up StatusNotifierItem resources. */
void sp_sni_shutdown(DBusConnection *conn);
