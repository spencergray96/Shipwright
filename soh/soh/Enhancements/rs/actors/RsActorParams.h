#ifndef SOH_RS_ACTOR_PARAMS_H
#define SOH_RS_ACTOR_PARAMS_H

#include <stdint.h>
#include "soh/Enhancements/rs/RsAssert.h"
#include "soh/Enhancements/rs/quest/QuestIds.h"
#include "soh/Enhancements/rs/dialogue/NpcIds.h"

// ============================================================================================
//  WHAT AN RS ACTOR'S `params` CARRIES  (sturdy-bassoon#58 P3 / #64 - the D25 decision)
// ============================================================================================
//
// `ActorEntry.params` is an `s16` in the compiled scene file, and it is the ONLY per-placement
// datum a scene can hand an actor. It carries the NpcId for a quest-giver, and the (quest, step)
// pair for a quest item.
//
// This is not gated on the grid tool (D24). Placement arrives in three stages: hand-inserted
// ActorEntry rows in a scene's C source now (all P3's proofs need), standard Blender + Fast64
// actor placement for authored content later - which carries `params` today - and possibly-never
// grid-tool helpers. Nothing here should be designed for the grid tool, and the grid tool emitting
// a literal 0x0000 is a description of its scope, not a gap.
//
// TWO RULES WORTH THE UP-FRONT COST:
//
// 1. The MASK IS BAKED IN NOW, rather than "params IS the id, we will add flags later". Changing
//    the packing after entries exist does not fail - it silently reinterprets every placement
//    already authored in a scene file or a .blend. Reserved bits must read zero today, and an
//    actor that finds them set says so.
//
// 2. BIT 15 IS PERMANENTLY ZERO, so every value is a non-negative `s16`. Fast64 authors params as
//    a plain positive number, nothing on any path sign-extends, and a future flag cannot make an
//    existing entry go negative. That is why the reserved span is 0x7000 and not 0xF000.

// --- quest-giver NPC ---------------------------------------------------------------------------

#define RS_NPC_PARAMS_ID_MASK 0x0FFF   // bits 0-11: NpcId
#define RS_NPC_PARAMS_RSVD_MASK 0x7000 // bits 12-14: reserved, must read zero today
#define RS_NPC_PARAMS(npcId) ((int16_t)((npcId) & RS_NPC_PARAMS_ID_MASK))
#define RS_NPC_PARAMS_GET_ID(params) ((int32_t)((uint16_t)(params) & RS_NPC_PARAMS_ID_MASK))
#define RS_NPC_PARAMS_GET_RSVD(params) ((int32_t)((uint16_t)(params) & RS_NPC_PARAMS_RSVD_MASK))

RS_STATIC_ASSERT(NPC_MAX <= RS_NPC_PARAMS_ID_MASK + 1, "an NpcId must fit in the params id field");
RS_STATIC_ASSERT((RS_NPC_PARAMS_ID_MASK & RS_NPC_PARAMS_RSVD_MASK) == 0, "params fields must not overlap");
RS_STATIC_ASSERT((RS_NPC_PARAMS_ID_MASK | RS_NPC_PARAMS_RSVD_MASK) <= 0x7FFF, "bit 15 stays zero: params is a signed s16");

// --- quest item --------------------------------------------------------------------------------
//
// A quest item is fully described by the (quest, step) pair it sets - there is deliberately no
// third id space. The pair is already frozen data: D3 freezes a shipped quest's step meanings, so
// nothing new becomes un-renumberable by putting it here.

#define RS_ITEM_PARAMS_STEP_MASK 0x001F  // bits 0-4: step
#define RS_ITEM_PARAMS_QUEST_SHIFT 5     // bits 5-10: QuestId
#define RS_ITEM_PARAMS_QUEST_MASK 0x003F
#define RS_ITEM_PARAMS(questId, step)                                                                                  \
    ((int16_t)(((((questId) & RS_ITEM_PARAMS_QUEST_MASK) << RS_ITEM_PARAMS_QUEST_SHIFT)) |                              \
               ((step) & RS_ITEM_PARAMS_STEP_MASK)))
#define RS_ITEM_PARAMS_GET_STEP(params) ((int32_t)((uint16_t)(params) & RS_ITEM_PARAMS_STEP_MASK))
#define RS_ITEM_PARAMS_GET_QUEST(params)                                                                               \
    ((int32_t)(((uint16_t)(params) >> RS_ITEM_PARAMS_QUEST_SHIFT) & RS_ITEM_PARAMS_QUEST_MASK))
#define RS_ITEM_PARAMS_RSVD_MASK 0x7800 // bits 11-14: reserved, must read zero today
#define RS_ITEM_PARAMS_GET_RSVD(params) ((int32_t)((uint16_t)(params) & RS_ITEM_PARAMS_RSVD_MASK))

RS_STATIC_ASSERT(QUEST_MAX <= RS_ITEM_PARAMS_QUEST_MASK + 1, "a QuestId must fit in the params quest field");
RS_STATIC_ASSERT(QUEST_STEP_MAX <= RS_ITEM_PARAMS_STEP_MASK + 1, "a step must fit in the params step field");
RS_STATIC_ASSERT((RS_ITEM_PARAMS_QUEST_MASK << RS_ITEM_PARAMS_QUEST_SHIFT) <= 0x7FFF,
                 "bit 15 stays zero: params is a signed s16");
RS_STATIC_ASSERT(((RS_ITEM_PARAMS_QUEST_MASK << RS_ITEM_PARAMS_QUEST_SHIFT) & RS_ITEM_PARAMS_STEP_MASK) == 0,
                 "params fields must not overlap");

#endif // SOH_RS_ACTOR_PARAMS_H
