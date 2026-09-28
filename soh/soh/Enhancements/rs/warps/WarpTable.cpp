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
const int32_t kTo1[] = { 1 };
const int32_t kTo2[] = { 2 };
const int32_t kTo3[] = { 3 };
const int32_t kTo4[] = { 4 };
const int32_t kFrom5[] = { 6, 7 };
const int32_t kFrom6[] = { 5, 7 };
const int32_t kFrom7[] = { 5, 6 };

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

void RegisterWarpTables() {
    RsWarp_RegisterScene(&kFixtureScene);
}

RegisterShipInitFunc warpTableInitFunc(RegisterWarpTables);

// --- the malformed tables, for `warps badcheck` ---------------------------------------------------
//
// One per refusal the validator makes. APPEND ONLY: a run names these by index. Scene 0 is never
// registered, except where the row is ABOUT a scene clash.
const int32_t kToSelf[] = { 1 };
const int32_t kToNowhere[] = { 9 };
const int32_t kTooMany[RS_WARP_MAX_DESTS + 1] = { 2, 2, 2, 2, 2, 2, 2, 2, 2 };

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
