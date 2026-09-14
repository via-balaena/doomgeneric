#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

	/* DG_ScreenBuffer is allocated by I_InitGraphics, not here, because only
	   it knows whether the buffer is needed at all: when the framebuffer is
	   exactly Doom's screen at the same depth, this buffer would never hold
	   anything but a copy of I_VideoBuffer, and it aliases the two instead.
	   A platform may also set DG_ScreenBuffer in DG_Init to supply its own. */

	DG_Init();

	D_DoomMain ();
}

