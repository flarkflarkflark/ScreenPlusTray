/* Unix domain socket client and state representation for the Screen+ service. */
#include "client.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define SOCKET_TIMEOUT_MS 500

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int sp_socket_path(char *out, size_t cap)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");
    int n = (dir && dir[0])
        ? snprintf(out, cap, "%s/%s", dir, SP_SOCKET_NAME)
        : snprintf(out, cap, "/tmp/%s-%u", SP_SOCKET_NAME, (unsigned)getuid());
    return n > 0 && (size_t)n < cap;
}

static int connect_nonblock(const char *path, int *not_running, int *timed_out)
{
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        if (not_running) *not_running = 1;
        return -1;
    }

    int rc = connect(fd, (struct sockaddr *)&a, sizeof a);
    if (rc < 0) {
        if (errno == EINPROGRESS || errno == EAGAIN) {
            struct pollfd p = { fd, POLLOUT, 0 };
            int pr = poll(&p, 1, SOCKET_TIMEOUT_MS);
            if (pr == 0) {
                if (timed_out) *timed_out = 1;
                close(fd);
                return -1;
            }
            if (pr < 0) {
                if (not_running) *not_running = 1;
                close(fd);
                return -1;
            }
            int err = 0;
            socklen_t len = sizeof err;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
                if (err == ETIMEDOUT) {
                    if (timed_out) *timed_out = 1;
                } else {
                    if (not_running) *not_running = 1;
                }
                close(fd);
                return -1;
            }
        } else {
            if (not_running) *not_running = 1;
            close(fd);
            return -1;
        }
    }
    return fd;
}

int sp_client_command_ex(const char *const *argv, JVal **out_root, int *not_running, int *timed_out)
{
    if (out_root) *out_root = NULL;
    if (not_running) *not_running = 0;
    if (timed_out) *timed_out = 0;

    char path[108];
    if (!sp_socket_path(path, sizeof path)) {
        if (not_running) *not_running = 1;
        return 0;
    }

    int fd = connect_nonblock(path, not_running, timed_out);
    if (fd < 0) return 0;

    StrBuf q = { 0 };
    sb_puts(&q, "{\"argv\":[");
    for (int i = 0; argv && argv[i]; i++) {
        if (i) sb_puts(&q, ",");
        json_write_string(&q, argv[i]);
    }
    sb_puts(&q, "]}");

    size_t off = 0;
    while (!q.failed && off < q.len) {
        struct pollfd p = { fd, POLLOUT, 0 };
        int pr = poll(&p, 1, SOCKET_TIMEOUT_MS);
        if (pr <= 0) {
            if (pr < 0 && errno == EINTR) continue;
            if (pr == 0 && timed_out) *timed_out = 1;
            sb_free(&q);
            close(fd);
            return 0;
        }
        ssize_t w = write(fd, q.p + off, q.len - off);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        if (w == 0) break;
        off += (size_t)w;
    }
    int write_ok = !q.failed && off == q.len;
    sb_free(&q);
    if (!write_ok) {
        close(fd);
        return 0;
    }

    shutdown(fd, SHUT_WR);

    StrBuf in = { 0 };
    char buf[8192];
    for (;;) {
        struct pollfd p = { fd, POLLIN, 0 };
        int pr = poll(&p, 1, SOCKET_TIMEOUT_MS);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) {
            if (timed_out) *timed_out = 1;
            sb_free(&in);
            close(fd);
            return 0;
        }
        ssize_t n = read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        if (n == 0) break; /* EOF */
        sb_append(&in, buf, (size_t)n);
        if (in.len > (4 * 1024 * 1024)) break;
    }
    close(fd);

    JVal *j = (in.len && !in.failed) ? json_parse(in.p, in.len) : NULL;
    sb_free(&in);
    if (!j) return 0;

    int ok = json_bool(json_get(j, "ok"), 0);
    if (!ok) {
        const char *text = json_str(json_get(j, "text"), "command failed");
        fprintf(stderr, "screenplus-tray: %s", text);
        size_t tlen = strlen(text);
        if (tlen == 0 || text[tlen - 1] != '\n') fputc('\n', stderr);
        json_free(j);
        return 0;
    }

    if (out_root) *out_root = j;
    else json_free(j);
    return 1;
}

