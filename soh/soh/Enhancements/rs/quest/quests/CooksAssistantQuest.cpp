// QUEST_COOKS_ASSISTANT (0) - the reference implementation (sturdy-bassoon#58 P4 / #68).
//
// The first PRODUCTION-band quest there has ever been, and the one D20 says is maintained as the
// thing people copy. It is deliberately shaped like RuneScape's Cook's Assistant: three ingredients
// in any order, no prerequisites, a reward that is as much a permission as a prize.
//
// What each part is here to demonstrate, since this file is a template as much as it is content:
//
//   - THREE ANY-ORDER STEPS, so the eight-state problem is real rather than notional. Seven of
//     those states are "still missing something", and the Cook names exactly what in each of them -
//     from ONE dialogue rule carrying a missing-steps clause (D26), not from seven hand-written
//     rules. See dialogue/npcs/CookNpc.cpp.
//   - STEP LABELS beside step names. `stepNames` are the greppable tokens a console line and an
//     agent marker use; `stepLabels` are what a character says out loud. Keeping them apart is what
//     stops a rename in the log rewriting a textbox.
//   - PICKUP TEXT per step (#99), which is what an item says when Link picks it up. The three
//     ingredients are placed with the GET-ITEM pickup style (RsActorParams.h) - Link holds each one
//     up - and say their own line; a quest that authors none still gets a sentence from its labels.
//   - NO REQUIREMENTS, which is not the same as no gate. The Cook's offer rule still asks
//     `QuestPrereqsMet(0)` - the D8 idiom - so the day this quest grows a prerequisite, the rule
//     table needs no edit at all. A quest-giver never carries its own copy of a quest's conditions.
//   - REWARDS ARE A FLAG AND RUPEES, and the flag is the interesting one. WORLD_FLAG_COOK_RANGE_OPEN
//     is an UNLOCK: durable permission that anything may read, deliberately separate from
//     `status == COMPLETE`. The Cook's post-completion line and journal block 4 both gate on the
//     FLAG, so clearing it leaves a completed quest whose unlocked behaviour is gone - which is how
//     the run proves the bit, and not the status, is what carries the permission.
//   - AN onComplete CALLBACK that does nothing but announce itself. D12 runs it after the
//     declarative rewards with the status already COMPLETE, so it can never run twice for one
//     completion - and a marker is the cheapest possible witness for exactly that claim.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "soh/Enhancements/rs/actors/RsActors.h" // RsAgent_Marker - the onComplete witness
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestJournalDef.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

