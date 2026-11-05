# NAOMI build of the native port: handoff

Branch `naomi-native` of 0skarzer0/daytona-arcade-recomp, from alphanu1/daytona-arcade-recomp main `5e81251` (PR #58,
dfchil's `platform/dreamcast-native`). What this directory is and how to build it: `README.md` here. Index of the
cabinet's projects: https://github.com/0skarzer0/naomi-wheel-games.

## Why (2026-11-05)

Issue #62 on alphanu1/daytona-arcade-recomp reported the recompiled port (`platform/dreamcast`, branch `naomi`) on a
NAOMI 2: about 27 frames/s, the i960's code alone 19 ms a frame, the CPU the limit. alphanu1 answered: "New native
SH-4 port will be committed soon. Please test that". It was merged the same minute as PR #58: the game rewritten in
C, bit-exact to the recompiled game, 60 frames/s on a Dreamcast. This branch is that port on the NAOMI.

## State (2026-11-05, night)

Built, **not yet run on the cabinet**. First image: `daytona_native_naomi.bin.gz`, 22.6 MB (46.7 MB unpacked:
program 1.39 MB, `/dimm` archive 45.3 MB, 387 files of `assets-ssfar4f` with music and effects).
sha256 of the .bin.gz: d07b861948d59f908470e9f285450bc3ce9d41e403a9d354f0d74ff823b95d1e (with that night's asset
set; the assets are ROM-derived and are never committed).

Checked here, without the hardware:
- The recipe on the user's ROM set: every lockstep check passes (courses' finishes, attract over four loops, the
  ten scripted games of `games_lockstep`: native = recompiled game on every row). Asset bytes against
  `recipes/expected.sha256`: 2,715 identical, 22 different, the variants not built. The 22 are four presentation
  files in every set: `world.dnm`, `course1-world.dnm`, `course1-actors.dna` and `headers/hud/hud_data.h`. Probably
  float rounding of the Python/numpy converters on Linux x86-64 against the author's macOS arm64; not investigated.
  Gameplay data (`*.dnc`, `*.dnr`, `*.dnp`, `drive.dnd`, the scripts) are identical.
- The ELF: only the two wrappers call `enj_ctrl_map_states` / `enj_ctrl_get_states` (the LTO unit did not swallow
  the `--wrap`), the MIE driver and `/dimm` are linked, the ROM header loads and enters at 0x8c020000.

## First run on the cabinet (2026-11-06)

First image on the Crazy Taxi upright: controls, graphics and music right ("perfect", the owner). Missing: the
announcer's voices (course select, MANUAL / AUTOMATIC, GENTLEMEN START YOUR ENGINES, TIME EXTENDED). Two causes:
- The recipe's "play" ELF, whose options the first image copied, has no `NATIVE_SFX_VOICE_STREAM=1`: only the
  countdown (in the race bank) spoke. `build_naomi.sh` now sets it.
- The select screens' voices (`include/select.h`: ae215a..ae2160, ae2112/ae2113, ae2173, ae221c) were in no bank of
  `recipes/sfx-banks.txt` at all (on a Dreamcast either). New line `select-voices`: the same streamed `voices.pak`
  (63 phrases instead of 50), and `build_sfx.sh` checks their fit beside the select screens' music t01 instead of the
  race music t03 (the AICA allocation is per phrase while it plays, `sfx.c voice_upload_begin`): the largest,
  32,832 bytes, leaves 528,561. The race phrases are as before (largest 23,904, 1,146 left beside t03); `race.afb` and
  `race.dsx` unchanged; `voices.pak` and `banks.tsv` now differ from `sfx-expected.sha256` by design.
Second image (with both): to test. A candidate change for upstream too (the select voices on a Dreamcast).

## What to test on the cabinet, in this order

1. Boot: the attract (cinematic, V.R. demo, title) at 60 frames/s, with music. A blue screen instead means the log
   explains it (`NO ASSETS...`, `FAILED`, `HANG` with pc/pr: `sh-elf-addr2line -e daytona_native_naomi.elf <pc> <pr>`).
2. Sound: music and effects. If silent: AICAflow's ARM7 boot report by interrupt did not arrive, or the G2 DMA of
   the sample uploads (`afx_sample_bank_stream_dma_begin`, `spu_dma_transfer`) does not complete on the NAOMI, as the
   PVR's texture DMA did not for the recompiled port. Plan B for the second: uploads by store queues or PIO.
3. Controls: START (credit and start), wheel (full lock at both ends? centre straight?), pedals (0 at rest, full at
   full press), shifter (manual: forward up, back down), START + accelerator (views), START + both pedals (settings).
4. Title stalls while the attract loads the next course (79-228 ms on a Dreamcast from dcload): `/dimm` reads are PIO
   through the cartridge port, so they may differ.
5. Settings and records kept after a power cycle (SRAM 0x6000-0x7FFF; Cannonball and the recompiled Daytona keep
   theirs).

## Environment notes (for whoever builds this next, person or AI)

- The cloud workspace this was built in blocks the GNU mirrors and sourceware: the sh-elf toolchain (binutils 2.45,
  GCC 15.2.0, newlib 4.6.0.20260123, KOS's kos-chain `stable` profile) was built from Ubuntu's archive tarballs
  (archive.ubuntu.com pool: `binutils_2.45.orig.tar.xz`, `gcc-15_15.2.0.orig.tar.gz` holding `gcc-15.2.0.tar.xz`,
  `newlib_4.6.0.20260123.orig.tar.xz`), the GCC prerequisites from the system (`gcc_prereqs_script=true`,
  libgmp/libmpfr/libmpc/libisl -dev), `config.guess` from the GCC tarball, `makejobs=3` (a number, not `-j3`).
- KOS at the port's pin `368a2eb9` twice: `/opt/toolchains/dc/kos` (Dreamcast: the recipe, pvrtex) and
  `/opt/toolchains/dc/kos-naomi` (`KOS_SUBARCH=naomi`, `KOS_BASE` changed in its environ.sh). Copy the tree and edit
  its environ.sh before any `make clean` there (sourcing the copied file first would clean the original).
- kos-ports zlib (the port's material packs need it): zlib.net is blocked, the 1.3.2 tarball made from
  github.com/madler/zlib tag v1.3.2 into `kos-ports/zlib/dist/`, its `distinfo` moved away.
- The host tools: Clang (GCC refuses `m2segment`, as `../README.md` says), libclang-rt-18-dev (AICAflow's tools build
  with ASan), SDL3's X11 dev packages, `scripts/setup.py --no-build` for `extern/`.
- `games_lockstep`'s comparison (`tests/test_games_lockstep.py`) needs more than 8 GB: killed by the kernel with an
  empty step log until 8 GB of swap were added.
- The ROM set: MAME's `daytona93` alone lacks the sound program `epr-16720.7` and `epr-16721.8`, which MAME keeps in
  the parent set `daytona`; `build_sfx.sh` then fails. A zip with both added from `daytona.zip` builds byte-identical
  effects (sfx-expected.sha256: 5 of 5).

## Not done

- A credit counter and the cabinet's rule for the other games (START + brake = a credit, besides the coin; no free
  play). The native port takes a credit with START in the attract.
- TEST / the arcade's test mode (not part of the native port).
- Use of the NAOMI's extra 16 MB (larger caches, all courses resident).
- Upstreaming: once it runs on the cabinet, offer `NATIVE_NAOMI` to alphanu1 / dfchil (a PR from this branch).
