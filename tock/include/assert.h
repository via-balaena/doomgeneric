#pragma once
/* Doom's own I_Error is the error path; a libc assert would need stderr and
   abort, neither of which mean anything in a Tock process. */
#define assert(x) ((void)0)
