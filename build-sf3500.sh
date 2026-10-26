#!/bin/bash
# Cross-build Hex-a-hop for the Datafrog SF3500 (MIPS32r2, little endian).
#
#   TOOLCHAIN_BIN  directory holding mips-mti-linux-gnu-{gcc,g++}
#   SYSROOT        sysroot with SDL 1.2 and a classic (1.2) SDL_ttf
#
# The binary is src/build/hex-a-hop.mipsel. Add -DHEXAHOP_DEBUG_LOG to
# CXXFLAGS_EXTRA to get a debug.log next to the executable.
set -e
: "${TOOLCHAIN_BIN:?set TOOLCHAIN_BIN}"
: "${SYSROOT:?set SYSROOT}"
SRC="$(cd "$(dirname "$0")" && pwd)/src"
GXX="$TOOLCHAIN_BIN/mips-mti-linux-gnu-g++"
GCC="$TOOLCHAIN_BIN/mips-mti-linux-gnu-gcc"
ARCH="-EL -mips32r2 -mtune=74kc -mhard-float -O2"

CXXFLAGS="--sysroot=$SYSROOT $ARCH -I$SRC -I$SRC/sf3500 -I$SYSROOT/usr/include/SDL \
  -DRELATIVE_PATHS -Dlinux=1 -D_GNU_SOURCE=1 ${CXXFLAGS_EXTRA:-}"
CFLAGS="--sysroot=$SYSROOT $ARCH -I$SRC -DRELATIVE_PATHS -Dlinux=1 -D_GNU_SOURCE=1"

cd "$SRC"
mkdir -p build
for f in gfx hex_puzzzle i18n text sfx; do
  "$GXX" $CXXFLAGS -c "$f.cpp" -o "build/$f.o"
done
for f in system-directory system-relative; do
  "$GCC" $CFLAGS -c "$f.c" -o "build/$f.o"
done
for f in hwdisp hwaudio joykey tfdriver sf3500_mixer stb_vorbis_impl; do
  "$GXX" $CXXFLAGS -c "sf3500/$f.cpp" -o "build/$f.o"
done
"$GXX" --sysroot="$SYSROOT" $ARCH build/*.o -o build/hex-a-hop.mipsel \
  -L"$SYSROOT/usr/lib" -lSDL -lSDL_ttf -lpthread -ldl -lm
file build/hex-a-hop.mipsel
