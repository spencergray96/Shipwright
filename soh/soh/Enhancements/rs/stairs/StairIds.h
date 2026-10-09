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
    RS_STAIR_CASTLE_SOUTH_TOWER = 0, // lumbridge_castle_traversal, the frozen copy of Lumbridge
                                     // castle (#173); the 1x1 shaft at grid (32,29):
                                     // ground, first and second floor through one 40-unit hole
    RS_STAIR_CASTLE_NORTH_TOWER = 1, // the same, at grid (32,40)

    // --- debug band: [RS_STAIR_ID_DEBUG_FIRST, RS_STAIR_MAX) ------------------------------
    RS_STAIR_DEBUG_ROOMS = 192, // the ROOM-CHANGE fixture, in terrain_f2p_rooms_2x2 (0x62A): two
                                // landings in two different rooms, so a move has to load one and
                                // retire the other. The castle is a single room and cannot prove it.
    RS_STAIR_DEBUG_UNPLACED = 193, // the MISSING-PLACEMENT fixture, same scene: rows but no placements,
                                   // one storey in the loaded room and one in a room that is not, so
                                   // both ways a move can find no placement to land in front of
    // The #178 storey-height fixtures, in storey_heights_in_game_178: one 1x1 shaft per building,
    // ground to roof, so the landings are each building's real floors (80 / 100 / 130 a storey).
    RS_STAIR_DEBUG_STOREY_STANDARD = 194, // 0 / 84 / 164, the control
    RS_STAIR_DEBUG_STOREY_TALL = 195,     // 0 / 104 / 204
    RS_STAIR_DEBUG_STOREY_GRAND = 196,    // 0 / 134 / 264
    RS_STAIR_DEBUG_STOREY_PARTY = 197,    // 0 / 104 / 184: the Party Room, tall below, standard above
    RS_STAIR_DEBUG_STOREY_MISTYPED = 198, // the grand building again, its upper placement typed at 84
                                          // ("level x 80" + slab) where the floor is 134: the planted
                                          // mistake `bad_placement reason=off_floor` must catch
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
RS_STATIC_ASSERT(RS_STAIR_DEBUG_UNPLACED >= RS_STAIR_ID_DEBUG_FIRST && RS_STAIR_DEBUG_UNPLACED < RS_STAIR_MAX,
                 "RS_STAIR_DEBUG_UNPLACED must sit in the debug band");
RS_STATIC_ASSERT(RS_STAIR_DEBUG_STOREY_STANDARD >= RS_STAIR_ID_DEBUG_FIRST &&
                     RS_STAIR_DEBUG_STOREY_MISTYPED < RS_STAIR_MAX,
                 "the #178 storey-height fixtures must sit in the debug band");

#endif // SOH_RS_STAIR_IDS_H
