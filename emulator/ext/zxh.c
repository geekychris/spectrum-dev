/*
 * zxh - spectrum_dev harness extension for ZEsarUX. See zxh.h.
 *
 * This file is #included at the end of zrcp/remote.c, so everything remote.c
 * sees (cpu_core_loop, registers, menu flags, escribir_socket...) is in scope.
 *
 * ZRCP commands added (all answer on one line unless noted):
 *
 *   zxh-status                 frames=N log=END nowait=0|1 step=0|1 pc=XXXX
 *   zxh-run frames=N [until=HEX] [since=OFF]
 *                              cpu-step mode only. Runs up to N complete frames
 *                              as fast as possible, deterministically. Stops
 *                              early when the debug log (after absolute offset
 *                              OFF, default: log end at call time) contains the
 *                              bytes HEX, or when a breakpoint fires. Answers:
 *                              frames=RAN total=N reason=frames|log|break
 *                              pc=XXXX log=END [match=OFF] [break=MESSAGE]
 *   zxh-log [since] [max]      two lines: "base=B end=E next=X", then the log
 *                              bytes [since, X) as hex (X-since <= max, default
 *                              32768). base = oldest offset still buffered.
 *   zxh-log-clear              forget buffered text (offsets keep counting)
 *   zxh-turbo 0|1              free-running mode without host pacing
 *   zxh-joystick [name]        get/set the emulated joystick (Kempston,
 *                              "Sinclair 1", "Sinclair 2", Cursor, None...)
 *   zxh-help                   this list
 *
 * GPL-3.0-or-later, like ZEsarUX.
 */

#include "zxh.h"

int zxh_nowait = 0;
unsigned long zxh_frames = 0;

static int zxh_frame_tick = 0;

/* ---- debug log ring: bytes written to ZXI register 1 by the guest ---- */

#define ZXH_LOG_MAX (4 * 1024 * 1024)

static unsigned char *zxh_log = NULL;
static size_t zxh_log_len = 0, zxh_log_cap = 0;
static unsigned long zxh_log_base = 0; /* absolute offset of zxh_log[0] */

#ifdef USE_PTHREADS
static pthread_mutex_t zxh_log_lock = PTHREAD_MUTEX_INITIALIZER;
#define ZXH_LOCK() pthread_mutex_lock(&zxh_log_lock)
#define ZXH_UNLOCK() pthread_mutex_unlock(&zxh_log_lock)
#else
#define ZXH_LOCK()
#define ZXH_UNLOCK()
#endif

static unsigned long zxh_log_end(void) { return zxh_log_base + zxh_log_len; }

void zxh_debug_char(unsigned char c)
{
    ZXH_LOCK();
    if (zxh_log_len == zxh_log_cap) {
        if (zxh_log_cap >= ZXH_LOG_MAX) {
            /* full: drop the oldest half */
            size_t drop = zxh_log_len / 2;
            memmove(zxh_log, zxh_log + drop, zxh_log_len - drop);
            zxh_log_len -= drop;
            zxh_log_base += drop;
        }
        else {
            size_t cap = zxh_log_cap ? zxh_log_cap * 2 : 65536;
            unsigned char *p = realloc(zxh_log, cap);
            if (p == NULL) { ZXH_UNLOCK(); return; }
            zxh_log = p;
            zxh_log_cap = cap;
        }
    }
    zxh_log[zxh_log_len++] = c;
    ZXH_UNLOCK();
}

/* Absolute offset just past the first occurrence of needle starting at or
   after `from`, or 0 if not found. */
static unsigned long zxh_log_find(unsigned long from, const unsigned char *needle, size_t n)
{
    unsigned long found = 0;
    size_t i, start;
    ZXH_LOCK();
    if (from < zxh_log_base) from = zxh_log_base;
    start = from - zxh_log_base;
    for (i = start; n && i + n <= zxh_log_len; i++) {
        if (zxh_log[i] == needle[0] && !memcmp(zxh_log + i, needle, n)) {
            found = zxh_log_base + i + n;
            break;
        }
    }
    ZXH_UNLOCK();
    return found;
}

/* ---- hooks ---- */

