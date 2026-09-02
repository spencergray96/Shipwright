#ifndef SOH_RS_QUEST_IDS_H
#define SOH_RS_QUEST_IDS_H

#include "soh/Enhancements/rs/RsAssert.h"

// ============================================================================================
//  QUEST IDS - SERIALIZED INTO SAVE FILES. NEVER REORDER. NEVER REUSE. NEVER RENUMBER.
// ============================================================================================
//
// Every QuestId is an index into the `quests` SaveManager section (QuestStore.h). The number is
// what is written to disk, so once a quest has shipped into a save that matters its ID and its
// step-bit meanings are frozen. To change a quest's steps, retire the ID (leave it in place with
// a _RETIRED suffix and a comment) and add a new one at the next free number in the same band.
// Dynamically assigned IDs are disqualified for the same reason: they cannot be persisted safely.
// (sturdy-bassoon#58, decisions D3/D4; the same contract WORLD_FLAG_MAX carries.)
//
// Every enumerator carries an explicit `= N`. Do not rely on implicit increment - an insertion
// above an existing entry would silently renumber everything below it.
//
// Bands (D7): one store, two reserved ID ranges. Production quests count up from 0; debug/test
// quests count up from QUEST_ID_DEBUG_FIRST. `quest debugwipe` clears only the debug band, and
// Quest_Register (Quest.h) refuses a definition whose declared tier does not match its band. A single store
// means debug quests exercise the production save path, so "it worked in testing" cannot be an
// artefact of testing running different code.
//
// QUEST_MAX may be RAISED freely without a section version bump (SaveManager::LoadArray
// default-constructs the tail); never lower it. Moving QUEST_ID_DEBUG_FIRST is a reorder of
// whichever band it cuts through - do not.

#define QUEST_MAX 64
#define QUEST_ID_DEBUG_FIRST 48

// Steps are a u32 bitmask per quest (D2): bit N = step N.
#define QUEST_STEP_MAX 32

typedef enum QuestTier {
    QUEST_TIER_PROD = 0,
    QUEST_TIER_DEBUG = 1,
} QuestTier;

typedef enum QuestId {
    // --- production band: [0, QUEST_ID_DEBUG_FIRST) ---------------------------------------
    QUEST_COOKS_ASSISTANT = 0, // sturdy-bassoon#58 P4 - the reference implementation

    // --- debug band: [QUEST_ID_DEBUG_FIRST, QUEST_MAX) ------------------------------------
    QUEST_DEBUG_SMOKE = 48,   // any-order fixture (quests/DebugQuests.cpp); never a real quest
    QUEST_DEBUG_ORDERED = 49, // ordered fixture with a declarative + escape-hatch prerequisite
    QUEST_DEBUG_JOURNAL = 50, // journal fixture (quests/DebugJournalQuest.cpp): Cook's-Assistant-shaped,
                              // three any-order steps, the block list P2's accumulation proof runs on
} QuestId;

#define QUEST_ID_IS_VALID(id) ((id) >= 0 && (id) < QUEST_MAX)
#define QUEST_ID_IS_DEBUG(id) ((id) >= QUEST_ID_DEBUG_FIRST)
#define QUEST_ID_TIER(id) (QUEST_ID_IS_DEBUG(id) ? QUEST_TIER_DEBUG : QUEST_TIER_PROD)

// Band-boundary guards. Add one line per enumerator; a quest declared in the wrong band fails to
// compile rather than silently landing in the range debugwipe clears (or does not).
RS_STATIC_ASSERT(QUEST_ID_DEBUG_FIRST > 0, "the production band must be non-empty");
RS_STATIC_ASSERT(QUEST_ID_DEBUG_FIRST < QUEST_MAX, "the debug band must be non-empty");
RS_STATIC_ASSERT(QUEST_STEP_MAX == 32, "stepMask is a u32; widening it is a save-format change");

RS_STATIC_ASSERT(QUEST_COOKS_ASSISTANT >= 0 && QUEST_COOKS_ASSISTANT < QUEST_ID_DEBUG_FIRST,
                 "QUEST_COOKS_ASSISTANT must sit in the production band");

RS_STATIC_ASSERT(QUEST_DEBUG_SMOKE >= QUEST_ID_DEBUG_FIRST && QUEST_DEBUG_SMOKE < QUEST_MAX,
                 "QUEST_DEBUG_SMOKE must sit in the debug band");
RS_STATIC_ASSERT(QUEST_DEBUG_ORDERED >= QUEST_ID_DEBUG_FIRST && QUEST_DEBUG_ORDERED < QUEST_MAX,
                 "QUEST_DEBUG_ORDERED must sit in the debug band");
RS_STATIC_ASSERT(QUEST_DEBUG_JOURNAL >= QUEST_ID_DEBUG_FIRST && QUEST_DEBUG_JOURNAL < QUEST_MAX,
                 "QUEST_DEBUG_JOURNAL must sit in the debug band");

#endif // SOH_RS_QUEST_IDS_H
