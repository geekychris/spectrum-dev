/*
 * zxh - spectrum_dev harness extension for ZEsarUX. See zxh.h.
 *
 * This file is #included at the end of zrcp/remote.c, so everything remote.c
 * sees (cpu_core_loop, registers, menu flags, escribir_socket...) is in scope.
 *
 * ZRCP commands added (all answer on one line unless noted):
 *
 *   zxh-status                 frames=N tstates=T log=END nowait=0|1 step=0|1 pc=XXXX
 *   zxh-run frames=N [until=HEX] [since=OFF] [at=FRAME:TSTATE]
 *                              cpu-step mode only. Runs up to N complete frames
 *                              as fast as possible, deterministically. Stops
 *                              early when the debug log (after absolute offset
 *                              OFF, default: log end at call time) contains the
 *                              bytes HEX, when a breakpoint fires, or at the
 *                              first instruction boundary at/after frame
 *                              FRAME, T-state TSTATE (exact replay). Answers:
 *                              frames=RAN total=N tstates=T
 *                              reason=frames|log|break|at pc=XXXX log=END
 *                              [match=OFF] [break=MESSAGE]
 *   zxh-log [since] [max]      two lines: "base=B end=E next=X", then the log
 *                              bytes [since, X) as hex (X-since <= max, default
 *                              32768). base = oldest offset still buffered.
 *   zxh-log-clear              forget buffered text (offsets keep counting)
 *   zxh-turbo 0|1              free-running mode without host pacing
 *   zxh-joystick [name]        get/set the emulated joystick (Kempston,
 *                              "Sinclair 1", "Sinclair 2", Cursor, None...)
 *   zxh-nextkeys [B0 B1]       get/set the Next extended-key registers (hex)
 *   zxh-rec start|stop|status|log [since]
 *                              input recorder: every change of the 12-byte input
 *                              state (8 half-rows, joystick bits, joystick type,
 *                              NextReg 0xB0/0xB1) from any source, as
 *                              "FRAME TSTATE HEX24" lines (log)
 *   zxh-input-set HEX24        set that whole state (replay)
 *   zxh-sync [ram]             reset the FLASH phase and (ram) zero RAM, which
 *                              ZEsarUX's hard reset leaves alone, so a load is
 *                              the same whatever ran before it
 *   zxh-hash on|off|clear|now|status|get [since]
 *                              per-frame FNV-1a hash of the rendered frame,
 *                              "FRAME HASH" lines (get)
 *   zxh-help                   this list
 *
 * GPL-3.0-or-later, like ZEsarUX.
 */

#include "zxh.h"

int zxh_nowait = 0;
unsigned long zxh_frames = 0;

static int zxh_frame_tick = 0;

static void zxh_hash_frame_end(void);
static int zxh_unhex(const char *s, unsigned char *out, int cap);

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
    zxh_hash_frame_end();
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

/* ---- Spectrum Next extended keys (NextReg 0xB0/0xB1), injected by zxh-nextkeys.
   ZEsarUX does not emulate these registers; bits are 1 while a key is held:
   0xB0: 7 ;  6 "  5 ,  4 .  3 UP  2 DOWN  1 LEFT  0 RIGHT
   0xB1: 7 DELETE  6 EDIT  5 BREAK  4 INV VIDEO  3 TRUE VIDEO  2 GRAPH  1 CAPS LOCK  0 EXTEND */

static unsigned char zxh_extkeys[2];

int zxh_next_extkeys(unsigned char reg)
{
    return zxh_extkeys[reg == 0xB1 ? 1 : 0];
}

/* ---- input recorder: every change of the emulated input state, wherever it
   came from (ZRCP set-ui-io-ports, zxh-nextkeys, the host keyboard/joystick in
   a window), stamped with the frame and T-state of the instruction boundary
   where the core first saw it. 12 bytes of state: 8 keyboard half-rows, the
   joystick byte, the joystick type, NextReg 0xB0, 0xB1. */

#define ZXH_IN_STATE 12
#define ZXH_REC_MAX 200000

