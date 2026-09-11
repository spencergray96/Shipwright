#include "NpcDialogue.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/rs/actors/RsActors.h" // RsText_ChoiceWouldPaginate - the renderer answers
#include "soh/Enhancements/rs/prefs/FloorText.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestIds.h"
#include "soh/Enhancements/rs/quest/QuestStore.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"

// --- registry -----------------------------------------------------------------------------------
//
// The same shape as Quest.cpp's: pointers to file-scope definitions, indexed by NpcId, in a
// zero-initialised POD array, so there is no static-initialisation-order hazard with the
// RegisterShipInitFunc objects that call RsNpc_Register.
static std::array<const RsNpcDef*, NPC_MAX> sDefs = {};

namespace {

// Tokens (name) - console/marker words, not prose. Same rule as Quest.cpp's TokenIsClean, and for
// the same reasons: '%' because the ImGui console hands a handler's output to vsnprintf as the
// FORMAT string, '"' because every line both sinks print is key="value", '#' so ONE rule holds
// across the project (a '#' appears only inside well-formed journal markup).
bool TokenIsClean(const char* s) {
    if (s == nullptr) {
        return false;
    }
    for (const char* p = s; *p != '\0'; p++) {
        if (*p == '%' || *p == '#' || *p == '"' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            return false;
        }
    }
    return true;
}

// Prose shown in a textbox. DELIBERATELY NOT JOURNAL MARKUP (D23) - that is the other surface.
// Dialogue goes through CustomMessageManager (D17), where '#' is EncodeColors' span marker, '%' is
// the colour escape and starts a control code, and '^' is a box break. Refusing all of them here
// keeps one rule per surface: journal prose carries `#tag:text#` and no '%'; dialogue prose carries
// '&' (the author's line break, which AutoFormat honours) and nothing else special.
bool ProseIsClean(const char* s) {
    if (s == nullptr) {
        return false;
    }
    for (const char* p = s; *p != '\0'; p++) {
        if (*p == '%' || *p == '#' || *p == '"' || *p == '^' || *p == '\n' || *p == '\r' || *p == '\t') {
            return false;
        }
    }
    return true;
}

int32_t CountLines(const char* s) {
    int32_t lines = 1;
    for (const char* p = s; *p != '\0'; p++) {
        if (*p == '&') {
            lines++;
        }
    }
    return lines;
}

int32_t Length(const char* s) {
    int32_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

// Writes the reason into the CALLER'S buffer and returns false, so validation composes as
// `if (!Problem(...)) return false;`. Caller-supplied for the reason Quest.cpp gives: a file-static
// buffer would be clobbered by a second call in the same expression - exactly what a probe printing
// several definitions' problems on one line does - and it would print plausible-looking WRONG text
// rather than failing visibly. The message never echoes the offending string.
bool Problem(char* buf, size_t len, const char* fmt, ...) {
    if (buf != nullptr && len > 0) {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, len, fmt, args);
        va_end(args);
    }
    return false;
}

// The `{floor:N}` gate (sturdy-bassoon#94, FloorText.h). Every string that reaches a player and
// might name a storey goes through here after ProseIsClean: braces are ordinary characters to
// ProseIsClean, so without this a mistyped token would sail through registration and render as
// literal `{flor:1}` in a textbox - the silent-failure class this validator exists for.
//
// Reports the KIND and the byte OFFSET and never echoes the offending string, exactly as every
// other message here does. `where` is a caller-built prefix so the message keeps naming the rule
// and option it came from.
bool CheckTokens(char* buf, size_t len, const char* where, const char* text) {
    const RsFloorTokenResult token = RsFloorText_Validate(text);
    if (token.error == RS_FLOOR_TOKEN_OK) {
        return true;
    }
    return Problem(buf, len, "%s: floor token %s at offset %d", where, RsFloorText_ErrorName(token.error), token.pos);
}

// Operand range checks for one predicate, mirroring Quest.cpp's. Registered-ness is deliberately
// NOT checked, for the reason stated there: nothing defines the order in which two translation
// units' ShipInit functions run, so "is quest 51 registered yet" is not a question this gate can
// answer without becoming link-order dependent. Only ranges, which are order-independent.
// QuestPrereqsMet and AllStepsSet handle an unresolvable-but-in-range id quietly at evaluation.
//
// `where` is a caller-built prefix ("rule[3].when[1]", "node[0].opt[2].when[0]") so one
// implementation serves the three sites a predicate list can appear at now: a rule's gate, and -
// since #96 - an OPTION's gate on either a rule or a node.
bool CheckPredicate(char* buf, size_t len, const char* where, const QuestPredicate& p) {
    switch (p.kind) {
        case QUEST_PRED_ALWAYS:
            break;
        case QUEST_PRED_QUEST_STATUS_IS:
            if (!QUEST_ID_IS_VALID(p.a)) {
                return Problem(buf, len, "%s: QuestStatusIs quest id %d out of range", where, p.a);
            }
            if (p.b < 0 || p.b >= QUEST_STATUS_COUNT) {
                return Problem(buf, len, "%s: QuestStatusIs status %d out of range", where, p.b);
            }
            break;
        case QUEST_PRED_QUEST_STEP_SET:
            if (!QUEST_ID_IS_VALID(p.a)) {
                return Problem(buf, len, "%s: QuestStepSet quest id %d out of range", where, p.a);
            }
            if (p.b < 0 || p.b >= QUEST_STEP_MAX) {
                return Problem(buf, len, "%s: QuestStepSet step %d out of range", where, p.b);
            }
            break;
        case QUEST_PRED_WORLD_FLAG_SET:
            if (p.a < 0 || p.a >= WORLD_FLAG_MAX) {
                return Problem(buf, len, "%s: WorldFlagSet flag %d out of range", where, p.a);
            }
            break;
        case QUEST_PRED_ALL_STEPS_SET:
            if (!QUEST_ID_IS_VALID(p.a)) {
                return Problem(buf, len, "%s: AllStepsSet quest id %d out of range", where, p.a);
            }
            break;
        case QUEST_PRED_QUEST_PREREQS_MET:
            if (!QUEST_ID_IS_VALID(p.a)) {
                return Problem(buf, len, "%s: QuestPrereqsMet quest id %d out of range", where, p.a);
            }
            break;
        default:
            return Problem(buf, len, "%s: unknown predicate kind %d", where, static_cast<int>(p.kind));
    }
    return true;
}

// One predicate LIST: the NULL-with-a-count check plus every operand. Same shape at all three
// sites, so a gate on an option is validated exactly as a gate on a rule is.
//
// Two prefixes rather than one because the two messages have always named different things: the
// NULL-count refusal names the OWNER ("rule[0]"), each operand refusal names the SLOT
// ("rule[0].when[1]"). Kept apart so both strings stay byte-identical to what the acceptance
// drivers pin.
bool CheckPredicateList(char* buf, size_t len, const char* owner, const char* list_where, const QuestPredicate* list,
                        int32_t count) {
    if (count < 0 || (count > 0 && list == nullptr)) {
        return Problem(buf, len, "%s: `when` is NULL with a nonzero count", owner);
    }
    char slot[96];
    for (int32_t i = 0; i < count; i++) {
        std::snprintf(slot, sizeof(slot), "%s[%d]", list_where, i);
        if (!CheckPredicate(buf, len, slot, list[i])) {
            return false;
        }
    }
    return true;
}

// Every refusal message names the screen it came from. A rule and a node are the same struct and
// share one validator, so the prefix is what tells an author which array to look in - and it is
// built once, here, rather than duplicated into forty format strings.
//
// `rule[%d]` is kept VERBATIM for rules: the acceptance drivers pin those messages.
struct ScreenRef {
    const RsDialogueRule* screen;
    int32_t kind; // RS_SCREEN_RULE or RS_SCREEN_NODE
    int32_t index;
};

// --- graph helpers (#96) -------------------------------------------------------------------------
//
// Every whole-definition question about a dialogue tree is a walk over the same graph, and each has
// TWO callers that must agree by construction: registration refuses on the answer, and `npc tree` or
// `npc dump` prints it so a run can ASSERT the gate from a console. A second copy of a walk would assert itself
// rather than the gate, and the two could drift apart without either one failing - so each walk
// exists exactly once, here.
//
// The graph. Entry rules and nodes are screens. A screen's exits are its options' `next`, or - for a
// statement - its own `next`; RS_DLG_NO_NEXT is an exit that closes. An exit to a node lands on that
// node's GROUP (NpcDialogueDef.h): the node and every node after it up to and including the first
// ungated one. WHICH member it lands on is a runtime fact, so every walk here treats an exit as
// reaching every member of the group.

// Every predicate in a screen's own `when` holds (an ungated screen always does). The ONE evaluation
// behind both a rule's entry match and a node group's resolution - they are the same question asked
// of the same struct, and two copies of it are two chances for "matches" to mean different things.
bool ScreenGateHolds(const RsDialogueRule& screen) {
    for (int32_t i = 0; i < screen.whenCount; i++) {
        if (!QuestPredicate_Eval(&screen.when[i])) {
            return false;
        }
    }
    return true;
}

// The quests a predicate list is gated on, as a set: the four quest-shaped words, negated or not -
// the same reading the rule-level missing-steps check has always used, because the question is "does
// this screen condition on the quest at all", which is what catches a forgotten sentinel. An operand
// out of range is skipped rather than trusted.
std::bitset<QUEST_MAX> QuestsGatedBy(const QuestPredicate* list, int32_t count) {
    std::bitset<QUEST_MAX> quests;
    for (int32_t i = 0; list != nullptr && i < count; i++) {
        switch (list[i].kind) {
            case QUEST_PRED_QUEST_STATUS_IS:
            case QUEST_PRED_QUEST_STEP_SET:
            case QUEST_PRED_ALL_STEPS_SET:
            case QUEST_PRED_QUEST_PREREQS_MET:
                if (QUEST_ID_IS_VALID(list[i].a)) {
                    quests.set(static_cast<size_t>(list[i].a));
                }
                break;
            default:
                break;
        }
    }
    return quests;
}

// The last member of the group an exit to `head` lands on: the first node at or after `head` with no
// gate. def->nodeCount when the group runs off the end of the array, which registration refuses.
int32_t GroupEnd(const RsNpcDef* def, int32_t head) {
    for (int32_t n = head; n < def->nodeCount; n++) {
        if (def->nodes[n].whenCount == 0) {
            return n;
        }
    }
    return def->nodeCount;
}

// Calls `visit(next, optionGates, ungated)` once per exit from `screen`: once per option, or once for
// a statement's own `next`. `optionGates` is the gate on the option taken - empty for a statement -
// and `ungated` says whether this exit is always offered. `next` may be RS_DLG_NO_NEXT.
template <typename Visit> void ForEachExit(const RsDialogueRule& screen, Visit visit) {
    if (screen.optionCount == 0) {
        visit(screen.next, std::bitset<QUEST_MAX>(), true);
        return;
    }
    for (int32_t i = 0; i < screen.optionCount; i++) {
        const RsDialogueOption& option = screen.options[i];
        visit(option.next, QuestsGatedBy(option.when, option.whenCount), option.whenCount == 0);
    }
}

// REACHABILITY. Marks every node some exit lands on, walking out from the ENTRY RULES. Cycles are
// legal - loop-back is the feature - so this is a visited set, not a depth limit. It starts at rules
// ONLY: a node reachable solely from another unreachable node is still unreachable, and a walk
// seeded from every node would call that pair fine. `reached` holds RS_DIALOGUE_MAX_NODES entries,
// zeroed by the caller.
void MarkReachableNodes(const RsNpcDef* def, bool* reached) {
    int32_t pending[RS_DIALOGUE_MAX_NODES];
    int32_t pendingCount = 0;
    const auto land = [&](int32_t head, const std::bitset<QUEST_MAX>&, bool) {
        if (head < 0 || head >= def->nodeCount) {
            return;
        }
        const int32_t end = std::min(GroupEnd(def, head), def->nodeCount - 1);
        for (int32_t m = head; m <= end; m++) {
            if (!reached[m]) {
                reached[m] = true;
                pending[pendingCount++] = m; // each node is pushed at most once, so this cannot overflow
            }
        }
    };
    for (int32_t r = 0; r < def->ruleCount; r++) {
        ForEachExit(def->rules[r], land);
    }
    while (pendingCount > 0) {
        ForEachExit(def->nodes[pending[--pendingCount]], land);
    }
}

// CAN THE CONVERSATION END (#96 follow-up). Which screens have a way out, following only UNGATED
// exits. A gated exit does not count: it may never be offered, and a player in front of a choice box
// whose every visible answer loops back has no way out at all - a choice cannot be dismissed without
// picking, and Link cannot walk away from an open textbox. That is a soft-lock, and the first cut of
// #96 could register one.
//
// A least fixpoint: nothing can end until shown otherwise. A screen can end once one of its ungated
// exits closes, or lands on a group EVERY member of which can end - every member, because which one
// it lands on is decided by the stores at runtime. `ruleEnds` / `nodeEnds` are zeroed by the caller.
void ComputeCanEnd(const RsNpcDef* def, bool* ruleEnds, bool* nodeEnds) {
    const auto groupEnds = [&](int32_t head) {
        if (head < 0 || head >= def->nodeCount) {
            return false;
        }
        const int32_t end = GroupEnd(def, head);
        if (end >= def->nodeCount) {
            return false;
        }
        for (int32_t m = head; m <= end; m++) {
            if (!nodeEnds[m]) {
                return false;
            }
        }
        return true;
    };
    const auto screenEnds = [&](const RsDialogueRule& screen) {
        bool ends = false;
        ForEachExit(screen, [&](int32_t next, const std::bitset<QUEST_MAX>&, bool ungated) {
            if (ungated && (next == RS_DLG_NO_NEXT || groupEnds(next))) {
                ends = true;
            }
        });
        return ends;
    };
    bool changed = true;
    while (changed) {
        changed = false;
        for (int32_t r = 0; r < def->ruleCount; r++) {
            if (!ruleEnds[r] && screenEnds(def->rules[r])) {
                ruleEnds[r] = true;
                changed = true;
            }
        }
        for (int32_t n = 0; n < def->nodeCount; n++) {
            if (!nodeEnds[n] && screenEnds(def->nodes[n])) {
                nodeEnds[n] = true;
                changed = true;
            }
        }
    }
}

// 1 when node `n`'s missing-steps clause is covered: its own gate names the clause's quest, or every
// path in guarantees it (`arrival`, from ComputeArrivalGates). A node with no clause is trivially
// covered. Shared by registration and `npc dump`'s `clause_gated=`, for the reason every walk here is.
// Declared ahead of ComputeArrivalGates so the two read top-down as question, then analysis.
bool NodeClauseGated(const RsNpcDef* def, const std::bitset<QUEST_MAX>* arrival, int32_t n) {
    const RsDialogueNode& node = def->nodes[n];
    if (node.missingOf == RS_DLG_NO_MISSING) {
        return true;
    }
    if (!QUEST_ID_IS_VALID(node.missingOf)) {
        return false;
    }
    const std::bitset<QUEST_MAX> gated = arrival[n] | QuestsGatedBy(node.when, node.whenCount);
    return gated.test(static_cast<size_t>(node.missingOf));
}

// WHAT IS GUARANTEED ON ARRIVAL (#96 follow-up). For every node, the quests gated on along EVERY path
// into it from an entry rule. This is what lets a missing-steps clause sit on a node: the clause's
// invariant is "only ever shown to a player the screen is gated on that quest for", and on a node
// that can be satisfied upstream - behind the option that leads there, or the rule the conversation
// started from - as well as by the node's own gate.
//
// A must-analysis, so a GREATEST fixpoint: every node starts out believing everything, and each path
// in takes away what that path does not guarantee. A path guarantees what its source screen was
// guaranteed, plus that screen's own gate (the screen is open, so its gate held), plus the gate on the
// option taken. Falling through a group is NOT knowledge: arriving at a later member means an earlier
// member's gate failed, which guarantees nothing positive, so every member inherits exactly what an
// exit to the group's head carries. Run only after reachability passes, so no stranded node is left
// believing everything. `arrival` holds RS_DIALOGUE_MAX_NODES entries.
void ComputeArrivalGates(const RsNpcDef* def, std::bitset<QUEST_MAX>* arrival) {
    for (int32_t n = 0; n < def->nodeCount; n++) {
        arrival[n].set();
    }
    bool changed = true;
    const auto narrowFrom = [&](const std::bitset<QUEST_MAX> base) {
        return [&, base](int32_t head, const std::bitset<QUEST_MAX>& optionGates, bool) {
            if (head < 0 || head >= def->nodeCount) {
                return;
            }
            const std::bitset<QUEST_MAX> context = base | optionGates;
            const int32_t end = std::min(GroupEnd(def, head), def->nodeCount - 1);
            for (int32_t m = head; m <= end; m++) {
                const std::bitset<QUEST_MAX> narrowed = arrival[m] & context;
                if (narrowed != arrival[m]) {
                    arrival[m] = narrowed;
                    changed = true;
                }
            }
        };
    };
    while (changed) {
        changed = false;
        for (int32_t r = 0; r < def->ruleCount; r++) {
            const RsDialogueRule& rule = def->rules[r];
            ForEachExit(rule, narrowFrom(QuestsGatedBy(rule.when, rule.whenCount)));
        }
        for (int32_t n = 0; n < def->nodeCount; n++) {
            const RsDialogueNode& node = def->nodes[n];
            ForEachExit(node, narrowFrom(arrival[n] | QuestsGatedBy(node.when, node.whenCount)));
        }
    }
}

bool CheckOption(char* buf, size_t len, const RsNpcDef* def, const ScreenRef& ref, int32_t index) {
    const char* word = RsNpc_ScreenKindName(ref.kind);
    const int32_t screenIndex = ref.index;
    const RsDialogueOption& option = ref.screen->options[index];
    if (!ProseIsClean(option.label)) {
        return Problem(buf, len, "%s[%d].opt[%d]: label is NULL, or carries percent, hash, quote, caret or a newline",
                       word, screenIndex, index);
    }
    if (Length(option.label) == 0) {
        return Problem(buf, len, "%s[%d].opt[%d]: label is empty", word, screenIndex, index);
    }
    // Before the width measurement below, which EXPANDS this label: a malformed token would be
    // measured as its own diagnostic string, which is a nonsense width to refuse a definition over.
    char where[48];
    std::snprintf(where, sizeof(where), "%s[%d].opt[%d]: label", word, screenIndex, index);
    if (!CheckTokens(buf, len, where, option.label)) {
        return false;
    }
    if (CountLines(option.label) != 1) {
        // Each label is one line of the choice block; an '&' inside one would shift the cursor rows
        // away from the lines they select, which renders plausibly and picks the wrong option.
        return Problem(buf, len, "%s[%d].opt[%d]: a label must be a single line", word, screenIndex, index);
    }
    // ...and one line that FITS. Labels were never measured at any option count - the gap surfaced
    // in #59's review. An option row is indented 32px, so its budget is 184 pixels, and a label
    // over it runs off the right edge of the box: still selectable, simply unreadable. That is the
    // same failure P3 shipped vertically, and it is why this asks the renderer's pixel table
    // rather than counting characters.
    if (RsText_LabelWouldOverflow(option.label)) {
        return Problem(buf, len, "%s[%d].opt[%d]: the label is too wide for an option row (184 pixels)", word,
                       screenIndex, index);
    }
    if (option.reply != nullptr && !ProseIsClean(option.reply)) {
        return Problem(buf, len, "%s[%d].opt[%d]: reply carries percent, hash, quote, caret or a newline", word,
                       screenIndex, index);
    }
    if (option.reply != nullptr) {
        std::snprintf(where, sizeof(where), "%s[%d].opt[%d]: reply", word, screenIndex, index);
        if (!CheckTokens(buf, len, where, option.reply)) {
            return false;
        }
    }
    switch (option.kind) {
        case RS_DLG_ACTION_NONE:
            break;
        case RS_DLG_ACTION_START_QUEST:
        case RS_DLG_ACTION_COMPLETE_QUEST:
            if (!QUEST_ID_IS_VALID(option.a)) {
                return Problem(buf, len, "%s[%d].opt[%d]: quest id %d out of range", word, screenIndex, index,
                               option.a);
            }
            break;
        case RS_DLG_ACTION_SET_WORLD_FLAG:
            if (option.a < 0 || option.a >= WORLD_FLAG_MAX) {
                return Problem(buf, len, "%s[%d].opt[%d]: world flag %d is outside the store", word, screenIndex, index,
                               option.a);
            }
            // The same rule Quest_Register applies to a world-flag reward: a debug NPC must not set
            // a production flag, because `quest debugwipe` clears only the debug band and could not
            // undo it.
            if ((WORLD_FLAG_IS_DEBUG(option.a) != 0) != (def->tier == QUEST_TIER_DEBUG)) {
                return Problem(buf, len, "%s[%d].opt[%d]: world flag %d is in the other tier's band", word,
                               screenIndex, index, option.a);
            }
            break;
        default:
            return Problem(buf, len, "%s[%d].opt[%d]: unknown action kind %d", word, screenIndex, index,
                           static_cast<int>(option.kind));
    }
    // --- navigation (#96 P1) --------------------------------------------------------------------
    //
    // RS_DLG_NO_NEXT closes; anything else names a NODE of this same NPC. There is no "next names a
    // rule" case by design (NpcDialogueDef.h), so an out-of-range value is the whole check - and it
    // is the one that catches the failure `next` is most likely to have: a row that stopped early
    // and value-initialised the field to 0. On a character with no nodes that is caught here; on a
    // character with nodes, node 0 is a real screen and only stating the field on every row saves
    // you, which is why the struct comment insists on it.
    if (option.next != RS_DLG_NO_NEXT && (option.next < 0 || option.next >= def->nodeCount)) {
        return Problem(buf, len, "%s[%d].opt[%d]: next=%d names no node (this npc has %d; use RS_DLG_NO_NEXT to close)",
                       word, screenIndex, index, option.next, def->nodeCount);
    }
    // --- gating (#96 P2) ------------------------------------------------------------------------
    std::snprintf(where, sizeof(where), "%s[%d].opt[%d]", word, screenIndex, index);
    char list_where[64];
    std::snprintf(list_where, sizeof(list_where), "%s.when", where);
    if (!CheckPredicateList(buf, len, where, list_where, option.when, option.whenCount)) {
        return false;
    }
    return true;
}

// One SCREEN - a rule or a node. They are the same struct and share this one validator; `ref.kind`
// only decides the prefix each refusal message carries and switches on the handful of things that
// genuinely differ, which are the two fields a node has no use for.
bool ValidateScreen(char* buf, size_t len, const RsNpcDef* def, const ScreenRef& ref) {
    const char* word = RsNpc_ScreenKindName(ref.kind);
    const int32_t screenIndex = ref.index;
    const RsDialogueRule& screen = *ref.screen;
    char owner[24];
    std::snprintf(owner, sizeof(owner), "%s[%d]", word, screenIndex);
    // A node MAY carry a gate: it makes the node the first member of a group (NpcDialogueDef.h). The
    // gate's operands are checked here like any other; that the group has an ungated end to fall
    // through to is a whole-definition fact, checked in ValidateDef.
    {
        char list_where[32];
        std::snprintf(list_where, sizeof(list_where), "%s.when", owner);
        if (!CheckPredicateList(buf, len, owner, list_where, screen.when, screen.whenCount)) {
            return false;
        }
    }
    if (!ProseIsClean(screen.text)) {
        return Problem(buf, len, "%s[%d]: text is NULL, or carries percent, hash, quote, caret or a newline", word,
                       screenIndex);
    }
    if (Length(screen.text) == 0) {
        return Problem(buf, len, "%s[%d]: text is empty", word, screenIndex);
    }
    // Before every width measurement below, for the reason CheckOption gives about labels.
    char where[40];
    std::snprintf(where, sizeof(where), "%s[%d]: text", word, screenIndex);
    if (!CheckTokens(buf, len, where, screen.text)) {
        return false;
    }
    if (screen.optionCount < 0 || (screen.optionCount > 0 && screen.options == nullptr)) {
        return Problem(buf, len, "%s[%d]: `options` is NULL with a nonzero count", word, screenIndex);
    }
    if (screen.optionCount == 1) {
        // A one-option "choice" is a statement with a cursor next to it. OoT has no such textbox,
        // and CTRL_TWO_CHOICE with one label puts the cursor on a line that is not there.
        return Problem(buf, len, "%s[%d]: one option is not a choice; use 0 for a statement", word, screenIndex);
    }
    if (screen.optionCount > RS_DIALOGUE_MAX_OPTIONS) {
        // The MODEL is N (NpcDialogueDef.h). This is the RENDERER's limit, and lifting it is a
        // change to one function - see sturdy-bassoon#59.
        return Problem(buf, len, "%s[%d]: %d options; the renderer does at most %d (see sturdy-bassoon#59)", word,
                       screenIndex, screen.optionCount, RS_DIALOGUE_MAX_OPTIONS);
    }
    // --- continue (#96 follow-up) -----------------------------------------------------------------
    //
    // A STATEMENT may name a node to continue to when it is dismissed. A choice may not: it leaves
    // through its options' own `next`, and a second exit on the screen would be dead data that looks
    // live. Checked before the range, so a stray `next` on a choice gets the message that says why.
    if (screen.next != RS_DLG_NO_NEXT) {
        if (screen.optionCount != 0) {
            return Problem(buf, len,
                           "%s[%d]: `next` on a screen with options - a choice leaves through its options' own "
                           "`next`; use RS_DLG_NO_NEXT here",
                           word, screenIndex);
        }
        if (screen.next < 0 || screen.next >= def->nodeCount) {
            return Problem(buf, len,
                           "%s[%d]: next=%d names no node (this npc has %d; use RS_DLG_NO_NEXT to close)", word,
                           screenIndex, screen.next, def->nodeCount);
        }
    }
    if (screen.missingOf != RS_DLG_NO_MISSING) {
        if (!QUEST_ID_IS_VALID(screen.missingOf)) {
            return Problem(buf, len, "%s[%d]: missingOf quest id %d out of range (use RS_DLG_NO_MISSING for none)",
                           word, screenIndex, screen.missingOf);
        }
        // The invariant that also catches a forgotten RS_DLG_NO_MISSING, since a value-initialised
        // 0 is a real QuestId (NpcDialogueDef.h). A clause listing what is missing from a quest the
        // screen is not gated on would render "I still need everything" at a player who has never
        // been offered it. Registered-ness is not checked here for the usual reason: nothing
        // defines the order two translation units' ShipInit functions run in.
        //
        // On a RULE the question is asked here, of the rule's own gate - a rule is entered by
        // matching, so its gate is the whole of what is known on arrival. On a NODE it is asked of
        // the node's own gate AND of every path into it, which is a whole-definition question: so
        // ValidateDef asks it, after reachability, and this screen-local pass only range-checks.
        if (ref.kind == RS_SCREEN_RULE &&
            !QuestsGatedBy(screen.when, screen.whenCount).test(static_cast<size_t>(screen.missingOf))) {
            return Problem(buf, len, "%s[%d]: missingOf names quest %d but no predicate in this rule gates on it",
                           word, screenIndex, screen.missingOf);
        }
        if (screen.optionCount != 0) {
            // A clause is only allowed on a STATEMENT, and the reason is that its length is a
            // RUNTIME fact - it grows with the number of steps still missing - while everything
            // else about a box's layout is checked here, once. A statement that grows too long
            // simply paginates, and pagination costs a reader one more A press. A CHOICE that
            // paginates lands on the second page, where the first A press turns the page instead
            // of picking an option: the box looks right and the conversation does something else.
            // Refusing the combination outright is the only check that holds for every future
            // state of the quest.
            return Problem(buf, len, "%s[%d]: a missing-steps clause needs a statement; this %s has %d options",
                           word, screenIndex, word, screen.optionCount);
        }
    }
    if (screen.optionCount >= 3) {
        // The multiline check is count-independent: an '&' in a body that will ever be
        // hand-laid-out is wrong at 3 visible options and wrong at 4, so it is asked once here
        // rather than inside the sweep below.
        if (CountLines(screen.text) != 1) {
            return Problem(buf, len, "%s[%d]: a %d-option body must be a single line", word, screenIndex,
                           screen.optionCount);
        }
    }
    for (int32_t i = 0; i < screen.optionCount; i++) {
        if (!CheckOption(buf, len, def, ref, i)) {
            return false;
        }
    }
    // --- the two-ungated rule (#96 P2) -----------------------------------------------------------
    //
    // THE SHARP EDGE OF GATING, and the one invariant here worth stating loudly. A screen with four
    // declared options can present 4, 3, 2, 1 or 0 visible. Zero would be a statement and close,
    // which is harmless. ONE IS NOT: CTRL_TWO_CHOICE with a single label puts the cursor on a row
    // that is not there - already a refusal above ("one option is not a choice") - and gating can
    // otherwise reach that state at RUNTIME, where no gate can see it and no marker reports it.
    //
    // Proving that no combination of gates yields exactly one would need a 2^N analysis of runtime
    // predicates, i.e. it cannot be done. Requiring two UNGATED options makes it impossible
    // instead, is checkable in one pass, and is an honest content rule: a choice has at least two
    // answers.
    const int32_t ungated = RsNpc_UngatedOptionCount(&screen);
    if (screen.optionCount >= 2 && ungated < 2) {
        return Problem(buf, len,
                       "%s[%d]: %d of %d options are ungated; a screen needs TWO that are always offered, or gating "
                       "can leave one option and a cursor on a row that is not there",
                       word, screenIndex, ungated, screen.optionCount);
    }
    // --- the width sweep (#96 P2) ----------------------------------------------------------------
    //
    // Every visible count this screen can REACH, which is [ungated, optionCount] - a handful of
    // layouts, not 2^N. A screen degrading from four visible to two switches from the hand-laid-out
    // Format() path to the AutoFormat two-choice path, which is a genuinely different formatter, so
    // both are asked.
    //
    // WHAT THE SWEEP ACTUALLY BUYS TODAY, stated honestly because the issue guessed higher: the two
    // budgets currently COINCIDE. AutoFormatString fills a full-width row at 216px and caps a page
    // at four rows, and a two-way choice spends three of them (a blank plus two labels), so its
    // body must be one row - which is exactly what RsText_BodyWouldWrap demands of the Format()
    // path. So no definition is refused here that a single `optionCount`-shaped check would have
    // let through. What sweeping does buy is real but smaller: the refusal names the count that
    // actually breaks rather than the declared one, no layout the player cannot reach is ever
    // measured, and the day either formatter's budget moves - a taller box, a different font - the
    // two stop coinciding and this is already asking both.
    //
    // LAST, and the ordering is load-bearing: RsText_ChoiceWouldPaginate FORMATS the screen's real
    // message, so it dereferences every option label. It must run only after CheckOption has proven
    // each label is a non-NULL single line - otherwise the validator crashes on exactly the
    // malformed definitions it exists to refuse, which is how `npc badcheck` first hung.
    //
    // Which K options stand in for a given count does not change the answer: every label has
    // already been proven to fit its 184-pixel row, so the choice block is exactly K rows whichever
    // K are showing, and what the measurement is really asking about is the BODY.
    for (int32_t visible = (ungated < 2 ? 2 : ungated); visible <= screen.optionCount; visible++) {
        if (visible >= 3) {
            // CustomMessage::AutoFormatString is CTRL_TWO_CHOICE-aware and lays the choice out
            // itself, but it knows neither CTRL_THREE_CHOICE nor our CTRL_FOUR_CHOICE - so those
            // are hand-laid-out through Format() and the body has to fit one row on its own,
            // because the remaining rows are already spent on options.
            //
            // This USED to be `Length(screen.text) > 24`, whose own comment called itself a stand-in
            // for the real budget of 216 PIXELS in a variable-width font. A character count is
            // wrong in both directions, and the expensive direction bit first: it refused "What can
            // I help you with, Link?" - 30 characters and comfortably inside 216px - which is the
            // sentence #59 was filed to make possible. It asks the renderer instead.
            if (RsText_BodyWouldWrap(&screen, visible)) {
                return Problem(buf, len,
                               "%s[%d]: the body is too wide to sit on one row above a %d-option choice - it would "
                               "wrap onto a row an option is already using",
                               word, screenIndex, visible);
            }
        } else if (visible == 2) {
            // Asked of the RENDERER rather than guessed at (RsActors.cpp). A two-option body that
            // wraps to a second line pushes AutoFormatString past four rows, and its answer is a
            // page break BEFORE the choice - so the box renders perfectly and the first A press
            // turns the page instead of choosing. That shipped once and only a screenshot found it.
            if (RsText_ChoiceWouldPaginate(&screen, visible)) {
                return Problem(buf, len,
                               "%s[%d]: the body is too long beside a two-option choice - the choice would land "
                               "on a second page, where the first A press turns the page instead of picking",
                               word, screenIndex);
            }
        }
    }
    return true;
}

// True when the definition is clean; otherwise writes the reason into `buf` and returns false.
bool ValidateDef(const RsNpcDef* def, char* buf, size_t len) {
    if (def == nullptr) {
        return Problem(buf, len, "NULL definition");
    }
    if (!NPC_ID_IS_VALID(def->id)) {
        return Problem(buf, len, "id %d out of range", def->id);
    }
    if (def->tier != NPC_ID_TIER(def->id)) {
        return Problem(buf, len, "tier does not match the id's band");
    }
    if (!TokenIsClean(def->name)) {
        return Problem(buf, len, "name is NULL, or carries whitespace, percent, hash or quote");
    }
    if (!ProseIsClean(def->displayName)) {
        return Problem(buf, len, "displayName is NULL, or carries percent, hash, quote, caret or a newline");
    }
    if (!CheckTokens(buf, len, "displayName", def->displayName)) {
        return false;
    }
    if (def->ruleCount < 1 || def->ruleCount > RS_DIALOGUE_MAX_RULES) {
        return Problem(buf, len, "ruleCount %d outside [1, %d]", def->ruleCount, RS_DIALOGUE_MAX_RULES);
    }
    if (def->rules == nullptr) {
        return Problem(buf, len, "rules list is NULL with a nonzero count");
    }
    // The node array FIRST, because CheckOption range-checks every `next` against def->nodeCount
    // and a garbage count would make those checks meaningless.
    if (def->nodeCount < 0 || def->nodeCount > RS_DIALOGUE_MAX_NODES) {
        return Problem(buf, len, "nodeCount %d outside [0, %d]", def->nodeCount, RS_DIALOGUE_MAX_NODES);
    }
    if (def->nodeCount > 0 && def->nodes == nullptr) {
        return Problem(buf, len, "nodes list is NULL with a nonzero count");
    }
    for (int32_t r = 0; r < def->ruleCount; r++) {
        const ScreenRef ref = { &def->rules[r], RS_SCREEN_RULE, r };
        if (!ValidateScreen(buf, len, def, ref)) {
            return false;
        }
    }
    for (int32_t n = 0; n < def->nodeCount; n++) {
        const ScreenRef ref = { &def->nodes[n], RS_SCREEN_NODE, n };
        if (!ValidateScreen(buf, len, def, ref)) {
            return false;
        }
    }
    // D8, made structural. An NPC whose gate is unmet has to land SOMEWHERE, and "nowhere" is a
    // silent failure: the actor would offer a textId naming a rule that does not match, or none.
    if (def->rules[def->ruleCount - 1].whenCount != 0) {
        return Problem(buf, len, "the last rule must be unconditional - it is the generic fallthrough (D8)");
    }
    // --- node groups (#96 follow-up) -----------------------------------------------------------
    //
    // D8's rule applied to a run of nodes instead of the whole table: a group that could resolve to
    // nothing is a silent failure. Reported at the gated node whose run has nowhere to fall through.
    for (int32_t n = 0; n < def->nodeCount; n++) {
        if (def->nodes[n].whenCount != 0 && GroupEnd(def, n) >= def->nodeCount) {
            return Problem(buf, len,
                           "node[%d]: a gated node needs an ungated node after it to fall through to - this "
                           "group runs off the end of the array",
                           n);
        }
    }
    // --- reachability (#96 P1) -------------------------------------------------------------------
    //
    // The check that earns its keep. `next` being in range is a typo check; this one catches the
    // class that strands content nobody can ever see: a node written, populated with prose, and never
    // landed on by anything. There is no in-game symptom to notice, because the symptom is a screen
    // that simply never appears. The walk itself, and why it starts at rules only, is
    // MarkReachableNodes.
    if (def->nodeCount > 0) {
        bool reached[RS_DIALOGUE_MAX_NODES] = {};
        MarkReachableNodes(def, reached);
        for (int32_t n = 0; n < def->nodeCount; n++) {
            if (!reached[n]) {
                return Problem(buf, len,
                               "node[%d] is unreachable - no exit from an entry rule, or from a node reachable "
                               "from one, lands on it",
                               n);
            }
        }
    }
    // --- the conversation can end (#96 follow-up) --------------------------------------------------
    //
    // After reachability, so a stranded node is reported as stranded rather than as a trap.
    {
        bool ruleEnds[RS_DIALOGUE_MAX_RULES] = {};
        bool nodeEnds[RS_DIALOGUE_MAX_NODES] = {};
        ComputeCanEnd(def, ruleEnds, nodeEnds);
        // Rules first, then nodes: the first screen found is the one reported.
        for (int32_t pass = 0; pass < 2; pass++) {
            const int32_t kind = (pass == 0) ? RS_SCREEN_RULE : RS_SCREEN_NODE;
            const int32_t count = (pass == 0) ? def->ruleCount : def->nodeCount;
            const bool* ends = (pass == 0) ? ruleEnds : nodeEnds;
            for (int32_t i = 0; i < count; i++) {
                if (!ends[i]) {
                    return Problem(buf, len,
                                   "%s[%d]: the conversation can never end from here - every ungated way out "
                                   "loops back, and a choice box cannot be dismissed without picking",
                                   RsNpc_ScreenKindName(kind), i);
                }
            }
        }
    }
    // --- a missing-steps clause on a node (#96 follow-up) ---------------------------------------
    //
    // ValidateScreen asks a RULE's clause of the rule's own gate. A node's clause is asked here, of
    // its own gate OR of every path into it, because only the whole graph knows the paths. Same
    // invariant, same reason: a clause shown to a player the screen is not gated on that quest for
    // reads "you still need everything" about a quest they were never offered - and it is still the
    // check that turns a forgotten RS_DLG_NO_MISSING into a refusal.
    if (def->nodeCount > 0) {
        std::bitset<QUEST_MAX> arrival[RS_DIALOGUE_MAX_NODES];
        ComputeArrivalGates(def, arrival);
        for (int32_t n = 0; n < def->nodeCount; n++) {
            const RsDialogueNode& node = def->nodes[n];
            if (!NodeClauseGated(def, arrival, n)) {
                return Problem(buf, len,
                               "node[%d]: missingOf names quest %d, but some path into this node is not gated on "
                               "it - gate the node, or every way in",
                               n, node.missingOf);
            }
        }
    }
    return true;
}

} // namespace

