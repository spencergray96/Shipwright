// QUEST_DEBUG_ERRAND (53) - the quest the debug shopkeeper (NPC_DEBUG_SHOP, dialogue/npcs/DebugNpcs.cpp)
// starts, hands the one item of, and completes (sturdy-bassoon#281). Beside it, QUEST_DEBUG_GROCERIES
// (54): the shopkeeper's wares, sold with BUY_ITEM.
//
// A NEW fixture rather than an edit to 51, for the reason every phase has given: 51's item stands in two
// scenes and its acceptance runs pick those items up, so a second way to set its steps would move them.
//
// What it is shaped to prove:
//
//   - ONE step, and no placed item: the only way to set it is the shopkeeper's GIVE_ITEM, so a set step
//     is proof that the give went through.
//   - An AUTHORED pickup line for that step, which is what Player's box shows after the get-item
//     ceremony. It says who handed it over - the template's "You found" would read wrong.
//   - No requirements, so `quest debugwipe` puts it straight back to offerable.
//   - A rupee reward, so a completion is visible in the wallet beside the shopkeeper's own charge.

#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestJournalDef.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/ShipInit.hpp"

namespace {

const char* const sStepNames[] = { "debug_parcel" };
const char* const sStepLabels[] = { "a debug parcel" };
const char* const sPickupTexts[] = {
    "The shopkeeper hands you&a debug parcel.",
};

const QuestReward sRewards[] = {
    { QUEST_REWARD_RUPEES, 15 },
};

const QuestPredicate sStartedWhen[] = {
    QP_NOT_QUEST_STATUS_IS(QUEST_DEBUG_ERRAND, QUEST_STATUS_NOT_STARTED),
};
const QuestJournalItem sChecklist[] = {
    { "a #item:Debug Parcel# from the shopkeeper", 0 },
};

const QuestJournalBlock sJournal[] = {
    {
        sStartedWhen,
        1,
        QUEST_BLOCK_PARAGRAPH,
        "The #npc:debug shopkeeper# in the #place:test level# has an errand for me.",
        nullptr,
        0,
    },
    {
        sStartedWhen,
        1,
        QUEST_BLOCK_CHECKLIST,
        nullptr,
        sChecklist,
        1,
    },
};

const QuestDef sErrand = {
    .id = QUEST_DEBUG_ERRAND,
    .tier = QUEST_TIER_DEBUG,
    .name = "debug_errand",
    .title = "Debug: the #npc:shopkeeper#'s errand",
    .ordered = 0,
    .stepCount = 1,
    .stepNames = sStepNames,
    .stepLabels = sStepLabels,
    .stepPickupTexts = sPickupTexts,
    .requirements = nullptr,
    .requirementCount = 0,
    .prereqFn = nullptr,
    .hints = nullptr,
    .hintCount = 0,
    .rewards = sRewards,
    .rewardCount = 1,
    .onComplete = nullptr,
    .journal = sJournal,
    .journalCount = 2,
};

// --- QUEST_DEBUG_GROCERIES (54) ------------------------------------------------------------------
//
// Not a quest anyone is given: two steps that exist to be SOLD, and the shopkeeper's wares screen sells
// each with BUY_ITEM. The loaf is ten rupees; the gold bar is a thousand, more than any wallet holds, so
// it is the refused sale every time. Never started, on purpose - the store accepts a step on any quest
// that is not COMPLETE, so a sale still sets it, and the missing "Quest updated" notice is the proof that
// a notice is only shown for a quest the journal shows. No journal, no rewards.
const char* const sGroceryNames[] = { "debug_loaf", "debug_gold_bar" };
const char* const sGroceryLabels[] = { "a debug loaf", "a debug gold bar" };
const char* const sGroceryPickupTexts[] = {
    "You buy a debug loaf.&Ten rupees, still warm.",
    "You buy a debug gold bar.&Nobody should ever read this.",
};

const QuestDef sGroceries = {
    .id = QUEST_DEBUG_GROCERIES,
    .tier = QUEST_TIER_DEBUG,
    .name = "debug_groceries",
    .title = "Debug: the shopkeeper's wares",
    .ordered = 0,
    .stepCount = 2,
    .stepNames = sGroceryNames,
    .stepLabels = sGroceryLabels,
    .stepPickupTexts = sGroceryPickupTexts,
    .requirements = nullptr,
    .requirementCount = 0,
    .prereqFn = nullptr,
    .hints = nullptr,
    .hintCount = 0,
    .rewards = nullptr,
    .rewardCount = 0,
    .onComplete = nullptr,
    .journal = nullptr,
    .journalCount = 0,
};

// Quest_Register is idempotent for the same pointer, which is what makes a ShipInit "*" re-run safe.
void RegisterDebugErrandQuest() {
    Quest_Register(&sErrand);
    Quest_Register(&sGroceries);
}

RegisterShipInitFunc debugErrandQuestInitFunc(RegisterDebugErrandQuest);

} // namespace