void zxh_end_frame(void)
{
    zxh_frames++;
    if (zxh_nowait) {
        zxh_frame_tick = 1;
        /* pretend the host timer already fired: the core never waits */
        interrupcion_timer_generada.v = 1;
    }
}

int zxh_timer_tick(int host_tick)
{
    int t;
    if (!zxh_nowait) return host_tick;
    /* A frame may have ended (core waiting for the host timer) before nowait
       mode started, e.g. when we grabbed a free-running CPU: release it too. */
    t = zxh_frame_tick || esperando_tiempo_final_t_estados.v;
    zxh_frame_tick = 0;
    return t;
}

int zxh_force_render(void) { return zxh_nowait; }

#ifdef USE_PTHREADS
static pthread_t zxh_main_thread;
static int zxh_main_thread_set = 0;

/* runs on the main thread before main() */
__attribute__((constructor)) static void zxh_init_main_thread(void)
{
    zxh_main_thread = pthread_self();
    zxh_main_thread_set = 1;
}

int zxh_host_io_allowed(void)
{
    return !zxh_main_thread_set || pthread_equal(pthread_self(), zxh_main_thread);
}
#else
int zxh_host_io_allowed(void) { return 1; }
#endif

/* ---- ZRCP commands ---- */

static int zxh_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* decode hex into out (capacity cap); returns length or -1 */
static int zxh_unhex(const char *s, unsigned char *out, int cap)
{
    int n = 0;
    while (s[0] && s[1]) {
        int h = zxh_hexval(s[0]), l = zxh_hexval(s[1]);
        if (h < 0 || l < 0 || n >= cap) return -1;
        out[n++] = (unsigned char)(h * 16 + l);
        s += 2;
    }
    return s[0] ? -1 : n;
}

/* value of key=... among argv, or NULL */
static char *zxh_arg(int argc, char **argv, const char *key)
{
    int i;
    size_t k = strlen(key);
    for (i = 0; i < argc; i++) {
        if (!strncmp(argv[i], key, k) && argv[i][k] == '=') return argv[i] + k + 1;
    }
    return NULL;
}

static void zxh_cmd_run(int sock, int argc, char **argv)
{
    char *s;
    unsigned long want, ran = 0, start, since, match = 0, seen;
    unsigned char needle[256];
    int nlen = 0, saved_nowait;
    const char *reason = "frames";

    if (menu_event_remote_protocol_enterstep.v == 0) {
        escribir_socket(sock, "ERROR. You must first enter cpu-step mode");
        return;
    }
    s = zxh_arg(argc, argv, "frames");
    if (s == NULL) {
        escribir_socket(sock, "ERROR. frames=N required");
        return;
    }
    want = strtoul(s, NULL, 0);
    s = zxh_arg(argc, argv, "until");
    if (s != NULL) {
        nlen = zxh_unhex(s, needle, sizeof(needle));
        if (nlen <= 0) {
            escribir_socket(sock, "ERROR. until= must be 1-255 bytes of hex");
            return;
        }
    }
    s = zxh_arg(argc, argv, "since");
    since = s ? strtoul(s, NULL, 0) : zxh_log_end();

    remote_cpu_before_core_loop();
    saved_nowait = zxh_nowait;
    zxh_nowait = 1;
    start = zxh_frames;
    seen = since;

    if (nlen && (match = zxh_log_find(since, needle, nlen)) != 0) {
        reason = "log";
    }
    else {
        while (zxh_frames - start < want) {
            cpu_core_loop();
            if (menu_abierto || menu_breakpoint_exception.v) {
                reason = "break";
                break;
            }
            if (nlen && zxh_log_end() != seen) {
                /* only rescan the tail that could contain a new match */
                unsigned long from = seen > since + nlen - 1 ? seen - (nlen - 1) : since;
                seen = zxh_log_end();
                if ((match = zxh_log_find(from, needle, nlen)) != 0) {
                    reason = "log";
                    break;
                }
            }
        }
    }
    zxh_nowait = saved_nowait;
    ran = zxh_frames - start;

    escribir_socket_format(sock, "frames=%lu total=%lu reason=%s pc=%04X log=%lu",
                           ran, zxh_frames, reason, (unsigned)reg_pc, zxh_log_end());
    if (match) escribir_socket_format(sock, " match=%lu", match);
    if (menu_breakpoint_exception.v) {
        escribir_socket_format(sock, " break=%s", catch_breakpoint_message);
    }
    if (menu_multitarea == 0) audio_playing.v = 0;
    menu_breakpoint_exception.v = 0;
    menu_set_menu_abierto(0);
}