extern "C" int32_t RsNpc_DefProblem(const RsNpcDef* def, char* buf, size_t len) {
    if (buf != nullptr && len > 0) {
        buf[0] = '\0';
    }
    return ValidateDef(def, buf, len) ? 0 : 1;
}

extern "C" int32_t RsNpc_Register(const RsNpcDef* def) {
    char problem[192];
    if (!ValidateDef(def, problem, sizeof(problem))) {
        SPDLOG_ERROR("RsNpc: register npc {} ({}): {}", def != nullptr ? def->id : -1,
                     (def != nullptr && def->name != nullptr) ? def->name : "<null>", problem);
        assert(false && "npc definition failed validation");
        return RS_NPC_ERR_BAD_DEF;
    }
    const RsNpcDef* existing = sDefs[def->id];
    if (existing == def) {
        return RS_NPC_OK; // ShipInit re-run; already ours
    }
    if (existing != nullptr) {
        SPDLOG_ERROR("RsNpc: register npc {} ({}): id already owned by '{}'", def->id, def->name, existing->name);
        assert(false && "duplicate npc id");
        return RS_NPC_ERR_DUPLICATE;
    }
    sDefs[def->id] = def;
    SPDLOG_INFO("RsNpc: registered {} '{}' tier={} rules={} nodes={}", def->id, def->name, Quest_TierName(def->tier),
                def->ruleCount, def->nodeCount);
    return RS_NPC_OK;
}

