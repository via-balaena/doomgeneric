//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Zone Memory Allocation. Neat.
//


#include <limits.h>

#include "z_zone.h"
#include "i_system.h"
#include "doomtype.h"


//
// ZONE MEMORY ALLOCATION
//
// There is never any space between memblocks,
//  and there will never be two contiguous free memblocks.
// The rover can be left pointing at a non-empty block.
//
// It is of no value to free a cachable block,
//  because it will get overwritten automatically if needed.
// 
 
#define MEM_ALIGN sizeof(void *)
#define ZONEID	0x1d4a11

typedef struct memblock_s
{
    int			size;	// including the header and possibly tiny fragments
    void**		user;
    int			tag;	// PU_FREE if this is free
    int			id;	// should be ZONEID
    struct memblock_s*	next;
    struct memblock_s*	prev;
} memblock_t;


typedef struct
{
    // total bytes malloced, including header
    int		size;

    // start / end cap for linked list
    memblock_t	blocklist;
    
    memblock_t*	rover;
    
} memzone_t;



memzone_t*	mainzone;

/* Instrumentation: how much is held in NON-purgeable blocks, which is the
   floor a smaller zone cannot go below. PU_CACHE and above are evictable and
   shrink with the zone, so they are counted separately. */
static long dg_nonpurge = 0;
static long dg_nonpurge_peak = 0;
static long dg_cache = 0;
static long dg_cache_peak = 0;
static long dg_tag[PU_NUM_TAGS];
static long dg_tag_peak[PU_NUM_TAGS];

/* Attribution: which caller asked for the non-purgeable bytes. */
#define DG_SITES 256
static void* dg_site[DG_SITES];
static long  dg_site_bytes[DG_SITES];
static long  dg_site_calls[DG_SITES];
static int   dg_nsites = 0;

/* High-water marks for the renderer's FIXED-SIZE arrays.
 *
 * These are static arrays, so no zone number can see them -- which is exactly
 * how 246,870 bytes of .bss went unmeasured. Their sizes are compile-time
 * limits chosen for a desktop, and the only honest way to shrink them is to
 * watch how full they actually get.
 */
#define DG_PEAKS 4
static long dg_peak[DG_PEAKS];
static const char *dg_peak_name[DG_PEAKS] = {
    "visplanes", "openings", "drawsegs", "vissprites"
};
static const char *dg_peak_limit[DG_PEAKS] = {
    "MAXVISPLANES", "MAXOPENINGS", "MAXDRAWSEGS", "MAXVISSPRITES"
};

/* What the zone actually holds, for a platform that cannot run Z_PrintPeak.
 *
 * `largest` is the biggest run Z_Malloc could satisfy: consecutive blocks that
 * are free or purgeable, which is exactly what its rover will merge. Total
 * free and largest free are DIFFERENT NUMBERS, and the difference is
 * fragmentation -- a zone with room to spare can still fail an allocation, and
 * saying only how much is left cannot tell the two apart.
 */
void DG_ZoneStats(long *peak, long *now, long *total_free, long *largest)
{
    memblock_t *b;
    long run = 0;

    if (peak)  *peak = dg_nonpurge_peak;
    if (now)   *now  = dg_nonpurge;
    if (total_free) *total_free = 0;
    if (largest)    *largest = 0;

    if (mainzone == NULL)
        return;

    for (b = mainzone->blocklist.next; b != &mainzone->blocklist; b = b->next)
    {
        if (b->tag == PU_FREE || b->tag >= PU_PURGELEVEL)
        {
            run += b->size;
            if (total_free) *total_free += b->size;
            if (largest && run > *largest) *largest = run;
        }
        else
        {
            run = 0;
        }
    }
}

