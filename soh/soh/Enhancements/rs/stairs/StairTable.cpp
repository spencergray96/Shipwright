#include "StairTable.h"
#include "Stairs.h"

#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h> // the SCENE_* ids a staircase's landings belong to
#include "macros.h"
}

// ============================================================================================
//  THE STAIRCASE TABLE  (sturdy-bassoon#147)
// ============================================================================================
//
// One RsStairDef per staircase, one row per storey, bottom first. THERE ARE NO COORDINATES HERE:
// Link lands `landForward` in front of the placement for the storey he picks, facing the way it
// faces (StairDef.h). So re-drawing a shaft is moving its placements in the scene; this file changes
// only when the storeys a staircase joins, or their rooms, do.

namespace {

// --- Lumbridge castle, the two 1x1 tower shafts -------------------------------------------------
//
// Each shaft is an open 40x40 hole through the first- and second-floor slabs, with floor on all
// four sides at every storey (read off lumbridge_castle_scene_col.c, not assumed). Too small for
// the walkable spiral the 2x2 shafts got (2026-09-25 spiral ADR), and three storeys through one
// hole is the straight-shot ladder the ladder ADR closed - so both are this actor's by design.
//
// The placements stand at each shaft's centre, one per storey, turned toward the tower room (see
// CustomLumbridgeCastleScene.cpp); Link lands on the neighbouring cell, 40 in front. 40 clears the
// placement's collider with room to spare and is inside its talk range, so he can turn round and
// go straight back. One room, so every row is room 0.
const RsStairLanding kCastleTower[] = {
    { 0, 0 },
    { 1, 0 },
    { 2, 0 },
};

const RsStairDef kStairCastleSouthTower = {
    RS_STAIR_CASTLE_SOUTH_TOWER, "castle_south_tower", SCENE_LUMBRIDGE_CASTLE, 40, kCastleTower,
    ARRAY_COUNT(kCastleTower),
};

const RsStairDef kStairCastleNorthTower = {
    RS_STAIR_CASTLE_NORTH_TOWER, "castle_north_tower", SCENE_LUMBRIDGE_CASTLE, 40, kCastleTower,
    ARRAY_COUNT(kCastleTower),
};

// --- debug: the room-change fixtures ------------------------------------------------------------
//
// terrain_f2p_rooms_2x2 (0x62A) is four rooms in a 2x2 grid split at X -60 / Z -60 (read off each
// room's cull box). The castle is one room and cannot prove any of this.
//
// 192 is PLACED (CustomTerrainF2pRooms2x2Scene.cpp, one placement per room): storey 0 in room 2
// beside the spawn, storey 1 in the diagonally opposite room 1 on a plateau 80 up. A move between
// them loads one room, retires the other - and with it the placement that opened the menu - and
// must land in front of a placement that did not exist until its room loaded. The "storeys" are
// only labels here.
const RsStairLanding kDebugRooms[] = {
    { 0, 2 },
    { 1, 1 },
};

const RsStairDef kStairDebugRooms = {
    RS_STAIR_DEBUG_ROOMS, "debug_rooms", SCENE_TERRAIN_F2P_ROOMS_2X2, 40, kDebugRooms, ARRAY_COUNT(kDebugRooms),
};

// 193 is deliberately UNPLACED - the fixture for the missing-placement paths. Its storey 0 is in the
// loaded room at spawn, so `stairs go 193 0` must be refused up front (`no_placement`); its storey
// 1 is in room 3, so `stairs go 193 1` can only find out after loading room 3, and must load room 2
// back and give Link back where he stood.
const RsStairLanding kDebugUnplaced[] = {
    { 0, 2 },
    { 1, 3 },
};

const RsStairDef kStairDebugUnplaced = {
    RS_STAIR_DEBUG_UNPLACED, "debug_unplaced", SCENE_TERRAIN_F2P_ROOMS_2X2, 40, kDebugUnplaced,
    ARRAY_COUNT(kDebugUnplaced),
};

void RegisterStairs() {
    RsStair_Register(&kStairCastleSouthTower);
    RsStair_Register(&kStairCastleNorthTower);
    RsStair_Register(&kStairDebugRooms);
    RsStair_Register(&kStairDebugUnplaced);
}

RegisterShipInitFunc stairTableInitFunc(RegisterStairs);

// --- the malformed table, for `stairs badcheck` ---------------------------------------------------
//
// One row per refusal the validator makes. APPEND ONLY: an acceptance script names these by index
// (`bad[3] problem=storey_order`), so inserting one renumbers every line after it. None of these is
// ever registered; the validator is asked about each one and reports.
//
// Ids are in the debug band and none is registered, except where the row is ABOUT an id clash.
const RsStairLanding kBadOneRow[] = { { 0, 0 } };
const RsStairLanding kBadFiveRows[] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 } };
const RsStairLanding kBadStorey[] = { { 0, 0 }, { 10, 0 } };
const RsStairLanding kBadOrder[] = { { 1, 0 }, { 0, 0 } };
const RsStairLanding kBadRepeat[] = { { 0, 0 }, { 0, 0 } };
const RsStairLanding kBadRoom[] = { { 0, 0 }, { 1, -1 } };
const RsStairLanding kGood[] = { { 0, 0 }, { 1, 0 } };

const RsStairDef kBadDefs[] = {
    /* 0 */ { RS_STAIR_MAX, "bad_id", 0, 40, kGood, ARRAY_COUNT(kGood) },
    /* 1 */ { 250, "has space", 0, 40, kGood, ARRAY_COUNT(kGood) },
    /* 2 */ { 250, nullptr, 0, 40, kGood, ARRAY_COUNT(kGood) },
    /* 3 */ { 250, "one_row", 0, 40, kBadOneRow, ARRAY_COUNT(kBadOneRow) },
    /* 4 */ { 250, "five_rows", 0, 40, kBadFiveRows, ARRAY_COUNT(kBadFiveRows) },
    /* 5 */ { 250, "null_landings", 0, 40, nullptr, 2 },
    /* 6 */ { 250, "storey_ten", 0, 40, kBadStorey, ARRAY_COUNT(kBadStorey) },
    /* 7 */ { 250, "descending", 0, 40, kBadOrder, ARRAY_COUNT(kBadOrder) },
    /* 8 */ { 250, "same_storey_twice", 0, 40, kBadRepeat, ARRAY_COUNT(kBadRepeat) },
    /* 9 */ { 250, "negative_room", 0, 40, kBadRoom, ARRAY_COUNT(kBadRoom) },
    /* 10 */ { RS_STAIR_CASTLE_SOUTH_TOWER, "id_taken", 0, 40, kGood, ARRAY_COUNT(kGood) },
    /* 11 */ { 250, "lands_in_collider", 0, RS_STAIR_MIN_LAND_FORWARD - 1, kGood, ARRAY_COUNT(kGood) },
};

} // namespace

int32_t RsStairTable_BadCount() {
    return ARRAY_COUNT(kBadDefs);
}

const RsStairDef* RsStairTable_Bad(int32_t index) {
    if (index < 0 || index >= RsStairTable_BadCount()) {
        return nullptr;
    }
    return &kBadDefs[index];
}