extern "C" const RsNpcDef* RsNpc_GetDef(int32_t npcId) {
    if (!NPC_ID_IS_VALID(npcId)) {
        return nullptr;
    }
    return sDefs[npcId];
}

extern "C" int32_t RsNpc_IsRegistered(int32_t npcId) {
    return RsNpc_GetDef(npcId) != nullptr;
}

extern "C" int32_t RsNpc_RegisteredCount(void) {
    int32_t count = 0;
    for (const RsNpcDef* def : sDefs) {
        if (def != nullptr) {
            count++;
        }
    }
    return count;
}

extern "C" int32_t RsNpc_RuleMatches(int32_t npcId, int32_t ruleIndex) {
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    if (def == nullptr || ruleIndex < 0 || ruleIndex >= def->ruleCount) {
        return 0;
    }
    return ScreenGateHolds(def->rules[ruleIndex]) ? 1 : 0;
}

// --- screens and navigation (#96) ---------------------------------------------------------------

extern "C" int32_t RsNpc_DecodeScreen(uint16_t textId, int32_t* npcId, int32_t* index) {
    if (textId >= RS_TEXT_NPC_BASE && textId <= RS_TEXT_NPC_END) {
        *npcId = RS_TEXT_NPC_GET_ID(textId);
        *index = RS_TEXT_NPC_GET_RULE(textId);
        return RS_SCREEN_RULE;
    }
    if (textId >= RS_TEXT_NODE_BASE && textId <= RS_TEXT_NODE_END) {
        *npcId = RS_TEXT_NODE_GET_ID(textId);
        *index = RS_TEXT_NODE_GET_NODE(textId);
        return RS_SCREEN_NODE;
    }
    return RS_SCREEN_NONE;
}

