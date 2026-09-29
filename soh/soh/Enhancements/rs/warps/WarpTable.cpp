#include "WarpTable.h"
#include "Warps.h"

#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h> // the SCENE_* ids a table belongs to
#include "macros.h"
}

// ============================================================================================
//  THE STEP WARP ROUTE TABLES  (sturdy-bassoon#154)
// ============================================================================================
//
// One RsWarpSceneDef per scene that has warp tiles, one row per tile. THERE ARE NO COORDINATES
// HERE: where each tile is, how big it is and which side its landing is on are in the scene's
// collision (WarpBits.h). A row says only how Link has to arrive on the tile for it to fire, which
// room it is in, and where it sends him. Re-drawing a tile is re-exporting the scene; this file
// changes only when what a tile DOES changes.

namespace {

// --- step_warp_fixture (0x93, entrance 0x63C): the #154 test map --------------------------------
//
// A REFERENCE FIXTURE: its tiles are spliced into the exported collision by
// sturdy-bassoon tools/step-warps/splice_warps.py. Re-export it only to a copy (Save As), never over
// it - a re-export drops the tiles, and the run record's coordinates address this map.
//
//   1 <-> 2   the column: a push tile on the hut's back wall (storey 0) and the hole straight above
//             it in deck A (storey 1)
//   3 <-> 4   the offset pair, #148's shape: a push tile on the closed cellar's back wall (storey 0)
//             and a hole in deck B one tile across and two back - going down lands beside 3, not
//             under the hole
//   5, 6, 7   the random group: 5 pushes into the hut's outer side wall (storey 0), 6 is a hole in
//             deck A's corner (storey 1), 7 is a plain tile in the yard (storey 0). Each sends Link
//             to one of the other two, picked every time it fires.
const RsWarpDest kTo1[] = { RS_WARP_TO(1) };
const RsWarpDest kTo2[] = { RS_WARP_TO(2) };
const RsWarpDest kTo3[] = { RS_WARP_TO(3) };
const RsWarpDest kTo4[] = { RS_WARP_TO(4) };
const RsWarpDest kFrom5[] = { RS_WARP_TO(6), RS_WARP_TO(7) };
const RsWarpDest kFrom6[] = { RS_WARP_TO(5), RS_WARP_TO(7) };
const RsWarpDest kFrom7[] = { RS_WARP_TO(5), RS_WARP_TO(6) };

const RsWarpTileDef kFixtureTiles[] = {
    { 1, RS_WARP_ENTRY_PUSH, 0, kTo2, ARRAY_COUNT(kTo2) },
    { 2, RS_WARP_ENTRY_STEP, 0, kTo1, ARRAY_COUNT(kTo1) },
    { 3, RS_WARP_ENTRY_PUSH, 0, kTo4, ARRAY_COUNT(kTo4) },
    { 4, RS_WARP_ENTRY_STEP, 0, kTo3, ARRAY_COUNT(kTo3) },
    { 5, RS_WARP_ENTRY_PUSH, 0, kFrom5, ARRAY_COUNT(kFrom5) },
    { 6, RS_WARP_ENTRY_STEP, 0, kFrom6, ARRAY_COUNT(kFrom6) },
    { 7, RS_WARP_ENTRY_STEP, 0, kFrom7, ARRAY_COUNT(kFrom7) },
};

const RsWarpSceneDef kFixtureScene = {
    SCENE_STEP_WARP_FIXTURE,
    "step_warp_fixture",
    kFixtureTiles,
    ARRAY_COUNT(kFixtureTiles),
};

// --- underground_entry_overworld (0x94, entrance 0x63D) and _basements (0x95, entrance 0x63E) ------
//
// The #148 test maps: getting Link from an overworld into an underground area and back, where the
// underground area is ITS OWN SCENE. A REFERENCE FIXTURE, like the one above: tiles spliced, never
// re-exported over. Each pair is a trapdoor (against a hut's north wall) and a ladder (against a
// cellar's north wall) that send Link to each other across the two scenes. The two cellars share one
// scene and one room - grouped basements, in miniature.
//
// Both ends are STEP tiles, the ladders included: they fire on any step onto them, from any
// direction, the way a vanilla grotto exit does. The ladders were push tiles until the owner played
// them (2026-09-28): crossing one at a shallow angle walked straight over it, and he wanted the
// vanilla behaviour. `push` stays in the code for a later use; the fixture above still exercises it.
//
//   overworld 1 <-> basements 1   hut A's trapdoor and cellar 1's ladder
//   overworld 2 <-> basements 2   hut B's trapdoor and cellar 2's ladder
const RsWarpDest kToCellar1[] = { RS_WARP_TO_SCENE(ENTR_UNDERGROUND_ENTRY_BASEMENTS_0, 1) };
const RsWarpDest kToCellar2[] = { RS_WARP_TO_SCENE(ENTR_UNDERGROUND_ENTRY_BASEMENTS_0, 2) };
const RsWarpDest kToHutA[] = { RS_WARP_TO_SCENE(ENTR_UNDERGROUND_ENTRY_OVERWORLD_0, 1) };
const RsWarpDest kToHutB[] = { RS_WARP_TO_SCENE(ENTR_UNDERGROUND_ENTRY_OVERWORLD_0, 2) };

const RsWarpTileDef kUndergroundOverworldTiles[] = {
    { 1, RS_WARP_ENTRY_STEP, 0, kToCellar1, ARRAY_COUNT(kToCellar1) },
    { 2, RS_WARP_ENTRY_STEP, 0, kToCellar2, ARRAY_COUNT(kToCellar2) },
};

const RsWarpTileDef kUndergroundBasementsTiles[] = {
    { 1, RS_WARP_ENTRY_STEP, 0, kToHutA, ARRAY_COUNT(kToHutA) },
    { 2, RS_WARP_ENTRY_STEP, 0, kToHutB, ARRAY_COUNT(kToHutB) },
};

const RsWarpSceneDef kUndergroundOverworldScene = {
    SCENE_UNDERGROUND_ENTRY_OVERWORLD,
    "underground_entry_overworld",
    kUndergroundOverworldTiles,
    ARRAY_COUNT(kUndergroundOverworldTiles),
};

const RsWarpSceneDef kUndergroundBasementsScene = {
    SCENE_UNDERGROUND_ENTRY_BASEMENTS,
    "underground_entry_basements",
    kUndergroundBasementsTiles,
    ARRAY_COUNT(kUndergroundBasementsTiles),
};

void RegisterWarpTables() {
    RsWarp_RegisterScene(&kFixtureScene);
    RsWarp_RegisterScene(&kUndergroundOverworldScene);
    RsWarp_RegisterScene(&kUndergroundBasementsScene);
}

RegisterShipInitFunc warpTableInitFunc(RegisterWarpTables);

// --- the malformed tables, for `warps badcheck` ---------------------------------------------------
//
// One per refusal the validator makes. APPEND ONLY: a run names these by index. Scene 0 is never
// registered, except where the row is ABOUT a scene clash.
const RsWarpDest kToSelf[] = { RS_WARP_TO(1) };
const RsWarpDest kToNowhere[] = { RS_WARP_TO(9) };
const RsWarpDest kTooMany[RS_WARP_MAX_DESTS + 1] = { RS_WARP_TO(2), RS_WARP_TO(2), RS_WARP_TO(2),
                                                     RS_WARP_TO(2), RS_WARP_TO(2), RS_WARP_TO(2),
                                                     RS_WARP_TO(2), RS_WARP_TO(2), RS_WARP_TO(2) };
// Another scene's tile, badly (#148): an entrance past the table, and one into the table's own scene
// (scene 0 is the Deku Tree, which ENTR_DEKU_TREE_ENTRANCE loads).
const RsWarpDest kToNoEntrance[] = { RS_WARP_TO_SCENE(ENTR_MAX, 1) };
const RsWarpDest kToOwnScene[] = { RS_WARP_TO_SCENE(ENTR_DEKU_TREE_ENTRANCE, 1) };

const RsWarpTileDef kBadId0[] = { { 0, RS_WARP_ENTRY_STEP, 0, kTo2, 1 }, { 2, RS_WARP_ENTRY_STEP, 0, nullptr, 0 } };
const RsWarpTileDef kBadId32[] = { { RS_WARP_TILE_ID_MAX + 1, RS_WARP_ENTRY_STEP, 0, nullptr, 0 } };
const RsWarpTileDef kBadDup[] = { { 2, RS_WARP_ENTRY_STEP, 0, nullptr, 0 }, { 2, RS_WARP_ENTRY_STEP, 0, nullptr, 0 } };
const RsWarpTileDef kBadEntry[] = { { 2, 7, 0, nullptr, 0 } };
const RsWarpTileDef kBadRoom[] = { { 2, RS_WARP_ENTRY_STEP, -1, nullptr, 0 } };
const RsWarpTileDef kBadCount[] = { { 1, RS_WARP_ENTRY_STEP, 0, kTooMany, ARRAY_COUNT(kTooMany) },
                                    { 2, RS_WARP_ENTRY_STEP, 0, nullptr, 0 } };
const RsWarpTileDef kBadNullDests[] = { { 1, RS_WARP_ENTRY_STEP, 0, nullptr, 1 } };
const RsWarpTileDef kBadSelf[] = { { 1, RS_WARP_ENTRY_STEP, 0, kToSelf, 1 } };
const RsWarpTileDef kBadDest[] = { { 1, RS_WARP_ENTRY_STEP, 0, kToNowhere, 1 } };
const RsWarpTileDef kGood[] = { { 1, RS_WARP_ENTRY_STEP, 0, kTo2, 1 }, { 2, RS_WARP_ENTRY_STEP, 0, kTo1, 1 } };
const RsWarpTileDef kBadEntrance[] = { { 1, RS_WARP_ENTRY_STEP, 0, kToNoEntrance, 1 } };
const RsWarpTileDef kBadEntranceHere[] = { { 1, RS_WARP_ENTRY_STEP, 0, kToOwnScene, 1 } };

const RsWarpSceneDef kBadDefs[] = {
    /* 0 */ { 0, "has space", kGood, ARRAY_COUNT(kGood) },
    /* 1 */ { 0, "no_tiles", nullptr, 0 },
    /* 2 */ { 0, "id_zero", kBadId0, ARRAY_COUNT(kBadId0) },
    /* 3 */ { 0, "id_too_big", kBadId32, ARRAY_COUNT(kBadId32) },
    /* 4 */ { 0, "duplicate", kBadDup, ARRAY_COUNT(kBadDup) },
    /* 5 */ { 0, "bad_entry", kBadEntry, ARRAY_COUNT(kBadEntry) },
    /* 6 */ { 0, "negative_room", kBadRoom, ARRAY_COUNT(kBadRoom) },
    /* 7 */ { 0, "too_many_dests", kBadCount, ARRAY_COUNT(kBadCount) },
    /* 8 */ { 0, "null_dests", kBadNullDests, ARRAY_COUNT(kBadNullDests) },
    /* 9 */ { 0, "to_itself", kBadSelf, ARRAY_COUNT(kBadSelf) },
    /* 10 */ { 0, "to_nowhere", kBadDest, ARRAY_COUNT(kBadDest) },
    /* 11 */ { SCENE_STEP_WARP_FIXTURE, "scene_taken", kGood, ARRAY_COUNT(kGood) },
    /* 12 */ { 0, "no_such_entrance", kBadEntrance, ARRAY_COUNT(kBadEntrance) },
    /* 13 */ { SCENE_DEKU_TREE, "entrance_into_itself", kBadEntranceHere, ARRAY_COUNT(kBadEntranceHere) },
};

} // namespace

int32_t RsWarpTable_BadCount() {
    return ARRAY_COUNT(kBadDefs);
}

const RsWarpSceneDef* RsWarpTable_Bad(int32_t index) {
    if (index < 0 || index >= RsWarpTable_BadCount()) {
        return nullptr;
    }
    return &kBadDefs[index];
}
