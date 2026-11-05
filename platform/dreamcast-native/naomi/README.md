# The native port on a SEGA NAOMI / NAOMI 2

`make ... NATIVE_NAOMI=1 naomi` builds the native Dreamcast port (`platform/dreamcast-native`, the game rewritten in C
for the SH-4) as a netboot image for a NAOMI or NAOMI 2 with a NetDIMM. The NAOMI has the Dreamcast's SH-4, PVR2 and
AICA, so the game, its renderer and its sound run unchanged; this directory supplies what the board does not have.

Target hardware it was written for: a Crazy Taxi upright (wheel, accelerator, brake, a two-way shifter, START, 31 kHz
monitor) with a NAOMI 2 multi-BIOS and a 512 MB NetDIMM, netbooted by a WiPi netbooter (a Raspberry Pi). Project
index for that cabinet: https://github.com/0skarzer0/naomi-wheel-games.

## What is different from the Dreamcast build

| Dreamcast | NAOMI (this directory) |
| --- | --- |
| assets from dcload's `/pc/` or the disc's `/cd/` | from `/dimm/`: the asset set appended to the netboot image, read through the cartridge port (`naomi_dimmfs.c`, `naomi_pack.py`) |
| pad on port A (enDjinn, Maple) | the cabinet's JVS I/O board through the MIE, turned into port A's pad (`naomi_input.c`) |
| settings and records on a VMU | in the board's battery-backed SRAM, 0x6000-0x7FFF (`naomi_save.c`) |
| console on dcload or the serial port | a ring of the last 40 lines, shown on the screen after a failure or a hang (`naomi_log.c`) |
| 16 MB of main RAM | 32 MB (the game uses what it needs; nothing is tuned for the extra RAM yet) |

All of it is behind `NM_NAOMI` (`local.cfg.mk`, `NATIVE_NAOMI=1`). The only changes outside this directory are the
build option, the `naomi` target in the `Makefile`, three `#ifdef NM_NAOMI` blocks in `code/main.c` and one line in
`code/audio/sfx.c` (a `/dimm` read holds the CPU like a `/pc` read, so the announcer's phrases take the main-loop path).
The Dreamcast build compiles what it did before.

### Reads from the NetDIMM

The BIOS copies only the program segment of the netboot image into main RAM. `naomi_pack.py` appends an archive after
it (aligned to 64 KB, header `CBDIMMFS`), and `dimmfs_init()` mounts it on `/dimm` as a read-only KOS file system.
Reads are 16-bit PIO through the cartridge port (`0xA05F7000/04/08`, as libnaomi does), interrupts off for 4 KB at a
time. A read holds the CPU, as a dcload `/pc/` read does, so the build takes the port's non-disc paths: the scene
loader's reads run inside the idle-wait slices (`scene_loader.c`), the music and the announcer's phrases are read a
chunk at a time in the main loop's waits. Not `NATIVE_DISC`: that path hands reads to a worker thread because a GD-ROM
read sleeps on a DMA; a PIO read would not give the CPU back.

### Controls (Crazy Taxi upright)

| Cabinet | Game (port A's pad) |
| --- | --- |
| wheel | stick X, read linearly (`NATIVE_STEER_CURVE=1.0`, `NATIVE_STEER_DEADZONE=2` by default with `NATIVE_NAOMI=1`) |
| accelerator / brake | right / left trigger |
| shifter forward / back | A / X: shift up / down (manual transmission) |
| START | START (in the attract: a credit and the game start) |
| START + accelerator | Y: the next view (VR1, VR2, VR3, VR4) |
| START + accelerator + brake | the operator settings (START with both triggers, as on a pad) |
| coin chute, SERVICE | A for four frames: a credit in the attract and the title |

The raw JVS channels are mapped here, not by KOS's `mie_default_cont_map`, which without a calibration run reads a
pedal at rest as a fifth pressed. The ranges start from the values measured on the Crazy Taxi cabinet (wheel
8448 / 33792 / 60160, accelerator 11776-47104, brake 14080-47360) and widen by themselves when a control goes further.
A Daytona twin or deluxe cabinet wires a four-position shifter and four VR buttons: map them in `read_cabinet()`.

Known limits of the mapping (first version, not tried on the cabinet yet):
- A coin during a manual race shifts up once (A is the coin's pulse and the shift up).
- The port has no credit counter of its own: START in the attract takes a credit and starts (as the native port does
  on a Dreamcast). The project's rule for the other games of the cabinet (START + brake for a credit besides the coin,
  no free play) is not implemented here yet.
- TEST does nothing (the arcade's test mode is not part of the native port).

### Save file in the SRAM

The SRAM (32 KB at `0x00200000`) is shared with the cabinet's other homebrew: Cannonball 0x0000-0x0FFF, the recompiled
Daytona (`platform/dreamcast` on the `naomi` branch) 0x1000-0x5FFF, this port 0x6000-0x7FFF (`"DTN1"`, size, checksum,
then the same file the Dreamcast writes to a VMU). The magic is cleared while writing and written last.

### Console and failures

No serial cable on a cabinet, and KOS's SCIF output would wait on the FIFO at 115200 baud. The console goes to a ring of
lines (`naomi-ring` dbgio device). The lines are shown on a blue screen when:
- the image has no `/dimm` archive (`NO ASSETS IN THE NETDIMM IMAGE`);
- `nm_practice_run` returns (`FAILED`, `THE GAME ENDED`);
- no frame has been rendered for 15 s (90 s before the first frame): `HANG`, with the main thread's pc and pr for
  `sh-elf-addr2line -e daytona-native-naomi*.elf`.

## Building

Two KallistiOS trees at the port's pin (`368a2eb9`): the Dreamcast one for the recipe's tools and assets, a second one
built with `KOS_SUBARCH=naomi` for the NAOMI ELF (its `environ.sh` sourced after enDjinn's, which sources
`/opt/toolchains/dc/kos/environ.sh`).

```sh
# 1. the assets and the Dreamcast ELFs, as in ../README.md (the recipe needs the Dreamcast KOS)
zsh platform/dreamcast-native/recipes/build_assets.sh roms/daytona93.zip build/my-assets \
    --music ~/Downloads/Daytona_USA_\(Sega_Model_2\).zip
# 2. the NAOMI ELF and the netboot image against those assets
source extern/endjinn/environ.sh
source /opt/toolchains/dc/kos-naomi/environ.sh      # KOS_SUBARCH=naomi
make -C platform/dreamcast-native $(grep -v '^#' build/my-assets/make-vars.txt) NATIVE_NAOMI=1 \
    ENJ_BUILDDIR=build-naomi naomi
```

The image is `platform/dreamcast-native/bin/daytona-native-naomi-....bin.gz`; the asset set (`assets-ssfar4f`) is
inside it. Copy it to the WiPi netbooter's games folder and add a line to its `romsinfo.csv`.

## Status

See `HANDOFF.md` in this directory: what was built, what was measured on the cabinet, what failed.
