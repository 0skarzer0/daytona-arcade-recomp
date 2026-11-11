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

## Second run (2026-11-06, 01:00)

Voices on, select voices too. New: **no race music**; a lag before GAME OVER, the horns of the course and the
transmission select and GENTLEMEN START YOUR ENGINES; the transmission screen's two turning cars missing (an upstream
gap: README "Not native yet", not a NAOMI fault). Diagnosis without a log (it was not visible):
- The lag: the phrases were read from `/dimm` in the main loop's idle waits, a chunk a frame, paced by dcload's costs.
- The race music: the race piece t03 leaves 1,146 bytes beside the race bank and the largest race phrase, so it
  loads only when no phrase holds AICA RAM (and the free space is not split by one). A late GENTLEMEN still playing
  when t03 loads makes `sound.c` LOAD_ROOM fall back to an ADPCM copy that exists only for t04 and t05: silence.
Third image:
- `voices.pak` (0.9 MB) copied into main RAM at boot (`dimmfs_cache`, `naomi_log.c`), read by sfx.c's worker thread
  (`/dimm` is no longer a main-loop path in `sfx.c`: upstream's line restored).
- `sound.c` LOAD_ROOM waits while a phrase holds AICA RAM (`nm_sfx_voice_in_aica`, audio/sfx.h) instead of giving
  up on the piece. Host tests `sound-check`, `sfx-check`, `sfx-voices-check` pass (clang; GCC rejects a scanf
  format in test_sfx.c, an upstream warning).
- TEST held 3 s: the console's sound and file lines on the screen (ring of 160 lines), to photograph.

## Third run (2026-11-06, 02:00)

Beginner's race music (t03) still silent; Advanced and Expert play. The GAME OVER jingle starts late. So t03 in PCM8
does not fit on the NAOMI even with no phrase playing (Advanced and Expert never do: they always load their ADPCM
copies). Cause not known yet (on a Dreamcast it fits with 25,050 bytes to spare): `sound.c` now prints a line with
the AICA free bytes and largest block when a piece falls back (TEST held 3 s shows it). Fourth image:
- an ADPCM copy of t03 too (`recipes/build_assets.sh` step `music_adpcm`: t03, t04, t05), the same fallback as
  Advanced and Expert;
- the pieces that must start on cue (t02, t07..t16: game start, finish, podium, GAME OVER, victory lane, 6.8 MB)
  copied into main RAM at boot like `voices.pak`.
To read on the cabinet: the fallback line's numbers (TEST after a Beginner race starts), to find what holds the AICA
RAM or how it is split.

## Fourth run (2026-11-06, 02:30)

A HANG screen at a Beginner race start (main thread in `genwait_wait`), no fallback line in the log. Cause: the
third image's wait in `sound.c` LOAD_ROOM for a phrase to leave the AICA RAM. `nm_sound_settle` (select_mode.c, before
the race) loops on the music's load with `nm_sound_update` alone, so the effects (and the phrase) never advance there:
the wait never ends, settle blocks (its loop calls `load_wait` up to 4000 times). Fifth image: the wait is skipped
while settling and bounded to 3 s otherwise; then the fallback line (with "a phrase holds AICA RAM" when one does) and
the piece's ADPCM copy. Lesson: never wait in the loader on something only `nm_sfx_update` advances.
Still unknown: why t03 does not fit in PCM8 on the NAOMI; read the fallback line under TEST in a Beginner race.

## Fifth run (2026-11-06, 03:50)

No hang; Beginner still silent, and TEST shows no `Sound:` line about track 03 at all: no "does not fit", no load
error, no start failure. So the load is not refused: the likely cause is that it never finishes. t03 is the largest
piece loaded in PCM8 (1.56 MB), read from `/dimm` a chunk at a time in the idle waits, and `sound.c loading()` drops
a load silently when the game wants another piece before it ends ("a change of mind"). Sixth image:
- the music in main RAM at boot within an 8 MB reserve for the game (`naomi_log.c`, `dimmfs_size`, `heap_left`):
  the race pieces first (t03, the ADPCM copies of t03/t04/t05, t06), the attract's t01, then the cued pieces;
- `NATIVE_SOUND_LOG=1` in `build_naomi.sh`: a console line for every music request, start and end ("Sound log",
  shown by TEST), to confirm the cause if Beginner is still silent.

## NetDIMM peeks (2026-11-11)

The owner asked to measure on the board through the NetDIMM (as dc2naomi does: Raspberry 192.168.68.60, NetDIMM
10.0.0.4, credentials only in the chat). A KallistiOS program does not answer peeks, so: `naomi_dimmcomms.c` (a 2 ms
polling thread, under the cartridge port's lock) and `nm_naomi_live` + `live.py` (fps, worst second, frame / render
times, the console ring). Image built, not yet on the board; `_nm_naomi_live` at 0x8c173164 in it. First things to
measure: fps in a full race start on each course (the anti-aliasing question: is there headroom?), Beginner's music
log (`live.py ... log`).

## Measured on the cabinet through the NetDIMM (2026-11-11, 01:20)

First run read live from the Raspberry (`live.py`, peeks answered by `naomi_dimmcomms.c`): it works, and both open
questions are answered.

**Beginner's music plays.** The log of a Beginner race (course 0, automatic):

    Sound log 261202 ms: want 3 (state 2, resident 2)
    Sound: track 03 does not fit in PCM8 (bank 1307072, with flow 1561760; AICA free 1572512, largest b...
    Sound log 262064 ms: play 3 (state 1, resident 3)

So t03's PCM8 bank (1,307,072 bytes) is larger than the **largest free block**, though the free bytes (1,572,512)
would hold the whole piece (1,561,760): the AICA arena is **fragmented**, not full. The streamed phrases allocate
and free their samples in it (the select screens' ones among them, added here on 2026-11-06), and what is left is
split. Upstream never meets this because Advanced and Expert always take their ADPCM copies and Beginner's PCM8 is
loaded before much has been streamed. The fallback added here (an ADPCM copy of t03) is what makes it play, 862 ms
after the request. The earlier silence was this check falling back to a copy that did not exist.
A better fix, for the day someone wants Beginner's music in PCM8 again: a fixed AICA slot for the streamed phrases
(reserved once, at one end of the arena) instead of an allocation a phrase.

**60 fps, with about 6 ms a frame to spare.** `live.py ... fps`: 60 fps (59 when a second straddles a vblank) in
the attract and in a race on course 0, frame 16.67 ms throughout. Rendering: 5.6-9.4 ms in the attract,
7.7-11.3 ms in a race. `worst` 10-11 fps only at boot, while the music is copied into RAM.
So **the anti-aliasing the owner asked about would not hold**: it doubles the pixels to fill, which puts rendering
at 15-22 ms against a 16.67 ms budget — over it at the peaks of a race. 16.3 MB of main RAM and about 10.8 MB of
video RAM are free (KOS sees the NAOMI's 16 MB of video RAM, not a Dreamcast's 8), so the room is in memory, not in
fill rate.

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
