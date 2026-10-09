#include "WarpTable.h"
#include "Warps.h"

#include "soh/ShipInit.hpp"

#include <vector>

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

// --- storey_heights_in_game_178: the #178 slice 3 test map ------------------------------------------
//
// Storey height classes in game: a standard, a tall and a grand building side by side (80 / 100 /
// 130 a storey). A TEST FIXTURE, spliced like the ones above (tools/step-warps/
// storey_heights_in_game_178.tiles.json), so a re-export drops the tiles. Each building has a stacked
// pair in its north-west corner: a step tile on the ground and a step tile over the hole straight
// above it, each landing east. The hole's cover sits at its landing's floor - 84, 104 and 134 - which
// the splice used to put at 84 in all three.
//
//   1 <-> 2   standard (the control)
//   3 <-> 4   tall
//   5 <-> 6   grand
const RsWarpDest kStoreyTo1[] = { RS_WARP_TO(1) };
const RsWarpDest kStoreyTo2[] = { RS_WARP_TO(2) };
const RsWarpDest kStoreyTo3[] = { RS_WARP_TO(3) };
const RsWarpDest kStoreyTo4[] = { RS_WARP_TO(4) };
const RsWarpDest kStoreyTo5[] = { RS_WARP_TO(5) };
const RsWarpDest kStoreyTo6[] = { RS_WARP_TO(6) };

const RsWarpTileDef kStoreyHeightsTiles[] = {
    { 1, RS_WARP_ENTRY_STEP, 0, kStoreyTo2, ARRAY_COUNT(kStoreyTo2) },
    { 2, RS_WARP_ENTRY_STEP, 0, kStoreyTo1, ARRAY_COUNT(kStoreyTo1) },
    { 3, RS_WARP_ENTRY_STEP, 0, kStoreyTo4, ARRAY_COUNT(kStoreyTo4) },
    { 4, RS_WARP_ENTRY_STEP, 0, kStoreyTo3, ARRAY_COUNT(kStoreyTo3) },
    { 5, RS_WARP_ENTRY_STEP, 0, kStoreyTo6, ARRAY_COUNT(kStoreyTo6) },
    { 6, RS_WARP_ENTRY_STEP, 0, kStoreyTo5, ARRAY_COUNT(kStoreyTo5) },
};

const RsWarpSceneDef kStoreyHeightsScene = {
    SCENE_STOREY_HEIGHTS_IN_GAME_178,
    "storey_heights_in_game_178",
    kStoreyHeightsTiles,
    ARRAY_COUNT(kStoreyHeightsTiles),
};

// --- GENERATED warp tiles (#173 slice F3) ----------------------------------------------------------
//
// Every warp tile the grid tool authored, from every exported scene's `<slug>_warps.inc`, through the
// one aggregate the export keeps (the grid tool README, "The generated tables"). MAP-keyed: a tile's
// local id is unique within its map, and its destinations name (map, local id). The hand tables above
// are scene-keyed (the slice F ADR's decision 18), and no scene holds both. A function rather than an
// array initialiser, so an aggregate with no rows yet compiles.
void CollectGenerated(std::vector<RsWarpGenRow>& rows, std::vector<RsWarpGenDest>& dests) {
#define RS_GEN_WARP(map, tile, entry, room) rows.push_back({ (map), (tile), (entry), (room) });
#define RS_GEN_WARP_DEST(map, tile, toMap, toTile) dests.push_back({ (map), (tile), (toMap), (toTile) });
#include "soh/custom/scenes/grid_tool/generated/GridToolWarps.inc"
#undef RS_GEN_WARP
#undef RS_GEN_WARP_DEST
}