namespace {

template <typename T, size_t N> constexpr int32_t Count(const T (&)[N]) {
    return static_cast<int32_t>(N);
}

// Tokens: console lines, markers, `quest dump` step rows. Never shown to a player.
const char* const sStepNames[] = { "egg", "milk", "flour" };

// Prose: what the Cook says he is still waiting for, and what an item says it was. Written to read
// inside a sentence ("I still need an egg and a pot of flour"), which is why each carries its own
// article.
const char* const sStepLabels[] = { "an egg", "a bucket of milk", "a pot of flour" };

// What each ingredient says when Link holds it up (#99). Dialogue prose, a whole box each: '&' is a
// line break, and '#', '%', '"' and '^' are refused at registration. Leave a step out (NULL) and its
// item says the template built from its label instead - "You found an egg." - which is what every
// quest with no list here still does.
const char* const sPickupTexts[] = {
    "You got an egg!&It is still warm. Try not&to run with it.",
    "You got a bucket of milk!&Fresh from the cow, and only&a little of it on your boots.",
    "You got a pot of flour!&Most of it is in the pot.&The rest is on your tunic.",
};

const char* const sHints[] = {
    "The #npc:Cook# frets in his #place:kitchen# whenever a feast is coming.",
};

const QuestReward sRewards[] = {
    // The unlock, and the first production-band world flag in the project.
    { QUEST_REWARD_WORLD_FLAG, WORLD_FLAG_COOK_RANGE_OPEN },
    // Countable, so a run can prove a reward was granted EXACTLY once by reading the wallet.
    { QUEST_REWARD_RUPEES, 20 },
};

// --- the journal (D13/D14) ----------------------------------------------------------------------
//
// Five blocks, and not one of them knows another exists. Block 1 goes false by itself the moment
// the third ingredient lands and block 2 becomes true in the same instant; nothing supersedes
// anything, which is the whole of D14. Block 4 is gated on the reward FLAG rather than on the
// status, so the journal is a second, independent reader of the unlock.

const QuestPredicate sWhenStarted[] = {
    QP_NOT_QUEST_STATUS_IS(QUEST_COOKS_ASSISTANT, QUEST_STATUS_NOT_STARTED),
};
const QuestPredicate sWhenCollecting[] = {
    QP_QUEST_STATUS_IS(QUEST_COOKS_ASSISTANT, QUEST_STATUS_IN_PROGRESS),
    QP_NOT_ALL_STEPS_SET(QUEST_COOKS_ASSISTANT),
};
const QuestPredicate sWhenReady[] = {
    QP_QUEST_STATUS_IS(QUEST_COOKS_ASSISTANT, QUEST_STATUS_IN_PROGRESS),
    QP_ALL_STEPS_SET(QUEST_COOKS_ASSISTANT),
};
const QuestPredicate sWhenComplete[] = {
    QP_QUEST_STATUS_IS(QUEST_COOKS_ASSISTANT, QUEST_STATUS_COMPLETE),
};
const QuestPredicate sWhenUnlocked[] = {
    QP_WORLD_FLAG_SET(WORLD_FLAG_COOK_RANGE_OPEN),
};

const QuestJournalItem sChecklist[] = {
    { "an #item:Egg#", 0 },
    { "a bucket of #item:Milk#", 1 },
    { "a pot of #item:Flour#", 2 },
};

const QuestJournalBlock sBlocks[] = {
    // 0 - the premise. True from the moment the quest starts and never false again.
    { sWhenStarted, Count(sWhenStarted), QUEST_BLOCK_PARAGRAPH,
      "The #npc:Cook# at the castle promised the Duke a cake and has nothing to bake it with. I "
      "said I would fetch what he needs.",
      nullptr, 0 },

    // 1 - the checklist. Each row strikes through on its own step, so this one block covers all
    // eight collection states without knowing what any of them are.
    { sWhenCollecting, Count(sWhenCollecting), QUEST_BLOCK_CHECKLIST, "I still need:", sChecklist,
      Count(sChecklist) },

    // 2 - appears in the instant block 1 vanishes.
    { sWhenReady, Count(sWhenReady), QUEST_BLOCK_PARAGRAPH,
      "I have #item:everything he asked for#. I should take it back to the #npc:Cook#.", nullptr, 0 },

    // 3 - the epilogue, printed alongside block 0, which is still true.
    { sWhenComplete, Count(sWhenComplete), QUEST_BLOCK_PARAGRAPH, "The #npc:Cook# baked his cake in time.", nullptr,
      0 },

    // 4 - the UNLOCK, read from the world flag rather than from the quest's status. Clearing the
    // flag takes this line away and leaves the three above it standing.
    { sWhenUnlocked, Count(sWhenUnlocked), QUEST_BLOCK_PARAGRAPH,
      "He lets me use his #place:range# whenever I like now.", nullptr, 0 },
};

// D12's optional bespoke hook. It runs AFTER the declarative rewards, with the status already
// COMPLETE, which is exactly why a second completion attempt cannot reach it - so one marker per
// playthrough is the whole claim, and a run can count them.
void CooksAssistant_OnComplete(int32_t questId) {
    char line[96];
    snprintf(line, sizeof(line), "rs_quest quest=%d event=on_complete", questId);
    RsAgent_Marker(line);
}

const QuestDef sCooksAssistant = {
    .id = QUEST_COOKS_ASSISTANT,
    .tier = QUEST_TIER_PROD,
    .name = "cooks_assistant",
    .title = "The #npc:Cook#'s Assistant",
    .ordered = 0, // any order: the three ingredients are independent errands
    .stepCount = Count(sStepNames),
    .stepNames = sStepNames,
    .stepLabels = sStepLabels,
    .stepPickupTexts = sPickupTexts,
    .requirements = nullptr,
    .requirementCount = 0,
    .prereqFn = nullptr,
    .hints = sHints,
    .hintCount = Count(sHints),
    .rewards = sRewards,
    .rewardCount = Count(sRewards),
    .onComplete = CooksAssistant_OnComplete,
    .journal = sBlocks,
    .journalCount = Count(sBlocks),
};

// Quest_Register is idempotent for the same pointer, which is what makes a ShipInit "*" re-run safe.
void RegisterCooksAssistant() {
    Quest_Register(&sCooksAssistant);
}

RegisterShipInitFunc cooksAssistantInitFunc(RegisterCooksAssistant);

} // namespace
