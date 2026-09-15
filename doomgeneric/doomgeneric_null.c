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
/* A positive control on the renderer. A measurement of what the renderer
 * needs is worthless if the renderer is not drawing, and "2 visplanes" looks
 * the same either way. */
static long frames_drawn, nonzero_peak;
static int distinct_peak;

void DG_DrawFrame(void) {
    unsigned char seen[256];
    int i, distinct = 0;
    long nonzero = 0;
    memset(seen, 0, sizeof seen);
    for (i = 0; i < DOOMGENERIC_RESX * DOOMGENERIC_RESY; i++) {
        unsigned char v = ((unsigned char *)DG_ScreenBuffer)[i];
        if (v) nonzero++;
        if (!seen[v]) { seen[v] = 1; distinct++; }
    }
    if (nonzero > nonzero_peak) nonzero_peak = nonzero;
    if (distinct > distinct_peak) distinct_peak = distinct;
    frames_drawn++;
}
void DG_SleepMs(uint32_t ms) { (void)ms; }
uint32_t DG_GetTicksMs(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}
/* Doom's own codes, from doomkeys.h. */
#define K_RIGHT 0xae
#define K_LEFT  0xac
#define K_FWD   0xad

/* Synthetic input, so a measurement is not taken of a player standing still.
 *
 * With DG_DRIVE set, the player turns continuously and walks in bursts, which
 * sweeps the view through the whole level instead of staring at one wall. It
 * matters: motionless at an E2M8 spawn the renderer peaked at 2 visplanes and
 * 0 vissprites, and limits chosen from that would fail the moment anyone
 * turned around. A sweep is still a SAMPLE and not a worst case -- it is a
 * floor under the limits, not a proof about them.
 */
static int driving = 0;
static int turn_held = 0, fwd_held = 0;
static long key_polls, key_events;

int DG_GetKey(int* pressed, unsigned char* key) {
    key_polls++;
    if (!driving) return 0;

    /* Hold turn from the start, so the view sweeps 360 degrees repeatedly. */
    if (!turn_held) {
        turn_held = 1; *pressed = 1; *key = K_RIGHT; key_events++; return 1;
    }
    /* And walk in bursts, so the player moves through the level rather than
       pirouetting on the spot. */
    int want_fwd = ((ticks / 35) % 3) != 0;
    if (want_fwd != fwd_held) {
        fwd_held = want_fwd; *pressed = want_fwd; *key = K_FWD; key_events++; return 1;
    }
    return 0;
}
void DG_SetWindowTitle(const char* title) { (void)title; }

/* Trigger a level exit at a given tic, to reproduce what pressing the exit
 * switch does without needing the hardware or a hand on the lever. */
extern void G_ExitLevel(void);
static int exit_at = 0;

int main(int argc, char** argv) {
    const char* lim = getenv("DG_TICKS");
    if (lim) tick_limit = atoi(lim);
    driving = getenv("DG_DRIVE") != NULL;
    doomgeneric_Create(argc, argv);
    {
        const char* e = getenv("DG_EXIT");
        if (e) exit_at = atoi(e);
    }
    for (ticks = 0; ticks < tick_limit; ticks++) {
        /* DG_EXIT is a PERIOD, not a one-shot: exiting repeatedly is how a
         * leak across level reloads shows itself. */
        if (exit_at && ticks && ticks % exit_at == 0) {
            printf("--- G_ExitLevel at tic %d ---\n", ticks);
            fflush(stdout);
            G_ExitLevel();
        }
        doomgeneric_Tick();
    }
    extern void Z_PrintPeak(void);
    Z_PrintPeak();
    printf("  RENDER frames=%ld nonzero_pixels_peak=%ld distinct_colours_peak=%d\n",
           frames_drawn, nonzero_peak, distinct_peak);
    {
        extern int viewwidth, viewheight;
        extern int gamestate;
        printf("  RENDER viewwidth=%d viewheight=%d gamestate=%d\n",
               viewwidth, viewheight, gamestate);
    }
    {
        /* Did the player actually move? A sweep that never happened would
           report the same peaks as standing still, and it did. */
        extern int viewx, viewy;
        extern unsigned int viewangle;
        printf("  RENDER key_polls=%ld key_events=%ld viewx=%d viewy=%d viewangle=%u\n",
               key_polls, key_events, viewx, viewy, viewangle);
    }
    return 0;
}
