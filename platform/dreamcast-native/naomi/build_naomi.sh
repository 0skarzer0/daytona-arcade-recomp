#!/usr/bin/env zsh
# The NAOMI netboot image of the native port, against an output directory of recipes/build_assets.sh.
#
#   zsh platform/dreamcast-native/naomi/build_naomi.sh OUTDIR [make variables...]
#
# The ELF has the options of the recipe's "play" ELF (the game from the attract: NATIVE_RESIDENT_CAR=1,
# NATIVE_FRAME_PROFILE=1, NATIVE_HANDOPT=1, AUTO_START=attract) plus the announcer's streamed phrases
# (NATIVE_SFX_VOICE_STREAM=1: without it only THREE, TWO, ONE, GO speak), a line a music request, start and end
# in the console (NATIVE_SOUND_LOG=1: TEST shows them) and NATIVE_NAOMI=1, and is built with the
# KallistiOS tree for KOS_SUBARCH=naomi (KOS_NAOMI, default /opt/toolchains/dc/kos-naomi). Output:
# OUTDIR/elf/naomi/bin/daytona-native-...-naomi-....bin.gz (the image for the WiPi netbooter) and the ELF beside it
# (sh-elf-addr2line for the pc / pr of a HANG screen).
set -e
OUT=${1:?usage: build_naomi.sh OUTDIR [make variables...]}; shift
OUT=${OUT:A}
REPO=${0:A:h:h:h:h}
ENDJINN_ROOT=${ENDJINN_ROOT:-$REPO/extern/endjinn}
KOS_NAOMI=${KOS_NAOMI:-/opt/toolchains/dc/kos-naomi}
[[ -f $OUT/make-vars.txt ]] || { print -u2 -r -- "$OUT/make-vars.txt missing: run recipes/build_assets.sh first"; exit 2; }
source $ENDJINN_ROOT/environ.sh
source $KOS_NAOMI/environ.sh
set -u
[[ $KOS_SUBARCH == naomi ]] || { print -u2 -r -- "$KOS_NAOMI/environ.sh does not set KOS_SUBARCH=naomi"; exit 2; }
vars=(${(f)"$(grep -v '^#' $OUT/make-vars.txt)"})
d=$OUT/elf/naomi
rm -rf $d/obj; mkdir -p $d
make -C $REPO/platform/dreamcast-native ENDJINN_ROOT=$ENDJINN_ROOT $vars NATIVE_RESIDENT_CAR=1 NATIVE_FRAME_PROFILE=1 \
  NATIVE_HANDOPT=1 RACE_TICK_LIMIT=0 AUTO_START=attract NATIVE_SFX_VOICE_STREAM=1 NATIVE_SOUND_LOG=1 NATIVE_NAOMI=1 ENJ_BUILDDIR=$d/obj ENJ_BINDIR=$d/bin \
  -j${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)} "$@" naomi
