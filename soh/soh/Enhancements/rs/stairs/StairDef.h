#ifndef SOH_RS_STAIR_DEF_H
#define SOH_RS_STAIR_DEF_H

#include <stdint.h>
#include "soh/Enhancements/rs/RsAssert.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h" // the text-id bands this one sits between
#include "StairIds.h"

// ============================================================================================
//  WHAT A STAIRCASE IS, AS DATA  (sturdy-bassoon#147)
// ============================================================================================
//
// Plain C, like QuestJournalDef.h, so a table reads the same from either language. One
// RsStairDef per staircase, registered from StairTable.cpp; one RsStairLanding per storey it
// serves. The actor placed on each storey carries (staircase id, row) in `params`
// (RsActorParams.h) and nothing else - every coordinate lives HERE, because `params` is one s16
// and cannot hold a position. That is the quest-giver's shape: params names the thing, the rules
// live in code.
//
// THE LANDING TABLE IS THE WHOLE OF A STAIRCASE'S GEOMETRY. Re-drawing a shaft - the castle's
// towers may become two one-storey ladders later - is a table edit and a placement edit, and no
// code changes.

// Rows per staircase, and the menu is why. Picking a destination is a choice box, the widest box
// is four rows (2026-09-07 ADR), and one of the four is Cancel - so three destinations, which is
// four storeys. A fifth storey means authored paging (a "More..." row, NPC_DEBUG_PAGE's shape),
// not a bigger number here. It is also the width of the row field in `params` and in the text id.
#define RS_STAIR_MAX_ROWS 4

typedef struct RsStairLanding {
    // The STOREY INDEX this row is, 0..9 - what `{floor:N}` names in the menu, never a label
    // (REGION_SETTINGS.md). Rows are ordered by it, strictly ascending, so "up" and "down" are a
    // comparison of row numbers and the menu can list them without sorting.
    int32_t storey;

    // Where Link is put down: on the floor of that storey, in world units. Registration cannot see
    // collision, so a landing in mid-air registers clean and drops him - `stairs dump` prints each
    // one, and the `landed` marker's `floor_y=` is what proves it in-game.
    int32_t x;
    int32_t y;
    int32_t z;

    // Which way he faces on arrival, s16 binary angle (0 = +Z, 0x4000 = +X, -0x8000 = -Z).
    // Facing AWAY from the shaft is the convention: stick-forward walks him off rather than back
    // into the collider that guards the hole.
    int32_t yaw;

    // The room this landing is in. A move into a different room loads it and retires the old one
    // (Room_RequestNewRoom / Room_FinishRoomChange), exactly as a door does; within one room it is
    // a no-op. Checked against the live scene's room count before any move, never here, because
    // registration does not know which scene will be loaded.
    int32_t room;
} RsStairLanding;

typedef struct RsStairDef {
    int32_t id;       // RsStairId (StairIds.h)
    const char* name; // snake_case token for console lines and markers

    // The scene whose coordinates the landings are in. A staircase is a PLACE, not a character, so
    // unlike an NpcId it does not travel: a placement in any other scene would put Link down at
    // coordinates that mean nothing there, and the mover refuses it (`wrong_scene`) rather than
    // teleporting him into the void. This is the one reason the stairs code reads a scene number.
    int32_t sceneId;

    const RsStairLanding* landings;
    int32_t landingCount; // [2, RS_STAIR_MAX_ROWS]
} RsStairDef;

// --- the staircase MENU text band ---------------------------------------------------------------
//
//     0xC400 + (stairId << 2) + row      256 staircases x 4 rows  ->  0xC400..0xC7FF
//
// The menu a staircase opens depends on exactly two things - which staircase, and which storey Link
// is standing on - so both ride in the id, and rendering needs no state. Same argument as the
// quest-giver's entry box (NpcDialogueDef.h): two staircases in talk range at once cannot render
// each other's menu. It sits in the gap between the direct-text id (0xC000) and the quest-item
// pickup band (0xC800), which nothing else uses; SoH's own highest custom id is 0x9215.
#define RS_TEXT_STAIR_BASE 0xC400
#define RS_TEXT_STAIR_ROW_SHIFT 2
#define RS_TEXT_STAIR_ID(stairId, row) ((uint16_t)(RS_TEXT_STAIR_BASE + ((stairId) << RS_TEXT_STAIR_ROW_SHIFT) + (row)))
#define RS_TEXT_STAIR_GET_ID(textId) ((int32_t)(((textId)-RS_TEXT_STAIR_BASE) >> RS_TEXT_STAIR_ROW_SHIFT))
#define RS_TEXT_STAIR_GET_ROW(textId) ((int32_t)(((textId)-RS_TEXT_STAIR_BASE) & (RS_STAIR_MAX_ROWS - 1)))
#define RS_TEXT_STAIR_END (RS_TEXT_STAIR_BASE + (RS_STAIR_MAX << RS_TEXT_STAIR_ROW_SHIFT) - 1)
#define RS_TEXT_IS_STAIR(textId) ((textId) >= RS_TEXT_STAIR_BASE && (textId) <= RS_TEXT_STAIR_END)

RS_STATIC_ASSERT(RS_STAIR_MAX_ROWS == (1 << RS_TEXT_STAIR_ROW_SHIFT),
                 "the stair text id's row field width and RS_STAIR_MAX_ROWS are the same number");
RS_STATIC_ASSERT(RS_STAIR_MAX_ROWS <= RS_DIALOGUE_MAX_OPTIONS,
                 "a staircase's menu lists every other row plus Cancel, and the box is the cap");
RS_STATIC_ASSERT(RS_TEXT_DIRECT < RS_TEXT_STAIR_BASE, "the stair band must start above RS_TEXT_DIRECT");
RS_STATIC_ASSERT(RS_TEXT_STAIR_END < RS_TEXT_ITEM_BASE,
                 "raising RS_STAIR_MAX must not push a stair text id onto the quest-item pickup band");

#endif // SOH_RS_STAIR_DEF_H
