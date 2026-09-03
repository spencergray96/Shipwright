// The debug-band NPC fixtures (sturdy-bassoon#58 P3 / #64), and the malformed table `npc badcheck`
// runs the registration gate over.
//
// New definitions rather than edits to anything P1 or P2 owns, on the P2 precedent: the earlier
// phases' acceptance regexes keep holding verbatim. The three characters split P3's claims so that
// no single one of them can pass by accident:
//
//   NPC_DEBUG_GIVER (192)  the quest-giver. Five rules covering every state of QUEST_DEBUG_GIVER,
//                          with rule 1 and rule 2 BOTH true at the all-collected state so that
//                          first-match-wins is a visible fact rather than a claim. Placed TWICE in
//                          the test level and once in terrain_f2p_step2: the multi-scene proof and
//                          the double-fire negative are the same character seen three times.
//   NPC_DEBUG_THREE (193)  one rule with THREE options, the third of which SETS A FLAG - so the
//                          non-binary response shape (D11, sturdy-bassoon#59) is proven by an
//                          option that does something, not only by one that renders.
//   NPC_DEBUG_TWIN  (194)  the same actor type and the same model as 192, and a different
//                          character (D21's other axis). Its one-shot is its own world flag, which
//                          nothing 192 does can touch.
//
// Prose here is NOT journal markup. Journal text carries `#tag:text#` (D23); dialogue text goes to
// CustomMessageManager (D17), where '#' is a colour span, '%' starts a control code and '^' is a
// box break - so the registration gate refuses all of them, and '&' means "line break" exactly as
// an author would expect.

#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/dialogue/NpcIds.h"
#include "soh/Enhancements/rs/quest/QuestIds.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/Enhancements/rs/quest/QuestStore.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

namespace {

// --- NPC_DEBUG_GIVER (192) ----------------------------------------------------------------------

const QuestPredicate sGiverDoneWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_GIVER, QUEST_STATUS_COMPLETE),
};
const QuestPredicate sGiverReadyWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_GIVER, QUEST_STATUS_IN_PROGRESS),
    QP_ALL_STEPS_SET(QUEST_DEBUG_GIVER),
};
const QuestPredicate sGiverCollectingWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_GIVER, QUEST_STATUS_IN_PROGRESS),
};
// D8, the whole point of the phase: the OFFER is gated on the quest's own declarative
// prerequisites, asked for through the vocabulary rather than copied into this table. With the
// gate flag clear this rule is false and the last rule speaks instead - the quest is never
// offered, and there is no second place for its wording to leak from.
const QuestPredicate sGiverOfferWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_GIVER, QUEST_STATUS_NOT_STARTED),
    QP_QUEST_PREREQS_MET(QUEST_DEBUG_GIVER),
};

const RsDialogueOption sGiverHandOverOptions[] = {
    { "Hand them over", RS_DLG_ACTION_COMPLETE_QUEST, QUEST_DEBUG_GIVER, "Wonderful. Take these rupees." },
    { "Not just yet", RS_DLG_ACTION_NONE, 0, "I will be right here." },
};
const RsDialogueOption sGiverOfferOptions[] = {
    { "Yes", RS_DLG_ACTION_START_QUEST, QUEST_DEBUG_GIVER, "Splendid. An egg and a bag of flour." },
    { "No", RS_DLG_ACTION_NONE, 0, "Another time, then." },
};

const RsDialogueRule sGiverRules[] = {
    { sGiverDoneWhen, 1, "Thanks again for the ingredients.", nullptr, 0 },
    { sGiverReadyWhen, 2, "You have everything I asked for!", sGiverHandOverOptions, 2 },
    // Rule 2's gate is true at rule 1's state too. That overlap is deliberate: it is what makes
    // first-match-wins an observable fact in `npc dump` (match=1 on both, first=1 on one).
    { sGiverCollectingWhen, 1, "You are still missing something.", nullptr, 0 },
    { sGiverOfferWhen, 2, "Fetch two things for me?", sGiverOfferOptions, 2 },
    // The generic fallthrough. Registration REQUIRES the last rule to be unconditional, so an NPC
    // whose gate is unmet can never resolve to nothing (D8).
    { nullptr, 0, "Lovely weather for standing about.", nullptr, 0 },
};

const RsNpcDef sGiver = {
    NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "debug_giver", "Debug: the giver", sGiverRules, 5,
};

// --- NPC_DEBUG_THREE (193) ----------------------------------------------------------------------

const RsDialogueOption sThreeOptions[] = {
    { "Red", RS_DLG_ACTION_NONE, 0, "Red it is." },
    { "Green", RS_DLG_ACTION_NONE, 0, "Green it is." },
    { "Blue", RS_DLG_ACTION_SET_WORLD_FLAG, WORLD_FLAG_DEBUG_THREE, "Blue it is. I will remember." },
};

