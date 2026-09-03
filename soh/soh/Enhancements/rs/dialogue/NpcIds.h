#ifndef SOH_RS_NPC_IDS_H
#define SOH_RS_NPC_IDS_H

#include <stdint.h>
#include "soh/Enhancements/rs/RsAssert.h"
#include "soh/Enhancements/rs/quest/QuestIds.h" // QuestTier and the band-naming convention

// ============================================================================================
//  NPC IDS - BAKED INTO SCENE SOURCES AS ActorEntry.params. NEVER REORDER. NEVER REUSE.
// ============================================================================================
//
// An NpcId is the identity of a CHARACTER (sturdy-bassoon#58 D21), orthogonal to two things it is
// easy to conflate with:
//
//     actor type       the code and behaviour     recycled - one type serves many NPCs
//     model/skeleton   the visual                 recycled - RS-style reuse is expected
//     NpcId            the character              UNIQUE, and STABLE ACROSS SCENES
//
// One character placed in several scenes is ONE NPC: same id, same dialogue rule table, same
// state. That works with no per-scene handling at all because quest progress and world flags are
// GLOBAL stores (D22) - unlike vanilla's per-scene `swch`/`chest`/`collect`/`clear`, which is
// exactly why an NPC placed twice in vanilla silently keeps two sets of state. Nothing in this
// system may branch on `sceneNum`.
//
// WHY "NEVER REORDER" WHEN THIS IS NOT SERIALIZED. An NpcId is not written to a save file. It IS
// written into every scene's compiled `ActorEntry.params` (RsActorParams.h) - so renumbering does
// not corrupt a save, it silently REPOINTS every existing placement at a different character.
// Same failure class, different file. Retire an id in place with a _RETIRED suffix rather than
// deleting or reusing it, and give every enumerator an explicit `= N`.
//
// Bands mirror QuestIds.h: production characters count up from 0, debug/test ones from
// NPC_ID_DEBUG_FIRST. RsNpc_Register refuses a definition whose declared tier does not match its
// band, and a debug NPC may not set a production world flag (`quest debugwipe` could not clear it).
// NPC_MAX may be RAISED, never lowered - but see the textId assert in NpcDialogueDef.h, which is
// what actually binds it.

#define NPC_MAX 256
#define NPC_ID_DEBUG_FIRST 192

typedef enum NpcId {
    // --- production band: [0, NPC_ID_DEBUG_FIRST) ------------------------------------------
    NPC_COOK = 0, // sturdy-bassoon#58 P4 - the Cook's Assistant quest-giver. Declared here, defined
                  // by nothing yet; an unregistered id renders a visible diagnostic, never silence.

    // --- debug band: [NPC_ID_DEBUG_FIRST, NPC_MAX) ----------------------------------------
    NPC_DEBUG_GIVER = 192, // the P3 quest-giver (dialogue/npcs/DebugNpcs.cpp). Placed TWICE in
                           // 0x614 and once in 0x629 - the multi-scene and double-fire proofs.
    NPC_DEBUG_THREE = 193, // one rule carrying THREE options: the live proof that the response
                           // shape is not binary (D11, sturdy-bassoon#59).
    NPC_DEBUG_TWIN = 194,  // a DIFFERENT character on the same actor type and the same model as
                           // 192 - the other axis of D21, proven by it being unaffected by 192.
} NpcId;

#define NPC_ID_IS_VALID(id) ((id) >= 0 && (id) < NPC_MAX)
#define NPC_ID_IS_DEBUG(id) ((id) >= NPC_ID_DEBUG_FIRST)
#define NPC_ID_TIER(id) (NPC_ID_IS_DEBUG(id) ? QUEST_TIER_DEBUG : QUEST_TIER_PROD)

RS_STATIC_ASSERT(NPC_ID_DEBUG_FIRST > 0, "the production band must be non-empty");
RS_STATIC_ASSERT(NPC_ID_DEBUG_FIRST < NPC_MAX, "the debug band must be non-empty");

RS_STATIC_ASSERT(NPC_COOK >= 0 && NPC_COOK < NPC_ID_DEBUG_FIRST, "NPC_COOK must sit in the production band");
RS_STATIC_ASSERT(NPC_DEBUG_GIVER >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_GIVER < NPC_MAX,
                 "NPC_DEBUG_GIVER must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_THREE >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_THREE < NPC_MAX,
                 "NPC_DEBUG_THREE must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_TWIN >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_TWIN < NPC_MAX,
                 "NPC_DEBUG_TWIN must sit in the debug band");

#endif // SOH_RS_NPC_IDS_H