extern "C" const RsDialogueRule* RsNpc_Screen(int32_t npcId, int32_t kind, int32_t index) {
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    if (def == nullptr || index < 0) {
        return nullptr;
    }
    if (kind == RS_SCREEN_RULE) {
        return index < def->ruleCount ? &def->rules[index] : nullptr;
    }
    if (kind == RS_SCREEN_NODE) {
        return index < def->nodeCount ? &def->nodes[index] : nullptr;
    }
    return nullptr;
}

extern "C" int32_t RsNpc_OptionVisible(const RsDialogueOption* option) {
    if (option == nullptr) {
        return 0;
    }
    for (int32_t i = 0; i < option->whenCount; i++) {
        if (!QuestPredicate_Eval(&option->when[i])) {
            return 0;
        }
    }
    return 1;
}

extern "C" int32_t RsNpc_VisibleOptions(const RsDialogueRule* screen, int32_t* out, int32_t max) {
    if (screen == nullptr || out == nullptr || screen->options == nullptr) {
        return 0;
    }
    int32_t count = 0;
    for (int32_t i = 0; i < screen->optionCount && count < max; i++) {
        if (RsNpc_OptionVisible(&screen->options[i])) {
            out[count++] = i;
        }
    }
    return count;
}

extern "C" int32_t RsNpc_UngatedOptionCount(const RsDialogueRule* screen) {
    if (screen == nullptr || screen->options == nullptr) {
        return 0;
    }
    int32_t count = 0;
    for (int32_t i = 0; i < screen->optionCount; i++) {
        if (screen->options[i].whenCount == 0) {
            count++;
        }
    }
    return count;
}

