/* Entry point, D-Bus event loop, single-instance lock, Watcher re-registration,
 * autostart management, and 2 s status polling for screenplus-tray. */
#include "client.h"
#include "dbusmenu.h"
#include "sni.h"
#include <dbus/dbus.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef SP_TRAY_VERSION
#define SP_TRAY_VERSION "0.1.0"
#endif

#define SP_TRAY_BUS_NAME "org.screenplus.Tray"
#define WATCHER_MATCH_RULE \
    "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus'," \
    "member='NameOwnerChanged',arg0='org.kde.StatusNotifierWatcher'"
#define PLASMA_MATCH_RULE \
    "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus'," \
    "member='NameOwnerChanged',arg0='org.kde.plasmashell'"
#define NOTIFY_MATCH_RULE \
    "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus'," \
    "member='NameOwnerChanged',arg0='org.freedesktop.Notifications'"

static volatile sig_atomic_t g_quit = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_quit = 1;
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

static DBusHandlerResult watcher_filter(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    (void)user_data;
    if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
        const char *name = NULL, *old_owner = NULL, *new_owner = NULL;
        DBusError err;
        dbus_error_init(&err);
        if (dbus_message_get_args(msg, &err,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_STRING, &old_owner,
                                  DBUS_TYPE_STRING, &new_owner,
                                  DBUS_TYPE_INVALID)) {
            if (name && !strcmp(name, "org.kde.StatusNotifierWatcher") &&
                new_owner && new_owner[0] != '\0') {
                sp_sni_register_watcher(conn);
            } else if (name) {
                sp_sni_on_name_owner_changed(name, new_owner);
            }
        }
        dbus_error_free(&err);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static int autostart_dir_and_file(char *dir_out, size_t dir_cap, char *file_out, size_t file_cap)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int nd = 0;
    if (xdg && xdg[0]) {
        nd = snprintf(dir_out, dir_cap, "%s/autostart", xdg);
    } else if (home && home[0]) {
        nd = snprintf(dir_out, dir_cap, "%s/.config/autostart", home);
    } else {
        return 0;
    }
    if (nd <= 0 || (size_t)nd >= dir_cap) return 0;

    int nf = snprintf(file_out, file_cap, "%s/screenplus-tray.desktop", dir_out);
    return nf > 0 && (size_t)nf < file_cap;
}

static int mkdir_p(const char *dir)
{
    char tmp[PATH_MAX];
    int n = snprintf(tmp, sizeof tmp, "%s", dir);
    if (n <= 0 || (size_t)n >= sizeof tmp) return 0;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return 0;
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

static void resolve_exec_path(char *out, size_t cap)
{
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = '\0';
        snprintf(out, cap, "%s", exe);
    } else {
        snprintf(out, cap, "screenplus-tray");
    }
}

static int handle_autostart(const char *arg)
{
    if (!arg || (strcmp(arg, "on") != 0 && strcmp(arg, "off") != 0)) {
        fprintf(stderr, "Usage: screenplus-tray --autostart <on|off>\n");
        return 2;
    }

    char dir[PATH_MAX], file[PATH_MAX];
    if (!autostart_dir_and_file(dir, sizeof dir, file, sizeof file)) {
        fprintf(stderr, "screenplus-tray: could not resolve ~/.config/autostart path\n");
        return 1;
    }

    if (!strcmp(arg, "off")) {
        if (unlink(file) != 0 && errno != ENOENT) {
            fprintf(stderr, "screenplus-tray: failed to remove %s: %s\n", file, strerror(errno));
            return 1;
        }
        return 0;
    }

    if (!mkdir_p(dir)) {
        fprintf(stderr, "screenplus-tray: failed to create directory %s: %s\n", dir, strerror(errno));
        return 1;
    }

    char exe[PATH_MAX];
    resolve_exec_path(exe, sizeof exe);

    FILE *f = fopen(file, "w");
    if (!f) {
        fprintf(stderr, "screenplus-tray: failed to write %s: %s\n", file, strerror(errno));
        return 1;
    }
    fprintf(f,
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=Screen+ Tray\n"
            "Comment=System tray icon for Screen+ live wallpaper service\n"
            "Exec=\"%s\"\n"
            "Icon=screenplus-tray\n"
            "Terminal=false\n"
            "Categories=Utility;DesktopSettings;\n"
            "StartupNotify=false\n"
            "NoDisplay=true\n"
            "X-KDE-autostart-phase=2\n",
            exe);
    if (fclose(f) != 0) {
        fprintf(stderr, "screenplus-tray: error closing %s\n", file);
        return 1;
    }
    return 0;
}

