// The floor-convention fixture quest (sturdy-bassoon#94).
//
// QUEST_DEBUG_FLOOR (52) exists to make the `{floor:N}` substitution assertable from a console with
// nobody at the keyboard: every player-facing string it owns carries a token, so `region set uk`
// and `region set us` change what `quest journal 52` prints and nothing else has to move.
//
// A NEW quest rather than a block appended to QUEST_DEBUG_JOURNAL, on this tree's standing
// precedent: 50's block count and every acceptance regex written against it keep holding verbatim.
//
// What each string is here to prove:
//
//   title        a token in the TITLE, which is the one display string that is not a journal block
//   hint         a token in a HINT, i.e. in a string that is never evaluated and only ever read
//   block 0      `{Floor:0}` at the START OF A SENTENCE - the capitalised form, which is the whole
//                reason there are two spellings of the token
//   block 1      three DIFFERENT indices in one line, so a run cannot pass by matching one label
//                three times - and the third of them sits INSIDE a `#hint:...#` span, because span
//                text is expanded like any other run and that is the part a reader would most
//                reasonably doubt
//   checklist    a token inside an `#item:...#` span in a CHECK ITEM - the surface that strikes
//                through, and the second of the two span kinds
//   stepLabels   a token in a LABEL, which is the string that reaches the OTHER surface: a dialogue
//                rule's missing-steps clause and a quest item's pickup line both read it
//
// Indices 0, 1 and 2 are used, which spans where the two conventions differ most visibly: under UK
// they read "ground floor", "first floor" and "second floor"; under US "first floor", "second
// floor" and "third floor". Note that "first floor" and "second floor" each appear under BOTH, for
// different indices - so a run that only greps for one of them proves nothing. That overlap is
// deliberate, and it is why the assertions in REGION_SETTINGS.md pair a positive with a negative.

#include <cstddef>
#include <cstdint>

#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestJournalDef.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/ShipInit.hpp"

namespace {

// Counts derived from their arrays rather than typed in twice, as DebugJournalQuest.cpp does it.
template <typename T, size_t N> constexpr int32_t Count(const T (&)[N]) {
    return static_cast<int32_t>(N);
}

const char* const sStepNames[] = { "cellar_key", "attic_key" };

// Player-facing labels, so they carry tokens. These are the strings a missing-steps clause reads,
// which is how the token reaches the DIALOGUE surface from a quest definition.
const char* const sStepLabels[] = {
    "the key from the {floor:0}",
    "the key from the {floor:2}",
};

const char* const sHints[] = {
    "The stairs to the {floor:1} are behind the #npc:steward#.",
};

const QuestJournalItem sKeys[] = {
    { "#item:the {floor:0} key#", 0 },
    { "#item:the {floor:2} key#", 1 },
};

const QuestPredicate sWhenStarted[] = {
    QP_NOT_QUEST_STATUS_IS(QUEST_DEBUG_FLOOR, QUEST_STATUS_NOT_STARTED),
};
const QuestPredicate sWhenCollecting[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_FLOOR, QUEST_STATUS_IN_PROGRESS),
};

const QuestJournalBlock sBlocks[] = {
    // 0 - the capitalised form at the start of a sentence.
    { sWhenStarted, Count(sWhenStarted), QUEST_BLOCK_PARAGRAPH,
      "{Floor:0} is where the steward keeps his ledger.", nullptr, 0 },

    // 1 - three indices on one line, the last of them inside a span.
    { nullptr, 0, QUEST_BLOCK_PARAGRAPH,
      "The stair from the {floor:0} reaches the {floor:1}, and #hint:the {floor:2} is above that#.", nullptr, 0 },

    // 2 - the checklist, so a struck row is proven to expand too.
    { sWhenCollecting, Count(sWhenCollecting), QUEST_BLOCK_CHECKLIST, "I still need:", sKeys, Count(sKeys) },
};

const QuestDef sFloorQuest = {
    .id = QUEST_DEBUG_FLOOR,
    .tier = QUEST_TIER_DEBUG,
    .name = "debug_floor",
    .title = "Debug: the {floor:1} keys",
    .ordered = 0,
    .stepCount = Count(sStepNames),
    .stepNames = sStepNames,
    .stepLabels = sStepLabels,
    .requirements = nullptr,
    .requirementCount = 0,
    .prereqFn = nullptr,
    .hints = sHints,
    .hintCount = Count(sHints),
    // No rewards on purpose. A reward flag would be one more thing to reset between an A and a B
    // reading of the same journal, and this fixture's entire job is to be read twice.
    .rewards = nullptr,
    .rewardCount = 0,
    .onComplete = nullptr,
    .journal = sBlocks,
    .journalCount = Count(sBlocks),
};

void RegisterFloorQuest() {
    Quest_Register(&sFloorQuest); // idempotent for the same pointer, so a ShipInit re-run is safe
}

RegisterShipInitFunc floorQuestInitFunc(RegisterFloorQuest);

} // namespace
