# Measurement scaffolding

Not part of Doom. These answer one question: **what does Doom actually need in
RAM**, as opposed to the 6 MiB `MIN_RAM` in `i_system.c`, which is a floor the
code enforces rather than a requirement.

The chain is three steps, and each one is checkable on its own.

    tools/build_null.sh  out/doomnull            # headless build, no window
    DG_TICKS=600 out/doomnull -iwad W -warp 1 1  # prints ZONEPEAK + per-site
    tools/project32.py W out/doomnull sizes.json     # projects onto 32-bit ARM

`build_null.sh` takes the object list from `doomgeneric/Makefile` itself and
swaps the xlib platform for `doomgeneric_null.c`, so it cannot drift from the
real build.

`z_zone.c` carries the accounting: every block by tag, and by
`__builtin_return_address(0)` with a call count beside the bytes. The count is
what makes the projection checkable -- bytes alone cannot tell one 50 KB block
from five hundred 100-byte ones, and the two project differently.

`sizeprobe.c` emits `sizeof` for the structs the level loader allocates, as a
`const unsigned[]` read back out of the object file. Compile it twice, once for
the host and once for `thumbv8m.main-none-eabi`, and the two answers are the
ratio each array shrinks by on the real target. Nothing runs, so no 32-bit
runtime is needed:

    clang --target=thumbv8m.main-none-eabi -ffreestanding \
          -I <stub-libc-headers> -I ../doomgeneric -c tools/sizeprobe.c

(The stubs are empty files named after the libc headers `doomdef.h` includes,
each with `#include <stddef.h>`; only struct layout is wanted, not libc.)

`project32.py` is arithmetic on measured inputs, not a measurement on ARM.
**Every modelled site is checked against the host bytes it claims to explain**,
and a model that does not reproduce them is reported and dropped rather than
used. Sites with no model keep their payload and lose only the smaller zone
header, which errs high.

The real number arrives when this runs on silicon. Until then the projection
has a stated method and a control, which is the most that can be said.

## The shim

`tock/` is not measurement. It is the libc surface Doom needs, sized by
measuring what Doom references rather than by adopting a whole libc:
`tock/include` declares it, `tock/shim_*.c` implements it, and
`tools/build_arm.sh` builds both for a Cortex-M33 with clang.

`tools/test_shim.sh` runs the host tests. The formatter is checked
**differentially against this host's libc** -- the one reference
implementation that costs nothing -- and the allocator and sscanf, which have
no reference, state the property each case checks.
