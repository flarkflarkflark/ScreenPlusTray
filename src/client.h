/* Unix domain socket client and state representation for the Screen+ service. */
#pragma once
#include "json.h"

#define SP_SOCKET_NAME      "screenplus.sock"
#define SP_MAX_MENU_WPS     20
#define SP_MAX_COLLECTIONS  64

typedef struct SpWallpaper {
    char id[32];
    char name[128];
    char type[16];
    int  missing;
    int  favourite;
    int  on_screen;
} SpWallpaper;

typedef struct SpState {
    int  running;
    int  unresponsive;      /* 1 if last socket request timed out */

    /* From `status` */
    int  paused;
    int  audio;
    char state_text[64];
    int  has_current;
    SpWallpaper current;

    /* From `settings` + `slideshow` (cached for scroll & updated on menu open/actions) */
    int  have_settings;
    int  volume;            /* 0..100 */
    int  slideshow;         /* 0 or 1 */
    int  slideshow_minutes; /* e.g. 5, 15, 30, 60 */
    int  shuffle;           /* 0 or 1 */
    char slideshow_from[64];/* "" = all, "*" = favourites, else collection name */
    int  schedule;          /* 0 or 1 */
    char display_mode[32];  /* "independent", "duplicate", "span" */
    int  start_with_linux;  /* 0 or 1 */

    /* From `list` (first 20 items, plus current if outside top 20) */
    int  have_list;
    int  total_wallpapers;
    int  wp_count;
    SpWallpaper wallpapers[SP_MAX_MENU_WPS + 1];

    /* From `collection list` */
    int  collection_count;
    char collections[SP_MAX_COLLECTIONS][64];
} SpState;

/* Resolve the socket path ($XDG_RUNTIME_DIR/screenplus.sock or /tmp/screenplus.sock-<uid>). */
int sp_socket_path(char *out, size_t cap);

/* Send a command argv (NULL-terminated) over the Unix socket with short (~500 ms) timeouts.
 * Returns 1 if the socket call succeeded and ok:true, 0 otherwise.
 * If not_running != NULL, sets *not_running = 1 when connect() fails immediately (ENOENT/ECONNREFUSED).
 * If timed_out != NULL, sets *timed_out = 1 when connect/write/read times out.
 * If out_root != NULL and ok:true, stores the parsed root JVal* (caller frees with json_free).
 * Logs error text to stderr when the service replies with ok:false. */
int sp_client_command_ex(const char *const *argv, JVal **out_root, int *not_running, int *timed_out);
int sp_client_command(const char *const *argv, JVal **out_root, int *not_running);

/* Run `screenplus <args...>` via fork/execvp (for local CLI commands like `start`). */
int sp_client_exec_binary(const char *const *argv, int wait_for_exit);

/* Initialize state with sane defaults. */
void sp_state_init(SpState *st);

/* Poll `status` only (quiet in service logs). If the service newly appeared, also fetches
 * `list` and `settings` once so initial volume and library count are known.
 * Stops after the first timeout and keeps cached state if unresponsive. */
int sp_client_poll_status(SpState *st);

/* Fetch `settings`, `slideshow`, `list`, and `collection list` (for AboutToShow / post-action).
 * Stops after the first timeout, marks service as unresponsive, and retains cached state. */
int sp_client_fetch_full(SpState *st);

/* Fetch `list` only (startup / service reappear). */
int sp_client_fetch_list(SpState *st);

/* Fetch `volume` once into st->volume if not yet cached. */
int sp_client_ensure_volume(SpState *st);

/* Print the parsed state to stdout (--dump). */
void sp_state_dump(const SpState *st);
