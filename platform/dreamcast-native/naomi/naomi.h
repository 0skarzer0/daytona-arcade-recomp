#ifndef DAYTONA_NATIVE_NAOMI_H
#define DAYTONA_NATIVE_NAOMI_H
/* SEGA NAOMI / NAOMI 2 with a NetDIMM (make NATIVE_NAOMI=1, KallistiOS built with KOS_SUBARCH=naomi). The board has
 * the Dreamcast's SH-4, PVR2 and AICA and 32 MB of main RAM; what it does not have is what this directory supplies
 * (naomi/README.md):
 *   - no disc and no dcload: the asset set is appended to the netboot image and read from the NetDIMM through the
 *     cartridge port, mounted at /dimm (naomi_dimmfs.c, naomi_pack.py);
 *   - no pad: the cabinet's JVS I/O board through the MIE, turned into port A's controller state at enDjinn's
 *     refresh (naomi_input.c, link-time wraps as tests/pad_monkey.c does);
 *   - no VMU: the save file in the board's battery-backed SRAM (naomi_save.c);
 *   - no serial cable on a cabinet: the console lines kept in a ring and shown on the screen after a failure or a
 *     hang (naomi_log.c).
 * Nothing here is compiled into the Dreamcast build. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* main(), first thing: the log ring as the console, /dimm mounted, the SRAM save device, the watchdog. Returns 0, or
 * -1 when the NetDIMM holds no asset archive (the log is shown then and this does not return). */
int nm_naomi_boot(void);

/* The PVR is up (after enj_state_startup): a failure screen has to shut it down first. */
void nm_naomi_pvr_up(void);

/* Show the kept console lines on the screen, for good. */
void nm_naomi_show_log(const char *why) __attribute__((noreturn));

/* TEST held on the cabinet: the console's sound, file and failure lines on the screen, for good (a diagnosis). */
void nm_naomi_show_sound_log(void) __attribute__((noreturn));

/* Live figures for a PC to peek at through the NetDIMM (naomi_dimmcomms.c; README "Reading the board"). All
 * 32-bit words; the block's address is printed in the console and given by sh-elf-nm (`_nm_naomi_live`). */
typedef struct {
    uint32_t magic;          /* +0x00 "NEST" (0x5453454e) */
    uint32_t version;        /* +0x04 1 */
    uint32_t seconds;        /* +0x08 seconds counted (the figures below are of the last one) */
    uint32_t fps;            /* +0x0c frames rendered in the last second */
    uint32_t vbls;           /* +0x10 vertical blanks in the last second (60 at 31 kHz) */
    uint32_t fps_worst;      /* +0x14 worst second since boot (poke 0 to start over) */
    uint32_t frames_total;   /* +0x18 frames rendered since the PVR started */
    uint32_t frame_us;       /* +0x1c last frame's time, vblank to vblank (KOS pvr_stats) */
    uint32_t render_us;      /* +0x20 last render's time in the PVR */
    uint32_t dimm_requests;  /* +0x24 peeks and pokes answered */
    uint32_t log_lines;      /* +0x28 address of the console ring: log_count lines of log_width bytes */
    uint32_t log_count;      /* +0x2c lines in the ring */
    uint32_t log_width;      /* +0x30 bytes a line (NUL-terminated) */
    uint32_t log_next;       /* +0x34 address of the number of lines written so far (index % log_count) */
    uint32_t heap_left;      /* +0x38 bytes above the heap's top at boot, after the music copies */
} nm_naomi_live_t;
extern volatile nm_naomi_live_t nm_naomi_live;

/* After the boot: the thread that answers the NetDIMM's peeks and refreshes nm_naomi_live. */
void nm_naomi_dimmcomms_start(void);

/* The SRAM save device (nm_save_device_t of save_store.h). */
const void *nm_naomi_save_device(void);

#ifdef __cplusplus
}
#endif
#endif
