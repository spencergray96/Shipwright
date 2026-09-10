// NPC_COOK (0) - the Cook's Assistant quest-giver (sturdy-bassoon#58 P4 / #68).
//
// The first PRODUCTION-band character, and the reference rule table. Six rules, first match wins,
// covering every state of QUEST_COOKS_ASSISTANT - including all EIGHT collection states, with one
// rule speaking seven of them.
//
// Read the table in order; the ordering is the logic:
//
//   0  complete AND the range is open   the permanent post-completion line, gated on the UNLOCK
//   1  complete                         the same character with the unlock taken away
//   2  in progress AND all three in     the hand-over, with the completing option
//   3  in progress                      "I still need ..." - the missing-steps clause (D26)
//   4  not started AND prereqs met      the offer, and the binary decision
//   5  -                                the unconditional fallthrough D8 requires
//
// Three things about it are worth copying and one is worth not mistaking:
//
//   RULES 0 AND 1 OVERLAP ON PURPOSE, and rule 0 is gated on the world flag rather than on the
//   status. That is the unlock made observable: with the quest COMPLETE and the flag cleared from
//   the console, the same character falls to rule 1 and the unlocked line is gone - so the run can
//   prove the permission is carried by a durable bit that anything may read, not by "the quest is
//   finished". It is also the deliberate match=/first= overlap that makes first-match-wins a fact
//   in `npc dump` rather than a claim.
//
//   RULE 3 IS ONE RULE FOR SEVEN STATES. Three any-order steps means seven distinct "still
//   missing" combinations, and static rule text would need seven rules to name them - the same 2^n
//   explosion D13 refuses for journal blocks. `missingOf` names the quest whose unset steps get
//   listed after the body, from the quest's own step labels, so the wording lives with the quest
//   and this table stays the same size whatever the step count becomes.
//
//   RULE 4 ASKS THE QUEST, NOT ITS OWN COPY OF THE QUEST'S CONDITIONS. Cook's Assistant has no
//   prerequisites today, so `QuestPrereqsMet(0)` is true; the day it gains one, this table does not
//   change. That is D8, and it is why the word exists.
//
//   WHAT NOT TO MISTAKE: nothing here remembers anything. Every rule is re-resolved from the global
//   stores on every idle frame, which is what lets one character stand in several scenes at once
//   and lets a second placement of him never re-offer a quest already accepted at the first.

#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/dialogue/NpcIds.h"
#include "soh/Enhancements/rs/quest/QuestIds.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/Enhancements/rs/quest/QuestStore.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

namespace {

const QuestPredicate sUnlockedWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_COOKS_ASSISTANT, QUEST_STATUS_COMPLETE),
    QP_WORLD_FLAG_SET(WORLD_FLAG_COOK_RANGE_OPEN),
};
const QuestPredicate sDoneWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_COOKS_ASSISTANT, QUEST_STATUS_COMPLETE),
};
const QuestPredicate sReadyWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_COOKS_ASSISTANT, QUEST_STATUS_IN_PROGRESS),
    QP_ALL_STEPS_SET(QUEST_COOKS_ASSISTANT),
};
const QuestPredicate sCollectingWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_COOKS_ASSISTANT, QUEST_STATUS_IN_PROGRESS),
};
const QuestPredicate sOfferWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_COOKS_ASSISTANT, QUEST_STATUS_NOT_STARTED),
    QP_QUEST_PREREQS_MET(QUEST_COOKS_ASSISTANT),
};

// Every row also states `next` (sturdy-bassoon#96), for the reason it states `missingOf`: a brace
// list that stops early value-initialises the field to 0, and 0 is NODE 0 - a real screen on any
// character that has a tree. RS_DLG_NO_NEXT is "this option ends the conversation", which is what
// all four of the Cook's do; he is a flat rule table and has no nodes at all.
const RsDialogueOption sHandOverOptions[] = {
    { "Here you are", RS_DLG_ACTION_COMPLETE_QUEST, QUEST_COOKS_ASSISTANT,
      "Wonderful! Take these rupees - and use my range whenever you like.", RS_DLG_NO_NEXT },
    { "Not just yet", RS_DLG_ACTION_NONE, 0, "Do not keep the Duke waiting.", RS_DLG_NO_NEXT },
};
const RsDialogueOption sOfferOptions[] = {
    { "I will help", RS_DLG_ACTION_START_QUEST, QUEST_COOKS_ASSISTANT,
      "Bless you! I need an egg, a bucket of milk and a pot of flour, and the Duke sits down to eat "
      "before long.",
      RS_DLG_NO_NEXT },
    { "Sorry, I am busy", RS_DLG_ACTION_NONE, 0, "Oh dear. Oh dear, oh dear.", RS_DLG_NO_NEXT },
};

// Every row states `missingOf`, including the five that do not use one: a brace list that stopped
// early would value-initialise it to 0, and 0 is this very quest (NpcDialogueDef.h).
const RsDialogueRule sCookRules[] = {
    { sUnlockedWhen, 2, "My range is yours whenever you need it, friend.", nullptr, 0, RS_DLG_NO_MISSING },
    { sDoneWhen, 1, "Thank you again for the ingredients.", nullptr, 0, RS_DLG_NO_MISSING },
    { sReadyWhen, 2, "You have everything I asked for!", sHandOverOptions, 2, RS_DLG_NO_MISSING },
    // The seven-state rule. The body is the lead-in; the list is appended from the quest. Both are
    // kept short deliberately: a box holds four lines, and the longest list (all three ingredients
    // missing) is two of them.
    { sCollectingWhen, 1, "Back already? I still need:", nullptr, 0, QUEST_COOKS_ASSISTANT },
    // A two-option body has to stay inside ONE rendered line, and this one was originally longer:
    // "Will you fetch what my cake needs?" wrapped, which pushed the Yes/No onto a second page
    // where the first A press turned the page instead of choosing. Registration now refuses that
    // outright (RsText_ChoiceWouldPaginate), because it is a bug no marker can see - the run's
    // screenshot is what caught it. Flavour belongs in the reply, which gets a whole box.
    { sOfferWhen, 2, "Will you help me cook?", sOfferOptions, 2, RS_DLG_NO_MISSING },
    // The generic fallthrough. Unreachable while the quest has no prerequisites - rules 0-4 cover
    // every status between them - and required all the same: registration refuses a table whose
    // last rule is conditional, because "true in every state" is a structural guarantee and
    // "true right now" is not (D8).
    { nullptr, 0, "Mind the flour. It gets everywhere.", nullptr, 0, RS_DLG_NO_MISSING },
};

const RsNpcDef sCook = {
    NPC_COOK, QUEST_TIER_PROD, "cook", "The Cook", sCookRules, 6,
};

// RsNpc_Register is idempotent for the same pointer, which is what makes a ShipInit "*" re-run safe.
void RegisterCookNpc() {
    RsNpc_Register(&sCook);
}

RegisterShipInitFunc cookNpcInitFunc(RegisterCookNpc);

} // namespace
