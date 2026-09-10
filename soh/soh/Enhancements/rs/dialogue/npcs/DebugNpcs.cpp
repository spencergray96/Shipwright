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
//   NPC_DEBUG_FOUR  (195)  added for #59: a QUESTION plus FOUR options, in the five-row box. 193
//                          shows the shape is not binary; this one shows it is not capped at what
//                          vanilla's control codes offer, and that a body too long for the old
//                          24-character cap renders fine when the budget is measured in pixels.
//                          Its fourth option sets a flag, for 193's reason applied to the last row.
//   NPC_DEBUG_FLOOR (196)  added for #94: every string a conversation can show carries a
//                          `{floor:N}` token, and each of them reaches the screen by a different
//                          road - so one `npc dump 196` under each convention proves all four.
//   NPC_DEBUG_TREE  (197)  added for #96: an entry rule whose reply leads into a two-screen tree
//                          that loops back on itself, and an option that only exists once another
//                          has been taken. Navigation, loop-back and the dynamic reveal at once.
//   NPC_DEBUG_PAGE  (198)  added for #96 P3: authored paging - "More..." and "Back" are ordinary
//                          options with a `next`, and the fixture's job is to show that paging
//                          needs no engine feature and to make its two costs to an author visible.
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
    { "Hand them over", RS_DLG_ACTION_COMPLETE_QUEST, QUEST_DEBUG_GIVER, "Wonderful. Take these rupees.",
      RS_DLG_NO_NEXT },
    { "Not just yet", RS_DLG_ACTION_NONE, 0, "I will be right here.", RS_DLG_NO_NEXT },
};
const RsDialogueOption sGiverOfferOptions[] = {
    { "Yes", RS_DLG_ACTION_START_QUEST, QUEST_DEBUG_GIVER, "Splendid. An egg and a bag of flour.", RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, "Another time, then.", RS_DLG_NO_NEXT },
};

const RsDialogueRule sGiverRules[] = {
    { sGiverDoneWhen, 1, "Thanks again for the ingredients.", nullptr, 0, RS_DLG_NO_MISSING },
    { sGiverReadyWhen, 2, "You have everything I asked for!", sGiverHandOverOptions, 2, RS_DLG_NO_MISSING },
    // Rule 2's gate is true at rule 1's state too. That overlap is deliberate: it is what makes
    // first-match-wins an observable fact in `npc dump` (match=1 on both, first=1 on one).
    { sGiverCollectingWhen, 1, "You are still missing something.", nullptr, 0, RS_DLG_NO_MISSING },
    { sGiverOfferWhen, 2, "Fetch two things for me?", sGiverOfferOptions, 2, RS_DLG_NO_MISSING },
    // The generic fallthrough. Registration REQUIRES the last rule to be unconditional, so an NPC
    // whose gate is unmet can never resolve to nothing (D8).
    { nullptr, 0, "Lovely weather for standing about.", nullptr, 0, RS_DLG_NO_MISSING },
};

const RsNpcDef sGiver = {
    NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "debug_giver", "Debug: the giver", sGiverRules, 5,
};

// --- NPC_DEBUG_THREE (193) ----------------------------------------------------------------------

const RsDialogueOption sThreeOptions[] = {
    { "Red", RS_DLG_ACTION_NONE, 0, "Red it is.", RS_DLG_NO_NEXT },
    { "Green", RS_DLG_ACTION_NONE, 0, "Green it is.", RS_DLG_NO_NEXT },
    { "Blue", RS_DLG_ACTION_SET_WORLD_FLAG, WORLD_FLAG_DEBUG_THREE, "Blue it is. I will remember.", RS_DLG_NO_NEXT },
};

// A three-option body is hand-laid-out and MEASURED to one row at registration, because
// CustomMessage::AutoFormatString knows CTRL_TWO_CHOICE and does not know CTRL_THREE_CHOICE.
const RsDialogueRule sThreeRules[] = {
    { nullptr, 0, "Pick a colour.", sThreeOptions, 3, RS_DLG_NO_MISSING },
};

const RsNpcDef sThree = {
    NPC_DEBUG_THREE, QUEST_TIER_DEBUG, "debug_three", "Debug: the three-way", sThreeRules, 1,
};

// --- NPC_DEBUG_FOUR (195) -----------------------------------------------------------------------

const RsDialogueOption sFourOptions[] = {
    { "The lost cucco", RS_DLG_ACTION_NONE, 0, "Nobody has seen it.", RS_DLG_NO_NEXT },
    { "The broken bridge", RS_DLG_ACTION_NONE, 0, "It has been broken for years.", RS_DLG_NO_NEXT },
    { "The missing letter", RS_DLG_ACTION_NONE, 0, "Ask at the post house.", RS_DLG_NO_NEXT },
    // The LAST row is the one a layout bug loses, so it is the one that has to DO something - the
    // same argument that put a flag on 193's third option.
    { "Nevermind", RS_DLG_ACTION_SET_WORLD_FLAG, WORLD_FLAG_DEBUG_FOUR, "Suit yourself.", RS_DLG_NO_NEXT },
};

