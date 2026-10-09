#ifndef SOH_RS_INTERACTION_IDS_H
#define SOH_RS_INTERACTION_IDS_H

// Interaction ids and the interaction text band (sturdy-bassoon#183; the interactive-props ADR,
// sturdy-bassoon docs/decisions/2026-10-02-interactive-props.md).
//
// An INTERACTION is what a prop does when Link checks it. It is its own entity, defined once in the
// grid tool's global `tools/grid-scene-tool/interactions.json`, and the grid tool MINTS its id: the
// next free number, written `I-0042`, frozen, retired and never reused. This header does not number
// anything - it holds the bounds the grid tool numbers within, and the one macro the code table
// (InteractionTable.cpp) spells an id with, which is also what the grid tool reads back as "this id
// has code" (`RS_INTERACTION(<n>)`, src/shared/interactions.ts `codeTableIds`).
//
// THE ID IS NOT AN ENUM, unlike NpcId and QuestId, because it is not chosen here: it arrives in a
// scene's ActorEntry params from the grid tool, and this side only ever looks it up. A placement
// naming an id with no row says the placeholder; it is never refused.

#include <stdint.h>
#include "soh/Enhancements/rs/RsAssert.h"
#include "soh/Enhancements/rs/quest/QuestIds.h" // QuestTier
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h" // RS_DIALOGUE_MAX_*: the band is sized from them

// 15 bits, which is what the trigger actor's params carry (bit 15 stays zero, as every rs/ actor's
// does - RsActorParams.h rule 2). 0 is no interaction.
#define RS_INTERACTION_ID_MIN 1
#define RS_INTERACTION_ID_MAX 0x7FFF
// The debug band: test fixtures only, as NPC_ID_DEBUG_FIRST is for characters. The grid tool's
// INTERACTION_ID_DEBUG_FIRST (src/shared/interactions.ts) is the same number and must stay so.
#define RS_INTERACTION_ID_DEBUG_FIRST 0x7F00

#define RS_INTERACTION_ID_IS_VALID(id) ((id) >= RS_INTERACTION_ID_MIN && (id) <= RS_INTERACTION_ID_MAX)
#define RS_INTERACTION_ID_IS_DEBUG(id) ((id) >= RS_INTERACTION_ID_DEBUG_FIRST)
#define RS_INTERACTION_ID_TIER(id) (RS_INTERACTION_ID_IS_DEBUG(id) ? QUEST_TIER_DEBUG : QUEST_TIER_PROD)

// How the code table spells an id. A plain number - the macro exists so the grid tool can find the
// rows by text without parsing C, and so a grep for one interaction finds its row.
#define RS_INTERACTION(n) (n)

RS_STATIC_ASSERT(RS_INTERACTION_ID_DEBUG_FIRST > RS_INTERACTION_ID_MIN, "the production band must be non-empty");
RS_STATIC_ASSERT(RS_INTERACTION_ID_DEBUG_FIRST <= RS_INTERACTION_ID_MAX, "the debug band must be non-empty");

