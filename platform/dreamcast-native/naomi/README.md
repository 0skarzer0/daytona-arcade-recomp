# The native port on a SEGA NAOMI / NAOMI 2

`make ... NATIVE_NAOMI=1 naomi` builds the native Dreamcast port (`platform/dreamcast-native`, the game rewritten in C
for the SH-4, by dfchil and alphanu1: see [Acknowledgements](#acknowledgements)) as a netboot image for a NAOMI or NAOMI 2 with a NetDIMM. The NAOMI has the Dreamcast's SH-4, PVR2 and
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

All of it is behind `NM_NAOMI` (`local.cfg.mk`, `NATIVE_NAOMI=1`). Outside this directory: the build option in
`local.cfg.mk` (which also defines `__DREAMCAST__`, the port's test for "the SH-4 console": KOS defines `__NAOMI__`
on a NAOMI instead), the `naomi` target in the `Makefile`, three `#ifdef NM_NAOMI` blocks in `code/main.c`, the save
device in `code/save/game_save.c`, the select screens' voices in `recipes/sfx-banks.txt` (line `select-voices`,
checked by `recipes/build_sfx.sh` beside the select music; they serve the Dreamcast build too), and one line in `code/audio/sfx.c` (a `/dimm` read holds the CPU like a `/pc`
read, so the announcer's phrases take the main-loop path). The Dreamcast build compiles what it did before.

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
| TEST held 3 s | the console's sound and file lines on the screen (diagnosis; the game stops: power cycle) |

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

## Reading the board while it runs (NetDIMM peeks)

`naomi_dimmcomms.c` answers the NetDIMM's peek / poke requests (libnaomi's protocol, as dc2naomi's resident does),
so the netbooter Raspberry can read the board's memory while the game runs. On top of it, `nm_naomi_live` is a block
of live figures refreshed once a second (layout in `naomi.h`): frames rendered in the last second, the worst second,
frame and render times, and the address of the console's ring. Its address is in the console's line
"NAOMI: NetDIMM peek/poke on; live figures at 0x8c......" and in `sh-elf-nm ELF | grep _nm_naomi_live`.

On the Raspberry (DragonMinded's `netdimm.py` in `/sbin/piforce/dm_netboot`, as on the WiPi):

```sh
python3 live.py 10.0.0.4 0x8c173164 fps 60    # one line a second: fps, worst second, frame / render ms
python3 live.py 10.0.0.4 0x8c173164 log       # the whole console ring (what TEST shows, unfiltered)
python3 live.py 10.0.0.4 0x8c173164 reset     # start the worst second over
```

(0x8c173164 is the address in the build of 2026-11-11; it moves with every build.) Any other address of the RAM can
be read with 0skarzer0/dc2naomi's `tools/board/pk.py`.

Sending an image to the board from the Raspberry, without touching the WiPi's menu:

```sh
pkill -f timehack.py                                    # it keeps the NetDIMM alive; it holds the link meanwhile
cd /sbin/piforce/dm_netboot && python3 netdimm_send 10.0.0.4 /boot/roms/IMAGE.bin.gz --disable-crc
nohup python3 /sbin/piforce/timehack.py 10.0.0.4 &      # back on, or the board resets with "error 33"
```

## Building

Two KallistiOS trees at the port's pin (`368a2eb9`): `/opt/toolchains/dc/kos` for the Dreamcast (the recipe and its
tools) and `/opt/toolchains/dc/kos-naomi`, a copy with `KOS_SUBARCH=naomi` and its own `KOS_BASE` in `environ.sh`,
for the NAOMI ELF. zlib from kos-ports for both. The ROM set must hold the sound program: MAME's `daytona93` alone
lacks `epr-16720.7` and `epr-16721.8` (they are in the parent set `daytona`); add them to the zip.

```sh
# 1. the assets, as in ../README.md
zsh platform/dreamcast-native/recipes/build_assets.sh roms/daytona93.zip build/my-assets \
    --music ~/Downloads/Daytona_USA_\(Sega_Model_2\).zip
# 2. the NAOMI ELF and its netboot image against them (options of the recipe's "play" ELF + NATIVE_NAOMI=1)
zsh platform/dreamcast-native/naomi/build_naomi.sh build/my-assets
```

Output: `build/my-assets/elf/naomi/bin/daytona-native-...-naomi.bin.gz` (the asset set `assets-ssfar4f` inside it: a
ROM-derived file, not for sharing) and the `.elf` beside it. Copy the `.bin.gz` to the WiPi netbooter and add its line
to `romsinfo.csv`. `HANDOFF.md` has the environment notes (toolchain without the GNU mirrors, Clang for the host
tools, memory for the lockstep checks).

## Status

See `HANDOFF.md` in this directory: what was built, what was measured on the cabinet, what failed.

## Acknowledgements

This directory only adapts other people's work to a different board; everything below made it possible, directly or as
inspiration. Licences and pinned versions are in [`../THIRD_PARTY.md`](../THIRD_PARTY.md).

- **alphanu1**, author of [daytona-arcade-recomp](https://github.com/alphanu1/daytona-arcade-recomp): the project this
  branch lives in, the Model 2 recompilation that the native port is checked against bit for bit, and the answer to
  issue #62 that pointed at the native port.
- **dfchil**, author of the native SH-4 port this branch builds (PR #58) and of
  [enDjinn](https://github.com/dfchil/enDjinn), [AICAflow](https://github.com/dfchil/aicaflow) and
  [AICAforge](https://github.com/dfchil/AICAforge), which the port's rendering, input and sound run on. The NAOMI build
  changes almost nothing of it: it is their game, running on another board.
- **DragonMinded**: [libnaomi](https://github.com/DragonMinded/libnaomi), whose cartridge-port reads
  (`naomi_dimmfs.c`) and NetDIMM peek / poke protocol (`naomi_dimmcomms.c`) this follows, and the NetDIMM tools
  (`netdimm.py`, `netdimm_send`) that load the image and that `live.py` uses to read the board while the game runs.
- **The KallistiOS team and contributors**: KallistiOS, its NAOMI subarchitecture (`KOS_SUBARCH=naomi`),
  `naomibintool` (the ROM header of the image) and `kos-chain`, without which there is no ELF to boot.
- **The GNU binutils, GCC and newlib projects**, which make up the `sh-elf` toolchain built for this branch.
- **gyrovorbis**, author of [SH4ZAM](https://github.com/gyrovorbis/sh4zam), linked into the port.
- **The vgmrips community**, whose Daytona USA (Sega Model 2) music pack is what the port plays.
- **The Raspberry Pi netbooter** that boots the board: the WiPi Netbooter (its `/sbin/piforce` tools) and the Piforce /
  [Piforce-Web](https://github.com/dev-tty0/Piforce-Web) line of work, which builds on debugmode's Triforce tools.
  Without it nothing here could be tried on the cabinet.
- **Cannonball** (Chris White) and its Dreamcast / NAOMI port, with which this port shares the board's SRAM.
- **[0skarzer0/dc2naomi](https://github.com/0skarzer0/dc2naomi)**, whose resident and `tools/board/pk.py` found the
  NetDIMM's quirks that `naomi_dimmcomms.c` takes into account.
- **Claude** (Anthropic), through Claude Code: the NAOMI adaptation in this directory (the `/dimm` file system, the JVS
  controls, the SRAM save, the console and hang screen, the music and voice fixes, the live figures) was written and
  debugged by Claude in sessions with 0skarzer0, who owns the cabinet, ran every image on it, and sent the photos and
  measurements that every diagnosis here came from.

If you contributed to something that is used here and are not named, please open an issue or a pull request.