extern "C" int32_t RsNpc_NodeReachable(int32_t npcId, int32_t nodeIndex) {
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    if (def == nullptr || nodeIndex < 0 || nodeIndex >= def->nodeCount) {
        return 0;
    }
    // THE SAME walk registration runs, not a restatement of it. That matters more than the few lines
    // it saves: `npc tree` prints this so a run can ASSERT the registration gate from a console, and
    // a second copy would be asserting itself instead - and the two could drift apart without either
    // one failing.
    bool reached[RS_DIALOGUE_MAX_NODES] = {};
    MarkReachableNodes(def, reached);
    return reached[nodeIndex] ? 1 : 0;
}

extern "C" int32_t RsNpc_NodeMatches(int32_t npcId, int32_t nodeIndex) {
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    if (def == nullptr || nodeIndex < 0 || nodeIndex >= def->nodeCount) {
        return 0;
    }
    return ScreenGateHolds(def->nodes[nodeIndex]) ? 1 : 0;
}

extern "C" int32_t RsNpc_ResolveNode(int32_t npcId, int32_t head) {
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    if (def == nullptr || head < 0 || head >= def->nodeCount) {
        return -1;
    }
    // First match wins, over the group - bounded by GroupEnd, the same boundary every walk uses, rather
    // than by the accident that an ungated node always matches.
    const int32_t end = GroupEnd(def, head);
    for (int32_t n = head; n <= end && n < def->nodeCount; n++) {
        if (ScreenGateHolds(def->nodes[n])) {
            return n;
        }
    }
    return -1; // a group that runs off the end: refused at registration
}

