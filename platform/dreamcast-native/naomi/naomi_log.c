/* NAOMI: the console in a ring of lines, the failure screen, the watchdog and the boot (naomi.h).
 *
 * A cabinet has no serial cable and no dcload, and KOS's SCIF output would wait on the port's FIFO at 115200 baud
 * for every line (about 10 ms a long one). So the console goes to a dbgio device that only keeps the last lines;
 * they are shown on the screen when main returns with an error, when the asset archive is missing, and when no
 * frame has been rendered for a while (a hang: the main thread's pc and pr are added, for sh-elf-addr2line on the
 * ELF). */
#include <kos.h>
#include <kos/dbgio.h>
#include <dc/pvr.h>
#include <dc/video.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "naomi.h"
#include "naomi_dimmfs.h"
#include <unistd.h>

#include <arch/arch.h> /* _arch_mem_top: 0x8e000000 on a NAOMI (32 MB) */
/* Main RAM above the heap's top (what malloc can still take from the system). */
static uintptr_t heap_left(void) {
    const uintptr_t top = (uintptr_t)sbrk(0), mem_top = (uintptr_t)_arch_mem_top;
    return mem_top > top ? mem_top - top : 0;
}

#define LOG_LINES 160
#define LOG_WIDTH 100
#define SHOW_LINES 26
#define HANG_FIRST_FRAME_S 90 /* boot: the asset loads before the first frame read megabytes through the cartridge port */
#define HANG_S 15             /* then: no rendered frame for this long */

static char lines[LOG_LINES][LOG_WIDTH];
static unsigned line_count;   /* lines started */
static unsigned column;       /* in the current line */
static volatile int pvr_up;
static kthread_t *main_thread;

static void put_char(char c) {
    if (c == '\r') return;
    char *line = lines[line_count % LOG_LINES];
    if (c == '\n') {
        line[column] = 0;
        ++line_count;
        column = 0;
        lines[line_count % LOG_LINES][0] = 0;
        return;
    }
    if (column < LOG_WIDTH - 1) {
        line[column++] = c;
        line[column] = 0;
    }
}

static int ring_detected(void) { return 1; }
static int ring_init(void) { return 0; }
static int ring_shutdown(void) { return 0; }
static int ring_set_irq_usage(int mode) { (void)mode; return 0; }
static int ring_flush(void) { return 0; }
static int ring_write_buffer(const uint8_t *data, int len, int xlat) {
    (void)xlat;
    const irq_mask_t old = irq_disable(); /* any thread may print */
    for (int i = 0; i < len; ++i) put_char((char)data[i]);
    irq_restore(old);
    return len;
}
static int ring_read_buffer(uint8_t *data, int len) { (void)data; (void)len; return -1; }

static dbgio_handler_t ring = {
    .name = "naomi-ring",
    .detected = ring_detected,
    .init = ring_init,
    .shutdown = ring_shutdown,
    .set_irq_usage = ring_set_irq_usage,
    .flush = ring_flush,
    .write_buffer = ring_write_buffer,
    .read_buffer = ring_read_buffer,
};

void nm_naomi_pvr_up(void) { pvr_up = 1; }

/* Lines worth a look when nothing failed (TEST held): sound, files, this port's own and failures. */
static int wanted(const char *line) {
    static const char *const keys[] = {"Sound", "sound", "music", "phrase", "NAOMI", "FAIL", "fail", "dimm", "no "};
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (strstr(line, keys[i])) return 1;
    return 0;
}

static void show(const char *why, int filtered) __attribute__((noreturn));
void nm_naomi_show_log(const char *why) { show(why, 0); }
void nm_naomi_show_sound_log(void) { show("TEST: sound and file lines", 1); }

static void show(const char *why, int filtered) {
    char copy[LOG_LINES][LOG_WIDTH];
    unsigned count;
    const irq_mask_t old = irq_disable();
    if (column) { lines[line_count % LOG_LINES][column] = 0; }
    count = line_count + (column ? 1u : 0u);
    memcpy(copy, lines, sizeof(copy));
    irq_restore(old);
    if (pvr_up) { pvr_up = 0; pvr_shutdown(); }
    vid_init(DM_640x480, PM_RGB565);
    vid_clear(0, 0, 64);
    dbgio_dev_select("fb");
    printf("\n DAYTONA USA native (NAOMI) - %s\n\n", why);
    unsigned pick[SHOW_LINES], n = 0;
    const unsigned oldest = count > LOG_LINES ? count - LOG_LINES : 0;
    for (unsigned i = count; i-- > oldest && n < SHOW_LINES;)
        if (!filtered || wanted(copy[i % LOG_LINES])) pick[n++] = i;
    while (n) {
        const unsigned i = pick[--n];
        char line[78];
        snprintf(line, sizeof(line), "%s", copy[i % LOG_LINES]);
        printf(" %s\n", line);
    }
    for (;;) thd_sleep(1000);
}