// Print the zone's block map, because "largest run 30152" says the space is
// stranded without saying what by.
//
// The failure path has already dropped every purgeable block (see the retry
// in Z_Malloc), so anything left between two free runs CANNOT be purged and
// is what actually splits the zone. This lists those blocks -- the free run
// that ends at each one, then its tag and size -- followed by a per-tag
// total, which is what separates "one allocation is in the way" from "a
// whole class of them is".
//
// Bounded output: blocks under DUMP_FLOOR are summarised rather than listed,
// so a zone full of small allocations cannot flood a 115200 baud console.
void DG_ZoneDump(void)
{
    static const char *const tagname[PU_NUM_TAGS] = {
        "?0", "STATIC", "SOUND", "MUSIC", "FREE", "LEVEL", "LEVSPEC",
        "?7", "PURGE", "CACHE"
    };
    const long DUMP_FLOOR = 2048;
    memblock_t *b;
    long run = 0;
    long tagtotal[PU_NUM_TAGS];
    int  tagcount[PU_NUM_TAGS];
    long small_total = 0;
    int  small_count = 0;
    long run_total = 0;
    int  runs = 0;
    int  big_runs = 0;
    int  listed = 0;
    int  t;

    if (mainzone == NULL)
        return;

    for (t = 0; t < PU_NUM_TAGS; t++) { tagtotal[t] = 0; tagcount[t] = 0; }

    printf("zone map: blocks that cannot be purged, in address order\n");

    for (b = mainzone->blocklist.next; b != &mainzone->blocklist; b = b->next)
    {
        if (b->tag == PU_FREE || b->tag >= PU_PURGELEVEL)
        {
            run += b->size;
            continue;
        }

        t = (b->tag >= 0 && b->tag < PU_NUM_TAGS) ? b->tag : 0;
        tagtotal[t] += b->size;
        tagcount[t]++;

        if (b->size >= DUMP_FLOOR && listed < 24)
        {
            printf("  free %7ld | %-7s %7d\n", run, tagname[t], b->size);
            listed++;
        }
        else if (b->size < DUMP_FLOOR)
        {
            small_total += b->size;
            small_count++;
        }
        // Every free run ends here, listed or not. Counting them separately
        // is what makes the free space this listing does not print visible:
        // without it the map showed one 32,636 run and said nothing about
        // where the other 24,880 free bytes were.
        if (run > 0)
        {
            runs++;
            if (run > DUMP_FLOOR) big_runs++;
            run_total += run;
        }
        run = 0;
    }
    printf("  free %7ld | (end)\n", run);
    if (run > 0) { runs++; if (run > DUMP_FLOOR) big_runs++; run_total += run; }

    if (small_count)
        printf("  plus %d unpurgeable blocks under %ld bytes, %ld total\n",
               small_count, DUMP_FLOOR, small_total);
    printf("  free space in %d runs, %d of them over %ld bytes, %ld total\n",
           runs, big_runs, DUMP_FLOOR, run_total);

    printf("zone by tag:");
    for (t = 0; t < PU_NUM_TAGS; t++)
        if (tagcount[t])
            printf(" %s=%d/%ld", tagname[t], tagcount[t], tagtotal[t]);
    printf("\n");
}

void DG_NotePeak(int which, long value)
{
    if (which >= 0 && which < DG_PEAKS && value > dg_peak[which])
        dg_peak[which] = value;
}

static void dg_note_site(void* pc, long bytes)
{
    int i;
    for (i = 0; i < dg_nsites; i++)
        if (dg_site[i] == pc) {
            dg_site_bytes[i] += bytes; dg_site_calls[i]++; return;
        }
    if (dg_nsites < DG_SITES) {
        dg_site[dg_nsites] = pc;
        dg_site_bytes[dg_nsites] = bytes;
        dg_site_calls[dg_nsites] = 1;
        dg_nsites++;
    }
}

static void dg_account(int tag, long delta)
{
    if (tag >= 0 && tag < PU_NUM_TAGS) {
        dg_tag[tag] += delta;
        if (dg_tag[tag] > dg_tag_peak[tag]) dg_tag_peak[tag] = dg_tag[tag];
    }
    if (tag >= PU_PURGELEVEL) {
        dg_cache += delta;
        if (dg_cache > dg_cache_peak) dg_cache_peak = dg_cache;
    } else {
        dg_nonpurge += delta;
        if (dg_nonpurge > dg_nonpurge_peak) dg_nonpurge_peak = dg_nonpurge;
    }
}

