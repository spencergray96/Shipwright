// QUEST_DEBUG_SPEAKER (55) - the token the speaker fixture's second speaker hands over
// (NPC_DEBUG_SPEAKER_A's conversation, dialogue/npcs/DebugNpcs.cpp, sturdy-bassoon#281).
//
// A NEW fixture rather than borrowing the shopkeeper's, for the reason every phase has given: the shop's
// acceptance runs set and check its steps, and a second way to set them would move those runs.
//
// One step, no placed item, never started - the shape of QUEST_DEBUG_GROCERIES: the store accepts a step
// on any quest that is not complete, so the gift sets it, and `quest debugwipe` puts it back. It exists
// so a gift's re-open (RsNpc_Resume) lands on a node somebody other than the owner speaks.

#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/ShipInit.hpp"

namespace {

const char* const sStepNames[] = { "debug_speaker_token" };
const char* const sStepLabels[] = { "a debug token" };
const char* const sPickupTexts[] = {
    "The second speaker hands you&a debug token.",
};

const QuestDef sSpeakerQuest = {
    .id = QUEST_DEBUG_SPEAKER,
    .tier = QUEST_TIER_DEBUG,
    .name = "debug_speaker",
    .title = "Debug: the speakers' token",
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
    .rewards = nullptr,
    .rewardCount = 0,
    .onComplete = nullptr,
    .journal = nullptr,
    .journalCount = 0,
};

// Quest_Register is idempotent for the same pointer, which is what makes a ShipInit "*" re-run safe.
void RegisterDebugSpeakerQuest() {
    Quest_Register(&sSpeakerQuest);
}

RegisterShipInitFunc debugSpeakerQuestInitFunc(RegisterDebugSpeakerQuest);

} // namespace
