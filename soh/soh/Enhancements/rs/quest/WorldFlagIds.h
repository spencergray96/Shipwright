#ifndef SOH_RS_WORLD_FLAG_IDS_H
#define SOH_RS_WORLD_FLAG_IDS_H

#include "soh/Enhancements/rs/RsAssert.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"

// ============================================================================================
//  WORLD FLAG IDS - SERIALIZED INTO SAVE FILES. NEVER REORDER. NEVER REUSE. NEVER RENUMBER.
// ============================================================================================
//
// Names for the bits in the project world-flag store (worldstate/WorldFlags.h, sturdy-bassoon#54).
// The number is what a save file carries, so a flag that has shipped into a save that matters
// keeps its number forever; retire it in place (_RETIRED suffix + comment) rather than deleting
// or renumbering. Every enumerator carries an explicit `= N` - never rely on implicit increment.
//
// worldFlags holds NON-quest world state ("this door is open", "this NPC has been met"). Quest
// progress lives in the `quests` section (QuestStore.h); do not mirror quest steps here.
//
// Bands mirror QuestIds.h: production flags count up from 0, debug/test flags count up from
// WORLD_FLAG_DEBUG_FIRST (the top 256 of the store). `quest debugwipe` (P1) clears the debug band.
// WORLD_FLAG_MAX may be raised freely (see WorldFlags.h); if it is, WORLD_FLAG_DEBUG_FIRST stays
// where it is - the debug band simply grows.
//
// Scope rule (D-pitfalls): these flags serve this mod's OWN actors only. Vanilla actors are reused
// for models and animation, never for persistent state.

#define WORLD_FLAG_DEBUG_FIRST 3840

typedef enum WorldFlagId {
    // --- production band: [0, WORLD_FLAG_DEBUG_FIRST) -------------------------------------
    // None yet. The first real entries arrive with the quest-giver NPC and quest items (P3/P4):
    //     WORLD_FLAG_COOK_MET = 0,
    // Add each one with an explicit number and a RS_STATIC_ASSERT line below.

    // --- debug band: [WORLD_FLAG_DEBUG_FIRST, WORLD_FLAG_MAX) -----------------------------
    WORLD_FLAG_DEBUG_SMOKE = 3840, // exercised by the agent-test predicate probe; never real state
} WorldFlagId;

#define WORLD_FLAG_IS_DEBUG(flag) ((flag) >= WORLD_FLAG_DEBUG_FIRST)

RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_FIRST > 0, "the production band must be non-empty");
RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_FIRST < WORLD_FLAG_MAX, "the debug band must be non-empty");

RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_SMOKE >= WORLD_FLAG_DEBUG_FIRST && WORLD_FLAG_DEBUG_SMOKE < WORLD_FLAG_MAX,
                 "WORLD_FLAG_DEBUG_SMOKE must sit in the debug band");

#endif // SOH_RS_WORLD_FLAG_IDS_H
