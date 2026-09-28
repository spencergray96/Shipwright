#ifndef SOH_RS_WARP_BITS_H
#define SOH_RS_WARP_BITS_H

#include <stdint.h>

// ============================================================================================
//  A WARP TILE'S MARK IN THE COLLISION  (sturdy-bassoon#154)
// ============================================================================================
//
// A step warp's tile is the floor polygons of that tile, and what makes them a warp tile is written
// into their SurfaceType, in the two bit ranges this fork's engine never reads (checked 2026-09-27:
// no accessor, no mask, no direct read anywhere under soh/, and both loaders copy the words
// verbatim):
//
//   data[1] [31:28]  tile id, bits 0-3
//   data[0] [20]     tile id, bit 4                  (data[0] [20:18] is `unk18`, func_80041D70,
//   data[0] [19:18]  landing direction                which nothing calls)
//
// TILE ID 1..31, one per warp tile in a scene; 0 means "not a warp tile", which is every polygon
// every exporter has ever written. The id is what the scene's route table (WarpTable.cpp) keys on.
//
// LANDING DIRECTION, in WORLD axes: 0 = +Z, 1 = +X, 2 = -Z, 3 = -X - the yaw a direction faces is
// `dir << 14`, the engine's binary angle (0 faces +Z, 0x4000 +X). Link arrives dead centre of the
// tile one tile-width away in this direction, facing it. World, not grid: the grid tool mirrors X
// on export, so whatever writes these bits converts a grid direction first (tools/step-warps/).
//
// Plain C and macros only: a spliced `_scene_col.c` (C) includes this to write the entries, and
// Warps.cpp includes it to read them, so the two cannot disagree about where a bit lives.
//
// The bits say WHERE a tile is and which way its landing lies - everything spatial, which is scene
// data. What a tile DOES (step or push, where it sends Link) is the route table's, in code: #147's
// split, and the reason routing can grow without a new bit layout.

#define RS_WARP_TILE_ID_MAX 31

#define RS_WARP_DIR_POS_Z 0
#define RS_WARP_DIR_POS_X 1
#define RS_WARP_DIR_NEG_Z 2
#define RS_WARP_DIR_NEG_X 3

// OR these into a floor SurfaceType's two words.
#define RS_WARP_DATA0(id, dir) (((((uint32_t)(id)) >> 4 & 1u) << 20) | ((((uint32_t)(dir)) & 3u) << 18))
#define RS_WARP_DATA1(id, dir) ((((uint32_t)(id)) & 0xFu) << 28)

// Read them back. 0 = not a warp tile.
#define RS_WARP_TILE_ID(data0, data1) ((int32_t)(((((uint32_t)(data0)) >> 20 & 1u) << 4) | (((uint32_t)(data1)) >> 28 & 0xFu)))
#define RS_WARP_TILE_DIR(data0) ((int32_t)(((uint32_t)(data0)) >> 18 & 3u))

#endif // SOH_RS_WARP_BITS_H
