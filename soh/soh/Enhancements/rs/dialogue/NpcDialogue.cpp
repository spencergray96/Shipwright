#include "NpcDialogue.h"

#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestIds.h"
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
    if (CountLines(option.label) != 1) {
        // Each label is one line of the choice block; an '&' inside one would shift the cursor rows
        // away from the lines they select, which renders plausibly and picks the wrong option.
        return Problem(buf, len, "rule[%d].opt[%d]: a label must be a single line", rule, index);
    }
    if (option.reply != nullptr && !ProseIsClean(option.reply)) {
        return Problem(buf, len, "rule[%d].opt[%d]: reply carries percent, hash, quote, caret or a newline", rule,
                       index);
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
    if (rule.optionCount == RS_DIALOGUE_MAX_OPTIONS) {
        // CustomMessage::AutoFormatString is CTRL_TWO_CHOICE-aware and lays the choice out itself,
        // but it does NOT know CTRL_THREE_CHOICE - so a three-way rule is hand-laid-out through
        // Format() and its body has to fit one line on its own. The cap is a deliberately
        // conservative stand-in for the real budget, which is 216 PIXELS in a variable-width font
        // (NextLineLength, custom-message/CustomMessageManager.cpp). Too strict can only refuse a
        // definition; too lax would run text off the box, which renders plausibly and silently.
        if (CountLines(rule.text) != 1 || Length(rule.text) > 24) {
            return Problem(buf, len, "rule[%d]: a %d-option body must be one line of at most 24 characters", r,
                           RS_DIALOGUE_MAX_OPTIONS);
        }
    }
    for (int32_t i = 0; i < rule.optionCount; i++) {
        if (!CheckOption(buf, len, def, r, i)) {
            return false;
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
    std::snprintf(buf, len, "id=%d name=%s tier=%s rules=%d rule=%d options=%d display=\"%s\"", def->id, def->name,
                  Quest_TierName(def->tier), def->ruleCount, rule, options, def->displayName);
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
