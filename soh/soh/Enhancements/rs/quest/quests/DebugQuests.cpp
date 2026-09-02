// The two permanent debug-band fixture quests (sturdy-bassoon#58 P1). They exist so every rule in
// Quest.h can be driven and proven from the console with no NPC, item or scene involved, and they
// double as the copy-paste shape for a real definition. Both live in the debug band, so
// `quest debugwipe` clears everything they touch.
//
//   QUEST_DEBUG_SMOKE   (48)  any-order, three steps, no prerequisites, two rewards (a debug-band
//                             world flag - the "unlock" primitive - and 20 rupees, the countable
//                             witness the idempotency proof reads) and an onComplete callback.
//   QUEST_DEBUG_ORDERED (49)  ordered, three steps, gated on SMOKE being complete (declarative,
//                             D8) AND on it being night (the escape-hatch predicate - something the
//                             vocabulary cannot say and `agenttest time day|night` can toggle).

#include <spdlog/spdlog.h>

#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "macros.h"
}

namespace {

// --- QUEST_DEBUG_SMOKE --------------------------------------------------------------------------

const char* const sSmokeStepNames[] = { "red", "green", "blue" };
const char* const sSmokeHints[] = { "Drive it from the console: quest start 48, quest setstep 48 <n>." };
const QuestReward sSmokeRewards[] = {
    { QUEST_REWARD_WORLD_FLAG, WORLD_FLAG_DEBUG_SMOKE },
    { QUEST_REWARD_RUPEES, 20 },
};

// Counted only for the log; the proof reads the real side effects (rupees, the flag), not this.
int32_t sSmokeCompletions = 0;

void SmokeOnComplete(int32_t questId) {
    sSmokeCompletions++;
    SPDLOG_INFO("DebugQuests: onComplete for quest {} (completion #{} this process)", questId, sSmokeCompletions);
}

const QuestDef sSmoke = {
    .id = QUEST_DEBUG_SMOKE,
    .tier = QUEST_TIER_DEBUG,
    .name = "debug_smoke",
    .title = "Debug: smoke",
    .ordered = 0,
    .stepCount = 3,
    .stepNames = sSmokeStepNames,
    .requirements = nullptr,
    .requirementCount = 0,
    .prereqFn = nullptr,
    .hints = sSmokeHints,
    .hintCount = 1,
    .rewards = sSmokeRewards,
    .rewardCount = 2,
    .onComplete = SmokeOnComplete,
};

// --- QUEST_DEBUG_ORDERED ------------------------------------------------------------------------

const char* const sOrderedStepNames[] = { "first", "second", "third" };
const QuestPredicate sOrderedRequirements[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_SMOKE, QUEST_STATUS_COMPLETE),
};
const QuestReward sOrderedRewards[] = {
    { QUEST_REWARD_RUPEES, 5 },
};

// IS_NIGHT is an expression macro over gSaveContext.nightFlag, so it needs a function to sit
// behind the QuestPrereqFn pointer.
int32_t PrereqIsNight(void) {
    return IS_NIGHT ? 1 : 0;
}

const QuestDef sOrdered = {
    .id = QUEST_DEBUG_ORDERED,
    .tier = QUEST_TIER_DEBUG,
    .name = "debug_ordered",
    .title = "Debug: ordered",
    .ordered = 1,
    .stepCount = 3,
    .stepNames = sOrderedStepNames,
    .requirements = sOrderedRequirements,
    .requirementCount = 1,
    .prereqFn = PrereqIsNight,
    .hints = nullptr,
    .hintCount = 0,
    .rewards = sOrderedRewards,
    .rewardCount = 1,
    .onComplete = nullptr,
};

// Quest_Register is idempotent for the same pointer, which is what makes a ShipInit "*" re-run safe.
void RegisterDebugQuests() {
    Quest_Register(&sSmoke);
    Quest_Register(&sOrdered);
}

RegisterShipInitFunc debugQuestsInitFunc(RegisterDebugQuests);

} // namespace
