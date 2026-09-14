#!/bin/bash
# Run the shim's host tests.
#
#   tools/test_shim.sh
#
# The formatter is checked DIFFERENTIALLY against this host's libc, which is
# the only reference implementation that costs nothing. The allocator and the
# narrow sscanf have no reference, so those tests state the property each case
# checks.
#
# The shim's entry points are renamed on the command line so they do not
# collide with the host libc. That is why shim_alloc.c and shim_scan.c include
# no system headers: a renamed `malloc` mangles the declaration in macOS's
# <malloc/_malloc.h>, and a renamed `sscanf` does the same to <stdio.h>.
set -uo pipefail
here=$(cd "$(dirname "$0")/../tock" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$here" || exit 2
fail=0

cc -O1 -c -D_FORTIFY_SOURCE=0 -o "$work/printf.o" shim_printf.c \
   -Dvsnprintf=shim_vsnprintf -Dsnprintf=shim_snprintf -Dprintf=shim_printf \
   -Dfprintf=shim_fprintf -Dvfprintf=shim_vfprintf -Dputs=shim_puts \
   -Dputchar=shim_putchar -Dfputc=shim_fputc || exit 2
cc -O1 -c -o "$work/test_printf.o" test_printf.c -Wno-format || exit 2
cc -o "$work/test_printf" "$work/test_printf.o" "$work/printf.o" || exit 2
echo "== formatter, against this host's libc =="
"$work/test_printf" || fail=1

cc -O1 -c -D_FORTIFY_SOURCE=0 -o "$work/alloc.o" shim_alloc.c \
   -Dmalloc=shim_malloc -Dcalloc=shim_calloc -Drealloc=shim_realloc -Dfree=shim_free || exit 2
cc -O1 -c -D_FORTIFY_SOURCE=0 -o "$work/scan.o" shim_scan.c -Dsscanf=shim_sscanf || exit 2
cc -O1 -c -o "$work/test_shim.o" test_shim.c || exit 2
cc -o "$work/test_shim" "$work/test_shim.o" "$work/alloc.o" "$work/scan.o" || exit 2
echo "== allocator and sscanf =="
"$work/test_shim" || fail=1

[ "$fail" -eq 0 ] && echo "shim tests pass" || echo "SHIM TESTS FAILED"
exit "$fail"
