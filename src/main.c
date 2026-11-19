/* Entry point, D-Bus event loop, and 2 s status polling for screenplus-tray. */
#include "client.h"
#include "sni.h"
#include <dbus/dbus.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump")) {
            SpState st;
            sp_state_init(&st);
            sp_client_fetch_full(&st);
            sp_state_dump(&st);
            return 0;
        }
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

    SpState state;
    sp_state_init(&state);
    sp_client_poll_status(&state);

    if (!sp_sni_init(conn, &state, "/NO_DBUSMENU")) {
        dbus_connection_unref(conn);
        return 1;
    }

    int dbus_fd = -1;
    dbus_connection_get_unix_fd(conn, &dbus_fd);

    int64_t next_poll = now_ms() + 2000;

    while (!g_quit && dbus_connection_get_is_connected(conn)) {
        while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS) {}
        dbus_connection_flush(conn);

        int64_t now = now_ms();
        int timeout_ms = (int)(next_poll - now);
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
        if (now >= next_poll) {
            next_poll = now + 2000;
            sp_client_poll_status(&state);
            sp_sni_sync(conn, &state, 0);
        }
    }

    sp_sni_shutdown(conn);
    dbus_connection_flush(conn);
    dbus_connection_unref(conn);
    return 0;
}
