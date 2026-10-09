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
// (RsActorParams.h).
//
// THE TABLE HOLDS NO COORDINATES. Where Link lands is read off the PLACEMENT for the storey he is
// going to: `landForward` units in front of it, on its floor, facing the way it faces
// (Stairs.cpp, LandingFromPlacement). So everything spatial about a staircase is where its
// placements stand and which way they are turned - scene data, which an exporter (the grid tool
// or Blender) emits anyway - and nobody converts grid cells to world units by hand. Moving a
// landing is moving its placement; resizing a map moves the landings with it. What the table keeps
// is what a placement cannot say: which storeys the staircase joins, and which room each is in.

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

    // The room this storey's placement - and so its landing - is in. A move into a different room
    // loads it and retires the old one (Room_RequestNewRoom / Room_FinishRoomChange), exactly as a
    // door does; within one room it is a no-op. The table has to say, because a placement in a room
    // that is not loaded does not exist yet: the move loads the room FIRST and then looks for it.
    // Checked against the live scene's room count before any move, never here, because
    // registration does not know which scene will be loaded.
    int32_t room;
} RsStairLanding;

// The shortest `landForward` that puts Link down clear of the placement he lands in front of: its
// collider (20, RsStairs.c) plus adult Link's (12), plus one. Any closer and the collision push
// shoves him on the first frame, which reads as the landing jittering.
#define RS_STAIR_MIN_LAND_FORWARD 33

typedef struct RsStairDef {
    int32_t id;       // RsStairId (StairIds.h)
    const char* name; // snake_case token for console lines and markers

    // The scene the staircase is in. A staircase is a PLACE, not a character, so unlike an NpcId it
    // does not travel: its rooms are that scene's rooms, and the mover refuses a move anywhere else
    // (`wrong_scene`) rather than loading a room number that means something different there. This
    // is the one reason the stairs code reads a scene number.
    int32_t sceneId;

    // How far in front of a placement Link is put down, in world units, at least
    // RS_STAIR_MIN_LAND_FORWARD. "In front" is the placement's own facing (its ActorEntry rot.y),
    // and he faces that way on arrival - away from the shaft, by convention, so stick-forward walks
    // him off rather than back into the collider that guards the hole.
    int32_t landForward;

    const RsStairLanding* landings;
    int32_t landingCount; // [2, RS_STAIR_MAX_ROWS]
} RsStairDef;

// --- the staircase MENU text band ---------------------------------------------------------------
//
//     0xC400 + row      4 ids  ->  0xC400..0xC403
//
// The menu a staircase opens depends on exactly two things - which staircase, and which storey Link
// is standing on. Until sturdy-bassoon#214 both rode in the text id (0xC400 + (id << 2) + row, 256
// staircases in 0xC400..0xC7FF). 8,192 staircases x 4 rows would need 32,768 text ids, and the
// whole 16-bit space above SoH's own highest custom id (0x9215) is 28,138 - so the id cannot ride.
//
// Now the text id carries only the ROW, and WHICH staircase is the actor Link is talking to: its
// params carry the id (RsActorParams.h). That is the interaction band's rule (InteractionIds.h,
// #183), read the same way - `Player::talkActor` at hook time, never a shared slot - so two
// staircases in talk range still cannot render each other's menu: Player picks exactly one talk
// actor. Two placements on the same row of different staircases now share a text id, so the actor
// tells its own box from another's by the talk actor, not the id (RsStairs.c, RsStairs_Talk).
// 0xC404..0xC7FF is free again.
#define RS_TEXT_STAIR_BASE 0xC400
#define RS_TEXT_STAIR_ID(row) ((uint16_t)(RS_TEXT_STAIR_BASE + (row)))
#define RS_TEXT_STAIR_GET_ROW(textId) ((int32_t)((textId)-RS_TEXT_STAIR_BASE))
#define RS_TEXT_STAIR_END (RS_TEXT_STAIR_BASE + RS_STAIR_MAX_ROWS - 1)
#define RS_TEXT_IS_STAIR(textId) ((textId) >= RS_TEXT_STAIR_BASE && (textId) <= RS_TEXT_STAIR_END)

RS_STATIC_ASSERT(RS_STAIR_MAX_ROWS <= RS_DIALOGUE_MAX_OPTIONS,
                 "a staircase's menu lists every other row plus Cancel, and the box is the cap");
RS_STATIC_ASSERT(RS_TEXT_DIRECT < RS_TEXT_STAIR_BASE, "the stair band must start above RS_TEXT_DIRECT");
RS_STATIC_ASSERT(RS_TEXT_STAIR_END < RS_TEXT_ITEM_BASE,
                 "a stair text id must not reach the quest-item pickup band");

#endif // SOH_RS_STAIR_DEF_H