void RegisterWarpTables() {
    RsWarp_RegisterScene(&kFixtureScene);
    RsWarp_RegisterScene(&kUndergroundOverworldScene);
    RsWarp_RegisterScene(&kUndergroundBasementsScene);
    RsWarp_RegisterScene(&kStoreyHeightsScene);

    std::vector<RsWarpGenRow> rows;
    std::vector<RsWarpGenDest> dests;
    CollectGenerated(rows, dests);
    RsWarp_RegisterGenerated(rows.data(), static_cast<int32_t>(rows.size()), dests.data(),
                             static_cast<int32_t>(dests.size()));
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

// --- the malformed GENERATED tables, for `warps badcheck` (#173 F3) ---------------------------------
//
// APPEND ONLY, like kBadDefs. Maps 90 and up are never in any scene. Each warp table has one mistake;
// the last is a GOOD one that must be accepted: a map's rows carried twice, as its solo scene and a
// stitched scene both carry them, destinations and all.
const RsWarpGenRow kGenPair[] = { { 90, 1, RS_WARP_ENTRY_STEP, 0 }, { 90, 2, RS_WARP_ENTRY_STEP, 0 } };
const RsWarpGenDest kGenPairDests[] = { { 90, 1, 90, 2 }, { 90, 2, 90, 1 } };
const RsWarpGenRow kGenEntryDiffers[] = { { 91, 1, RS_WARP_ENTRY_STEP, 0 },
                                          { 91, 2, RS_WARP_ENTRY_STEP, 0 },
                                          { 91, 1, RS_WARP_ENTRY_PUSH, 0 } };
const RsWarpGenDest kGenEntryDiffersDests[] = { { 91, 1, 91, 2 }, { 91, 2, 91, 1 }, { 91, 1, 91, 2 } };
const RsWarpGenRow kGenDestsDiffer[] = { { 92, 1, RS_WARP_ENTRY_STEP, 0 },
                                         { 92, 2, RS_WARP_ENTRY_STEP, 0 },
                                         { 92, 3, RS_WARP_ENTRY_STEP, 0 },
                                         { 92, 1, RS_WARP_ENTRY_STEP, 0 } };
const RsWarpGenDest kGenDestsDifferDests[] = { { 92, 1, 92, 2 }, { 92, 1, 92, 3 } };
const RsWarpGenDest kGenOrphanDests[] = { { 90, 1, 90, 2 }, { 90, 2, 90, 1 }, { 90, 5, 90, 1 } };
const RsWarpGenDest kGenNowhereDests[] = { { 90, 1, 90, 9 } };
const RsWarpGenDest kGenSelfDests[] = { { 90, 1, 90, 1 } };
const RsWarpGenRow kGenMapZero[] = { { 0, 1, RS_WARP_ENTRY_STEP, 0 } };
const RsWarpGenRow kGenTooBig[] = { { 93, RS_WARP_TILE_ID_MAX + 1, RS_WARP_ENTRY_STEP, 0 } };
const RsWarpGenRow kGenRepeatGood[] = { { 94, 1, RS_WARP_ENTRY_STEP, 0 },
                                        { 94, 2, RS_WARP_ENTRY_STEP, 0 },
                                        { 94, 1, RS_WARP_ENTRY_STEP, 0 },
                                        { 94, 2, RS_WARP_ENTRY_STEP, 0 } };
const RsWarpGenDest kGenRepeatGoodDests[] = { { 94, 1, 94, 2 }, { 94, 2, 94, 1 }, { 94, 1, 94, 2 },
                                              { 94, 2, 94, 1 } };

const RsWarpBadGen kBadGen[] = {
    /* 0 */
    { kGenEntryDiffers, ARRAY_COUNT(kGenEntryDiffers), kGenEntryDiffersDests, ARRAY_COUNT(kGenEntryDiffersDests),
      RS_WARP_PROBLEM_ROWS_DISAGREE },
    /* 1 */
    { kGenDestsDiffer, ARRAY_COUNT(kGenDestsDiffer), kGenDestsDifferDests, ARRAY_COUNT(kGenDestsDifferDests),
      RS_WARP_PROBLEM_ROWS_DISAGREE },
    /* 2 */ { kGenPair, ARRAY_COUNT(kGenPair), kGenOrphanDests, ARRAY_COUNT(kGenOrphanDests), RS_WARP_PROBLEM_DEST_ORPHAN },
    /* 3 */ { kGenPair, ARRAY_COUNT(kGenPair), kGenNowhereDests, ARRAY_COUNT(kGenNowhereDests), RS_WARP_PROBLEM_BAD_DEST },
    /* 4 */ { kGenPair, ARRAY_COUNT(kGenPair), kGenSelfDests, ARRAY_COUNT(kGenSelfDests), RS_WARP_PROBLEM_SELF_DEST },
    /* 5 */ { kGenMapZero, ARRAY_COUNT(kGenMapZero), nullptr, 0, RS_WARP_PROBLEM_BAD_MAP },
    /* 6 */ { kGenTooBig, ARRAY_COUNT(kGenTooBig), nullptr, 0, RS_WARP_PROBLEM_BAD_ID },
    /* 7 */
    { kGenRepeatGood, ARRAY_COUNT(kGenRepeatGood), kGenRepeatGoodDests, ARRAY_COUNT(kGenRepeatGoodDests),
      RS_WARP_PROBLEM_NONE },
};

// The map->scene validator's (SceneMaps.h). The step warp fixture's scene and entrance stand in for an
// exported scene: the validator checks only that the entrance loads the scene the row names.
const RsGenSceneRow kScene[] = { { SCENE_STEP_WARP_FIXTURE, ENTR_STEP_WARP_FIXTURE_0, RS_GEN_WORLD_NEUTRAL } };
const RsGenSceneRow kSceneTwice[] = { { SCENE_STEP_WARP_FIXTURE, ENTR_STEP_WARP_FIXTURE_0, RS_GEN_WORLD_NEUTRAL },
                                      { SCENE_STEP_WARP_FIXTURE, ENTR_STEP_WARP_FIXTURE_0, RS_GEN_WORLD_SOLO } };
const RsGenSceneRow kSceneWrongEntrance[] = { { SCENE_STEP_WARP_FIXTURE, ENTR_STOREY_HEIGHTS_IN_GAME_178_0,
                                                RS_GEN_WORLD_NEUTRAL } };
const RsGenSceneRow kSceneBadWorld[] = { { SCENE_STEP_WARP_FIXTURE, ENTR_STEP_WARP_FIXTURE_0, -7 } };
const RsGenSceneMapRow kOneMap[] = { { SCENE_STEP_WARP_FIXTURE, { 1, -240, -200, 240, 200 } } };
const RsGenSceneMapRow kOtherScene[] = { { SCENE_STEP_WARP_FIXTURE, { 1, -240, -200, 240, 200 } },
                                         { SCENE_STOREY_HEIGHTS_IN_GAME_178, { 2, -240, -200, 240, 200 } } };
const RsGenSceneMapRow kMapZero[] = { { SCENE_STEP_WARP_FIXTURE, { 0, -240, -200, 240, 200 } } };
const RsGenSceneMapRow kMapTwice[] = { { SCENE_STEP_WARP_FIXTURE, { 1, -240, -200, 0, 200 } },
                                       { SCENE_STEP_WARP_FIXTURE, { 1, 0, -200, 240, 200 } } };
const RsGenSceneMapRow kFlatRect[] = { { SCENE_STEP_WARP_FIXTURE, { 1, 240, -200, 240, 200 } } };
const RsGenSceneMapRow kOverlap[] = { { SCENE_STEP_WARP_FIXTURE, { 1, -240, -200, 1, 200 } },
                                      { SCENE_STEP_WARP_FIXTURE, { 2, 0, -200, 240, 200 } } };
// Two chunks side by side, sharing the edge x = 0: min <= p < max puts x = 0 in the second only.
const RsGenSceneMapRow kTwoChunks[] = { { SCENE_STEP_WARP_FIXTURE, { 1, -240, -200, 0, 200 } },
                                        { SCENE_STEP_WARP_FIXTURE, { 2, 0, -200, 240, 200 } } };

const RsMapsBad kBadMaps[] = {
    /* 0 */ { kSceneTwice, ARRAY_COUNT(kSceneTwice), kOneMap, ARRAY_COUNT(kOneMap), RS_MAPS_PROBLEM_SCENE_TWICE },
    /* 1 */
    { kSceneWrongEntrance, ARRAY_COUNT(kSceneWrongEntrance), kOneMap, ARRAY_COUNT(kOneMap),
      RS_MAPS_PROBLEM_BAD_ENTRANCE },
    /* 2 */ { kSceneBadWorld, ARRAY_COUNT(kSceneBadWorld), kOneMap, ARRAY_COUNT(kOneMap), RS_MAPS_PROBLEM_BAD_WORLD },
    /* 3 */ { kScene, ARRAY_COUNT(kScene), kOtherScene, ARRAY_COUNT(kOtherScene), RS_MAPS_PROBLEM_NO_SCENE },
    /* 4 */ { kScene, ARRAY_COUNT(kScene), nullptr, 0, RS_MAPS_PROBLEM_NO_MAPS },
    /* 5 */ { kScene, ARRAY_COUNT(kScene), kMapZero, ARRAY_COUNT(kMapZero), RS_MAPS_PROBLEM_BAD_MAP },
    /* 6 */ { kScene, ARRAY_COUNT(kScene), kMapTwice, ARRAY_COUNT(kMapTwice), RS_MAPS_PROBLEM_MAP_TWICE },
    /* 7 */ { kScene, ARRAY_COUNT(kScene), kFlatRect, ARRAY_COUNT(kFlatRect), RS_MAPS_PROBLEM_BAD_RECT },
    /* 8 */ { kScene, ARRAY_COUNT(kScene), kOverlap, ARRAY_COUNT(kOverlap), RS_MAPS_PROBLEM_OVERLAP },
    /* 9 */ { kScene, ARRAY_COUNT(kScene), kTwoChunks, ARRAY_COUNT(kTwoChunks), RS_MAPS_PROBLEM_NONE },
};

} // namespace

int32_t RsWarpTable_BadGenCount() {
    return ARRAY_COUNT(kBadGen);
}

const RsWarpBadGen* RsWarpTable_BadGen(int32_t index) {
    return index >= 0 && index < RsWarpTable_BadGenCount() ? &kBadGen[index] : nullptr;
}

int32_t RsWarpTable_BadMapsCount() {
    return ARRAY_COUNT(kBadMaps);
}

const RsMapsBad* RsWarpTable_BadMaps(int32_t index) {
    return index >= 0 && index < RsWarpTable_BadMapsCount() ? &kBadMaps[index] : nullptr;
}

int32_t RsWarpTable_BadCount() {
    return ARRAY_COUNT(kBadDefs);
}

const RsWarpSceneDef* RsWarpTable_Bad(int32_t index) {
    if (index < 0 || index >= RsWarpTable_BadCount()) {
        return nullptr;
    }
    return &kBadDefs[index];
}