extern "C" int32_t RsNpc_NodeClauseGated(int32_t npcId, int32_t nodeIndex) {
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    if (def == nullptr || nodeIndex < 0 || nodeIndex >= def->nodeCount) {
        return 0;
    }
    // THE SAME analysis and the same question registration refuses on.
    std::bitset<QUEST_MAX> arrival[RS_DIALOGUE_MAX_NODES];
    ComputeArrivalGates(def, arrival);
    return NodeClauseGated(def, arrival, nodeIndex) ? 1 : 0;
}

extern "C" int32_t RsNpc_ScreenCanEnd(int32_t npcId, int32_t kind, int32_t index) {
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    if (def == nullptr || index < 0) {
        return 0;
    }
    // THE SAME fixpoint registration refuses on, for the reason MarkReachableNodes is shared.
    bool ruleEnds[RS_DIALOGUE_MAX_RULES] = {};
    bool nodeEnds[RS_DIALOGUE_MAX_NODES] = {};
    ComputeCanEnd(def, ruleEnds, nodeEnds);
    if (kind == RS_SCREEN_RULE) {
        return (index < def->ruleCount && ruleEnds[index]) ? 1 : 0;
    }
    if (kind == RS_SCREEN_NODE) {
        return (index < def->nodeCount && nodeEnds[index]) ? 1 : 0;
    }
    return 0;
}