void Z_PrintPeak(void)
{
    static const char* names[PU_NUM_TAGS] = {
        "0", "STATIC", "SOUND", "MUSIC", "FREE", "LEVEL", "LEVSPEC",
        "7", "PURGELEVEL", "CACHE"
    };
    int i;
    printf("REFADDR Z_Malloc=%p\n", (void*)Z_Malloc);
    printf("ZONEPEAK nonpurge_peak=%ld cache_peak=%ld nonpurge_now=%ld\n",
           dg_nonpurge_peak, dg_cache_peak, dg_nonpurge);
    for (i = 0; i < PU_NUM_TAGS; i++)
        if (dg_tag_peak[i] > 0)
            printf("  TAG %-10s peak %8ld  now %8ld\n",
                   names[i] ? names[i] : "?", dg_tag_peak[i], dg_tag[i]);
    {
        int a, b;
        for (a = 0; a < dg_nsites; a++)
            for (b = a + 1; b < dg_nsites; b++)
                if (dg_site_bytes[b] > dg_site_bytes[a]) {
                    long tb = dg_site_bytes[a]; void* tp = dg_site[a];
                    long tc = dg_site_calls[a];
                    dg_site_bytes[a] = dg_site_bytes[b]; dg_site[a] = dg_site[b];
                    dg_site_calls[a] = dg_site_calls[b];
                    dg_site_bytes[b] = tb; dg_site[b] = tp;
                    dg_site_calls[b] = tc;
                }
        for (a = 0; a < DG_PEAKS; a++)
            printf("  ARRAYPEAK %-11s %7ld  (%s)\n",
                   dg_peak_name[a], dg_peak[a], dg_peak_limit[a]);
        for (a = 0; a < dg_nsites && a < 18; a++)
            printf("  SITE %p %10ld in %6ld call(s)\n",
                   dg_site[a], dg_site_bytes[a], dg_site_calls[a]);
    }
}



//
// Z_ClearZone
//
void Z_ClearZone (memzone_t* zone)
{
    memblock_t*		block;
	
    // set the entire zone to one free block
    zone->blocklist.next =
	zone->blocklist.prev =
	block = (memblock_t *)( (byte *)zone + sizeof(memzone_t) );
    
    zone->blocklist.user = (void *)zone;
    zone->blocklist.tag = PU_STATIC;
    zone->rover = block;
	
    block->prev = block->next = &zone->blocklist;
    
    // a free block.
    block->tag = PU_FREE;

    block->size = zone->size - sizeof(memzone_t);
}



//
// Z_Init
//
void Z_Init (void)
{
    memblock_t*	block;
    int		size;

    mainzone = (memzone_t *)I_ZoneBase (&size);
    mainzone->size = size;

    // set the entire zone to one free block
    mainzone->blocklist.next =
	mainzone->blocklist.prev =
	block = (memblock_t *)( (byte *)mainzone + sizeof(memzone_t) );

    mainzone->blocklist.user = (void *)mainzone;
    mainzone->blocklist.tag = PU_STATIC;
    mainzone->rover = block;
	
    block->prev = block->next = &mainzone->blocklist;

    // free block
    block->tag = PU_FREE;
    
    block->size = mainzone->size - sizeof(memzone_t);
}


//
// Z_Free
//
void Z_Free (void* ptr)
{
    memblock_t*		block;
    memblock_t*		other;
	
    block = (memblock_t *) ( (byte *)ptr - sizeof(memblock_t));

    if (block->id != ZONEID)
	I_Error ("Z_Free: freed a pointer without ZONEID");
		
    if (block->tag != PU_FREE && block->user != NULL)
    {
    	// clear the user's mark
	    *block->user = 0;
    }

    // mark as free
    dg_account(block->tag, -(long)block->size);
    block->tag = PU_FREE;
    block->user = NULL;
    block->id = 0;
	
    other = block->prev;

    if (other->tag == PU_FREE)
    {
        // merge with previous free block
        other->size += block->size;
        other->next = block->next;
        other->next->prev = other;

        if (block == mainzone->rover)
            mainzone->rover = other;

        block = other;
    }
	
    other = block->next;
    if (other->tag == PU_FREE)
    {
        // merge the next free block onto the end
        block->size += other->size;
        block->next = other->next;
        block->next->prev = block;

        if (other == mainzone->rover)
            mainzone->rover = block;
    }
}



//
// Z_Malloc
// You can pass a NULL user if the tag is < PU_PURGELEVEL.
//
#define MINFRAGMENT		64