static void print_usage(void)
{
    puts("Usage: screenplus-tray [--dump] [--autostart on|off] [--version] [--help]");
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_usage();
            return 0;
        }
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            puts("screenplus-tray " SP_TRAY_VERSION);
            return 0;
        }
        if (!strcmp(argv[i], "--dump")) {
            SpState st;
            sp_state_init(&st);
            sp_client_fetch_full(&st);
            sp_state_dump(&st);
            return 0;
        }
        if (!strcmp(argv[i], "--autostart")) {
            return handle_autostart(i + 1 < argc ? argv[i + 1] : NULL);
        }
        fprintf(stderr, "screenplus-tray: unknown option '%s'\n", argv[i]);
        print_usage();
        return 2;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    DBusError err;
    dbus_error_init(&err);
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err) || !conn) {
        fprintf(stderr, "screenplus-tray: failed to connect to session D-Bus: %s\n",
                dbus_error_is_set(&err) ? err.message : "null connection");
        dbus_error_free(&err);
        return 1;
    }
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    /* Single instance: claim org.screenplus.Tray and exit quietly if already taken. */
    int own = dbus_bus_request_name(conn, SP_TRAY_BUS_NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "screenplus-tray: failed to request %s: %s\n", SP_TRAY_BUS_NAME, err.message);
        dbus_error_free(&err);
        dbus_connection_unref(conn);
        return 1;
    }
    if (own != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER &&
        own != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER) {
        /* Another instance is already running; exit quietly with status 0. */
        dbus_connection_unref(conn);
        return 0;
    }

    /* Watch for org.kde.StatusNotifierWatcher and OSD services (re)appearing. */
    dbus_bus_add_match(conn, WATCHER_MATCH_RULE, &err);
    dbus_error_free(&err);
    dbus_bus_add_match(conn, PLASMA_MATCH_RULE, &err);
    dbus_error_free(&err);
    dbus_bus_add_match(conn, NOTIFY_MATCH_RULE, &err);
    dbus_error_free(&err);
    dbus_connection_add_filter(conn, watcher_filter, NULL, NULL);

    SpState state;
    sp_state_init(&state);
    sp_client_poll_status(&state);

    int menu_quit = 0;
    if (!sp_dbusmenu_init(conn, &state, &menu_quit)) {
        dbus_bus_release_name(conn, SP_TRAY_BUS_NAME, NULL);
        dbus_connection_unref(conn);
        return 1;
    }

    if (!sp_sni_init(conn, &state, DBUSMENU_OBJECT_PATH)) {
        sp_dbusmenu_shutdown(conn);
        dbus_bus_release_name(conn, SP_TRAY_BUS_NAME, NULL);
        dbus_connection_unref(conn);
        return 1;
    }

    int dbus_fd = -1;
    dbus_connection_get_unix_fd(conn, &dbus_fd);

    int64_t next_poll = now_ms() + 2000;

    while (!g_quit && !menu_quit && dbus_connection_get_is_connected(conn)) {
        while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS) {}
        dbus_connection_flush(conn);
        if (g_quit || menu_quit) break;

        int64_t now = now_ms();
        int64_t scroll_due = sp_sni_tick_scroll(conn, now);

        int64_t wake_at = next_poll;
        if (scroll_due > 0 && scroll_due < wake_at) {
            wake_at = scroll_due;
        }

        int timeout_ms = (int)(wake_at - now);
        if (timeout_ms < 0) timeout_ms = 0;
        if (timeout_ms > 2000) timeout_ms = 2000;

        if (dbus_fd >= 0) {
            struct pollfd pfd = { dbus_fd, POLLIN, 0 };
            int pr = poll(&pfd, 1, timeout_ms);
            if (pr < 0 && errno != EINTR) break;
            if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
                dbus_connection_read_write(conn, 0);
            }
        } else {
            dbus_connection_read_write(conn, timeout_ms);
        }

        while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS) {}

        now = now_ms();
        sp_sni_tick_scroll(conn, now);

        if (now >= next_poll) {
            next_poll = now + 2000;
            sp_client_poll_status(&state);
            sp_sni_sync(conn, &state, 0);
            sp_dbusmenu_sync(conn, &state, 0);
        }
    }

    dbus_connection_remove_filter(conn, watcher_filter, NULL);
    sp_sni_shutdown(conn);
    sp_dbusmenu_shutdown(conn);
    dbus_bus_release_name(conn, SP_TRAY_BUS_NAME, NULL);
    dbus_connection_flush(conn);
    dbus_connection_unref(conn);
    return 0;
}