static void zxh_cmd_log(int sock, int argc, char **argv)
{
    unsigned long since = argc > 0 ? strtoul(argv[0], NULL, 0) : 0;
    unsigned long maxb = argc > 1 ? strtoul(argv[1], NULL, 0) : 32768;
    unsigned long end, next, i;
    char *out;
    static const char hx[] = "0123456789abcdef";

    ZXH_LOCK();
    end = zxh_log_end();
    if (since < zxh_log_base) since = zxh_log_base;
    if (since > end) since = end;
    next = end - since > maxb ? since + maxb : end;
    out = malloc((next - since) * 2 + 128);
    if (out == NULL) {
        ZXH_UNLOCK();
        escribir_socket(sock, "ERROR. out of memory");
        return;
    }
    sprintf(out, "base=%lu end=%lu next=%lu since=%lu\n", zxh_log_base, end, next, since);
    {
        char *p = out + strlen(out);
        for (i = since; i < next; i++) {
            unsigned char c = zxh_log[i - zxh_log_base];
            *p++ = hx[c >> 4];
            *p++ = hx[c & 15];
        }
        *p = 0;
    }
    ZXH_UNLOCK();
    escribir_socket(sock, out);
    free(out);
}

static void zxh_cmd_joystick(int sock, int argc, char **argv)
{
    int i;
    if (argc > 0) {
        char name[64];
        /* the tokenizer splits "Sinclair 1"; glue the words back together */
        name[0] = 0;
        for (i = 0; i < argc; i++) {
            if (i) strncat(name, " ", sizeof(name) - strlen(name) - 1);
            strncat(name, argv[i], sizeof(name) - strlen(name) - 1);
        }
        for (i = 0; i <= JOYSTICK_TOTAL; i++) {
            if (!strcasecmp(name, joystick_texto[i])) break;
        }
        if (i > JOYSTICK_TOTAL) {
            escribir_socket(sock, "ERROR. unknown joystick type");
            return;
        }
        joystick_emulation = i;
    }
    escribir_socket(sock, joystick_texto[joystick_emulation]);
}

void zxh_remote_command(int sock, char *command, char *params)
{
    int argc;
    char *argv[REMOTE_MAX_PARAMETERS_COMMAND];

    remote_parse_commands_argvc(params, &argc, argv);

    if (!strcmp(command, "zxh-status")) {
        escribir_socket_format(sock, "frames=%lu log=%lu nowait=%d step=%d pc=%04X",
                               zxh_frames, zxh_log_end(), zxh_nowait,
                               menu_event_remote_protocol_enterstep.v, (unsigned)reg_pc);
    }
    else if (!strcmp(command, "zxh-run")) zxh_cmd_run(sock, argc, argv);
    else if (!strcmp(command, "zxh-log")) zxh_cmd_log(sock, argc, argv);
    else if (!strcmp(command, "zxh-log-clear")) {
        ZXH_LOCK();
        zxh_log_base += zxh_log_len;
        zxh_log_len = 0;
        ZXH_UNLOCK();
        escribir_socket_format(sock, "log=%lu", zxh_log_end());
    }
    else if (!strcmp(command, "zxh-turbo")) {
        if (argc > 0) zxh_nowait = atoi(argv[0]) ? 1 : 0;
        escribir_socket_format(sock, "nowait=%d", zxh_nowait);
    }
    else if (!strcmp(command, "zxh-joystick")) zxh_cmd_joystick(sock, argc, argv);
    else if (!strcmp(command, "zxh-help")) {
        escribir_socket(sock,
            "zxh-status\n"
            "zxh-run frames=N [until=HEX] [since=OFFSET]   (cpu-step mode)\n"
            "zxh-log [since] [max]\n"
            "zxh-log-clear\n"
            "zxh-turbo 0|1\n"
            "zxh-joystick [type]\n");
    }
    else {
        escribir_socket(sock, "ERROR. unknown zxh command (zxh-help)");
    }
}