int sp_client_command(const char *const *argv, JVal **out_root, int *not_running)
{
    return sp_client_command_ex(argv, out_root, not_running, NULL);
}

int sp_client_exec_binary(const char *const *argv, int wait_for_exit)
{
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        if (!wait_for_exit) setsid();
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > 2) close(null_fd);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    if (!wait_for_exit) return 1;
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

void sp_state_init(SpState *st)
{
    memset(st, 0, sizeof *st);
    st->volume = 100;
    st->slideshow_minutes = 30;
    copy_str(st->display_mode, sizeof st->display_mode, "independent");
}

static void parse_wallpaper_obj(const JVal *w, SpWallpaper *out, int on_screen)
{
    memset(out, 0, sizeof *out);
    if (!w || w->t != J_OBJ) return;
    copy_str(out->id, sizeof out->id, json_str(json_get(w, "id"), ""));
    copy_str(out->name, sizeof out->name, json_str(json_get(w, "name"), ""));
    copy_str(out->type, sizeof out->type, json_str(json_get(w, "type"), ""));
    out->missing = json_bool(json_get(w, "missing"), 0);
    out->favourite = json_bool(json_get(w, "favourite"), 0);
    out->on_screen = on_screen;
}

static int fetch_settings_and_slideshow(SpState *st, int *timed_out)
{
    JVal *root = NULL;
    int to = 0;
    const char *argv_set[] = { "settings", NULL };
    if (!sp_client_command_ex(argv_set, &root, NULL, &to)) {
        if (to) {
            st->unresponsive = 1;
            if (timed_out) *timed_out = 1;
        }
        return 0;
    }
    const JVal *d = json_get(root, "data");
    if (d && d->t == J_OBJ) {
        st->audio = json_bool(json_get(d, "audio"), st->audio);
        st->paused = json_bool(json_get(d, "paused"), st->paused);
        st->volume = (int)json_num(json_get(d, "volume"), st->volume);
        if (st->volume < 0) st->volume = 0;
        if (st->volume > 100) st->volume = 100;
        st->slideshow = json_bool(json_get(d, "slideshow"), st->slideshow);
        st->slideshow_minutes = (int)json_num(json_get(d, "slideshow_minutes"), st->slideshow_minutes);
        st->shuffle = json_bool(json_get(d, "shuffle"), st->shuffle);
        st->schedule = json_bool(json_get(d, "schedule"), st->schedule);
        copy_str(st->display_mode, sizeof st->display_mode,
                 json_str(json_get(d, "display_mode"), st->display_mode));
        st->start_with_linux = json_bool(json_get(d, "start_with_linux"), st->start_with_linux);
        st->have_settings = 1;
    }
    json_free(root);

    root = NULL;
    to = 0;
    const char *argv_ss[] = { "slideshow", NULL };
    if (!sp_client_command_ex(argv_ss, &root, NULL, &to)) {
        if (to) {
            st->unresponsive = 1;
            if (timed_out) *timed_out = 1;
        }
        return 0;
    }
    d = json_get(root, "data");
    if (d && d->t == J_OBJ) {
        st->slideshow = json_bool(json_get(d, "slideshow"), st->slideshow);
        st->slideshow_minutes = (int)json_num(json_get(d, "minutes"), st->slideshow_minutes);
        st->shuffle = json_bool(json_get(d, "shuffle"), st->shuffle);
        copy_str(st->slideshow_from, sizeof st->slideshow_from,
                 json_str(json_get(d, "from"), ""));
    }
    json_free(root);
    return 1;
}

