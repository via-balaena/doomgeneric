#!/bin/bash
# Compile every doomgeneric source for a Cortex-M33, with clang and the shim
# headers in tock/include -- no arm-none-eabi toolchain and no newlib.
#
#   tools/build_arm.sh <objdir>
#
# -mfloat-abi=soft is not optional: it makes the objects match Rust's
# thumbv8m.main-none-eabi, which is soft-float with no FPU. Without it clang
# turns the FPU on for cortex-m33, emits FPv5 instructions, and the objects
# would need the kernel to have enabled the FPU for the process.
#
# No platform object is built. doomgeneric's platform file supplies the six
# DG_ hooks AND a main(), and on Tock both come from the Rust side -- linking
# one in would collide with the runtime rather than merely be unused.
set -u
SRC=$(cd "$(dirname "$0")/../doomgeneric" && pwd)
INC=$(cd "$(dirname "$0")/../tock/include" && pwd)
OBJ=${1:?usage: build_arm.sh <objdir>}
mkdir -p "$OBJ"
# The four limits on the second line are cut from measurement, not taste: the
# renderer's fixed arrays were watched across all 36 Freedoom maps with the
# view sweeping, and the high-water marks were openings 2879 of 20480,
# vissprites 54 of 128, visplanes 93 of 128 and drawsegs 172 of 256. So the
# first two have real slack and are cut with headroom left; THE LAST TWO ARE
# NOT CUT -- 93 of 128 is not spare capacity, and vanilla Doom's 128 is tight
# for a reason. MAX_CAPTURES only feeds -statdump, which needs a filesystem.
#
# CMAP256 keeps the frame 8-bit paletted all the way to the platform, which is
# what the panel path wants and what makes `colors` and `palette_changed`
# non-static so the platform can read them. RESX/RESY are Doom's own frame:
# left at the 640x400 default, I_InitGraphics would build a framebuffer four
# times the size for no gain.
FLAGS="--target=thumbv8m.main-none-eabi -mcpu=cortex-m33 -Os -ffreestanding -mfloat-abi=soft \
       -fno-stack-protector -fno-builtin -DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE \
       -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
       -DMAXOPENINGS=6144 -DBACKUPTICS=16 -DMAXVISSPRITES=96 -DMAX_CAPTURES=1 \
       -DDOOM_TABLES_CONST \
       -Wno-everything -I$INC -I$SRC"
ok=0; bad=0
while read -r b; do
  [ -n "$b" ] || continue
  if clang $FLAGS -c "$SRC/$b.c" -o "$OBJ/$b.o" 2> "$OBJ/$b.err"; then
    ok=$((ok+1))
  else
    bad=$((bad+1)); echo "$b"
  fi
done < <(sed -n 's/^SRC_DOOM = //p' "$SRC/Makefile" | tr ' ' '\n' | sed 's/\.o$//' | sed '/^doomgeneric_xlib$/d')
echo "--- compiled $ok, failed $bad ---"

# The shim, built with the same flags. -fno-builtin matters most here: without
# it clang recognises the body of memcpy and rewrites it into a call to itself.
for f in "$(dirname "$0")"/../tock/shim_*.c; do
  b=$(basename "$f" .c)
  clang $FLAGS -c "$f" -o "$OBJ/$b.o" 2> "$OBJ/$b.err" || { echo "FAILED $b"; cat "$OBJ/$b.err"; exit 1; }
done
echo "--- shim built ---"