struct zxh_input_event {
    unsigned long frame;
    int t;
    unsigned char st[ZXH_IN_STATE];
};

static int zxh_rec_on = 0;
static struct zxh_input_event *zxh_rec = NULL;
static long zxh_rec_len = 0, zxh_rec_cap = 0;
static unsigned char zxh_rec_last[ZXH_IN_STATE];

static void zxh_input_state(unsigned char *st)
{
    st[0] = puerto_65278; st[1] = puerto_65022; st[2] = puerto_64510; st[3] = puerto_63486;
    st[4] = puerto_61438; st[5] = puerto_57342; st[6] = puerto_49150; st[7] = puerto_32766;
    st[8] = puerto_especial_joystick;
    st[9] = (unsigned char)joystick_emulation;
    st[10] = zxh_extkeys[0];
    st[11] = zxh_extkeys[1];
}

static void zxh_rec_add(const unsigned char *st)
{
    if (zxh_rec_len == zxh_rec_cap) {
        long cap = zxh_rec_cap ? zxh_rec_cap * 2 : 1024;
        struct zxh_input_event *p;
        if (cap > ZXH_REC_MAX) return;   /* full: further changes are dropped */
        p = realloc(zxh_rec, cap * sizeof(*p));
        if (p == NULL) return;
        zxh_rec = p;
        zxh_rec_cap = cap;
    }
    zxh_rec[zxh_rec_len].frame = zxh_frames;
    zxh_rec[zxh_rec_len].t = t_estados;
    memcpy(zxh_rec[zxh_rec_len].st, st, ZXH_IN_STATE);
    zxh_rec_len++;
}

void zxh_input_poll(void)
{
    unsigned char st[ZXH_IN_STATE];
    if (!zxh_rec_on) return;
    zxh_input_state(st);
    if (memcmp(st, zxh_rec_last, ZXH_IN_STATE)) {
        memcpy(zxh_rec_last, st, ZXH_IN_STATE);
        zxh_rec_add(st);
    }
}

/* ---- per-frame screen hashes (FNV-1a over the rendered frame) ---- */

#define ZXH_HASH_MAX 500000
static int zxh_hash_on = 0;
static unsigned long *zxh_hash_frame = NULL;
static unsigned int *zxh_hash_val = NULL;
static long zxh_hash_len = 0, zxh_hash_cap = 0;

static unsigned int zxh_frame_fnv(void)
{
    unsigned int h = 2166136261u;
    long i, n = (long)screen_get_emulated_display_width_no_zoom_border_en() *
                screen_get_emulated_display_height_no_zoom_border_en();
    if (rainbow_buffer == NULL) return 0;
    for (i = 0; i < n; i++) {
        z80_int v = rainbow_buffer[i];
        h = (h ^ (v & 0xFF)) * 16777619u;
        h = (h ^ (v >> 8)) * 16777619u;
    }
    return h;
}

static void zxh_hash_frame_end(void)
{
    if (!zxh_hash_on) return;
    if (zxh_hash_len == zxh_hash_cap) {
        long cap = zxh_hash_cap ? zxh_hash_cap * 2 : 4096;
        unsigned long *f;
        unsigned int *v;
        if (cap > ZXH_HASH_MAX) return;
        f = realloc(zxh_hash_frame, cap * sizeof(*f));
        if (f == NULL) return;
        zxh_hash_frame = f;
        v = realloc(zxh_hash_val, cap * sizeof(*v));
        if (v == NULL) return;
        zxh_hash_val = v;
        zxh_hash_cap = cap;
    }
    zxh_hash_frame[zxh_hash_len] = zxh_frames;
    zxh_hash_val[zxh_hash_len] = zxh_frame_fnv();
    zxh_hash_len++;
}

/* write lines in chunks (escribir_socket_format has a 4 KB buffer) */
static void zxh_send_big(int sock, char *buf, size_t *len, int flush)
{
    if (*len && (flush || *len > 60000)) {
        buf[*len] = 0;
        escribir_socket(sock, buf);
        *len = 0;
    }
}

