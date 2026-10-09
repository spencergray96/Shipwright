#include "StairTable.h"
#include "Stairs.h"

#include "soh/ShipInit.hpp"

#include <vector>

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
// In lumbridge_castle_traversal (ENTR_LUMBRIDGE_CASTLE_TRAVERSAL_0), the frozen copy of the castle
// that keeps its staircases, so the grid tool's lumbridge_castle can be re-exported freely (#173). The
// geometry is the castle's as it was when these landings were read; the real castle has the same
// shafts but no placements, so these staircases do not exist there.
//
// Each shaft is an open 40x40 hole through the first- and second-floor slabs, with floor on all
// four sides at every storey (read off the castle's _scene_col.c, not assumed). Too small for
// the walkable spiral the 2x2 shafts got (2026-09-25 spiral ADR), and three storeys through one
// hole is the straight-shot ladder the ladder ADR closed - so both are this actor's by design.
//
// The placements stand at each shaft's centre, one per storey, turned toward the tower room (see
// CustomLumbridgeCastleTraversalScene.cpp); Link lands on the neighbouring cell, 40 in front. 40
// clears the placement's collider with room to spare and is inside its talk range, so he can turn
// round and go straight back. One room, so every row is room 0.
const RsStairLanding kCastleTower[] = {
    { 0, 0 },
    { 1, 0 },
    { 2, 0 },
};

const RsStairDef kStairCastleSouthTower = {
    RS_STAIR_CASTLE_SOUTH_TOWER, "castle_south_tower", SCENE_LUMBRIDGE_CASTLE_TRAVERSAL, 40, kCastleTower,
    ARRAY_COUNT(kCastleTower),
};