static void *watchdog(void *unused) {
    (void)unused;
    size_t last = 0;
    unsigned still = 0;
    int seen_frame = 0;
    for (;;) {
        thd_sleep(1000);
        size_t frames = 0;
        if (pvr_up) {
            pvr_stats_t stats;
            if (pvr_get_stats(&stats) == 0) frames = stats.frame_count;
        }
        if (frames != last) { last = frames; still = 0; seen_frame = 1; continue; }
        ++still;
        if (still >= (seen_frame ? HANG_S : HANG_FIRST_FRAME_S)) {
            printf("NAOMI HANG: no frame for %u s (%u rendered), main thread pc %08lx pr %08lx\n", still,
                   (unsigned)frames, main_thread ? (unsigned long)main_thread->context.pc : 0ul,
                   main_thread ? (unsigned long)main_thread->context.pr : 0ul);
            nm_naomi_show_log("HANG");
        }
    }
    return NULL;
}

int nm_naomi_boot(void) {
    setvbuf(stdout, NULL, _IONBF, 0); /* every line into the ring at once: a crash must not swallow the last ones */
    lines[0][0] = 0;
    if (dbgio_add_handler(&ring) == 0) dbgio_dev_select("naomi-ring");
    printf("NAOMI: native port, KOS_SUBARCH=naomi\n");
    main_thread = thd_get_current();
    const int files = dimmfs_init();
    printf("NAOMI: NetDIMM archive: %d files\n", files);
    if (files <= 0) nm_naomi_show_log("NO ASSETS IN THE NETDIMM IMAGE (naomi/naomi_pack.py)");
    /* The announcer's phrases are read the moment the game asks for one (sfx.c): from main RAM, not through the
     * cartridge port, so they start on time (32 MB of RAM: 0.9 MB is cheap here). */
    printf("NAOMI: sfx/voices.pak in RAM: %d bytes\n", dimmfs_cache("sfx/voices.pak"));
    /* Music in RAM too, as long as the game keeps its room (32 MB here, 16 on a Dreamcast): loads from the
     * NetDIMM go a chunk at a time in the idle waits, and a load the game changes its mind about before it ends is
     * dropped silently (sound.c loading). Beginner's race piece t03, the largest loaded in PCM8 (1.56 MB), never
     * finished in time on the cabinet (2026-11-06: no music, no error). In order: the race pieces and their ADPCM
     * copies, the attract's, then the pieces that must start on cue (game start, finish, podium, GAME OVER,
     * victory lane). What does not fit above the reserve stays on the NetDIMM. */
    static const char *const music[] = {
        "music/t03", "music/adpcm/t03", "music/adpcm/t04", "music/adpcm/t05", "music/t06", "music/t01",
        "music/t02", "music/t15", "music/t07", "music/t08", "music/t09", "music/t10", "music/t11", "music/t12",
        "music/t13", "music/t14", "music/t16"};
    const uintptr_t reserve = 8u << 20; /* for the game's own allocations (courses, cars, caches) */
    long cached = 0;
    unsigned pieces = 0;
    for (unsigned i = 0; i < sizeof(music) / sizeof(music[0]); ++i) {
        char afb[32], afx[32];
        snprintf(afb, sizeof(afb), "%s.afb", music[i]);
        snprintf(afx, sizeof(afx), "%s.afx", music[i]);
        const int size = dimmfs_size(afb) + dimmfs_size(afx);
        if (size <= 0 || heap_left() < reserve + (uintptr_t)size) continue;
        const int a = dimmfs_cache(afb), b = dimmfs_cache(afx);
        if (a > 0 && b > 0) { cached += a + b; ++pieces; }
    }
    printf("NAOMI: music in RAM: %u pieces, %ld bytes; heap left %lu\n", pieces, cached, (unsigned long)heap_left());
    const kthread_attr_t attributes = {.create_detached = true, .stack_size = 8192, .label = "naomi watchdog",
                                       .prio = PRIO_DEFAULT - 1};
    if (!thd_create_ex(&attributes, watchdog, NULL)) printf("NAOMI: no watchdog thread\n");
    return 0;
}