static void zxh_cmd_rec(int sock, int argc, char **argv)
{
    const char *sub = argc ? argv[0] : "status";
    if (!strcmp(sub, "start")) {
        zxh_rec_len = 0;
        zxh_input_state(zxh_rec_last);
        zxh_rec_on = 1;
        zxh_rec_add(zxh_rec_last);          /* initial state at the start position */
    }
    else if (!strcmp(sub, "stop")) zxh_rec_on = 0;
    else if (!strcmp(sub, "log")) {
        long i, since = argc > 1 ? atol(argv[1]) : 0;
        char *buf = malloc(65536 + 128);
        size_t len = 0;
        if (buf == NULL) { escribir_socket(sock, "ERROR. out of memory"); return; }
        len += sprintf(buf + len, "count=%ld\n", zxh_rec_len);
        for (i = since < 0 ? 0 : since; i < zxh_rec_len; i++) {
            int k;
            len += sprintf(buf + len, "%lu %d ", zxh_rec[i].frame, zxh_rec[i].t);
            for (k = 0; k < ZXH_IN_STATE; k++) len += sprintf(buf + len, "%02x", zxh_rec[i].st[k]);
            buf[len++] = '\n';
            zxh_send_big(sock, buf, &len, 0);
        }
        zxh_send_big(sock, buf, &len, 1);
        free(buf);
        return;
    }
    escribir_socket_format(sock, "on=%d count=%ld frame=%lu tstates=%d", zxh_rec_on, zxh_rec_len,
                           zxh_frames, t_estados);
}

/* zxh-input-set HEX24: set the whole input state at once (replay) */
static void zxh_cmd_input_set(int sock, int argc, char **argv)
{
    unsigned char st[ZXH_IN_STATE];
    if (argc < 1 || zxh_unhex(argv[0], st, ZXH_IN_STATE) != ZXH_IN_STATE) {
        escribir_socket(sock, "ERROR. zxh-input-set needs 12 bytes of hex");
        return;
    }
    puerto_65278 = st[0]; puerto_65022 = st[1]; puerto_64510 = st[2]; puerto_63486 = st[3];
    puerto_61438 = st[4]; puerto_57342 = st[5]; puerto_49150 = st[6]; puerto_32766 = st[7];
    puerto_especial_joystick = st[8];
    if (st[9] <= JOYSTICK_TOTAL) joystick_emulation = st[9];
    zxh_extkeys[0] = st[10];
    zxh_extkeys[1] = st[11];
    escribir_socket(sock, "ok");
}