static int fetch_list_ex(SpState *st, int *timed_out)
{
    JVal *root = NULL;
    int to = 0;
    const char *argv_ls[] = { "list", NULL };
    if (!sp_client_command_ex(argv_ls, &root, NULL, &to)) {
        if (to) {
            st->unresponsive = 1;
            if (timed_out) *timed_out = 1;
        }
        return 0;
    }

    const JVal *arr = json_get(root, "data");
    if (arr && arr->t == J_ARR) {
        st->total_wallpapers = arr->n;
        st->wp_count = 0;
        int current_in_top = 0;
        SpWallpaper current_extra;
        int found_current_extra = 0;

        for (int i = 0; i < arr->n; i++) {
            const JVal *item = &arr->kids[i];
            int on_scr = json_bool(json_get(item, "onScreen"), 0);
            const JVal *w = json_get(item, "wallpaper");
            if (!w || w->t != J_OBJ) continue;

            SpWallpaper parsed;
            parse_wallpaper_obj(w, &parsed, on_scr);

            if (!st->has_current && on_scr && parsed.id[0]) {
                st->current = parsed;
                st->has_current = 1;
            }

            int is_cur = st->has_current && !strcmp(parsed.id, st->current.id);
            if (st->wp_count < SP_MAX_MENU_WPS) {
                st->wallpapers[st->wp_count++] = parsed;
                if (is_cur) current_in_top = 1;
            } else if (is_cur && !current_in_top) {
                current_extra = parsed;
                found_current_extra = 1;
            }
        }
        if (!current_in_top && st->has_current && st->current.id[0]) {
            if (found_current_extra) {
                st->wallpapers[st->wp_count++] = current_extra;
            } else {
                st->wallpapers[st->wp_count++] = st->current;
            }
        }
        st->have_list = 1;
    }
    json_free(root);
    return 1;
}

int sp_client_fetch_list(SpState *st)
{
    return fetch_list_ex(st, NULL);
}

static int fetch_collections(SpState *st, int *timed_out)
{
    JVal *root = NULL;
    int to = 0;
    const char *argv_col[] = { "collection", "list", NULL };
    if (!sp_client_command_ex(argv_col, &root, NULL, &to)) {
        if (to) {
            st->unresponsive = 1;
            if (timed_out) *timed_out = 1;
        }
        return 0;
    }

    const JVal *arr = json_get(root, "data");
    if (arr && arr->t == J_ARR) {
        st->collection_count = 0;
        for (int i = 0; i < arr->n && st->collection_count < SP_MAX_COLLECTIONS; i++) {
            const char *name = json_str(json_get(&arr->kids[i], "name"), "");
            if (name[0]) {
                copy_str(st->collections[st->collection_count++],
                         sizeof st->collections[0], name);
            }
        }
    }
    json_free(root);
    return 1;
}

static int poll_status_only(SpState *st, int *was_running_out, int *timed_out)
{
    int was_running = st->running;
    if (was_running_out) *was_running_out = was_running;

    int not_running = 0;
    int to = 0;
    JVal *root = NULL;
    const char *argv_st[] = { "status", NULL };

    if (!sp_client_command_ex(argv_st, &root, &not_running, &to)) {
        if (to) {
            /* Service timed out: mark unresponsive and keep cached state. */
            st->unresponsive = 1;
            if (timed_out) *timed_out = 1;
            return 0;
        }
        if (not_running) {
            st->running = 0;
            st->unresponsive = 0;
            st->window_open = 0;
            st->has_current = 0;
            memset(&st->current, 0, sizeof st->current);
            st->have_list = 0;
            st->have_settings = 0;
        }
        return 0;
    }

    if (!was_running) {
        st->window_open = 0;
    }
    st->running = 1;
    st->unresponsive = 0;
    const JVal *d = json_get(root, "data");
    if (d && d->t == J_OBJ) {
        st->paused = json_bool(json_get(d, "paused"), 0);
        st->audio = json_bool(json_get(d, "audio"), 0);
        copy_str(st->state_text, sizeof st->state_text, json_str(json_get(d, "state"), "Active"));

        st->has_current = 0;
        memset(&st->current, 0, sizeof st->current);
        const JVal *displays = json_get(d, "displays");
        if (displays && displays->t == J_ARR && displays->n > 0) {
            const JVal *chosen_wp = NULL;
            for (int i = 0; i < displays->n; i++) {
                const JVal *disp = &displays->kids[i];
                const JVal *wp = json_get(disp, "wallpaper");
                if (wp && wp->t == J_OBJ) {
                    if (!chosen_wp || json_bool(json_get(disp, "primary"), 0)) {
                        chosen_wp = wp;
                        if (json_bool(json_get(disp, "primary"), 0)) break;
                    }
                }
            }
            if (chosen_wp) {
                parse_wallpaper_obj(chosen_wp, &st->current, 1);
                st->has_current = st->current.id[0] != '\0';
            }
        }
    }
    json_free(root);
    return 1;
}

