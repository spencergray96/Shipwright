#include "NpcDialogue.h"

#include <array>
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
bool CheckPredicate(char* buf, size_t len, int32_t rule, int32_t index, const QuestPredicate& p) {
    switch (p.kind) {
        case QUEST_PRED_ALWAYS:
            break;
        case QUEST_PRED_QUEST_STATUS_IS:
            if (!QUEST_ID_IS_VALID(p.a)) {
                return Problem(buf, len, "rule[%d].when[%d]: QuestStatusIs quest id %d out of range", rule, index, p.a);
            }
            if (p.b < 0 || p.b >= QUEST_STATUS_COUNT) {
                return Problem(buf, len, "rule[%d].when[%d]: QuestStatusIs status %d out of range", rule, index, p.b);
            }
            break;
        case QUEST_PRED_QUEST_STEP_SET:
            if (!QUEST_ID_IS_VALID(p.a)) {
                return Problem(buf, len, "rule[%d].when[%d]: QuestStepSet quest id %d out of range", rule, index, p.a);
            }
            if (p.b < 0 || p.b >= QUEST_STEP_MAX) {
                return Problem(buf, len, "rule[%d].when[%d]: QuestStepSet step %d out of range", rule, index, p.b);
            }
            break;
        case QUEST_PRED_WORLD_FLAG_SET:
            if (p.a < 0 || p.a >= WORLD_FLAG_MAX) {
                return Problem(buf, len, "rule[%d].when[%d]: WorldFlagSet flag %d out of range", rule, index, p.a);
            }
            break;
        case QUEST_PRED_ALL_STEPS_SET:
            if (!QUEST_ID_IS_VALID(p.a)) {
                return Problem(buf, len, "rule[%d].when[%d]: AllStepsSet quest id %d out of range", rule, index, p.a);
            }
            break;
        case QUEST_PRED_QUEST_PREREQS_MET:
            if (!QUEST_ID_IS_VALID(p.a)) {
                return Problem(buf, len, "rule[%d].when[%d]: QuestPrereqsMet quest id %d out of range", rule, index,
                               p.a);
            }
            break;
        default:
            return Problem(buf, len, "rule[%d].when[%d]: unknown predicate kind %d", rule, index,
                           static_cast<int>(p.kind));
    }
    return true;
}

bool CheckOption(char* buf, size_t len, const RsNpcDef* def, int32_t rule, int32_t index) {
    const RsDialogueOption& option = def->rules[rule].options[index];
    if (!ProseIsClean(option.label)) {
        return Problem(buf, len, "rule[%d].opt[%d]: label is NULL, or carries percent, hash, quote, caret or a newline",
                       rule, index);
    }
    if (Length(option.label) == 0) {
        return Problem(buf, len, "rule[%d].opt[%d]: label is empty", rule, index);
    }
    // Before the width measurement below, which EXPANDS this label: a malformed token would be
    // measured as its own diagnostic string, which is a nonsense width to refuse a definition over.
    char where[48];
    std::snprintf(where, sizeof(where), "rule[%d].opt[%d]: label", rule, index);
    if (!CheckTokens(buf, len, where, option.label)) {
        return false;
    }
    if (CountLines(option.label) != 1) {
        // Each label is one line of the choice block; an '&' inside one would shift the cursor rows
        // away from the lines they select, which renders plausibly and picks the wrong option.
        return Problem(buf, len, "rule[%d].opt[%d]: a label must be a single line", rule, index);
    }
    // ...and one line that FITS. Labels were never measured at any option count - the gap surfaced
    // in #59's review. An option row is indented 32px, so its budget is 184 pixels, and a label
    // over it runs off the right edge of the box: still selectable, simply unreadable. That is the
    // same failure P3 shipped vertically, and it is why this asks the renderer's pixel table
    // rather than counting characters.
    if (RsText_LabelWouldOverflow(option.label)) {
        return Problem(buf, len, "rule[%d].opt[%d]: the label is too wide for an option row (184 pixels)", rule, index);
    }
    if (option.reply != nullptr && !ProseIsClean(option.reply)) {
        return Problem(buf, len, "rule[%d].opt[%d]: reply carries percent, hash, quote, caret or a newline", rule,
                       index);
    }
    if (option.reply != nullptr) {
        std::snprintf(where, sizeof(where), "rule[%d].opt[%d]: reply", rule, index);
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
                return Problem(buf, len, "rule[%d].opt[%d]: quest id %d out of range", rule, index, option.a);
            }
            break;
        case RS_DLG_ACTION_SET_WORLD_FLAG:
            if (option.a < 0 || option.a >= WORLD_FLAG_MAX) {
                return Problem(buf, len, "rule[%d].opt[%d]: world flag %d is outside the store", rule, index, option.a);
            }
            // The same rule Quest_Register applies to a world-flag reward: a debug NPC must not set
            // a production flag, because `quest debugwipe` clears only the debug band and could not
            // undo it.
            if ((WORLD_FLAG_IS_DEBUG(option.a) != 0) != (def->tier == QUEST_TIER_DEBUG)) {
                return Problem(buf, len, "rule[%d].opt[%d]: world flag %d is in the other tier's band", rule, index,
                               option.a);
            }
            break;
        default:
            return Problem(buf, len, "rule[%d].opt[%d]: unknown action kind %d", rule, index,
                           static_cast<int>(option.kind));
    }
    return true;
}