static void zxh_cmd_hash(int sock, int argc, char **argv)
{
    const char *sub = argc ? argv[0] : "status";
    if (!strcmp(sub, "on")) zxh_hash_on = 1;
    else if (!strcmp(sub, "off")) zxh_hash_on = 0;
    else if (!strcmp(sub, "clear")) zxh_hash_len = 0;
    else if (!strcmp(sub, "now")) {
        escribir_socket_format(sock, "%08x", zxh_frame_fnv());
        return;
    }
    else if (!strcmp(sub, "get")) {
        long i, since = argc > 1 ? atol(argv[1]) : 0;
        char *buf = malloc(65536 + 64);
        size_t len = 0;
        if (buf == NULL) { escribir_socket(sock, "ERROR. out of memory"); return; }
        len += sprintf(buf + len, "count=%ld\n", zxh_hash_len);
        for (i = since < 0 ? 0 : since; i < zxh_hash_len; i++) {
            len += sprintf(buf + len, "%lu %08x\n", zxh_hash_frame[i], zxh_hash_val[i]);
            zxh_send_big(sock, buf, &len, 0);
        }
        zxh_send_big(sock, buf, &len, 1);
        free(buf);
        return;
    }
    escribir_socket_format(sock, "on=%d count=%ld", zxh_hash_on, zxh_hash_len);
}

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
    int nlen = 0, saved_nowait, use_at = 0, at_t = 0;
    unsigned long at_frame = 0;
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
    s = zxh_arg(argc, argv, "at");
    if (s != NULL) {
        char *colon = strchr(s, ':');
        at_frame = strtoul(s, NULL, 0);
        at_t = colon ? atoi(colon + 1) : 0;
        use_at = 1;
    }

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
            if (use_at && (zxh_frames > at_frame || (zxh_frames == at_frame && t_estados >= at_t))) {
                reason = "at";
                break;
            }
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

    escribir_socket_format(sock, "frames=%lu total=%lu tstates=%d reason=%s pc=%04X log=%lu",
                           ran, zxh_frames, t_estados, reason, (unsigned)reg_pc, zxh_log_end());
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
        escribir_socket_format(sock, "frames=%lu tstates=%d log=%lu nowait=%d step=%d pc=%04X menu=%d brk=%d",
                               zxh_frames, t_estados, zxh_log_end(), zxh_nowait,
                               menu_event_remote_protocol_enterstep.v, (unsigned)reg_pc,
                               menu_abierto, menu_breakpoint_exception.v);
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
    else if (!strcmp(command, "zxh-sync")) {
        /* emulator state a hard reset leaves alone but the display depends on:
           the FLASH phase. Reset it so loads are reproducible whatever ran before. */
        int rw, a, cleared = 0;
        unsigned int size;
        contador_parpadeo = 16;
        estado_parpadeo.v = 0;
        /* "ram" also zeroes RAM (memory zone 0; on the Next its first 256 KB
           are the ROMs/firmware, left alone): ZEsarUX's hard reset keeps RAM,
           so leftovers of whatever ran before would leak into the next boot. */
        if (argc > 0 && !strcmp(argv[0], "ram")) {
#ifdef EMULATE_MEMPTR
            memptr = 0;                       /* not settable through set-register */
#endif
            if (rainbow_buffer != NULL)       /* no stale picture before the first frame */
                memset(rainbow_buffer, 0, (size_t)screen_get_emulated_display_width_no_zoom_border_en() *
                       screen_get_emulated_display_height_no_zoom_border_en() * sizeof(z80_int));
            size = machine_get_memory_zone_attrib(0, &rw);
            for (a = MACHINE_IS_TBBLUE ? 0x40000 : 0; a < (int)size; a += 1024) {
                z80_byte *p = machine_get_memory_zone_pointer(0, a);
                if (p != NULL) { memset(p, 0, 1024); cleared += 1024; }
            }
        }
        escribir_socket_format(sock, "ok cleared=%d", cleared);
    }
    else if (!strcmp(command, "zxh-rec")) zxh_cmd_rec(sock, argc, argv);
    else if (!strcmp(command, "zxh-input-set")) zxh_cmd_input_set(sock, argc, argv);
    else if (!strcmp(command, "zxh-hash")) zxh_cmd_hash(sock, argc, argv);
    else if (!strcmp(command, "zxh-nextkeys")) {
        if (argc >= 2) {
            zxh_extkeys[0] = (unsigned char)strtoul(argv[0], NULL, 16);
            zxh_extkeys[1] = (unsigned char)strtoul(argv[1], NULL, 16);
        }
        escribir_socket_format(sock, "b0=%02x b1=%02x", zxh_extkeys[0], zxh_extkeys[1]);
    }
    else if (!strcmp(command, "zxh-help")) {
        escribir_socket(sock,
            "zxh-status\n"
            "zxh-run frames=N [until=HEX] [since=OFFSET]   (cpu-step mode)\n"
            "zxh-log [since] [max]\n"
            "zxh-log-clear\n"
            "zxh-turbo 0|1\n"
            "zxh-joystick [type]\n"
            "zxh-nextkeys [B0hex B1hex]\n"
            "zxh-rec start|stop|status|log [since]\n"
            "zxh-input-set HEX24\n"
            "zxh-sync [ram]\n"
            "zxh-hash on|off|clear|now|status|get [since]\n");
    }
    else {
        escribir_socket(sock, "ERROR. unknown zxh command (zxh-help)");
    }
}