extern "C" int32_t RsNpc_ResolveRule(int32_t npcId) {
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    if (def == nullptr) {
        return -1;
    }
    for (int32_t r = 0; r < def->ruleCount; r++) {
        if (RsNpc_RuleMatches(npcId, r)) {
            return r;
        }
    }
    // Unreachable for a registered definition: the last rule is unconditional or it did not
    // register. Quiet rather than loud, because this is read on the console's behalf.
    return -1;
}

extern "C" const char* RsNpc_ResultName(int32_t result) {
    switch (result) {
        case RS_NPC_OK:
            return "ok";
        case RS_NPC_ERR_INVALID_ID:
            return "invalid_id";
        case RS_NPC_ERR_NOT_REGISTERED:
            return "not_registered";
        case RS_NPC_ERR_BAD_DEF:
            return "bad_def";
        case RS_NPC_ERR_DUPLICATE:
            return "duplicate";
        default:
            return "?";
    }
}

extern "C" const char* RsNpc_ScreenKindName(int32_t kind) {
    switch (kind) {
        case RS_SCREEN_RULE:
            return "rule";
        case RS_SCREEN_NODE:
            return "node";
        default:
            return "?";
    }
}

extern "C" const char* RsNpc_ActionName(int32_t kind) {
    switch (kind) {
        case RS_DLG_ACTION_NONE:
            return "none";
        case RS_DLG_ACTION_START_QUEST:
            return "start_quest";
        case RS_DLG_ACTION_COMPLETE_QUEST:
            return "complete_quest";
        case RS_DLG_ACTION_SET_WORLD_FLAG:
            return "set_world_flag";
        default:
            return "?";
    }
}