// The shape #59 was actually filed for: a QUESTION, then four answers, in a five-row box. The body
// is 31 characters - over the old 24-character cap, comfortably inside the 216 pixels that cap was
// standing in for, and now measured rather than counted.
const RsDialogueRule sFourRules[] = {
    { nullptr, 0, "What can I help you with, Link?", sFourOptions, 4, RS_DLG_NO_MISSING },
};

const RsNpcDef sFour = {
    NPC_DEBUG_FOUR, QUEST_TIER_DEBUG, "debug_four", "Debug: the four-way", sFourRules, 1,
};

// --- NPC_DEBUG_TWIN (194) -----------------------------------------------------------------------

const QuestPredicate sTwinMetWhen[] = {
    QP_WORLD_FLAG_SET(WORLD_FLAG_DEBUG_TWIN),
};
const RsDialogueOption sTwinOptions[] = {
    { "Nice to meet you", RS_DLG_ACTION_SET_WORLD_FLAG, WORLD_FLAG_DEBUG_TWIN, "Likewise. I will remember you.",
      RS_DLG_NO_NEXT },
    { "Say nothing", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sTwinRules[] = {
    { sTwinMetWhen, 1, "We have met before.", nullptr, 0, RS_DLG_NO_MISSING },
    { nullptr, 0, "Hello there, stranger.", sTwinOptions, 2, RS_DLG_NO_MISSING },
};

const RsNpcDef sTwin = {
    NPC_DEBUG_TWIN, QUEST_TIER_DEBUG, "debug_twin", "Debug: the twin", sTwinRules, 2,
};

// --- NPC_DEBUG_FLOOR (196) ----------------------------------------------------------------------
//
// The floor-convention fixture (sturdy-bassoon#94). Its whole job is that one `npc dump 196` under
// each convention proves the substitution reaches every string a conversation can show, because
// each of them travels a different road to the screen:
//
//   body    RsNpc_ComposeRuleText, printed by `npc dump` and `npc resolve` and fed to the box
//   label   expanded by the renderer while it lays the choice out, and measured for WIDTH under
//           EVERY convention at registration - the trap this NPC is the live check for
//   reply   handed to the box through RsText_SetDirectCopy, which is the ONLY reason that call is
//           SetDirectCopy and not SetDirect
//   label   of a quest STEP, reached only through a missing-steps clause - see rule 0 below
//
// Two options rather than a statement, so the label and reply roads are both walked, and no option
// writes anything - a fixture whose whole job is to be read twice must not change state between
// the two readings.
//
// Rule 0 exists for the FOURTH road, which none of the three above reaches: a step LABEL. A quest's
// stepLabels are expanded by RsNpc_MissingList, so the only way to see one composed is a rule
// carrying a missing-steps clause (D26). Gated on quest 52 being in progress, because registration
// refuses a `missingOf` that no predicate in its own rule gates on - and a statement, because
// registration refuses a clause on a rule with options.

const QuestPredicate sFloorCollectingWhen[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_FLOOR, QUEST_STATUS_IN_PROGRESS),
};

const RsDialogueOption sFloorOptions[] = {
    // A token in a LABEL, deliberately short: an option row's budget is 184 pixels, and the
    // registration check now measures this label under both conventions.
    { "Up to the {floor:1}", RS_DLG_ACTION_NONE, 0, "The stair is past the well.", RS_DLG_NO_NEXT },
    // ...and a token in a REPLY, on the option that does nothing, so reading it twice is free.
    { "Nothing, thank you", RS_DLG_ACTION_NONE, 0, "I will be on the {floor:0} if you need me.", RS_DLG_NO_NEXT },
};

const RsDialogueRule sFloorRules[] = {
    // 0 - the missing-steps clause. Its own text carries no token; the tokens arrive from quest 52's
    // stepLabels, appended after a '&'. That is the whole point: this rule proves a label written in
    // a QUEST definition is expanded on the DIALOGUE surface.
    { sFloorCollectingWhen, 1, "The steward is still short of:", nullptr, 0, QUEST_DEBUG_FLOOR },

    // 1 - the fallthrough, and the unconditional last rule registration requires.
    // `{Floor:0}` opens the sentence - the capitalised spelling, which is the reason there are two.
    // Short on purpose: a TWO-option body gets one row of a four-row box, and the registration
    // check now measures that row under BOTH conventions, where the UK label is a character longer.
    { nullptr, 0, "{Floor:0}. Going up?", sFloorOptions, 2, RS_DLG_NO_MISSING },
};

const RsNpcDef sFloor = {
    NPC_DEBUG_FLOOR, QUEST_TIER_DEBUG, "debug_floor", "Debug: the {floor:1} sweeper", sFloorRules, 2,
};

// --- NPC_DEBUG_TREE (197) -----------------------------------------------------------------------
//
// The dialogue-tree fixture (sturdy-bassoon#96). One conversation that exercises every claim the
// phase makes, so no single leg of it can pass by accident:
//
//   entry rule           two options, both ungated - so the reply-then-navigate path and the
//                        plain close path are both walked from the ENTRY box.
//                        #96 sketches the entry as a statement leading into SCREEN A; it cannot be
//                        one, because navigation lives on an OPTION and a statement has none. A
//                        two-option entry is the smallest thing that can navigate at all, and it
//                        buys the close path for free.
//   opt 0's reply        deliberately long enough to PAGINATE, which is the whole argument for
//                        not having a separate "intermediate dialogue" node kind
//   node 0 (SCREEN A)    four options, so arriving here GROWS the box on a CONTINUED textbox -
//                        the geometry claim, which only a screenshot can check
//   node 1 (SCREEN B)    three ungated options and a fourth that only exists once one of them has
//                        been taken. Two of its options come straight back to it, so B is where
//                        LOOP-BACK is proven; a third goes back to A, so the loop is a real cycle
//                        through two screens and not a self-edge
//
// The reveal is an ordinary world flag and an ordinary predicate. There is no new machinery for it,
// which is exactly why the unlock is permanent - nothing in the system expires a flag.
//
// Every label sits well inside an option row's 184-pixel budget and every body inside one 216-pixel
// row, because registration measures them under BOTH floor conventions and refuses otherwise. The
// prose is short on purpose; flavour belongs in a reply, which gets a whole box to itself.

const QuestPredicate sTreeMillerKnownWhen[] = {
    QP_WORLD_FLAG_SET(WORLD_FLAG_DEBUG_TREE_MILLER),
};

// SCREEN B (node 1). Declared FOURTH-first in reading order below; the array order is the
// definition order, and the fourth row is the one that is not always there.
const RsDialogueOption sTreeScreenBOptions[] = {
    // Loop-back, unchanged: answer, then straight back to this same screen.
    { "How does it work?", RS_DLG_ACTION_NONE, 0,
      "The river turns the wheel, and the wheel turns the stone. It has done since before anyone here was born.", 1 },
    // Loop-back, CHANGED: the same edge, and on the way it sets the flag the fourth option reads.
    // The player now has the context to ask about the miller, so the topic appears.
    { "Who owns it?", RS_DLG_ACTION_SET_WORLD_FLAG, WORLD_FLAG_DEBUG_TREE_MILLER,
      "The miller does, and has for thirty years. A quiet sort. You could ask me about him, if you like.", 1 },
    // Back to SCREEN A, so the cycle runs through two screens rather than being a self-edge.
    { "Back to the start", RS_DLG_ACTION_NONE, 0, nullptr, 0 },
    // THE REVEAL. Gated, so it is the fourth visible option only after the option above has been
    // taken - and permanently after that, because a world flag does not decay.
    { "About the miller", RS_DLG_ACTION_NONE, 0, "He keeps to himself. Do not take it personally.", 1,
      sTreeMillerKnownWhen, 1 },
};

// SCREEN A (node 0). Four options, all ungated, so arriving here always grows the box.
const RsDialogueOption sTreeScreenAOptions[] = {
    { "About the mill", RS_DLG_ACTION_NONE, 0, "Ah, the mill. Ask me anything.", 1 },
    { "Who lives here?", RS_DLG_ACTION_NONE, 0, "Six families, and the miller makes seven.", 0 },
    { "Where is the well?", RS_DLG_ACTION_NONE, 0, "Behind the chapel. Mind the step.", 0 },
    { "That is all", RS_DLG_ACTION_NONE, 0, "Come back any time.", RS_DLG_NO_NEXT },
};

const RsDialogueNode sTreeNodes[] = {
    // 0 - SCREEN A
    { nullptr, 0, "What would you like to know?", sTreeScreenAOptions, 4, RS_DLG_NO_MISSING },
    // 1 - SCREEN B
    { nullptr, 0, "What about the mill?", sTreeScreenBOptions, 4, RS_DLG_NO_MISSING },
};

const RsDialogueOption sTreeEntryOptions[] = {
    // A reply AND a `next`: the action fires, the reply box opens on an id that names the pending
    // node, and dismissing it lands on SCREEN A. The reply is long enough to need two pages, which
    // is the point - multi-page intermediate dialogue is a long string, not a new node kind.
    { "Tell me the tale", RS_DLG_ACTION_NONE, 0,
      "Gladly. The mill was here before the village was, and the village grew up around it the way moss grows on a "
      "stone. Everyone you will meet here measures their year by the harvest and the grinding, and half of them owe "
      "the miller something. Now then - what would you like to know first?",
      0 },
    { "Not just now", RS_DLG_ACTION_NONE, 0, "Another time, then.", RS_DLG_NO_NEXT },
};

const RsDialogueRule sTreeRules[] = {
    { nullptr, 0, "Shall I tell you the tale?", sTreeEntryOptions, 2, RS_DLG_NO_MISSING },
};

const RsNpcDef sTree = {
    NPC_DEBUG_TREE, QUEST_TIER_DEBUG, "debug_tree", "Debug: the tree", sTreeRules, 1, sTreeNodes, 2,
};

// --- NPC_DEBUG_PAGE (198) -----------------------------------------------------------------------
//
// AUTHORED PAGING (sturdy-bassoon#96 P3), and the demonstration of what it costs.
//
// Engine-level option paging was considered and rejected: it would fight the four-option cap and
// force auto-generated More/Back labels that no character could phrase in its own voice. Authored
// paging needs NO engine work at all - page 2 is just a node, "More..." is just an option with a
// `next` - and this fixture is the proof, since nothing in it uses a feature the tree fixture above
// does not already use.
//
// The two costs the author eats, both visible here:
//   "More..." SPENDS ONE OF THE FOUR SLOTS, so page 1 carries three real topics.
//   PAGE 1 NEEDS A TOPIC BESIDE "More...", because of the two-ungated rule. Page 2 satisfies that
//   naturally with "Back" plus its own topics.
//
// The entry option into page 1 carries NO reply, so this fixture also walks the third of the three
// navigation paths: straight from a choice to a node with no box in between.

const RsDialogueOption sPagePageTwoOptions[] = {
    { "Back", RS_DLG_ACTION_NONE, 0, nullptr, 0 },
    { "The graveyard", RS_DLG_ACTION_NONE, 0, "North of the chapel, through the lych gate.", 1 },
    { "The old bridge", RS_DLG_ACTION_NONE, 0, "Downriver. It has not been safe in years.", 1 },
    { "That is all", RS_DLG_ACTION_NONE, 0, "Safe travels.", RS_DLG_NO_NEXT },
};

const RsDialogueOption sPagePageOneOptions[] = {
    { "The mill", RS_DLG_ACTION_NONE, 0, "Follow the river until you hear it.", 0 },
    { "The well", RS_DLG_ACTION_NONE, 0, "Behind the chapel. Mind the step.", 0 },
    { "The chapel", RS_DLG_ACTION_NONE, 0, "The tall roof. You cannot miss it.", 0 },
    // The paging option. No reply and no action: it is pure navigation, which is what makes it
    // read as a page turn rather than as an answer.
    { "More...", RS_DLG_ACTION_NONE, 0, nullptr, 1 },
};

const RsDialogueNode sPageNodes[] = {
    // 0 - page one
    { nullptr, 0, "Where are you headed?", sPagePageOneOptions, 4, RS_DLG_NO_MISSING },
    // 1 - page two
    { nullptr, 0, "Anywhere else?", sPagePageTwoOptions, 4, RS_DLG_NO_MISSING },
};

const RsDialogueOption sPageEntryOptions[] = {
    { "Yes please", RS_DLG_ACTION_NONE, 0, nullptr, 0 },
    { "No thank you", RS_DLG_ACTION_NONE, 0, "Mind how you go.", RS_DLG_NO_NEXT },
};

const RsDialogueRule sPageRules[] = {
    { nullptr, 0, "Looking for directions?", sPageEntryOptions, 2, RS_DLG_NO_MISSING },
};

const RsNpcDef sPage = {
    NPC_DEBUG_PAGE, QUEST_TIER_DEBUG, "debug_page", "Debug: the page turner", sPageRules, 1, sPageNodes, 2,
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
    { "Yes", RS_DLG_ACTION_NONE, 0, "Fine.", RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sOkRules[] = {
    { nullptr, 0, "A clean rule.", nullptr, 0, RS_DLG_NO_MISSING },
};
// The one table that does NOT state missingOf per row, because it cannot: it is zero-initialised
// on purpose to be too long. Its rows never reach rule validation - ruleCount is refused first.
const RsDialogueRule sManyRules[RS_DIALOGUE_MAX_RULES + 1] = {};

// Every one of these is a NAMED file-scope object. A table of pointers to temporaries would dangle
// the moment its initialiser finished, and `badcheck` would be reading freed memory in order to
// report that a definition is bad - a failure that would look exactly like success.
const RsDialogueRule sBadWhenNull[] = { { nullptr, 1, "text", nullptr, 0, RS_DLG_NO_MISSING } };
const QuestPredicate sPredUnknownKind[] = { { (QuestPredicateKind)99, 0, 0, 0 } };
const RsDialogueRule sBadPredKind[] = { { sPredUnknownKind, 1, "text", nullptr, 0, RS_DLG_NO_MISSING } };
const QuestPredicate sPredStatusRange[] = { QP_QUEST_STATUS_IS(999, QUEST_STATUS_COMPLETE) };
const RsDialogueRule sBadPredStatus[] = { { sPredStatusRange, 1, "text", nullptr, 0, RS_DLG_NO_MISSING } };
const QuestPredicate sPredPrereqRange[] = { QP_QUEST_PREREQS_MET(999) };
const RsDialogueRule sBadPredPrereq[] = { { sPredPrereqRange, 1, "text", nullptr, 0, RS_DLG_NO_MISSING } };
const QuestPredicate sPredFlagRange[] = { QP_WORLD_FLAG_SET(999999) };
const RsDialogueRule sBadPredFlag[] = { { sPredFlagRange, 1, "text", nullptr, 0, RS_DLG_NO_MISSING } };

const RsDialogueRule sBadTextNull[] = { { nullptr, 0, nullptr, nullptr, 0, RS_DLG_NO_MISSING } };
const RsDialogueRule sBadTextHash[] = { { nullptr, 0, "a #item:hash# span", nullptr, 0, RS_DLG_NO_MISSING } };
const RsDialogueRule sBadTextPercent[] = { { nullptr, 0, "one hundred percent: 100%", nullptr, 0, RS_DLG_NO_MISSING } };
const RsDialogueRule sBadTextCaret[] = { { nullptr, 0, "a box^break", nullptr, 0, RS_DLG_NO_MISSING } };
const RsDialogueRule sBadTextQuote[] = { { nullptr, 0, "a \"quoted\" word", nullptr, 0, RS_DLG_NO_MISSING } };
const RsDialogueRule sBadTextEmpty[] = { { nullptr, 0, "", nullptr, 0, RS_DLG_NO_MISSING } };
const RsDialogueRule sBadOptionsNull[] = { { nullptr, 0, "text", nullptr, 2, RS_DLG_NO_MISSING } };
const RsDialogueRule sBadOneOption[] = { { nullptr, 0, "text", sOkOptions, 1, RS_DLG_NO_MISSING } };
// One option past what the renderer draws. This entry USED to be `sOkOptions, 4` back when the cap
// was three - an option COUNT of 4 over a 2-element array, safe only because the count check
// refused it before anything indexed it. Raising the cap to 4 would have turned that into a real
// out-of-bounds read inside the validator whose entire job is to refuse bad definitions, so the
// array is now genuinely as long as the count claims. Replaced IN PLACE, never reordered: the
// acceptance drivers pin these by index.
const RsDialogueOption sFiveOpts[] = {
    { "A", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT }, { "B", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "C", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT }, { "D", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "E", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadFiveOptions[] = {
    { nullptr, 0, "text", sFiveOpts, RS_DIALOGUE_MAX_OPTIONS + 1, RS_DLG_NO_MISSING },
};

const RsDialogueOption sThreeOk[] = {
    { "A", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "B", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "C", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadThreeLong[] = {
    { nullptr, 0, "This body is far too long to fit one line beside a three-way choice.", sThreeOk, 3,
      RS_DLG_NO_MISSING },
};
const RsDialogueRule sBadThreeMultiline[] = { { nullptr, 0, "Two&lines", sThreeOk, 3, RS_DLG_NO_MISSING } };

const RsDialogueOption sOptLabelNull[] = {
    { nullptr, RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadLabelNull[] = { { nullptr, 0, "text", sOptLabelNull, 2, RS_DLG_NO_MISSING } };
const RsDialogueOption sOptLabelEmpty[] = {
    { "", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadLabelEmpty[] = { { nullptr, 0, "text", sOptLabelEmpty, 2, RS_DLG_NO_MISSING } };
const RsDialogueOption sOptLabelLines[] = {
    { "Two&lines", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadLabelLines[] = { { nullptr, 0, "text", sOptLabelLines, 2, RS_DLG_NO_MISSING } };
const RsDialogueOption sOptReplyPercent[] = {
    { "Yes", RS_DLG_ACTION_NONE, 0, "a reply with 50% too much", RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadReply[] = { { nullptr, 0, "text", sOptReplyPercent, 2, RS_DLG_NO_MISSING } };
const RsDialogueOption sOptActionKind[] = {
    { "Yes", (RsDialogueActionKind)99, 0, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadActionKind[] = { { nullptr, 0, "text", sOptActionKind, 2, RS_DLG_NO_MISSING } };
const RsDialogueOption sOptActionQuest[] = {
    { "Yes", RS_DLG_ACTION_START_QUEST, 999, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadActionQuest[] = { { nullptr, 0, "text", sOptActionQuest, 2, RS_DLG_NO_MISSING } };
const RsDialogueOption sOptActionFlagRange[] = {
    { "Yes", RS_DLG_ACTION_SET_WORLD_FLAG, 999999, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadActionFlagRange[] = { { nullptr, 0, "text", sOptActionFlagRange, 2, RS_DLG_NO_MISSING } };
// A DEBUG-band NPC setting a PRODUCTION-band flag: `quest debugwipe` clears only the debug band, so
// this would leave state a wipe cannot undo. Same rule Quest_Register applies to a world-flag reward.
const RsDialogueOption sOptActionFlagBand[] = {
    { "Yes", RS_DLG_ACTION_SET_WORLD_FLAG, 0, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadActionFlagBand[] = { { nullptr, 0, "text", sOptActionFlagBand, 2, RS_DLG_NO_MISSING } };

// The three ways a missing-steps clause can be wrong (D26, P4). The third one is the interesting
// one: a rule whose `missingOf` names a quest it does not gate on is usually not a typo'd quest id
// at all - it is a row that stopped one field early, value-initialising the field to 0, which is a
// real QuestId. This is the check that turns that silent copy-paste into a refused definition.
const RsDialogueRule sBadMissingRange[] = { { nullptr, 0, "text", nullptr, 0, 999 } };
const RsDialogueRule sBadMissingUngated[] = { { nullptr, 0, "text", nullptr, 0, QUEST_DEBUG_GIVER } };
const QuestPredicate sMissingGate[] = { QP_QUEST_STATUS_IS(QUEST_DEBUG_GIVER, QUEST_STATUS_IN_PROGRESS) };
const RsDialogueRule sBadMissingThree[] = { { sMissingGate, 1, "Pick one.", sThreeOk, 3, QUEST_DEBUG_GIVER } };

// A two-option body long enough that AutoFormatString pushes the choice onto a second page. The
// cost of getting this wrong is not cosmetic: the first A press turns the page instead of
// picking, so the conversation does something other than what the table says. It shipped once,
// in P4, and only a screenshot found it - which is why the check now asks the renderer.
const RsDialogueRule sBadTwoOptionLong[] = {
    { nullptr, 0, "Will you fetch what my cake needs, and be quick about it before the Duke arrives?",
      sOkOptions, 2, RS_DLG_NO_MISSING },
};

// The gate here is Always() - it EVALUATES true. The check is structural (whenCount == 0), not
// semantic, because "this rule happens to be true right now" is not the same guarantee as "this
// rule is true in every state", and only the second one makes the fallthrough safe.
// A label wider than an option row (#59). Single line, clean prose, well under any character count
// anyone would have guessed at - and 24 W's is 288 pixels against a 184-pixel budget. The whole
// argument for measuring instead of counting, in one fixture.
const RsDialogueOption sOptLabelWide[] = {
    { "WWWWWWWWWWWWWWWWWWWWWWWW", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadLabelWide[] = { { nullptr, 0, "text", sOptLabelWide, 2, RS_DLG_NO_MISSING } };

// The five ways a `{floor:N}` token can be wrong (sturdy-bassoon#94), one per RsFloorTokenError
// that a definition can actually carry. Spread across the three strings that take tokens rather
// than piled onto `text`, so the table also proves the gate is wired at each site and not only at
// the first one it reaches.
const RsDialogueRule sBadTokenUnclosed[] = { { nullptr, 0, "Up on the {floor:1", nullptr, 0, RS_DLG_NO_MISSING } };
const RsDialogueRule sBadTokenStray[] = { { nullptr, 0, "Up on the floor:1}", nullptr, 0, RS_DLG_NO_MISSING } };
const RsDialogueRule sBadTokenNoColon[] = { { nullptr, 0, "Up on the {floor}", nullptr, 0, RS_DLG_NO_MISSING } };
// `{storey:1}` - a near miss. It has to be refused for the reason the journal's tag table gives:
// a near miss that quietly rendered as literal prose is the failure this grammar exists to stop.
const RsDialogueOption sOptTokenUnknown[] = {
    { "Up to the {storey:1}", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadTokenUnknown[] = { { nullptr, 0, "text", sOptTokenUnknown, 2, RS_DLG_NO_MISSING } };
// Two digits. The cap is one, and `{floor:12}` is the shape a would-be twelfth storey takes.
const RsDialogueOption sOptTokenBadIndex[] = {
    { "Yes", RS_DLG_ACTION_NONE, 0, "See you on the {floor:12}.", RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadTokenBadIndex[] = { { nullptr, 0, "text", sOptTokenBadIndex, 2, RS_DLG_NO_MISSING } };

const QuestPredicate sAlwaysGate[] = { QP_ALWAYS() };
const RsDialogueRule sBadLastConditional[] = { { sAlwaysGate, 1, "a conditional last rule", nullptr, 0, RS_DLG_NO_MISSING } };

// --- the dialogue-tree refusals (sturdy-bassoon#96) ----------------------------------------------
//
// One entry per refusal the tree work added. They are grouped here, and APPENDED to the table at
// the bottom, for the reason every earlier group was: `bad[N]` indices already pinned by the P3 and
// #59 and #94 acceptance drivers stay verbatim.

// `next` naming a node that does not exist. Written as node 0 on a definition with NO nodes,
// because that is also what a row that stopped one field early looks like - the value-initialised
// 0 the struct comment warns about.
const RsDialogueOption sOptNextNoNode[] = {
    { "Yes", RS_DLG_ACTION_NONE, 0, nullptr, 0 },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sBadNextRange[] = { { nullptr, 0, "text", sOptNextNoNode, 2, RS_DLG_NO_MISSING } };

// A node nothing names. The rules reach node 0; node 1 is written, populated and stranded - which
// has no in-game symptom at all, because the symptom is a screen that never appears.
const RsDialogueOption sOptToNodeZero[] = {
    { "Yes", RS_DLG_ACTION_NONE, 0, nullptr, 0 },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sUnreachableRules[] = { { nullptr, 0, "text", sOptToNodeZero, 2, RS_DLG_NO_MISSING } };
const RsDialogueNode sUnreachableNodes[] = {
    { nullptr, 0, "the reachable one", nullptr, 0, RS_DLG_NO_MISSING },
    { nullptr, 0, "nobody can get here", nullptr, 0, RS_DLG_NO_MISSING },
};

// A node reachable ONLY from another unreachable node. The pair would pass a walk seeded from every
// node, and is exactly why the real walk starts at rules only.
const RsDialogueNode sOrphanPairNodes[] = {
    { nullptr, 0, "the reachable one", nullptr, 0, RS_DLG_NO_MISSING },
    { nullptr, 0, "orphan", sOptToNodeZero, 2, RS_DLG_NO_MISSING }, // -> node 0, but nothing -> here
};

// THE TWO-UNGATED RULE. Two declared options, one of them gated: gating can take this screen down
// to a single visible option, which is CTRL_TWO_CHOICE with one label - a cursor on a row that is
// not there. Unreachable to prove at runtime and trivial to refuse here.
const QuestPredicate sSomeGate[] = { QP_WORLD_FLAG_SET(WORLD_FLAG_DEBUG_SMOKE) };
const RsDialogueOption sOptOneUngated[] = {
    { "Always", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "Sometimes", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT, sSomeGate, 1 },
};
const RsDialogueRule sBadOneUngated[] = { { nullptr, 0, "text", sOptOneUngated, 2, RS_DLG_NO_MISSING } };

// An option's own gate, validated exactly as a rule's is: a NULL list with a nonzero count, and an
// operand out of range. Two entries because they are two different messages at a NEW call site.
const RsDialogueOption sOptWhenNull[] = {
    { "Yes", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT, nullptr, 1 },
};
const RsDialogueRule sBadOptWhenNull[] = { { nullptr, 0, "text", sOptWhenNull, 2, RS_DLG_NO_MISSING } };
const QuestPredicate sOptPredFlagRange[] = { QP_WORLD_FLAG_SET(999999) };
const RsDialogueOption sOptWhenRange[] = {
    { "Yes", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "No", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "Maybe", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT, sOptPredFlagRange, 1 },
};
const RsDialogueRule sBadOptWhenRange[] = { { nullptr, 0, "Pick one.", sOptWhenRange, 3, RS_DLG_NO_MISSING } };

// The two fields a NODE has no use for. Both would be dead data that looks live: a gate on a node
// would never be evaluated, and a missing-steps clause has no gate to be checked against.
const RsDialogueNode sGatedNodes[] = {
    { sAlwaysGate, 1, "a gated node", nullptr, 0, RS_DLG_NO_MISSING },
};
const RsDialogueNode sMissingOfNodes[] = {
    { nullptr, 0, "a node with a clause", nullptr, 0, QUEST_DEBUG_GIVER },
};

// The WIDTH SWEEP (#96 P2). Four declared options, THREE of them ungated, so the screen presents 3
// or 4 - never 2. The body is far too wide for one row either way, so what this fixture pins is not
// that it is refused but WHICH COUNT the refusal names: the sweep starts at the smallest count the
// screen can reach, so the message says "a 3-option choice" on a screen that declares four. A check
// reading `optionCount` would have said 4, and would have measured a layout the player can also
// never see.
const RsDialogueOption sOptSweep[] = {
    { "A", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "B", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "C", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
    { "D", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT, sSomeGate, 1 },
};
const RsDialogueRule sBadSweepWidth[] = {
    { nullptr, 0, "This body is far too long to fit one line beside a three-way choice.", sOptSweep, 4,
      RS_DLG_NO_MISSING },
};

// nodeCount out of range, and a nonzero count over a NULL array. The long node array is
// zero-initialised on purpose; its rows never reach screen validation, because the count is
// refused first.
const RsDialogueNode sManyNodes[RS_DIALOGUE_MAX_NODES + 1] = {};

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
BAD_NPC_DEF(sDefFiveOptions, sBadFiveOptions, 1);
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
BAD_NPC_DEF(sDefLabelTooWide, sBadLabelWide, 1);
BAD_NPC_DEF(sDefMissingRange, sBadMissingRange, 1);
BAD_NPC_DEF(sDefMissingUngated, sBadMissingUngated, 1);
BAD_NPC_DEF(sDefMissingThree, sBadMissingThree, 1);
BAD_NPC_DEF(sDefTwoOptionLong, sBadTwoOptionLong, 1);
BAD_NPC_DEF(sDefTokenUnclosed, sBadTokenUnclosed, 1);
BAD_NPC_DEF(sDefTokenStray, sBadTokenStray, 1);
BAD_NPC_DEF(sDefTokenNoColon, sBadTokenNoColon, 1);
BAD_NPC_DEF(sDefTokenUnknown, sBadTokenUnknown, 1);
BAD_NPC_DEF(sDefTokenBadIndex, sBadTokenBadIndex, 1);
// A token in the DISPLAY NAME, which is prose no rule owns.
const RsNpcDef sBadDisplayToken = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad", "Debug: the {floor:} sweeper",
                                    sOkRules, 1 };

// The #96 definitions. The ones carrying nodes cannot go through BAD_NPC_DEF, which stops at
// `ruleCount` and leaves the node array value-initialised - so they are written out.
BAD_NPC_DEF(sDefNextRange, sBadNextRange, 1);
BAD_NPC_DEF(sDefOneUngated, sBadOneUngated, 1);
BAD_NPC_DEF(sDefOptWhenNull, sBadOptWhenNull, 1);
BAD_NPC_DEF(sDefOptWhenRange, sBadOptWhenRange, 1);
BAD_NPC_DEF(sDefSweepWidth, sBadSweepWidth, 1);
const RsNpcDef sDefNodeUnreachable = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG,  "bad", "Bad", sUnreachableRules, 1,
                                       sUnreachableNodes, 2 };
const RsNpcDef sDefNodeOrphanPair = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad", "Bad", sUnreachableRules, 1,
                                      sOrphanPairNodes, 2 };
const RsNpcDef sDefNodeConditional = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad", "Bad", sUnreachableRules, 1,
                                       sGatedNodes, 1 };
const RsNpcDef sDefNodeMissingOf = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad", "Bad", sUnreachableRules, 1,
                                     sMissingOfNodes, 1 };
const RsNpcDef sDefNodeCountHigh = { NPC_DEBUG_GIVER,   QUEST_TIER_DEBUG,          "bad", "Bad", sOkRules, 1,
                                     sManyNodes,        RS_DIALOGUE_MAX_NODES + 1 };
const RsNpcDef sDefNodesNull = { NPC_DEBUG_GIVER, QUEST_TIER_DEBUG, "bad", "Bad", sOkRules, 1, nullptr, 1 };

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
    { "five_options", &sDefFiveOptions },
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
    // APPENDED, never inserted: an acceptance run pins several of these by index.
    { "missing_of_range", &sDefMissingRange },
    { "missing_of_ungated", &sDefMissingUngated },
    { "missing_of_with_options", &sDefMissingThree },
    { "two_option_body_paginates", &sDefTwoOptionLong },
    // #59. Only ONE new entry: the body-too-long case already has a fixture
    // (`three_option_body_too_long`), which now exercises the renderer measurement that replaced the
    // character cap rather than the cap itself - a better test of the same slot, not a new one.
    { "label_too_wide", &sDefLabelTooWide },
    // #94, appended for the same reason every earlier group was: `bad[N]` indices above stay
    // verbatim. One row per way a `{floor:N}` token can be malformed, plus the displayName site.
    { "token_unclosed", &sDefTokenUnclosed },
    { "token_stray_close", &sDefTokenStray },
    { "token_missing_colon", &sDefTokenNoColon },
    { "token_unknown", &sDefTokenUnknown },
    { "token_bad_index", &sDefTokenBadIndex },
    { "token_in_display_name", &sBadDisplayToken },
    // #96, appended for the reason every earlier group was: `bad[N]` indices above stay verbatim.
    // In validator order, so a run reading top to bottom sees the same sequence the gate does.
    { "node_count_too_high", &sDefNodeCountHigh },
    { "nodes_null_nonzero_count", &sDefNodesNull },
    { "node_conditional", &sDefNodeConditional },
    { "node_missing_of", &sDefNodeMissingOf },
    { "option_next_no_node", &sDefNextRange },
    { "option_when_null_nonzero_count", &sDefOptWhenNull },
    { "option_when_operand_range", &sDefOptWhenRange },
    { "one_ungated_option", &sDefOneUngated },
    { "width_sweep_degraded_count", &sDefSweepWidth },
    { "node_unreachable", &sDefNodeUnreachable },
    { "node_reachable_only_from_orphan", &sDefNodeOrphanPair },
};

// RsNpc_Register is idempotent for the same pointer, which is what makes a ShipInit "*" re-run safe.
void RegisterDebugNpcs() {
    RsNpc_Register(&sGiver);
    RsNpc_Register(&sThree);
    RsNpc_Register(&sFour);
    RsNpc_Register(&sTwin);
    RsNpc_Register(&sFloor);
    RsNpc_Register(&sTree);
    RsNpc_Register(&sPage);
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
