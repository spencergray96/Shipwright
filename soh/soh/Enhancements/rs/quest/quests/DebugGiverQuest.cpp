// QUEST_DEBUG_GIVER (51) - the quest a debug NPC actually gives (sturdy-bassoon#58 P3 / #64).
//
// A NEW fixture rather than an edit to 48/49/50, for the reason P2 gave: it keeps the P1 and P2
// regression surfaces at zero, so their acceptance regexes still hold verbatim. The one thing that
// necessarily moves is `quest list`'s `registered=` count, which every phase has bumped by one.
//
// What it is shaped to prove, and why each part is the way it is:
//
//   - Two ANY-ORDER steps, so a quest-giver's "still missing something" rule is reachable from a
//     single item pickup. Two rather than three: the 8-state any-order explosion is already proven
//     by QUEST_DEBUG_JOURNAL, and P3's subject is the NPC, not the step algebra.
//   - Its prerequisite is a single WORLD FLAG, so the agent loop can toggle the gate with
//     `agenttest worldflag 3843 0|1` and watch the giver refuse to offer, then offer (D8). A
//     time-of-day escape hatch would prove the same thing less directly - 49 already covers that.
//   - Rewards are a debug-band world flag and SEVEN RUPEES. The rupee count is the countable
//     witness for the double-fire negative: talk to one placement of the character in one scene,
//     then the other placement in the other scene, and the wallet must move exactly once.

#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestJournalDef.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

namespace {

const char* const sStepNames[] = { "egg", "flour" };

const QuestPredicate sRequirements[] = {
    QP_WORLD_FLAG_SET(WORLD_FLAG_DEBUG_GIVER_GATE),
};

const char* const sHints[] = {
    "Speak to the #npc:signpost fellow# in the #place:test level#.",
};

const QuestReward sRewards[] = {
    { QUEST_REWARD_WORLD_FLAG, WORLD_FLAG_DEBUG_GIVER },
    { QUEST_REWARD_RUPEES, 7 },
};

// A small journal, so the P2 read API keeps being exercised by the first quest that has a real
// NPC attached - and so `journal 51` is a second, independent view of the state the dialogue rules
// are gating on.
const QuestPredicate sStartedWhen[] = {
    QP_NOT_QUEST_STATUS_IS(QUEST_DEBUG_GIVER, QUEST_STATUS_NOT_STARTED),
};
const QuestPredicate sCollectingWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_GIVER, QUEST_STATUS_IN_PROGRESS),
};
const QuestJournalItem sChecklist[] = {
    { "an #item:Egg#", 0 },
    { "a bag of #item:Flour#", 1 },
};

const QuestJournalBlock sJournal[] = {
    {
        sStartedWhen,
        1,
        QUEST_BLOCK_PARAGRAPH,
        "A fellow by the #place:test level# signpost asked me to fetch two things.",
        nullptr,
        0,
    },
    {
        sCollectingWhen,
        1,
        QUEST_BLOCK_CHECKLIST,
        "I still need:",
        sChecklist,
        2,
    },
};

const QuestDef sGiver = {
    .id = QUEST_DEBUG_GIVER,
    .tier = QUEST_TIER_DEBUG,
    .name = "debug_giver",
    .title = "Debug: the #npc:giver#'s errand",
    .ordered = 0,
    .stepCount = 2,
    .stepNames = sStepNames,
    .requirements = sRequirements,
    .requirementCount = 1,
    .prereqFn = nullptr,
    .hints = sHints,
    .hintCount = 1,
    .rewards = sRewards,
    .rewardCount = 2,
    .onComplete = nullptr,
    .journal = sJournal,
    .journalCount = 2,
};

// Quest_Register is idempotent for the same pointer, which is what makes a ShipInit "*" re-run safe.
void RegisterDebugGiverQuest() {
    Quest_Register(&sGiver);
}

RegisterShipInitFunc debugGiverQuestInitFunc(RegisterDebugGiverQuest);

} // namespace
