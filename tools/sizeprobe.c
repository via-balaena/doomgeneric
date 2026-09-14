#include "doomdef.h"
#include "r_defs.h"
#include "p_mobj.h"
#include "w_wad.h"
#define S(t) (unsigned)sizeof(t)
const unsigned probe[] = {
  0xABCD0001,
  S(vertex_t), S(seg_t), S(sector_t), S(subsector_t),
  S(line_t), S(side_t), S(node_t), S(mobj_t),
  S(line_t *), S(lumpinfo_t),
  0xABCD0002
};
