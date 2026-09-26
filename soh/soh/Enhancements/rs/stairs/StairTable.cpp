#include "StairTable.h"
#include "Stairs.h"

#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h> // the SCENE_* ids a staircase's landings belong to
#include "macros.h"
}

// ============================================================================================
//  THE LANDING TABLE  (sturdy-bassoon#147)
// ============================================================================================
//
// One RsStairDef per staircase, one row per storey, bottom first. Every coordinate a staircase has
// is here - the placements in each scene carry only (staircase id, row) - so re-drawing a shaft is
// an edit to this file and to the scene's placement rows, and nothing else.
//
// Grid -> world for the castle (62x79 tiles at 40 units, mirrored in X - the #147 test-site
// comment and tools/castle-stairs/README.md): worldX = 1240 - x*40, worldZ = y*40 - 1580, a cell
// spanning 40 units from there. Floor heights are what the export has at these cells: 0, 84, 164.

namespace {

// --- Lumbridge castle, the two 1x1 tower shafts -------------------------------------------------
//
// Each shaft is an open 40x40 hole through the first- and second-floor slabs, with floor on all
// four sides at every storey (read off lumbridge_castle_scene_col.c, not assumed). Too small for
// the walkable spiral the 2x2 shafts got (2026-09-25 spiral ADR), and three storeys through one
// hole is the straight-shot ladder the ladder ADR closed - so both are this actor's by design.
//
// Landing: the neighbouring cell on the ROOM side of the shaft, 40 units from its centre, facing
// away from it. 40 clears the staircase's own collider (20) plus Link's (12) with room to spare,
// and is inside its talk range, so he can turn round and go straight back.

// South tower: shaft at grid (32,29), centre X -60 Z -400. The tower rooms run +Z from it, so the
// landing is the +Z neighbour (32,30), centre Z -360, facing +Z.
const RsStairLanding kCastleSouthTower[] = {
    { 0, -60, 0, -360, 0x0000, 0 },
    { 1, -60, 84, -360, 0x0000, 0 },
    { 2, -60, 164, -360, 0x0000, 0 },
};

// North tower: shaft at grid (32,40), centre X -60 Z 40. The room runs -Z from it, so the landing
// is the -Z neighbour (32,39), centre Z 0, facing -Z.
const RsStairLanding kCastleNorthTower[] = {
    { 0, -60, 0, 0, -0x8000, 0 },
    { 1, -60, 84, 0, -0x8000, 0 },
    { 2, -60, 164, 0, -0x8000, 0 },
};

const RsStairDef kStairCastleSouthTower = {
    RS_STAIR_CASTLE_SOUTH_TOWER, "castle_south_tower", SCENE_LUMBRIDGE_CASTLE,
    kCastleSouthTower,           ARRAY_COUNT(kCastleSouthTower),
};

const RsStairDef kStairCastleNorthTower = {
    RS_STAIR_CASTLE_NORTH_TOWER, "castle_north_tower", SCENE_LUMBRIDGE_CASTLE,
    kCastleNorthTower,           ARRAY_COUNT(kCastleNorthTower),
};

// --- debug: the room-change fixture -------------------------------------------------------------
//
// terrain_f2p_rooms_2x2 (0x62A) is four rooms in a 2x2 grid split at X -60 / Z -60 (read off each
// room's cull box). Row 0 is beside the spawn, in room 2 (-X, +Z); row 1 is in the diagonally
// opposite room 1 (+X, -Z), on a plateau 80 units up. The "storeys" are only labels here - what
// the fixture proves is that a move into another room loads it, retires the old one, and survives
// the retirement of the actor that asked for it. Heights from the collision file (2.9 and 80.0).
const RsStairLanding kDebugRooms[] = {
    { 0, -1540, 3, 1900, -0x8000, 2 },
    { 1, 1540, 80, -1900, 0x0000, 1 },
};

const RsStairDef kStairDebugRooms = {
    RS_STAIR_DEBUG_ROOMS, "debug_rooms", SCENE_TERRAIN_F2P_ROOMS_2X2, kDebugRooms, ARRAY_COUNT(kDebugRooms),
};

void RegisterStairs() {
    RsStair_Register(&kStairCastleSouthTower);
    RsStair_Register(&kStairCastleNorthTower);
    RsStair_Register(&kStairDebugRooms);
}

RegisterShipInitFunc stairTableInitFunc(RegisterStairs);

// --- the malformed table, for `stairs badcheck` ---------------------------------------------------
//
// One row per refusal the validator makes. APPEND ONLY: an acceptance script names these by index
// (`bad[3] problem=storey_order`), so inserting one renumbers every line after it. None of these is
// ever registered; the validator is asked about each one and reports.
//
// Ids are in the debug band and none is registered, except where the row is ABOUT an id clash.
const RsStairLanding kBadOneRow[] = { { 0, 0, 0, 0, 0, 0 } };
const RsStairLanding kBadFiveRows[] = { { 0, 0, 0, 0, 0, 0 }, { 1, 0, 80, 0, 0, 0 }, { 2, 0, 160, 0, 0, 0 },
                                        { 3, 0, 240, 0, 0, 0 }, { 4, 0, 320, 0, 0, 0 } };
const RsStairLanding kBadStorey[] = { { 0, 0, 0, 0, 0, 0 }, { 10, 0, 80, 0, 0, 0 } };
const RsStairLanding kBadOrder[] = { { 1, 0, 80, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } };
const RsStairLanding kBadRepeat[] = { { 0, 0, 0, 0, 0, 0 }, { 0, 0, 80, 0, 0, 0 } };
const RsStairLanding kBadRoom[] = { { 0, 0, 0, 0, 0, 0 }, { 1, 0, 80, 0, 0, -1 } };
const RsStairLanding kGood[] = { { 0, 0, 0, 0, 0, 0 }, { 1, 0, 80, 0, 0, 0 } };

const RsStairDef kBadDefs[] = {
    /* 0 */ { RS_STAIR_MAX, "bad_id", 0, kGood, ARRAY_COUNT(kGood) },
    /* 1 */ { 250, "has space", 0, kGood, ARRAY_COUNT(kGood) },
    /* 2 */ { 250, nullptr, 0, kGood, ARRAY_COUNT(kGood) },
    /* 3 */ { 250, "one_row", 0, kBadOneRow, ARRAY_COUNT(kBadOneRow) },
    /* 4 */ { 250, "five_rows", 0, kBadFiveRows, ARRAY_COUNT(kBadFiveRows) },
    /* 5 */ { 250, "null_landings", 0, nullptr, 2 },
    /* 6 */ { 250, "storey_ten", 0, kBadStorey, ARRAY_COUNT(kBadStorey) },
    /* 7 */ { 250, "descending", 0, kBadOrder, ARRAY_COUNT(kBadOrder) },
    /* 8 */ { 250, "same_storey_twice", 0, kBadRepeat, ARRAY_COUNT(kBadRepeat) },
    /* 9 */ { 250, "negative_room", 0, kBadRoom, ARRAY_COUNT(kBadRoom) },
    /* 10 */ { RS_STAIR_CASTLE_SOUTH_TOWER, "id_taken", 0, kGood, ARRAY_COUNT(kGood) },
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
