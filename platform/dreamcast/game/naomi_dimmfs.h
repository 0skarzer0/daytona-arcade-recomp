/***************************************************************************
    NAOMI: read-only files stored in the NetDIMM (or cartridge) memory.

    The program the BIOS copies into the 32 MB of main RAM is only the first
    part of the netboot image. naomi/pack_bin.py appends an archive after it
    (aligned to 64 KB): big files (the music) stay there and are read on
    demand through the cartridge port, like a Dreamcast reads its GD-ROM.

    dimmfs_init() finds the archive and mounts it at /dimm.
***************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Find the archive after the program and mount it at /dimm.
   Returns the number of files, or -1 if there is no archive. */
int dimmfs_init(void);

/* Non-zero once /dimm is mounted */
int dimmfs_mounted(void);

#ifdef __cplusplus
}
#endif
