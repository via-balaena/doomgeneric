// Headless platform: runs the game with no window and no input, so the
// zone allocator can be measured rather than argued about.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "doomgeneric.h"

static int tick_limit = 400;
static int ticks = 0;

void DG_Init(void) {}
void DG_DrawFrame(void) {}
void DG_SleepMs(uint32_t ms) { (void)ms; }
uint32_t DG_GetTicksMs(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}
int DG_GetKey(int* pressed, unsigned char* key) { (void)pressed; (void)key; return 0; }
void DG_SetWindowTitle(const char* title) { (void)title; }

int main(int argc, char** argv) {
    const char* lim = getenv("DG_TICKS");
    if (lim) tick_limit = atoi(lim);
    doomgeneric_Create(argc, argv);
    for (ticks = 0; ticks < tick_limit; ticks++) doomgeneric_Tick();
    extern void Z_PrintPeak(void);
    Z_PrintPeak();
    return 0;
}
