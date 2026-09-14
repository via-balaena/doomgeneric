#!/bin/bash
# Build the headless doomgeneric on this Mac. The object list comes from the
# repo's own Makefile (SRC_DOOM) with the xlib platform swapped for the null
# one, so it tracks the real build instead of a hand-kept list.
set -e
SRC=$(cd "$(dirname "$0")/../doomgeneric" && pwd)
OUT=${1:?output binary path}
OBJ=${2:-$(dirname "$OUT")/nullobj}
mkdir -p "$OBJ"
CFLAGS="-Os -w -DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE"
objs=()
while read -r b; do
  [ -n "$b" ] || continue
  cc $CFLAGS -c "$SRC/$b.c" -o "$OBJ/$b.o" 2> "$OBJ/$b.err" || { echo "FAILED $b"; cat "$OBJ/$b.err"; exit 1; }
  objs+=("$OBJ/$b.o")
done < <(sed -n 's/^SRC_DOOM = //p' "$SRC/Makefile" | tr ' ' '\n' | sed 's/\.o$//' | sed 's/^doomgeneric_xlib$/doomgeneric_null/')
cc -Os "${objs[@]}" -lm -o "$OUT"
echo "built $OUT  (${#objs[@]} objects)"