bool ValidateRule(char* buf, size_t len, const RsNpcDef* def, int32_t r) {
    const RsDialogueRule& rule = def->rules[r];
    if (rule.whenCount < 0 || (rule.whenCount > 0 && rule.when == nullptr)) {
        return Problem(buf, len, "rule[%d]: `when` is NULL with a nonzero count", r);
    }
    for (int32_t i = 0; i < rule.whenCount; i++) {
        if (!CheckPredicate(buf, len, r, i, rule.when[i])) {
            return false;
        }
    }
    if (!ProseIsClean(rule.text)) {
        return Problem(buf, len, "rule[%d]: text is NULL, or carries percent, hash, quote, caret or a newline", r);
    }
    if (Length(rule.text) == 0) {
        return Problem(buf, len, "rule[%d]: text is empty", r);
    }
    // Before every width measurement below, for the reason CheckOption gives about labels.
    char where[32];
    std::snprintf(where, sizeof(where), "rule[%d]: text", r);
    if (!CheckTokens(buf, len, where, rule.text)) {
        return false;
    }
    if (rule.optionCount < 0 || (rule.optionCount > 0 && rule.options == nullptr)) {
        return Problem(buf, len, "rule[%d]: `options` is NULL with a nonzero count", r);
    }
    if (rule.optionCount == 1) {
        // A one-option "choice" is a statement with a cursor next to it. OoT has no such textbox,
        // and CTRL_TWO_CHOICE with one label puts the cursor on a line that is not there.
        return Problem(buf, len, "rule[%d]: one option is not a choice; use 0 for a statement", r);
    }
    if (rule.optionCount > RS_DIALOGUE_MAX_OPTIONS) {
        // The MODEL is N (NpcDialogueDef.h). This is the RENDERER's limit, and lifting it is a
        // change to one function - see sturdy-bassoon#59.
        return Problem(buf, len, "rule[%d]: %d options; the renderer does at most %d (see sturdy-bassoon#59)", r,
                       rule.optionCount, RS_DIALOGUE_MAX_OPTIONS);
    }
    if (rule.missingOf != RS_DLG_NO_MISSING) {
        if (!QUEST_ID_IS_VALID(rule.missingOf)) {
            return Problem(buf, len, "rule[%d]: missingOf quest id %d out of range (use RS_DLG_NO_MISSING for none)",
                           r, rule.missingOf);
        }
        // The invariant that also catches a forgotten RS_DLG_NO_MISSING, since a value-initialised
        // 0 is a real QuestId (NpcDialogueDef.h). A clause listing what is missing from a quest the
        // rule does not gate on would render "I still need everything" at a player who has never
        // been offered it. Registered-ness is not checked here for the usual reason: nothing
        // defines the order two translation units' ShipInit functions run in.
        bool gated = false;
        for (int32_t i = 0; i < rule.whenCount && !gated; i++) {
            const QuestPredicate& p = rule.when[i];
            switch (p.kind) {
                case QUEST_PRED_QUEST_STATUS_IS:
                case QUEST_PRED_QUEST_STEP_SET:
                case QUEST_PRED_ALL_STEPS_SET:
                case QUEST_PRED_QUEST_PREREQS_MET:
                    gated = (p.a == rule.missingOf);
                    break;
                default:
                    break;
            }
        }
        if (!gated) {
            return Problem(buf, len, "rule[%d]: missingOf names quest %d but no predicate in this rule gates on it",
                           r, rule.missingOf);
        }
        if (rule.optionCount != 0) {
            // A clause is only allowed on a STATEMENT, and the reason is that its length is a
            // RUNTIME fact - it grows with the number of steps still missing - while everything
            // else about a box's layout is checked here, once. A statement that grows too long
            // simply paginates, and pagination costs a reader one more A press. A CHOICE that
            // paginates lands on the second page, where the first A press turns the page instead
            // of picking an option: the box looks right and the conversation does something else.
            // Refusing the combination outright is the only check that holds for every future
            // state of the quest.
            return Problem(buf, len, "rule[%d]: a missing-steps clause needs a statement; this rule has %d options",
                           r, rule.optionCount);
        }
    }
    if (rule.optionCount >= 3) {
        // CustomMessage::AutoFormatString is CTRL_TWO_CHOICE-aware and lays the choice out itself,
        // but it knows neither CTRL_THREE_CHOICE nor CTRL_FOUR_CHOICE - so those rules are
        // hand-laid-out through Format() and the body has to fit one row on its own, because the
        // remaining rows are already spent on options.
        //
        // This USED to be `Length(rule.text) > 24`, whose own comment called itself a stand-in for
        // the real budget of 216 PIXELS in a variable-width font. A character count is wrong in
        // both directions, and the expensive direction bit first: it refused "What can I help you
        // with, Link?" - 30 characters and comfortably inside 216px - which is the sentence #59 was
        // filed to make possible. It now asks the renderer, like the two-option check below.
        if (CountLines(rule.text) != 1) {
            return Problem(buf, len, "rule[%d]: a %d-option body must be a single line", r, rule.optionCount);
        }
        if (RsText_BodyWouldWrap(&rule)) {
            return Problem(buf, len,
                           "rule[%d]: the body is too wide to sit on one row above a %d-option choice - it would "
                           "wrap onto a row an option is already using",
                           r, rule.optionCount);
        }
    }
    for (int32_t i = 0; i < rule.optionCount; i++) {
        if (!CheckOption(buf, len, def, r, i)) {
            return false;
        }
    }
    // LAST, and the ordering is load-bearing: this one FORMATS the rule's real message, so it
    // dereferences every option label. It must run only after CheckOption has proven each label is
    // a non-NULL single line - otherwise the validator crashes on exactly the malformed definitions
    // it exists to refuse, which is how `npc badcheck` first hung.
    //
    // Asked of the RENDERER rather than guessed at (RsActors.cpp). A two-option body that wraps to a
    // second line pushes AutoFormatString past four rows, and its answer is a page break before the
    // choice - so the box renders perfectly and the first A press turns the page instead of
    // choosing. The alternative is a character cap standing in for a 216-pixel budget, which is
    // exactly the guess that let this ship once.
    if (RsText_ChoiceWouldPaginate(&rule)) {
        return Problem(buf, len, "rule[%d]: the body is too long beside a two-option choice - the choice would land "
                                 "on a second page, where the first A press turns the page instead of picking",
                       r);
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
    for (int32_t r = 0; r < def->ruleCount; r++) {
        if (!ValidateRule(buf, len, def, r)) {
            return false;
        }
    }
    // D8, made structural. An NPC whose gate is unmet has to land SOMEWHERE, and "nowhere" is a
    // silent failure: the actor would offer a textId naming a rule that does not match, or none.
    if (def->rules[def->ruleCount - 1].whenCount != 0) {
        return Problem(buf, len, "the last rule must be unconditional - it is the generic fallthrough (D8)");
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
    SPDLOG_INFO("RsNpc: registered {} '{}' tier={} rules={}", def->id, def->name, Quest_TierName(def->tier),
                def->ruleCount);
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
    const RsDialogueRule& rule = def->rules[ruleIndex];
    for (int32_t i = 0; i < rule.whenCount; i++) {
        if (!QuestPredicate_Eval(&rule.when[i])) {
            return 0;
        }
    }
    return 1;
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
    // `display` is prose, so it is composed like every other prose field a surface prints (#94).
    const std::string display = RsFloorText_Compose(def->displayName != nullptr ? def->displayName : "");
    std::snprintf(buf, len, "id=%d name=%s tier=%s rules=%d rule=%d options=%d display=\"%s\"", def->id, def->name,
                  Quest_TierName(def->tier), def->ruleCount, rule, options, display.c_str());
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
