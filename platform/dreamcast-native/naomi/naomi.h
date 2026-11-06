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

/* The SRAM save device (nm_save_device_t of save_store.h). */
const void *nm_naomi_save_device(void);

#ifdef __cplusplus
}
#endif
#endif
