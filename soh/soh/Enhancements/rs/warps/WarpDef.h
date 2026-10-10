#ifndef SOH_RS_WARP_DEF_H
#define SOH_RS_WARP_DEF_H

#include <stdint.h>
#include "WarpBits.h"

// ============================================================================================
//  WHAT A STEP WARP IS, AS DATA  (sturdy-bassoon#154)
// ============================================================================================
//
// A STEP WARP moves Link when he steps onto a WARP TILE: one full tile whose floor polygons carry a
// tile id in their SurfaceType (WarpBits.h). To a tile in the same scene the move is IN PLACE - no
// scene load, every actor carries on; to a tile in ANOTHER scene (sturdy-bassoon#148) it is a real
// transition inside the same fade - see RsWarpDest. Either way the move is the staircase's
// storey-move controller (Stairs.cpp, RsStair_BeginWarpMove), so the fade, the room change, the
// camera seat and the respawn point are all the same.
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
    //
    // KEPT, BUT NO CONTENT USES IT (the owner, 2026-09-28): ladders are STEP tiles, which fire on any
    // step like a vanilla grotto exit - crossing a push ladder at a shallow angle walked straight over
    // it. It stays for a later use; the #154 fixture (step_warp_fixture) still exercises it.
    RS_WARP_ENTRY_PUSH = 1,
    RS_WARP_ENTRY_COUNT,
} RsWarpEntry;

// Destinations per tile. One is picked uniformly at random each time the tile fires, so this is only
// the widest random group; raise it freely.
#define RS_WARP_MAX_DESTS 8

// A DESTINATION is a tile - in this scene, or in another one (sturdy-bassoon#148).
//
//   RS_WARP_TO(tile)                  a tile in this scene: the move is IN PLACE, every actor
//                                     carries on (#154)
//   RS_WARP_TO_SCENE(entrance, tile)  a tile in the scene that `entrance` (an ENTR_* id) loads: a
//                                     real transition, with the same fade either side of the load.
//                                     Link arrives at THAT tile's landing, facing away - the rule
//                                     is the same either way, so the way back is just the other
//                                     tile's route. For underground areas, which are their own scenes
//                                     (#148 ADR).
//
// The entrance, not the scene, because a transition is to an entrance; the scene is the one that
// entrance names. The tile is looked up in THAT scene's table and collision, so a destination scene
// needs a route table of its own with the tile in it (`dest_unrouted` otherwise).
#define RS_WARP_HERE (-1)

typedef struct RsWarpDest {
    int32_t tile;     // 1..RS_WARP_TILE_ID_MAX, in the destination scene (or map)
    int32_t entrance; // RS_WARP_HERE, or an entrance id into ANOTHER scene
    // A GENERATED destination (#173 F3) names a (map, local id) instead of an entrance: `map` is the
    // destination tile's map number and `entrance` is RS_WARP_HERE. 0 in a hand row, which the two
    // macros below leave it. A map in the scene Link is in is moved to in place; one in no map of it is a
    // scene change through the scene picker (#173 F5, SceneMaps.h), at the scene the picker chooses.
    int32_t map;
} RsWarpDest;

#define RS_WARP_TO(tile) { (tile), RS_WARP_HERE }
#define RS_WARP_TO_SCENE(entrance, tile) { (tile), (entrance) }

typedef struct RsWarpTileDef {
    int32_t id;    // 1..RS_WARP_TILE_ID_MAX: the id this tile's collision carries (WarpBits.h)
    int32_t entry; // RsWarpEntry

    // The room this tile - and so its landing, one tile away - is in. A move to a tile in another
    // room loads that room first, exactly as a staircase's does; a move from another SCENE loads
    // the scene with this room current. Every grid-tool scene is one room, so 0 there.
    int32_t room;

    // Where it sends Link, never to itself. One is fixed; several is a random pick each time it
    // fires, and may mix this scene's tiles with other scenes'; none (NULL, 0) is a landing only,
    // which never fires.
    const RsWarpDest* dests;
    int32_t destCount;
} RsWarpTileDef;

// One scene's warp tiles. A tile is a PLACE, so the table names its scene, as a staircase does.
typedef struct RsWarpSceneDef {
    int32_t sceneId;
    const char* name; // snake_case token for console lines
    const RsWarpTileDef* tiles;
    int32_t tileCount;
} RsWarpSceneDef;

// --- GENERATED warp tiles (sturdy-bassoon#173 slice F3) -------------------------------------------
//
// A warp tile the grid tool authored is MAP-keyed: its local id is unique within its MAP, not its scene
// (the slice F ADR's decision 3), because a stitched scene holds many maps and each numbers its own
// tiles from 1. Each exported scene's `<slug>_warps.inc` carries its maps' rows (RS_GEN_WARP and
// RS_GEN_WARP_DEST, the grid tool README's "The generated tables"); WarpTable.cpp reads them all and
// RsWarp_RegisterGenerated (Warps.h) merges them into one RsWarpMapDef per map. A map's rows come from
// every scene that holds it - its solo scene and a stitched one - so a row seen twice is taken once,
// and rows for one tile that disagree are refused.
//
// The scan finds which map each warp polygon is in from the scene's map->scene rows (SceneMaps.h), then
// that map's table: position -> map -> (map, local id) (decision 17). A scene is either map-keyed or
// has a hand RsWarpSceneDef, never both (decision 18).
typedef struct RsWarpMapDef {
    int32_t map; // map number, from 1
    const RsWarpTileDef* tiles;
    int32_t tileCount;
} RsWarpMapDef;

typedef struct RsWarpGenRow {
    int32_t map;
    int32_t tile;
    int32_t entry; // RsWarpEntry
    int32_t room;  // always 0 today
} RsWarpGenRow;

typedef struct RsWarpGenDest {
    int32_t map;
    int32_t tile;
    int32_t toMap;
    int32_t toTile;
} RsWarpGenDest;

#endif // SOH_RS_WARP_DEF_H
