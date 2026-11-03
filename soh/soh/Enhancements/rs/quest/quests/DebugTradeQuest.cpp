// QUEST_DEBUG_TRADE (56) - the inventory's rewards (sturdy-bassoon#286, items/RsItems.h).
//
// One step and no giver: a run sets the step and completes it from the console (`quest setstep 56 0`, `quest
// complete 56`), and the rewards dispatch - 20 logs, their icon over Link's head, and the debug key item, whose
// get-item ceremony plays once Link is free. A new fixture rather than a reward on an old one, for the reason
// every phase has given: other runs complete those quests and assert on what they paid.

#include "soh/Enhancements/rs/items/RsItems.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/ShipInit.hpp"

namespace {

const char* const sStepNames[] = { "debug_trade_done" };
const char* const sStepLabels[] = { "the trade" };

const QuestReward sRewards[] = {
    { QUEST_REWARD_ITEMS, RS_ITEMS(RS_ITEM_LOGS, 20) },
    { QUEST_REWARD_KEY_ITEM, RS_KEY_ITEM_DEBUG },
};

const QuestDef sTradeQuest = {
    .id = QUEST_DEBUG_TRADE,
    .tier = QUEST_TIER_DEBUG,
    .name = "debug_trade",
    .title = "Debug: the trade",
    .ordered = 0,
    .stepCount = 1,
    .stepNames = sStepNames,
    .stepLabels = sStepLabels,
    .stepPickupTexts = nullptr,
    .requirements = nullptr,
    .requirementCount = 0,
    .prereqFn = nullptr,
    .hints = nullptr,
    .hintCount = 0,
    .rewards = sRewards,
    .rewardCount = 2,
    .onComplete = nullptr,
    .journal = nullptr,
    .journalCount = 0,
};

// Quest_Register is idempotent for the same pointer, which is what makes a ShipInit "*" re-run safe.
void RegisterDebugTradeQuest() {
    Quest_Register(&sTradeQuest);
}

RegisterShipInitFunc debugTradeQuestInitFunc(RegisterDebugTradeQuest);

} // namespace
