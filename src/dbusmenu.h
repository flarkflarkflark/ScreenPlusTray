/* com.canonical.dbusmenu D-Bus interface implementation.
 * Adapted from SDL3 (src/core/linux/SDL_dbus.c), zlib License. */
#pragma once
#include "client.h"
#include <dbus/dbus.h>

#define DBUSMENU_OBJECT_PATH "/MenuBar"

/* Register the com.canonical.dbusmenu object at /MenuBar on `conn`.
 * Returns 1 on success, 0 on error. */
int  sp_dbusmenu_init(DBusConnection *conn, SpState *state, int *quit_flag);

/* Rebuild the menu layout from `state`. Emits LayoutUpdated ONLY if the layout changed
 * (or if force_emit is non-zero). Returns 1 if the layout changed, 0 if identical. */
int  sp_dbusmenu_sync(DBusConnection *conn, const SpState *state, int force_emit);

/* Unregister the dbusmenu object path. */
void sp_dbusmenu_shutdown(DBusConnection *conn);