const RsStairDef kStairCastleNorthTower = {
    RS_STAIR_CASTLE_NORTH_TOWER, "castle_north_tower", SCENE_LUMBRIDGE_CASTLE_TRAVERSAL, 40, kCastleTower,
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

// 7937 (193 before #214) is deliberately UNPLACED - the fixture for the missing-placement paths. Its
// storey 0 is in the loaded room at spawn, so `stairs go 7937 0` must be refused up front
// (`no_placement`); its storey 1 is in room 3, so `stairs go 7937 1` can only find out after loading room 3, and must load room 2
// back and give Link back where he stood.
const RsStairLanding kDebugUnplaced[] = {
    { 0, 2 },
    { 1, 3 },
};

const RsStairDef kStairDebugUnplaced = {
    RS_STAIR_DEBUG_UNPLACED, "debug_unplaced", SCENE_TERRAIN_F2P_ROOMS_2X2, 40, kDebugUnplaced,
    ARRAY_COUNT(kDebugUnplaced),
};

// --- debug: the storey-height fixtures (#178) ------------------------------------------------------
//
// storey_heights_in_game_178: a standard, a tall, a grand building and the Falador Party Room (tall
// below, standard above), each with a 1x1 shaft at its east wall through the floor and the roof. One
// placement per storey, facing grid west (+X, the export mirrors X), so Link lands on the next tile in.
// The placements' heights come from `npm run tile:height` (sturdy-bassoon), never from "level x 80":
// that is the whole point of the fixture. A test map, so the placements are a hand edit in the
// generated scene .cpp (its banner says so) and a re-export drops them. One room.
const RsStairLanding kStoreyThree[] = {
    { 0, 0 },
    { 1, 0 },
    { 2, 0 },
};
const RsStairLanding kStoreyTwo[] = {
    { 0, 0 },
    { 1, 0 },
};

const RsStairDef kStairStoreyStandard = {
    RS_STAIR_DEBUG_STOREY_STANDARD, "storey_standard", SCENE_STOREY_HEIGHTS_IN_GAME_178, 40, kStoreyThree,
    ARRAY_COUNT(kStoreyThree),
};
const RsStairDef kStairStoreyTall = {
    RS_STAIR_DEBUG_STOREY_TALL, "storey_tall", SCENE_STOREY_HEIGHTS_IN_GAME_178, 40, kStoreyThree,
    ARRAY_COUNT(kStoreyThree),
};
const RsStairDef kStairStoreyGrand = {
    RS_STAIR_DEBUG_STOREY_GRAND, "storey_grand", SCENE_STOREY_HEIGHTS_IN_GAME_178, 40, kStoreyThree,
    ARRAY_COUNT(kStoreyThree),
};
const RsStairDef kStairStoreyParty = {
    RS_STAIR_DEBUG_STOREY_PARTY, "storey_party_room", SCENE_STOREY_HEIGHTS_IN_GAME_178, 40, kStoreyThree,
    ARRAY_COUNT(kStoreyThree),
};
// Its upper placement is deliberately wrong (StairIds.h): the move lands Link where it says, 50 under
// the floor, and he drops to the ground.
const RsStairDef kStairStoreyMistyped = {
    RS_STAIR_DEBUG_STOREY_MISTYPED, "storey_mistyped", SCENE_STOREY_HEIGHTS_IN_GAME_178, 40, kStoreyTwo,
    ARRAY_COUNT(kStoreyTwo),
};

// --- GENERATED staircases (#173 slice F3) ----------------------------------------------------------
//
// Every staircase the grid tool authored, from every exported scene's `<slug>_stairs.inc`, through the
// one aggregate the export keeps (the grid tool README, "The generated tables"). These rows are
// MAP-keyed - each names the map its placement is in - where the hand rows above are scene-keyed
// (the slice F ADR's decision 18); no scene holds both. RsStair_RegisterGenerated merges them by id.
// A function rather than an array initialiser, so an aggregate with no rows yet compiles.
void CollectGenerated(std::vector<RsStairGenRow>& rows, std::vector<RsStairGenOption>& options) {
#define RS_GEN_STAIR(stair, name, row, storey, map, room, destName) \
    rows.push_back({ (stair), (name), (row), (storey), (map), (room), (destName) });
#define RS_GEN_STAIR_OPTION(stair, row, toStorey, text) options.push_back({ (stair), (row), (toStorey), (text) });
#include "soh/custom/scenes/grid_tool/generated/GridToolStairs.inc"
#undef RS_GEN_STAIR
#undef RS_GEN_STAIR_OPTION
}

void RegisterStairs() {
    RsStair_Register(&kStairCastleSouthTower);
    RsStair_Register(&kStairCastleNorthTower);
    RsStair_Register(&kStairDebugRooms);
    RsStair_Register(&kStairDebugUnplaced);
    RsStair_Register(&kStairStoreyStandard);
    RsStair_Register(&kStairStoreyTall);
    RsStair_Register(&kStairStoreyGrand);
    RsStair_Register(&kStairStoreyParty);
    RsStair_Register(&kStairStoreyMistyped);

    // After the hand rows, so a generated id that clashes with one is the one refused (`id_taken`).
    std::vector<RsStairGenRow> rows;
    std::vector<RsStairGenOption> options;
    CollectGenerated(rows, options);
    RsStair_RegisterGenerated(rows.data(), static_cast<int32_t>(rows.size()), options.data(),
                              static_cast<int32_t>(options.size()));
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

// --- the malformed GENERATED tables, for `stairs badcheck` (#173 F3) -------------------------------
//
// Rows as an export would write them, one mistake per table. APPEND ONLY, like kBadDefs. Ids 8100 and
// up are in the debug band and never registered; the last-but-one table is about a clash with a
// registered hand fixture, and the last is a GOOD table that must be accepted: a row and an option
// carried twice, as a map's solo scene and a stitched scene that holds it both carry them.
const RsStairGenRow kGenNameDiffers[] = { { 8100, "gen_a", 0, 0, 1, 0, nullptr }, { 8100, "gen_b", 1, 1, 1, 0, nullptr } };
const RsStairGenRow kGenStoreyDiffers[] = { { 8101, "gen", 0, 0, 1, 0, nullptr },
                                            { 8101, "gen", 1, 1, 1, 0, nullptr },
                                            { 8101, "gen", 1, 2, 1, 0, nullptr } };
const RsStairGenRow kGenMapDiffers[] = { { 8102, "gen", 0, 0, 1, 0, nullptr },
                                         { 8102, "gen", 1, 1, 1, 0, nullptr },
                                         { 8102, "gen", 1, 1, 2, 0, nullptr } };
const RsStairGenRow kGenWordsDiffer[] = { { 8103, "gen", 0, 0, 1, 0, nullptr },
                                          { 8103, "gen", 1, 1, 1, 0, "the attic" },
                                          { 8103, "gen", 1, 1, 1, 0, "the loft" } };
const RsStairGenRow kGenTwoRows[] = { { 8104, "gen", 0, 0, 1, 0, nullptr }, { 8104, "gen", 1, 1, 1, 0, nullptr } };
const RsStairGenOption kGenOptionDiffers[] = { { 8104, 0, 1, "Climb up" }, { 8104, 0, 1, "Clamber up" } };
const RsStairGenRow kGenGap[] = { { 8105, "gen", 0, 0, 1, 0, nullptr }, { 8105, "gen", 2, 2, 1, 0, nullptr } };
const RsStairGenRow kGenTwoRows106[] = { { 8106, "gen", 0, 0, 1, 0, nullptr }, { 8106, "gen", 1, 1, 1, 0, nullptr } };
const RsStairGenOption kGenOrphan[] = { { 8106, 3, 1, "Up" } };
const RsStairGenRow kGenTwoRows107[] = { { 8107, "gen", 0, 0, 1, 0, nullptr }, { 8107, "gen", 1, 1, 1, 0, nullptr } };
const RsStairGenOption kGenOwnStorey[] = { { 8107, 0, 0, "Stay here" } };
const RsStairGenRow kGenThreeRows108[] = { { 8108, "gen", 0, 0, 1, 0, nullptr },
                                           { 8108, "gen", 1, 1, 1, 0, nullptr },
                                           { 8108, "gen", 2, 2, 1, 0, nullptr } };
// Too long for its row: since decision 23 this falls back instead of refusing, so the table is ACCEPTED
// (the expectation below changed with it; the words are checked in kWords).
const RsStairGenOption kGenTooLong[] = {
    { 8108, 0, 2, "Climb all the way up the winding stair to the very top of the tall tower" }
};
const RsStairGenRow kGenClash[] = { { RS_STAIR_DEBUG_STOREY_STANDARD, "gen", 0, 0, 1, 0, nullptr },
                                    { RS_STAIR_DEBUG_STOREY_STANDARD, "gen", 1, 1, 1, 0, nullptr } };
const RsStairGenRow kGenRepeatGood[] = { { 8109, "gen", 0, 0, 1, 0, nullptr },
                                         { 8109, "gen", 1, 1, 1, 0, "the attic" },
                                         { 8109, "gen", 0, 0, 1, 0, nullptr },
                                         { 8109, "gen", 1, 1, 1, 0, "the attic" } };
const RsStairGenOption kGenRepeatGoodOptions[] = { { 8109, 0, 1, "Climb to {floor:1}" },
                                                   { 8109, 0, 1, "Climb to {floor:1}" } };

const RsStairBadGen kBadGen[] = {
    /* 0 */ { kGenNameDiffers, ARRAY_COUNT(kGenNameDiffers), nullptr, 0, RS_STAIR_PROBLEM_ROWS_DISAGREE },
    /* 1 */ { kGenStoreyDiffers, ARRAY_COUNT(kGenStoreyDiffers), nullptr, 0, RS_STAIR_PROBLEM_ROWS_DISAGREE },
    /* 2 */ { kGenMapDiffers, ARRAY_COUNT(kGenMapDiffers), nullptr, 0, RS_STAIR_PROBLEM_ROWS_DISAGREE },
    /* 3 */ { kGenWordsDiffer, ARRAY_COUNT(kGenWordsDiffer), nullptr, 0, RS_STAIR_PROBLEM_ROWS_DISAGREE },
    /* 4 */
    { kGenTwoRows, ARRAY_COUNT(kGenTwoRows), kGenOptionDiffers, ARRAY_COUNT(kGenOptionDiffers),
      RS_STAIR_PROBLEM_ROWS_DISAGREE },
    /* 5 */ { kGenGap, ARRAY_COUNT(kGenGap), nullptr, 0, RS_STAIR_PROBLEM_ROW_MISSING },
    /* 6 */
    { kGenTwoRows106, ARRAY_COUNT(kGenTwoRows106), kGenOrphan, ARRAY_COUNT(kGenOrphan), RS_STAIR_PROBLEM_OPTION_ORPHAN },
    /* 7 */
    { kGenTwoRows107, ARRAY_COUNT(kGenTwoRows107), kGenOwnStorey, ARRAY_COUNT(kGenOwnStorey), RS_STAIR_PROBLEM_BAD_WORDS },
    /* 8 */
    { kGenThreeRows108, ARRAY_COUNT(kGenThreeRows108), kGenTooLong, ARRAY_COUNT(kGenTooLong),
      RS_STAIR_PROBLEM_NONE },
    /* 9 */ { kGenClash, ARRAY_COUNT(kGenClash), nullptr, 0, RS_STAIR_PROBLEM_ID_TAKEN },
    /* 10 */
    { kGenRepeatGood, ARRAY_COUNT(kGenRepeatGood), kGenRepeatGoodOptions, ARRAY_COUNT(kGenRepeatGoodOptions),
      RS_STAIR_PROBLEM_NONE },
};

// --- the words that fall back, for `stairs badcheck` (decision 23, #173 F3) ------------------------
//
// Each staircase registers - none of these is refused - and each must drop exactly `fallbacks` of its
// words for not fitting. Widths are the renderer's pixel table: an option row is 184 px, and
// "Down to " is 57 of them, so a destination name has 127 when another storey points DOWN at it. The
// three balconies straddle that: 126, 127 (exactly at the limit) and 133 px. Ids 8120 and up, never
// registered.
const char kLongName[] = "the very top of the party room's great balcony";
const char kLongOverride[] = "Climb all the way up to the balcony at the very top of the tower";
const RsStairOverride kLongToStorey1[] = { { 1, kLongOverride } };
const RsStairOverride kLongToStorey2[] = { { 2, kLongOverride } };

const RsStairLanding kWordsTwoOverride[] = { { 0, 0, 0, nullptr, kLongToStorey1, 1 }, { 1, 0 } };
const RsStairLanding kWordsTwoName[] = { { 0, 0 }, { 1, 0, 0, kLongName } };
const RsStairLanding kWordsThreeName[] = { { 0, 0 }, { 1, 0 }, { 2, 0, 0, kLongName } };
const RsStairLanding kWordsNearLimit[] = { { 0, 0 }, { 1, 0, 0, "the musicians balcony" }, { 2, 0 } };
const RsStairLanding kWordsAtLimit[] = { { 0, 0 }, { 1, 0, 0, "the minstrels balcony" }, { 2, 0 } };
const RsStairLanding kWordsOverLimit[] = { { 0, 0 }, { 1, 0, 0, "the bandstand balcony" }, { 2, 0 } };
const RsStairLanding kWordsChain[] = { { 0, 0, 0, nullptr, kLongToStorey2, 1 }, { 1, 0 }, { 2, 0, 0, kLongName } };

const RsStairDef kWordsDefs[] = {
    { 8120, "words_two_override", 0, 40, kWordsTwoOverride, ARRAY_COUNT(kWordsTwoOverride) },
    { 8121, "words_two_name", 0, 40, kWordsTwoName, ARRAY_COUNT(kWordsTwoName) },
    { 8122, "words_three_name", 0, 40, kWordsThreeName, ARRAY_COUNT(kWordsThreeName) },
    { 8123, "words_near_limit", 0, 40, kWordsNearLimit, ARRAY_COUNT(kWordsNearLimit) },
    { 8124, "words_at_limit", 0, 40, kWordsAtLimit, ARRAY_COUNT(kWordsAtLimit) },
    { 8125, "words_over_limit", 0, 40, kWordsOverLimit, ARRAY_COUNT(kWordsOverLimit) },
    { 8126, "words_chain", 0, 40, kWordsChain, ARRAY_COUNT(kWordsChain) },
};

const RsStairWordsCase kWords[] = {
    /* 0 */ { &kWordsDefs[0], 1 }, // the two-storey question, overridden too long: back to "Go up to ..."
    /* 1 */ { &kWordsDefs[1], 1 }, // the two-storey question, named too long
    /* 2 */ { &kWordsDefs[2], 2 }, // the roof named too long: rows 0 and 1 both point at it
    /* 3 */ { &kWordsDefs[3], 0 }, // 126 px of name: fits under "Down to "
    /* 4 */ { &kWordsDefs[4], 0 }, // 127 px: exactly the row
    /* 5 */ { &kWordsDefs[5], 1 }, // 133 px: fits "Up to " from the ground, not "Down to " from the roof
    /* 6 */ { &kWordsDefs[6], 3 }, // override AND name too long: the ground's option drops both, row 1 the name
};

} // namespace

int32_t RsStairTable_WordsCount() {
    return ARRAY_COUNT(kWords);
}

const RsStairWordsCase* RsStairTable_Words(int32_t index) {
    return index >= 0 && index < RsStairTable_WordsCount() ? &kWords[index] : nullptr;
}

int32_t RsStairTable_BadGenCount() {
    return ARRAY_COUNT(kBadGen);
}

const RsStairBadGen* RsStairTable_BadGen(int32_t index) {
    if (index < 0 || index >= RsStairTable_BadGenCount()) {
        return nullptr;
    }
    return &kBadGen[index];
}

int32_t RsStairTable_BadCount() {
    return ARRAY_COUNT(kBadDefs);
}

const RsStairDef* RsStairTable_Bad(int32_t index) {
    if (index < 0 || index >= RsStairTable_BadCount()) {
        return nullptr;
    }
    return &kBadDefs[index];
}
