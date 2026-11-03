# The Dreamcast port on a SEGA NAOMI

The Dreamcast build also runs on a NAOMI / NAOMI 2 with a NetDIMM, netbooted
(tested with a WiPi netbooter on a Crazy Taxi upright, NAOMI 2 multi-BIOS,
512 MB NetDIMM, 31 kHz monitor). The NAOMI has the Dreamcast's SH-4, PVR2 and
AICA, so the figures and the hardware findings below hold for a Dreamcast too.

## What changes on the NAOMI

All of it is behind `_arch_sub_naomi` (KallistiOS `KOS_SUBARCH=naomi`);
the Dreamcast build is unchanged.

- **No disc:** the ROM images, the sound pack and the default NVRAM are
  appended to the netboot image and read from the NetDIMM through the
  cartridge port (`game/naomi_dimmfs.c`, mounted on `/dimm`).
- **32 MB of main RAM:** the ROM page cache is 12 MB (3,072 pages) instead
  of 2 MB.
- **Controls:** the cabinet's JVS I/O board through the MIE (`game/naomi.h`):
  wheel, pedals, a two-way shifter (forward = shift up, back = shift down),
  START, ACCELERATOR + START for the next view, TEST / SERVICE, coin.
- **Settings and records:** the EEPROM and backup RAM are kept in the board's
  battery-backed SRAM (offset 0x1000, saved every 5 s when they change).
- **Default NVRAM:** single cabinet, upright, export, free play (made with
  `m2run --save-nvram` from a scripted test-mode run). The factory Rev. A
  NVRAM is a twin/master cabinet and stops at NETWORK CHECKING.
- **Texture uploads without DMA** (`renderer.h`): `pvr_txr_load_dma` never
  completed on the NAOMI and the first frame hung in the wait.
- **No log on the screen once the PVR is up:** the last 32 log lines are
  kept and shown only on a failure or a hang (no serial port on a cabinet).

## Building

From `platform/dreamcast`, with the KallistiOS environment for the NAOMI
(`KOS_SUBARCH=naomi`) and the desktop build's output in `$H`:

    make OUT=$H/naomi GEN=$H/gen GEN_I960=$H/dreamcast/gen_fast/daytona \
         GEN_TGP=$H/dreamcast/gen_fast/daytona_tgp NATIVE_GEO=1 game
    naomibintool build prog.bin -t "DAYTONA USA RECOMP" -d "HOMEBREW" -e $H/naomi/game.elf
    python3 naomi_pack.py prog.bin <dimm folder> daytona_naomi.bin
    gzip -9 daytona_naomi.bin

The dimm folder holds `rom/` as on the Dreamcast disc: the importer's images,
`sound.pak`, `ioboard_eeprom.bin` and `backup_ram.bin` (about 78 MB). The
WiPi netbooter wants the `.bin.gz`.

## Measured on the hardware

Attract mode and a race, `kDrawEvery` 2, from the PROFILE lines:

| ms per game frame        |      |
|--------------------------|-----:|
| core (i960)              | 19.2 |
| geometry (TGP)           |  7.1 |
| video                    |  5.4 |
| draw (9.3 per drawn frame) | 1.7 |
| ROM reads                | 0.97 |
| sound                    | 0.22 |
| printing                 | 1.91 |
| **total**                | **37.2** |

Draw per drawn frame: sort+wait 3.5, materials 2.6, polygons 3.2, layer
upload wait 0.0. Frame wait skipped 77% of the i960's instructions.

About 27 game frames a second against the arcade's 57.5: the same speed as in
Flycast (27-32), i.e. the CPU, not the disc (ROM reads are under 1 ms with
the 12 MB cache). The printing cost went away with the on-screen log.

## Hardware findings

- **Texture RAM must not take byte writes.** The game's texture RAM lives in
  video RAM (`main.cpp`), and `M2Board::tex_write` wrote it a byte at a time.
  Flycast accepts that; the hardware does not, and every texture showed as a
  regular grid of wrong texels. One 16-bit store fixes it (`m2_board.cpp`,
  under `M2_DC_MEMORY`). This applies to a real Dreamcast as well.
- **PVR DMA for the tile layers:** it did not finish on the NAOMI (KallistiOS
  naomi subarch); `pvr_txr_load` (store queues) is used there. Not measured
  on a Dreamcast.
- Source textures are now mipmapped (square, short side repeated, point-
  sampled levels). This was tried first for the grid above, which it did not
  cause; it does help the far shimmer. Not measured on a Dreamcast.