// --- the text band ------------------------------------------------------------------------------
//
// ONE FIXED BAND FOR EVERY INTERACTION, never a band per interaction. The 16-bit text space above
// 0xA000 is mostly taken (NpcDialogueDef.h), and a per-interaction band at NPC density would cap
// props near 256. Instead the id names only WHICH SCREEN of the speaker's definition is open, and
// the speaker is the actor Link is talking to: its params carry the interaction id. That is the
// ADR's rule, and it is why two props in range cannot render each other's line - Player picks
// exactly one talk actor - where the #117 prototype's shared direct-text slot let the last actor to
// update win.
//
// Which actor is talking, at hook time: `Player::talkActor`, on every box. NOT `msgCtx->talkActor`
// - Message_StartTextbox assigns that after Message_OpenText has already fired the hook
// (z_message_PAL.c), and only Message_StartOcarina clears it, so on a first box it names the
// previous partner, which a scene change may have freed (sturdy-bassoon#214). Player sets its own
// before it starts the box (z_player.c, Player_StartTalking) and holds it until the talk ends, so
// continued boxes find it too. RsActors.cpp reads it.
//
//   0xC100            ENTRY: the speaker's rule, resolved when the box opens. No rule index rides
//                     on the id, because it is the actor's to resolve and the hook resolves the
//                     same gates in the same frame.
//   0xC120 + node     a NODE of the speaker's tree (RS_DIALOGUE_MAX_NODES of them).
//   0xC180 + slot*4 + option
//                     an option's REPLY, rendered from the definition: slot is the rule (0..31) or
//                     32 + the node (32..63) the option was picked on. No direct-text slot - the
//                     reply is composed from the speaker's own definition when its box opens.
//
// 0xC001..0xC3FF was free (NpcDialogueDef.h's map); this takes 0xC100..0xC27F of it.
#define RS_TEXT_INTERACTION_BASE 0xC100
#define RS_TEXT_INTERACTION_ENTRY RS_TEXT_INTERACTION_BASE
#define RS_TEXT_INTERACTION_NODE_BASE (RS_TEXT_INTERACTION_BASE + 0x20)
#define RS_TEXT_INTERACTION_NODE_ID(node) ((uint16_t)(RS_TEXT_INTERACTION_NODE_BASE + (node)))
#define RS_TEXT_INTERACTION_REPLY_BASE (RS_TEXT_INTERACTION_BASE + 0x80)
// A reply slot is the screen its option was on: a rule, then a node.
#define RS_TEXT_INTERACTION_REPLY_SLOTS (RS_DIALOGUE_MAX_RULES + RS_DIALOGUE_MAX_NODES)
#define RS_TEXT_INTERACTION_REPLY_ID(slot, option) \
    ((uint16_t)(RS_TEXT_INTERACTION_REPLY_BASE + (slot) * RS_DIALOGUE_MAX_OPTIONS + (option)))
#define RS_TEXT_INTERACTION_END \
    (RS_TEXT_INTERACTION_REPLY_BASE + RS_TEXT_INTERACTION_REPLY_SLOTS * RS_DIALOGUE_MAX_OPTIONS - 1)

#define RS_TEXT_IS_INTERACTION(id) ((id) >= RS_TEXT_INTERACTION_BASE && (id) <= RS_TEXT_INTERACTION_END)
#define RS_TEXT_IS_INTERACTION_NODE(id) \
    ((id) >= RS_TEXT_INTERACTION_NODE_BASE && (id) < RS_TEXT_INTERACTION_NODE_BASE + RS_DIALOGUE_MAX_NODES)
#define RS_TEXT_IS_INTERACTION_REPLY(id) ((id) >= RS_TEXT_INTERACTION_REPLY_BASE && (id) <= RS_TEXT_INTERACTION_END)
#define RS_TEXT_INTERACTION_GET_NODE(id) ((int32_t)((id) - RS_TEXT_INTERACTION_NODE_BASE))
#define RS_TEXT_INTERACTION_GET_SLOT(id) ((int32_t)(((id) - RS_TEXT_INTERACTION_REPLY_BASE) / RS_DIALOGUE_MAX_OPTIONS))
#define RS_TEXT_INTERACTION_GET_OPTION(id) ((int32_t)(((id) - RS_TEXT_INTERACTION_REPLY_BASE) % RS_DIALOGUE_MAX_OPTIONS))

// What an interaction with no code yet says (the ADR), and what a placement naming an id with no
// row says. Player prose, so it is a plain sentence that names no storey.
#define RS_INTERACTION_PLACEHOLDER_TEXT "Nothing interesting happens."

RS_STATIC_ASSERT(RS_TEXT_INTERACTION_BASE > 0xC000, "the interaction band must sit above RS_TEXT_DIRECT");
RS_STATIC_ASSERT(RS_TEXT_INTERACTION_END < 0xC400, "the interaction band must end below the staircase band");
RS_STATIC_ASSERT(RS_TEXT_INTERACTION_ENTRY < RS_TEXT_INTERACTION_NODE_BASE, "the entry id must sit below the node ids");
RS_STATIC_ASSERT(RS_TEXT_INTERACTION_NODE_BASE + RS_DIALOGUE_MAX_NODES <= RS_TEXT_INTERACTION_REPLY_BASE,
                 "raising RS_DIALOGUE_MAX_NODES must not push an interaction node id onto its reply ids");

#endif // SOH_RS_INTERACTION_IDS_H
