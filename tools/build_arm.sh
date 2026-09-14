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
set -u
SRC=$(cd "$(dirname "$0")/../doomgeneric" && pwd)
INC=$(cd "$(dirname "$0")/../tock/include" && pwd)
OBJ=${1:?usage: build_arm.sh <objdir>}
mkdir -p "$OBJ"
FLAGS="--target=thumbv8m.main-none-eabi -mcpu=cortex-m33 -Os -ffreestanding -mfloat-abi=soft \
       -fno-stack-protector -fno-builtin -DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE \
       -Wno-everything -I$INC -I$SRC"
ok=0; bad=0
while read -r b; do
  [ -n "$b" ] || continue
  if clang $FLAGS -c "$SRC/$b.c" -o "$OBJ/$b.o" 2> "$OBJ/$b.err"; then
    ok=$((ok+1))
  else
    bad=$((bad+1)); echo "$b"
  fi
done < <(sed -n 's/^SRC_DOOM = //p' "$SRC/Makefile" | tr ' ' '\n' | sed 's/\.o$//' | sed 's/^doomgeneric_xlib$/doomgeneric_null/')
echo "--- compiled $ok, failed $bad ---"
