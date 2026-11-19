/* org.kde.StatusNotifierItem D-Bus implementation.
 * Adapted from SDL3 (src/tray/unix/SDL_dbustray.c), zlib License. */
#pragma once
#include "client.h"
#include <dbus/dbus.h>

#define SNI_OBJECT_PATH   "/StatusNotifierItem"
#define MENU_OBJECT_PATH  "/MenuBar"

/* Initialize the StatusNotifierItem on the session bus and register with StatusNotifierWatcher.
 * Returns 1 on success, 0 on error. */
int  sp_sni_init(DBusConnection *conn, SpState *state, const char *menu_path);

/* Re-register with org.kde.StatusNotifierWatcher (called at startup and on NameOwnerChanged). */
void sp_sni_register_watcher(DBusConnection *conn);

/* Sync IconName and ToolTip from `state`. Emits NewIcon / NewToolTip ONLY when changed.
 * If force_emit is 1, emits both signals unconditionally. */
void sp_sni_sync(DBusConnection *conn, const SpState *state, int force_emit);

/* Clean up StatusNotifierItem resources. */
void sp_sni_shutdown(DBusConnection *conn);