void*
Z_Malloc
( int		size,
  int		tag,
  void*		user )
{
    int		extra;
    memblock_t*	start;
    memblock_t* rover;
    memblock_t* newblock;
    memblock_t*	base;
    void *result;
    boolean	retried;

    size = (size + MEM_ALIGN - 1) & ~(MEM_ALIGN - 1);
    
    // scan through the block list,
    // looking for the first free block
    // of sufficient size,
    // throwing out any purgable blocks along the way.

    // account for size of block header
    size += sizeof(memblock_t);
    
    // if there is a free block behind the rover,
    //  back up over them
    retried = false;

retry:
    base = mainzone->rover;
    
    if (base->prev->tag == PU_FREE)
        base = base->prev;
	
    rover = base;
    start = base->prev;
	
    do
    {
        if (rover == start)
        {
            // Scanned all the way around the list.
            //
            // That is NOT the same as being out of memory. This search is a
            // single-pass first fit that purges cached blocks as it walks, so
            // space it consolidates BEHIND the point `base` has already
            // reached is never revisited -- and the pass gives up with a run
            // larger than the request sitting in the zone. Measured on a Pico
            // 2 W reloading a level: it failed a 16,408 byte allocation with a
            // 32,272 byte contiguous run free.
            //
            // A zone big enough to make first fit succeed by luck hides this;
            // a tight one does not. So before declaring failure, drop every
            // purgeable block and scan once more. One retry, so a genuine
            // exhaustion still errors instead of looping.
            if (!retried)
            {
                retried = true;
                Z_FreeTags (PU_PURGELEVEL, INT_MAX);
                goto retry;
            }

            {
                long zp, zn, zf, zl;
                DG_ZoneStats (&zp, &zn, &zf, &zl);
                // The map first: I_Error does not return, so anything printed
                // after it is lost.
                DG_ZoneDump ();
                // Say what the zone looked like, not just what was asked for.
                I_Error ("Z_Malloc: failed on allocation of %i bytes "
                         "(tag %i, zone %i, in use %ld, free %ld, largest run %ld)",
                         size, tag, mainzone->size, zn, zf, zl);
            }
        }
	
        if (rover->tag != PU_FREE)
        {
            if (rover->tag < PU_PURGELEVEL)
            {
                // hit a block that can't be purged,
                // so move base past it
                base = rover = rover->next;
            }
            else
            {
                // free the rover block (adding the size to base)

                // the rover can be the base block
                base = base->prev;
                Z_Free ((byte *)rover+sizeof(memblock_t));
                base = base->next;
                rover = base->next;
            }
        }
        else
        {
            rover = rover->next;
        }

    } while (base->tag != PU_FREE || base->size < size);

    
    // found a block big enough
    extra = base->size - size;
    
    if (extra >  MINFRAGMENT)
    {
        // there will be a free fragment after the allocated block
        newblock = (memblock_t *) ((byte *)base + size );
        newblock->size = extra;
	
        newblock->tag = PU_FREE;
        newblock->user = NULL;	
        newblock->prev = base;
        newblock->next = base->next;
        newblock->next->prev = newblock;

        base->next = newblock;
        base->size = size;
    }
	
	if (user == NULL && tag >= PU_PURGELEVEL)
	    I_Error ("Z_Malloc: an owner is required for purgable blocks");

    dg_account(tag, (long)base->size);
    if (tag < PU_PURGELEVEL)
        dg_note_site(__builtin_return_address(0), (long)base->size);
    base->user = user;
    base->tag = tag;

    result  = (void *) ((byte *)base + sizeof(memblock_t));

    if (base->user)
    {
        *base->user = result;
    }

    // next allocation will start looking here
    mainzone->rover = base->next;	
	
    base->id = ZONEID;
    
    return result;
}



//
// Z_FreeTags
//
void
Z_FreeTags
( int		lowtag,
  int		hightag )
{
    memblock_t*	block;
    memblock_t*	next;
	
    for (block = mainzone->blocklist.next ;
	 block != &mainzone->blocklist ;
	 block = next)
    {
	// get link before freeing
	next = block->next;

	// free block?
	if (block->tag == PU_FREE)
	    continue;
	
	if (block->tag >= lowtag && block->tag <= hightag)
	    Z_Free ( (byte *)block+sizeof(memblock_t));
    }
}