extern "C" void RsNpc_Describe(int32_t npcId, char* buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return;
    }
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    if (def == nullptr) {
        std::snprintf(buf, len, "id=%d name=- registered=0 rules=0 rule=-1", npcId);
        return;
    }
    const int32_t rule = RsNpc_ResolveRule(npcId);
    const int32_t options = (rule >= 0) ? def->rules[rule].optionCount : 0;
    // DECLARED, then VISIBLE (#96). `options=` keeps meaning what it has always meant so every
    // earlier acceptance regex holds; `visible=` is the live count after each option's own gate is
    // evaluated, and the two differ exactly when a rule is a multi-quest hub with a topic the
    // player has not earned yet.
    int32_t slots[RS_DIALOGUE_MAX_OPTIONS];
    const int32_t visible = (rule >= 0) ? RsNpc_VisibleOptions(&def->rules[rule], slots, RS_DIALOGUE_MAX_OPTIONS) : 0;
    // `display` is prose, so it is composed like every other prose field a surface prints (#94).
    const std::string display = RsFloorText_Compose(def->displayName != nullptr ? def->displayName : "");
    std::snprintf(buf, len, "id=%d name=%s tier=%s rules=%d rule=%d options=%d display=\"%s\" visible=%d nodes=%d",
                  def->id, def->name, Quest_TierName(def->tier), def->ruleCount, rule, options, display.c_str(),
                  visible, def->nodeCount);
}

// --- the missing-steps clause (D26) -------------------------------------------------------------
//
// Every read here is quiet: Quest_GetDef answers NULL for an invalid or unregistered id without
// asserting, QuestStore_IsStepSet is range-checked by the loop bound, and Quest_StepLabel has its
// own fallback chain. It has to be - this runs while a textbox is opening and from a console
// command, and an assert on either path hangs the agent loop.
std::string RsNpc_MissingList(const RsDialogueRule& rule) {
    if (rule.missingOf == RS_DLG_NO_MISSING) {
        return "";
    }
    const QuestDef* def = Quest_GetDef(rule.missingOf);
    if (def == nullptr) {
        return "";
    }
    std::vector<const char*> missing;
    for (int32_t step = 0; step < def->stepCount; step++) {
        if (!QuestStore_IsStepSet(def->id, step)) {
            missing.push_back(Quest_StepLabel(def->id, step));
        }
    }
    std::string out;
    for (size_t i = 0; i < missing.size(); i++) {
        if (i > 0) {
            // "a, b and c" - the last separator is a word, not a comma. One place, so the giver
            // and the console agree on the wording as well as on the contents.
            out += (i + 1 == missing.size()) ? " and " : ", ";
        }
        // Expanded per label (#94). A step label is prose and may name a storey; doing it here
        // rather than over the joined line is what keeps `npc dump`'s `missing="…"` field showing
        // exactly what the clause appends to the body, with no second expansion anywhere.
        out += RsFloorText_Compose(missing[i]);
    }
    return out;
}

std::string RsNpc_ComposeRuleText(const RsDialogueRule& rule) {
    // The read-time half of the `{floor:N}` token (#94). The console prints this same function's
    // output, so what a run asserts is exactly what the textbox shows. The clause's labels arrive
    // already expanded from RsNpc_MissingList - each string is expanded exactly once, by whichever
    // function owns it.
    std::string text = RsFloorText_Compose(rule.text != nullptr ? rule.text : "");
    const std::string missing = RsNpc_MissingList(rule);
    if (!missing.empty()) {
        // '&' is the author's line break and what AutoFormatString breaks on, so the list starts
        // its own line rather than running on from the lead-in.
        text += "&";
        text += missing;
    }
    return text;
}

std::string RsNpc_ComposeOptionLabel(const RsDialogueOption& option) {
    return RsFloorText_Compose(option.label != nullptr ? option.label : "");
}

std::string RsNpc_ComposeOptionReply(const RsDialogueOption& option) {
    return RsFloorText_Compose(option.reply != nullptr ? option.reply : "");
}

extern "C" int32_t RsNpc_RunAction(const RsDialogueOption* option) {
    if (option == nullptr) {
        return QUEST_ERR_INVALID_ID;
    }
    switch (option->kind) {
        case RS_DLG_ACTION_NONE:
            return QUEST_OK;
        case RS_DLG_ACTION_START_QUEST: {
            // Check-then-write, the P1 rule: an outcome-class refusal (prereqs unmet, already
            // started) must not reach the write path's log-and-assert on a Debug build.
            const int32_t check = Quest_CheckStart(option->a);
            return check == QUEST_OK ? Quest_Start(option->a) : check;
        }
        case RS_DLG_ACTION_COMPLETE_QUEST: {
            // D12: status goes COMPLETE before rewards dispatch, so a second completion - from a
            // second placement of this same character, in this scene or another - is
            // QUEST_ALREADY_COMPLETE and grants nothing. That idempotency lives in Quest.cpp, not
            // here; this must not grow a "have I already done this" flag of its own.
            const int32_t check = Quest_CheckComplete(option->a);
            return check == QUEST_OK ? Quest_Complete(option->a) : check;
        }
        case RS_DLG_ACTION_SET_WORLD_FLAG:
            Flags_SetWorldFlag(option->a);
            return QUEST_OK;
        default:
            return QUEST_ERR_BAD_DEF;
    }
}
