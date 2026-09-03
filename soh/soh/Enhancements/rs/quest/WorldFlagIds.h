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
// WORLD_FLAG_DEBUG_FIRST (the top 256 of the store). `quest debugwipe` clears the debug band, and
// Quest_Register refuses a quest whose world-flag reward sits in the other tier's band.
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
    WORLD_FLAG_DEBUG_SMOKE = 3840,   // exercised by the agent-test predicate probe; never real state
    WORLD_FLAG_DEBUG_JOURNAL = 3841, // QUEST_DEBUG_JOURNAL's completion witness (P2). Its OWN flag on
                                     // purpose: sharing 3840 would couple it to the P1 smoke fixture,
                                     // whose acceptance run asserts that flag's value directly.
    WORLD_FLAG_DEBUG_GIVER = 3842,   // QUEST_DEBUG_GIVER's completion witness (P3)
    WORLD_FLAG_DEBUG_GIVER_GATE = 3843, // QUEST_DEBUG_GIVER's declarative prerequisite. Toggled with
                                        // `agenttest worldflag 3843 0|1` to show the quest-giver
                                        // refusing to offer, then offering (D8).
    WORLD_FLAG_DEBUG_THREE = 3844,      // set by the THIRD option of NPC_DEBUG_THREE's only rule, so the
                                        // non-binary response shape is proven by an option that DOES
                                        // something, not only by one that renders (D11, #59).
    WORLD_FLAG_DEBUG_TWIN = 3845,       // NPC_DEBUG_TWIN's own one-shot. Its whole point is that no
                                        // other character can move it: 194 shares 192's actor type and
                                        // model and is still a different character (D21).
} WorldFlagId;

#define WORLD_FLAG_IS_DEBUG(flag) ((flag) >= WORLD_FLAG_DEBUG_FIRST)

RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_FIRST > 0, "the production band must be non-empty");
RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_FIRST < WORLD_FLAG_MAX, "the debug band must be non-empty");

RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_SMOKE >= WORLD_FLAG_DEBUG_FIRST && WORLD_FLAG_DEBUG_SMOKE < WORLD_FLAG_MAX,
                 "WORLD_FLAG_DEBUG_SMOKE must sit in the debug band");
RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_JOURNAL >= WORLD_FLAG_DEBUG_FIRST && WORLD_FLAG_DEBUG_JOURNAL < WORLD_FLAG_MAX,
                 "WORLD_FLAG_DEBUG_JOURNAL must sit in the debug band");
RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_GIVER >= WORLD_FLAG_DEBUG_FIRST && WORLD_FLAG_DEBUG_GIVER < WORLD_FLAG_MAX,
                 "WORLD_FLAG_DEBUG_GIVER must sit in the debug band");
RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_GIVER_GATE >= WORLD_FLAG_DEBUG_FIRST && WORLD_FLAG_DEBUG_GIVER_GATE < WORLD_FLAG_MAX,
                 "WORLD_FLAG_DEBUG_GIVER_GATE must sit in the debug band");
RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_THREE >= WORLD_FLAG_DEBUG_FIRST && WORLD_FLAG_DEBUG_THREE < WORLD_FLAG_MAX,
                 "WORLD_FLAG_DEBUG_THREE must sit in the debug band");
RS_STATIC_ASSERT(WORLD_FLAG_DEBUG_TWIN >= WORLD_FLAG_DEBUG_FIRST && WORLD_FLAG_DEBUG_TWIN < WORLD_FLAG_MAX,
                 "WORLD_FLAG_DEBUG_TWIN must sit in the debug band");

#endif // SOH_RS_WORLD_FLAG_IDS_H