//
// Z_DumpHeap
// Note: TFileDumpHeap( stdout ) ?
//
void
Z_DumpHeap
( int		lowtag,
  int		hightag )
{
    memblock_t*	block;
	
    printf ("zone size: %i  location: %p\n",
	    mainzone->size,mainzone);
    
    printf ("tag range: %i to %i\n",
	    lowtag, hightag);
	
    for (block = mainzone->blocklist.next ; ; block = block->next)
    {
	if (block->tag >= lowtag && block->tag <= hightag)
	    printf ("block:%p    size:%7i    user:%p    tag:%3i\n",
		    block, block->size, block->user, block->tag);
		
	if (block->next == &mainzone->blocklist)
	{
	    // all blocks have been hit
	    break;
	}
	
	if ( (byte *)block + block->size != (byte *)block->next)
	    printf ("ERROR: block size does not touch the next block\n");

	if ( block->next->prev != block)
	    printf ("ERROR: next block doesn't have proper back link\n");

	if (block->tag == PU_FREE && block->next->tag == PU_FREE)
	    printf ("ERROR: two consecutive free blocks\n");
    }
}


//
// Z_FileDumpHeap
//
void Z_FileDumpHeap (FILE* f)
{
    memblock_t*	block;
	
    fprintf (f,"zone size: %i  location: %p\n",mainzone->size,mainzone);
	
    for (block = mainzone->blocklist.next ; ; block = block->next)
    {
	fprintf (f,"block:%p    size:%7i    user:%p    tag:%3i\n",
		 block, block->size, block->user, block->tag);
		
	if (block->next == &mainzone->blocklist)
	{
	    // all blocks have been hit
	    break;
	}
	
	if ( (byte *)block + block->size != (byte *)block->next)
	    fprintf (f,"ERROR: block size does not touch the next block\n");

	if ( block->next->prev != block)
	    fprintf (f,"ERROR: next block doesn't have proper back link\n");

	if (block->tag == PU_FREE && block->next->tag == PU_FREE)
	    fprintf (f,"ERROR: two consecutive free blocks\n");
    }
}



//
// Z_CheckHeap
//
void Z_CheckHeap (void)
{
    memblock_t*	block;
	
    for (block = mainzone->blocklist.next ; ; block = block->next)
    {
	if (block->next == &mainzone->blocklist)
	{
	    // all blocks have been hit
	    break;
	}
	
	if ( (byte *)block + block->size != (byte *)block->next)
	    I_Error ("Z_CheckHeap: block size does not touch the next block\n");

	if ( block->next->prev != block)
	    I_Error ("Z_CheckHeap: next block doesn't have proper back link\n");

	if (block->tag == PU_FREE && block->next->tag == PU_FREE)
	    I_Error ("Z_CheckHeap: two consecutive free blocks\n");
    }
}




//
// Z_ChangeTag
//
void Z_ChangeTag2(void *ptr, int tag, char *file, int line)
{
    memblock_t*	block;
	
    block = (memblock_t *) ((byte *)ptr - sizeof(memblock_t));

    if (block->id != ZONEID)
        I_Error("%s:%i: Z_ChangeTag: block without a ZONEID!",
                file, line);

    if (tag >= PU_PURGELEVEL && block->user == NULL)
        I_Error("%s:%i: Z_ChangeTag: an owner is required "
                "for purgable blocks", file, line);

    dg_account(block->tag, -(long)block->size);
    dg_account(tag, (long)block->size);
    block->tag = tag;
}

void Z_ChangeUser(void *ptr, void **user)
{
    memblock_t*	block;

    block = (memblock_t *) ((byte *)ptr - sizeof(memblock_t));

    if (block->id != ZONEID)
    {
        I_Error("Z_ChangeUser: Tried to change user for invalid block!");
    }

    block->user = user;
    *user = ptr;
}



//
// Z_FreeMemory
//
int Z_FreeMemory (void)
{
    memblock_t*		block;
    int			free;
	
    free = 0;
    
    for (block = mainzone->blocklist.next ;
         block != &mainzone->blocklist;
         block = block->next)
    {
        if (block->tag == PU_FREE || block->tag >= PU_PURGELEVEL)
            free += block->size;
    }

    return free;
}

unsigned int Z_ZoneSize(void)
{
    return mainzone->size;
}

