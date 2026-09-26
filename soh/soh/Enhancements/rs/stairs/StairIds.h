#ifndef SOH_RS_STAIR_IDS_H
#define SOH_RS_STAIR_IDS_H

#include <stdint.h>
#include "soh/Enhancements/rs/RsAssert.h"

// ============================================================================================
//  STAIRCASE IDS - BAKED INTO SCENE SOURCES AS ActorEntry.params. NEVER REORDER. NEVER REUSE.
// ============================================================================================
//
// A staircase (sturdy-bassoon#147) is one vertical link between storeys that the geometry cannot
// carry on its own - a shaft too tight for a walkable spiral, or a multi-storey ladder, which the
// 2026-09-25 ladder ADR closed for vanilla collision. Its id names a row of StairTable.cpp: one
// LANDING per storey it serves, which is where Link is put down when he picks that storey.
//
// Not serialized, but written into every placement's `params` (RsActorParams.h), so the rule is
// NpcIds.h's for the same reason: renumbering does not corrupt a save, it silently repoints every
// placement at somebody else's landings. Retire an id in place with a _RETIRED suffix, and give
// every enumerator an explicit `= N`.
//
// Bands mirror NpcIds.h: production staircases count up from 0, debug fixtures from
// RS_STAIR_ID_DEBUG_FIRST. RS_STAIR_MAX is what the params field and the text-id band are sized
// for (StairDef.h asserts both), so raising it is a band change, not a one-line edit.

#define RS_STAIR_MAX 256
#define RS_STAIR_ID_DEBUG_FIRST 192

typedef enum RsStairId {
    // --- production band: [0, RS_STAIR_ID_DEBUG_FIRST) ------------------------------------
    RS_STAIR_CASTLE_SOUTH_TOWER = 0, // Lumbridge castle (0x625), the 1x1 shaft at grid (32,29):
                                     // ground, first and second floor through one 40-unit hole
    RS_STAIR_CASTLE_NORTH_TOWER = 1, // the same, at grid (32,40)

    // --- debug band: [RS_STAIR_ID_DEBUG_FIRST, RS_STAIR_MAX) ------------------------------
    RS_STAIR_DEBUG_ROOMS = 192, // the ROOM-CHANGE fixture, in terrain_f2p_rooms_2x2 (0x62A): two
                                // landings in two different rooms, so a move has to load one and
                                // retire the other. The castle is a single room and cannot prove it.
} RsStairId;

#define RS_STAIR_ID_IS_VALID(id) ((id) >= 0 && (id) < RS_STAIR_MAX)
#define RS_STAIR_ID_IS_DEBUG(id) ((id) >= RS_STAIR_ID_DEBUG_FIRST)

RS_STATIC_ASSERT(RS_STAIR_ID_DEBUG_FIRST > 0, "the production band must be non-empty");
RS_STATIC_ASSERT(RS_STAIR_ID_DEBUG_FIRST < RS_STAIR_MAX, "the debug band must be non-empty");

RS_STATIC_ASSERT(RS_STAIR_CASTLE_SOUTH_TOWER >= 0 && RS_STAIR_CASTLE_SOUTH_TOWER < RS_STAIR_ID_DEBUG_FIRST,
                 "RS_STAIR_CASTLE_SOUTH_TOWER must sit in the production band");
RS_STATIC_ASSERT(RS_STAIR_CASTLE_NORTH_TOWER >= 0 && RS_STAIR_CASTLE_NORTH_TOWER < RS_STAIR_ID_DEBUG_FIRST,
                 "RS_STAIR_CASTLE_NORTH_TOWER must sit in the production band");
RS_STATIC_ASSERT(RS_STAIR_DEBUG_ROOMS >= RS_STAIR_ID_DEBUG_FIRST && RS_STAIR_DEBUG_ROOMS < RS_STAIR_MAX,
                 "RS_STAIR_DEBUG_ROOMS must sit in the debug band");

#endif // SOH_RS_STAIR_IDS_H
