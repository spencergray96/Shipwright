#ifndef SOH_RS_WARP_DEF_H
#define SOH_RS_WARP_DEF_H

#include <stdint.h>
#include "WarpBits.h"

// ============================================================================================
//  WHAT A STEP WARP IS, AS DATA  (sturdy-bassoon#154)
// ============================================================================================
//
// A STEP WARP moves Link in place - no scene load, every actor carries on - when he steps onto a
// WARP TILE: one full tile whose floor polygons carry a tile id in their SurfaceType (WarpBits.h).
// The move itself is the staircase's storey-move controller (Stairs.cpp, RsStair_BeginWarpMove),
// so the fade, the room change, the camera seat and the respawn point are all the same.
//
// The unit is the TILE, not a pair. Each tile has its own entry and its own destinations:
//   - a PAIR is two tiles that send Link to each other;
//   - a RANDOM GROUP is tiles that each list the others - one is picked each time it fires;
//   - a LANDING ONLY lists nothing: it never fires, it is only somewhere to arrive (a one-way warp's
//     far end);
//   - a CYCLE, a maze tile that sends you somewhere unexpected on the same storey - anything a list
//     of destinations can say. "Storey" is an authoring idea; nothing here cares whether two tiles
//     are on the same one.
//
// THE TABLE HOLDS NO COORDINATES, #147's rule: where a tile is, how big it is and which side its
// landing is on are in the collision the exporter writes. The table says only what the tile does.
// Plain C, like StairDef.h, so a table reads the same from either language.

// How Link has to arrive on a tile for it to fire.
typedef enum RsWarpEntry {
    // Standing on it is enough - a hole in a floor, a trapdoor, a pad. Walking onto it is intent.
    RS_WARP_ENTRY_STEP = 0,
    // He must be MOVING INTO it: within RS_WARP_PUSH_CONE of the direction opposite its landing,
    // which for a ladder against a wall is "into the wall". Walking across it, or standing on it,
    // does not fire - so a ladder's tile in an open room is not a trap for someone walking along the
    // wall (`refused reason=aim`).
    RS_WARP_ENTRY_PUSH = 1,
    RS_WARP_ENTRY_COUNT,
} RsWarpEntry;

// Destinations per tile. One is picked uniformly at random each time the tile fires, so this is only
// the widest random group; raise it freely.
#define RS_WARP_MAX_DESTS 8

typedef struct RsWarpTileDef {
    int32_t id;    // 1..RS_WARP_TILE_ID_MAX: the id this tile's collision carries (WarpBits.h)
    int32_t entry; // RsWarpEntry

    // The room this tile - and so its landing, one tile away - is in. A move to a tile in another
    // room loads that room first, exactly as a staircase's does. Every grid-tool scene is one room,
    // so 0 there; the field is for the multi-room case (#148's "underground as a room").
    int32_t room;

    // Where it sends Link: tile ids in the same scene, never itself. One is fixed; several is a
    // random pick each time it fires; none (NULL, 0) is a landing only, which never fires.
    const int32_t* dests;
    int32_t destCount;
} RsWarpTileDef;

// One scene's warp tiles. A tile is a PLACE, so the table names its scene, as a staircase does.
typedef struct RsWarpSceneDef {
    int32_t sceneId;
    const char* name; // snake_case token for console lines
    const RsWarpTileDef* tiles;
    int32_t tileCount;
} RsWarpSceneDef;

#endif // SOH_RS_WARP_DEF_H