// A three-option body is hand-laid-out and capped to one short line at registration, because
// CustomMessage::AutoFormatString knows CTRL_TWO_CHOICE and does not know CTRL_THREE_CHOICE.
const RsDialogueRule sThreeRules[] = {
    { nullptr, 0, "Pick a colour.", sThreeOptions, 3 },
};

const RsNpcDef sThree = {
    NPC_DEBUG_THREE, QUEST_TIER_DEBUG, "debug_three", "Debug: the three-way", sThreeRules, 1,
};

// --- NPC_DEBUG_TWIN (194) -----------------------------------------------------------------------

const QuestPredicate sTwinMetWhen[] = {
    QP_WORLD_FLAG_SET(WORLD_FLAG_DEBUG_TWIN),
};
const RsDialogueOption sTwinOptions[] = {
    { "Nice to meet you", RS_DLG_ACTION_SET_WORLD_FLAG, WORLD_FLAG_DEBUG_TWIN, "Likewise. I will remember you." },
    { "Say nothing", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sTwinRules[] = {
    { sTwinMetWhen, 1, "We have met before.", nullptr, 0 },
    { nullptr, 0, "Hello there, stranger.", sTwinOptions, 2 },
};

const RsNpcDef sTwin = {
    NPC_DEBUG_TWIN, QUEST_TIER_DEBUG, "debug_twin", "Debug: the twin", sTwinRules, 2,
};

// --- the malformed table --------------------------------------------------------------------
//
// One entry per refusal message in NpcDialogue.cpp's validator. `npc badcheck` runs
// RsNpc_DefProblem - the SAME validator with no log and no assert - over all of them, and the run
// is a pass only when every single one is refused. Nothing here is ever registered.
//
// The point is not the count. It is that only this proves REGISTRATION is wired to the validator,
// which a table of good definitions cannot show - the distinction P2 drew for markup.

const RsDialogueOption sOkOptions[] = {
    { "Yes", RS_DLG_ACTION_NONE, 0, "Fine." },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sOkRules[] = {
    { nullptr, 0, "A clean rule.", nullptr, 0 },
};
const RsDialogueRule sManyRules[RS_DIALOGUE_MAX_RULES + 1] = {};

// Every one of these is a NAMED file-scope object. A table of pointers to temporaries would dangle
// the moment its initialiser finished, and `badcheck` would be reading freed memory in order to
// report that a definition is bad - a failure that would look exactly like success.
const RsDialogueRule sBadWhenNull[] = { { nullptr, 1, "text", nullptr, 0 } };
const QuestPredicate sPredUnknownKind[] = { { (QuestPredicateKind)99, 0, 0, 0 } };
const RsDialogueRule sBadPredKind[] = { { sPredUnknownKind, 1, "text", nullptr, 0 } };
const QuestPredicate sPredStatusRange[] = { QP_QUEST_STATUS_IS(999, QUEST_STATUS_COMPLETE) };
const RsDialogueRule sBadPredStatus[] = { { sPredStatusRange, 1, "text", nullptr, 0 } };
const QuestPredicate sPredPrereqRange[] = { QP_QUEST_PREREQS_MET(999) };
const RsDialogueRule sBadPredPrereq[] = { { sPredPrereqRange, 1, "text", nullptr, 0 } };
const QuestPredicate sPredFlagRange[] = { QP_WORLD_FLAG_SET(999999) };
const RsDialogueRule sBadPredFlag[] = { { sPredFlagRange, 1, "text", nullptr, 0 } };

const RsDialogueRule sBadTextNull[] = { { nullptr, 0, nullptr, nullptr, 0 } };
const RsDialogueRule sBadTextHash[] = { { nullptr, 0, "a #item:hash# span", nullptr, 0 } };
const RsDialogueRule sBadTextPercent[] = { { nullptr, 0, "one hundred percent: 100%", nullptr, 0 } };
const RsDialogueRule sBadTextCaret[] = { { nullptr, 0, "a box^break", nullptr, 0 } };
const RsDialogueRule sBadTextQuote[] = { { nullptr, 0, "a \"quoted\" word", nullptr, 0 } };
const RsDialogueRule sBadTextEmpty[] = { { nullptr, 0, "", nullptr, 0 } };
const RsDialogueRule sBadOptionsNull[] = { { nullptr, 0, "text", nullptr, 2 } };
const RsDialogueRule sBadOneOption[] = { { nullptr, 0, "text", sOkOptions, 1 } };
const RsDialogueRule sBadFourOptions[] = { { nullptr, 0, "text", sOkOptions, 4 } };

const RsDialogueOption sThreeOk[] = {
    { "A", RS_DLG_ACTION_NONE, 0, nullptr },
    { "B", RS_DLG_ACTION_NONE, 0, nullptr },
    { "C", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sBadThreeLong[] = {
    { nullptr, 0, "This body is far too long to fit one line beside a three-way choice.", sThreeOk, 3 },
};
const RsDialogueRule sBadThreeMultiline[] = { { nullptr, 0, "Two&lines", sThreeOk, 3 } };

const RsDialogueOption sOptLabelNull[] = {
    { nullptr, RS_DLG_ACTION_NONE, 0, nullptr },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sBadLabelNull[] = { { nullptr, 0, "text", sOptLabelNull, 2 } };
const RsDialogueOption sOptLabelEmpty[] = {
    { "", RS_DLG_ACTION_NONE, 0, nullptr },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sBadLabelEmpty[] = { { nullptr, 0, "text", sOptLabelEmpty, 2 } };
const RsDialogueOption sOptLabelLines[] = {
    { "Two&lines", RS_DLG_ACTION_NONE, 0, nullptr },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sBadLabelLines[] = { { nullptr, 0, "text", sOptLabelLines, 2 } };
const RsDialogueOption sOptReplyPercent[] = {
    { "Yes", RS_DLG_ACTION_NONE, 0, "a reply with 50% too much" },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sBadReply[] = { { nullptr, 0, "text", sOptReplyPercent, 2 } };
const RsDialogueOption sOptActionKind[] = {
    { "Yes", (RsDialogueActionKind)99, 0, nullptr },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sBadActionKind[] = { { nullptr, 0, "text", sOptActionKind, 2 } };
const RsDialogueOption sOptActionQuest[] = {
    { "Yes", RS_DLG_ACTION_START_QUEST, 999, nullptr },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sBadActionQuest[] = { { nullptr, 0, "text", sOptActionQuest, 2 } };
const RsDialogueOption sOptActionFlagRange[] = {
    { "Yes", RS_DLG_ACTION_SET_WORLD_FLAG, 999999, nullptr },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sBadActionFlagRange[] = { { nullptr, 0, "text", sOptActionFlagRange, 2 } };
// A DEBUG-band NPC setting a PRODUCTION-band flag: `quest debugwipe` clears only the debug band, so
// this would leave state a wipe cannot undo. Same rule Quest_Register applies to a world-flag reward.
const RsDialogueOption sOptActionFlagBand[] = {
    { "Yes", RS_DLG_ACTION_SET_WORLD_FLAG, 0, nullptr },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr },
};
const RsDialogueRule sBadActionFlagBand[] = { { nullptr, 0, "text", sOptActionFlagBand, 2 } };

// The gate here is Always() - it EVALUATES true. The check is structural (whenCount == 0), not
// semantic, because "this rule happens to be true right now" is not the same guarantee as "this
// rule is true in every state", and only the second one makes the fallthrough safe.
const QuestPredicate sAlwaysGate[] = { QP_ALWAYS() };
const RsDialogueRule sBadLastConditional[] = { { sAlwaysGate, 1, "a conditional last rule", nullptr, 0 } };

#define BAD_NPC_DEF(sym, rules, count)                                                                                 \
    const RsNpcDef sym = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad", "Bad", (rules), (count) }

const RsNpcDef sBadIdRange = { NPC_MAX, QUEST_TIER_DEBUG, "bad", "Bad", sOkRules, 1 };
const RsNpcDef sBadTier = { NPC_DEBUG_GIVER, QUEST_TIER_PROD, "bad", "Bad", sOkRules, 1 };
const RsNpcDef sBadNameNull = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, nullptr, "Bad", sOkRules, 1 };
const RsNpcDef sBadNameSpace = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad name", "Bad", sOkRules, 1 };
const RsNpcDef sBadNamePercent = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad%name", "Bad", sOkRules, 1 };
const RsNpcDef sBadNameHash = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad#name", "Bad", sOkRules, 1 };
const RsNpcDef sBadDisplayNull = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad", nullptr, sOkRules, 1 };
const RsNpcDef sBadDisplayQuote = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad", "a \"bad\" name", sOkRules, 1 };
const RsNpcDef sBadRuleCountZero = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad", "Bad", sOkRules, 0 };
const RsNpcDef sBadRuleCountHigh = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG,          "bad", "Bad",
                                     sManyRules,      RS_DIALOGUE_MAX_RULES + 1 };
const RsNpcDef sBadRulesNull = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad", "Bad", nullptr, 1 };

BAD_NPC_DEF(sDefWhenNull, sBadWhenNull, 1);
BAD_NPC_DEF(sDefPredKind, sBadPredKind, 1);
BAD_NPC_DEF(sDefPredStatus, sBadPredStatus, 1);
BAD_NPC_DEF(sDefPredPrereq, sBadPredPrereq, 1);
BAD_NPC_DEF(sDefPredFlag, sBadPredFlag, 1);
BAD_NPC_DEF(sDefTextNull, sBadTextNull, 1);
BAD_NPC_DEF(sDefTextHash, sBadTextHash, 1);
BAD_NPC_DEF(sDefTextPercent, sBadTextPercent, 1);
BAD_NPC_DEF(sDefTextCaret, sBadTextCaret, 1);
BAD_NPC_DEF(sDefTextQuote, sBadTextQuote, 1);
BAD_NPC_DEF(sDefTextEmpty, sBadTextEmpty, 1);
BAD_NPC_DEF(sDefOptionsNull, sBadOptionsNull, 1);
BAD_NPC_DEF(sDefOneOption, sBadOneOption, 1);
BAD_NPC_DEF(sDefFourOptions, sBadFourOptions, 1);
BAD_NPC_DEF(sDefThreeLong, sBadThreeLong, 1);
BAD_NPC_DEF(sDefThreeMultiline, sBadThreeMultiline, 1);
BAD_NPC_DEF(sDefLabelNull, sBadLabelNull, 1);
BAD_NPC_DEF(sDefLabelEmpty, sBadLabelEmpty, 1);
BAD_NPC_DEF(sDefLabelLines, sBadLabelLines, 1);
BAD_NPC_DEF(sDefReply, sBadReply, 1);
BAD_NPC_DEF(sDefActionKind, sBadActionKind, 1);
BAD_NPC_DEF(sDefActionQuest, sBadActionQuest, 1);
BAD_NPC_DEF(sDefActionFlagRange, sBadActionFlagRange, 1);
BAD_NPC_DEF(sDefActionFlagBand, sBadActionFlagBand, 1);
BAD_NPC_DEF(sDefLastConditional, sBadLastConditional, 1);

struct BadEntry {
    const char* label;
    const RsNpcDef* def;
};

const BadEntry sBadDefs[] = {
    { "null_def", nullptr },
    { "id_out_of_range", &sBadIdRange },
    { "tier_mismatch", &sBadTier },
    { "name_null", &sBadNameNull },
    { "name_space", &sBadNameSpace },
    { "name_percent", &sBadNamePercent },
    { "name_hash", &sBadNameHash },
    { "display_null", &sBadDisplayNull },
    { "display_quote", &sBadDisplayQuote },
    { "rule_count_zero", &sBadRuleCountZero },
    { "rule_count_too_high", &sBadRuleCountHigh },
    { "rules_null", &sBadRulesNull },
    { "when_null_nonzero_count", &sDefWhenNull },
    { "predicate_unknown_kind", &sDefPredKind },
    { "predicate_status_id_range", &sDefPredStatus },
    { "predicate_prereqs_id_range", &sDefPredPrereq },
    { "predicate_flag_range", &sDefPredFlag },
    { "text_null", &sDefTextNull },
    { "text_hash", &sDefTextHash },
    { "text_percent", &sDefTextPercent },
    { "text_caret", &sDefTextCaret },
    { "text_quote", &sDefTextQuote },
    { "text_empty", &sDefTextEmpty },
    { "options_null_nonzero_count", &sDefOptionsNull },
    { "one_option", &sDefOneOption },
    { "four_options", &sDefFourOptions },
    { "three_option_body_too_long", &sDefThreeLong },
    { "three_option_body_multiline", &sDefThreeMultiline },
    { "label_null", &sDefLabelNull },
    { "label_empty", &sDefLabelEmpty },
    { "label_multiline", &sDefLabelLines },
    { "reply_percent", &sDefReply },
    { "action_unknown_kind", &sDefActionKind },
    { "action_quest_id_range", &sDefActionQuest },
    { "action_flag_range", &sDefActionFlagRange },
    { "action_flag_other_band", &sDefActionFlagBand },
    { "last_rule_conditional", &sDefLastConditional },
};

// RsNpc_Register is idempotent for the same pointer, which is what makes a ShipInit "*" re-run safe.
void RegisterDebugNpcs() {
    RsNpc_Register(&sGiver);
    RsNpc_Register(&sThree);
    RsNpc_Register(&sTwin);
}

RegisterShipInitFunc debugNpcsInitFunc(RegisterDebugNpcs);

} // namespace

// Read by NpcConsole.cpp's `badcheck`, which lives next to the rest of the console surface rather
// than here.
int32_t RsNpcDebug_BadDefCount() {
    return (int32_t)(sizeof(sBadDefs) / sizeof(sBadDefs[0]));
}

const char* RsNpcDebug_BadDefLabel(int32_t index) {
    if (index < 0 || index >= RsNpcDebug_BadDefCount()) {
        return "?";
    }
    return sBadDefs[index].label;
}

const RsNpcDef* RsNpcDebug_BadDef(int32_t index) {
    if (index < 0 || index >= RsNpcDebug_BadDefCount()) {
        return nullptr;
    }
    return sBadDefs[index].def;
}