int sp_client_poll_status(SpState *st)
{
    int was_running = 0;
    int to = 0;
    if (!poll_status_only(st, &was_running, &to)) return 0;

    /* Whenever the service newly appears (or at startup), fetch list and initial settings once.
     * Stop immediately after the first timeout. */
    if (!was_running || !st->have_list || !st->have_settings) {
        if (!fetch_settings_and_slideshow(st, &to) && to) return 0;
        if (!fetch_list_ex(st, &to) && to) return 0;
        if (!fetch_collections(st, &to) && to) return 0;
    }
    return 1;
}

int sp_client_fetch_full(SpState *st)
{
    int to = 0;
    if (!poll_status_only(st, NULL, &to)) return 0;
    if (!fetch_settings_and_slideshow(st, &to) && to) return 0;
    if (!fetch_list_ex(st, &to) && to) return 0;
    if (!fetch_collections(st, &to) && to) return 0;
    return 1;
}

int sp_client_ensure_volume(SpState *st)
{
    if (st->have_settings) return 1;
    JVal *root = NULL;
    int to = 0;
    const char *argv[] = { "volume", NULL };
    if (!sp_client_command_ex(argv, &root, NULL, &to)) {
        if (to) st->unresponsive = 1;
        return 0;
    }
    const JVal *d = json_get(root, "data");
    if (d && d->t == J_OBJ) {
        st->volume = (int)json_num(json_get(d, "volume"), st->volume);
        if (st->volume < 0) st->volume = 0;
        if (st->volume > 100) st->volume = 100;
        st->audio = json_bool(json_get(d, "audio"), st->audio);
        st->have_settings = 1;
    }
    json_free(root);
    return 1;
}

void sp_state_dump(const SpState *st)
{
    printf("running: %s\n", st->running ? "true" : "false");
    if (st->unresponsive) printf("unresponsive: true\n");
    if (!st->running) return;

    printf("state: %s\n", st->state_text);
    printf("paused: %s\n", st->paused ? "true" : "false");
    printf("audio: %s\n", st->audio ? "true" : "false");
    printf("volume: %d%%\n", st->volume);
    if (st->has_current) {
        printf("current_wallpaper:\n");
        printf("  id: %s\n", st->current.id);
        printf("  name: %s\n", st->current.name);
        printf("  type: %s\n", st->current.type);
        printf("  favourite: %s\n", st->current.favourite ? "true" : "false");
        printf("  missing: %s\n", st->current.missing ? "true" : "false");
    } else {
        printf("current_wallpaper: (none)\n");
    }
    printf("slideshow: %s (every %d min, shuffle: %s, from: \"%s\")\n",
           st->slideshow ? "on" : "off",
           st->slideshow_minutes,
           st->shuffle ? "on" : "off",
           st->slideshow_from[0] ? st->slideshow_from : "all");
    printf("schedule: %s\n", st->schedule ? "on" : "off");
    printf("display_mode: %s\n", st->display_mode);
    printf("start_with_linux: %s\n", st->start_with_linux ? "on" : "off");
    printf("library: %d total, %d in switch menu:\n", st->total_wallpapers, st->wp_count);
    for (int i = 0; i < st->wp_count; i++) {
        const SpWallpaper *w = &st->wallpapers[i];
        int checked = st->has_current && !strcmp(w->id, st->current.id);
        printf("  [%c] %2d. %-36s (%s, id=%s%s%s)\n",
               checked ? '*' : ' ',
               i + 1,
               w->name,
               w->type,
               w->id,
               w->favourite ? ", fav" : "",
               w->missing ? ", MISSING" : "");
    }
    printf("collections (%d):\n", st->collection_count);
    for (int i = 0; i < st->collection_count; i++) {
        printf("  - %s\n", st->collections[i]);
    }
}
