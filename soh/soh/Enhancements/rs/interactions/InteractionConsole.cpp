// The interaction console (sturdy-bassoon#183). See InteractionConsole.h for the lines.
#include "InteractionConsole.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <ship/debug/Console.h>
#include "Interaction.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/rs/actors/RsActorParams.h"
#include "soh/Enhancements/rs/actors/RsInteraction.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

const char* kUsage = "error=usage: interaction status | list | describe <id> | badcheck | "
                     "spawn <id> <n> [dist] [cols] [spacing] | clear";

// A whole decimal number in [lo, hi], or false: never atoi's silent 0 for "abc".
bool ParseInt(const std::string& text, int32_t lo, int32_t hi, int32_t* out) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (text.empty() || end == nullptr || *end != '\0' || value < lo || value > hi) {
        return false;
    }
    *out = static_cast<int32_t>(value);
    return true;
}

bool ParseFloat(const std::string& text, float* out) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end == nullptr || *end != '\0') {
        return false;
    }
    *out = static_cast<float>(value);
    return true;
}

bool ParseId(const std::string& text, int32_t* id) {
    std::string digits = text;
    if (digits.size() > 2 && (digits[0] == 'I' || digits[0] == 'i') && digits[1] == '-') {
        digits = digits.substr(2);
    }
    char* end = nullptr;
    const long value = std::strtol(digits.c_str(), &end, 10);
    if (digits.empty() || end == nullptr || *end != '\0' || !RS_INTERACTION_ID_IS_VALID(value)) {
        return false;
    }
    *id = static_cast<int32_t>(value);
    return true;
}

int32_t Status(std::vector<std::string>& lines) {
    if (gPlayState == nullptr) {
        Addf(lines, "op=status scene=none resident=0 registered=%d", RsInteraction_RegisteredCount());
        return 0;
    }
    std::vector<std::string> rows;
    int32_t found = 0;
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].head; actor != nullptr; actor = actor->next) {
        if (actor->id != ACTOR_RS_INTERACTION) {
            continue;
        }
        const RsInteraction* trigger = reinterpret_cast<const RsInteraction*>(actor);
        const RsInteractionDef* def = RsInteraction_GetDef(trigger->interactionId);
        char row[224];
        std::snprintf(row, sizeof(row),
                      "actor[%d]=rs_interaction id=%d code=%d name=%s pos=%d,%d,%d range=%d height=%d checks=%d "
                      "talking=%d",
                      found, trigger->interactionId, def != nullptr ? 1 : 0, def != nullptr ? def->name : "-",
                      static_cast<int>(actor->world.pos.x), static_cast<int>(actor->world.pos.y),
                      static_cast<int>(actor->world.pos.z), static_cast<int>(trigger->range),
                      static_cast<int>(actor->focus.pos.y - actor->world.pos.y), trigger->checks,
                      Message_GetState(&gPlayState->msgCtx) != TEXT_STATE_NONE &&
                              GET_PLAYER(gPlayState)->talkActor == actor
                          ? 1
                          : 0);
        rows.push_back(row);
        found++;
    }
    Addf(lines, "op=status scene=%d resident=%d registered=%d", gPlayState->sceneNum, found,
         RsInteraction_RegisteredCount());
    for (const std::string& row : rows) {
        lines.push_back(row);
    }
    return 0;
}

int32_t List(std::vector<std::string>& lines) {
    const int32_t count = RsInteraction_RegisteredCount();
    Addf(lines, "op=list registered=%d", count);
    for (int32_t i = 0; i < count; i++) {
        const RsInteractionDef* def = RsInteraction_DefAt(i);
        Addf(lines, "def[%d] id=%d name=%s tier=%s rules=%d nodes=%d", i, def->id, def->name,
             Quest_TierName(def->tier), def->ruleCount, def->nodeCount);
    }
    return 0;
}

int32_t Describe(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t id = 0;
    if (args.size() < 2 || !ParseId(args[1], &id)) {
        lines.push_back("error=describe takes an interaction id, 1-32767 or I-0042");
        return 1;
    }
    const RsInteractionDef* def = RsInteraction_GetDef(id);
    if (def == nullptr) {
        // Not an error: an interaction with no code yet is the normal starting state.
        Addf(lines, "op=describe id=%d code=0 says=placeholder", id);
        return 0;
    }
    const int32_t rule = RsDialogue_ResolveRule(def);
    int32_t slots[RS_DIALOGUE_MAX_OPTIONS];
    const int32_t visible =
        rule >= 0 ? RsNpc_VisibleOptions(&def->rules[rule], slots, RS_DIALOGUE_MAX_OPTIONS) : 0;
    Addf(lines, "op=describe id=%d code=1 name=%s tier=%s rules=%d nodes=%d rule=%d options=%d visible=%d", id,
         def->name, Quest_TierName(def->tier), def->ruleCount, def->nodeCount, rule,
         rule >= 0 ? def->rules[rule].optionCount : 0, visible);
    return 0;
}

// Registration is the only gate between a typo in the code table and a prop that says the wrong
// thing, so it is challenged on planted faults rather than trusted: each must be refused, for the
// reason named.
int32_t BadCheck(std::vector<std::string>& lines) {
    static const RsDialogueRule kRule[] = {
        { nullptr, 0, "A plain line.", nullptr, 0, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    };
    static const RsDialogueRule kTwoPlain[] = {
        { nullptr, 0, "A plain line.", nullptr, 0, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
        { nullptr, 0, "Another.", nullptr, 0, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    };
    static const QuestPredicate kWhen[] = { QP_ALWAYS() };
    static const RsDialogueRule kGated[] = {
        { kWhen, 1, "Gated last.", nullptr, 0, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    };
    struct Case {
        const char* label;
        RsInteractionDef def;
        bool clean;
    };
    const Case cases[] = {
        { "clean", { RS_INTERACTION(32767), QUEST_TIER_DEBUG, "bad_clean", "Bad: clean", kRule, 1 }, true },
        { "id_zero", { 0, QUEST_TIER_PROD, "bad_zero", "Bad: zero", kRule, 1 }, false },
        { "id_past_15_bits", { 0x8000, QUEST_TIER_DEBUG, "bad_wide", "Bad: wide", kRule, 1 }, false },
        { "prod_tier_in_debug_band", { RS_INTERACTION(32767), QUEST_TIER_PROD, "bad_tier", "Bad: tier", kRule, 1 }, false },
        { "debug_tier_in_prod_band", { RS_INTERACTION(1), QUEST_TIER_DEBUG, "bad_tier2", "Bad: tier", kRule, 1 }, false },
        { "last_rule_gated", { RS_INTERACTION(32767), QUEST_TIER_DEBUG, "bad_gate", "Bad: gate", kGated, 1 }, false },
        { "no_rules", { RS_INTERACTION(32767), QUEST_TIER_DEBUG, "bad_none", "Bad: none", kTwoPlain, 0 }, false },
    };
    int32_t wrong = 0;
    for (const Case& c : cases) {
        char problem[192];
        const bool clean = RsInteraction_DefProblem(&c.def, problem, sizeof(problem)) == 0;
        if (clean != c.clean) {
            wrong++;
        }
        // The reason is the validator's own words, which never echo a definition's strings.
        Addf(lines, "case=%s expected=%s got=%s%s%s%s", c.label, c.clean ? "clean" : "refused",
             clean ? "clean" : "refused", clean ? "" : " why=\"", clean ? "" : problem, clean ? "" : "\"");
    }
    Addf(lines, "op=badcheck cases=%d wrong=%d", static_cast<int>(sizeof(cases) / sizeof(cases[0])), wrong);
    return wrong == 0 ? 0 : 1;
}

int32_t Clear(std::vector<std::string>& lines) {
    int32_t killed = 0;
    if (gPlayState != nullptr) {
        for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].head; actor != nullptr;
             actor = actor->next) {
            if (actor->id == ACTOR_RS_INTERACTION) {
                Actor_Kill(actor);
                killed++;
            }
        }
    }
    Addf(lines, "op=clear killed=%d", killed);
    return 0;
}

// #117 stage C's layout, so the two runs measure the same thing: a wall `dist` in front of Link,
// `cols` wide at `spacing`, rising a row at a time. Each trigger gets a 60-unit range and a 40-unit
// focus, which is what a crate's emitted row carries.
int32_t Spawn(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t id = 0;
    if (args.size() < 3 || !ParseId(args[1], &id)) {
        lines.push_back("error=spawn takes <id> <n> [dist] [cols] [spacing]");
        return 1;
    }
    int32_t n = 0;
    int32_t cols = 25;
    float dist = 500.0f;
    float spacing = 34.0f;
    if (!ParseInt(args[2], 0, 1500, &n) || (args.size() >= 4 && !ParseFloat(args[3], &dist)) ||
        (args.size() >= 5 && !ParseInt(args[4], 1, 1500, &cols)) || (args.size() >= 6 && !ParseFloat(args[5], &spacing))) {
        lines.push_back("error=spawn takes <id> <n 0-1500> [dist] [cols 1-1500] [spacing], all numbers");
        return 1;
    }
    if (gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr) {
        lines.push_back("error=spawn needs a scene");
        return 1;
    }
    Player* player = GET_PLAYER(gPlayState);
    const s16 yaw = player->actor.shape.rot.y;
    const float fx = Math_SinS(yaw);
    const float fz = Math_CosS(yaw);
    const Vec3f base = player->actor.world.pos;
    int32_t spawned = 0;
    for (int32_t i = 0; i < n; i++) {
        const int32_t row = i / cols;
        const int32_t col = i % cols;
        const float lateral = (col - (cols - 1) * 0.5f) * spacing;
        const float x = base.x + fx * dist + fz * lateral;
        const float z = base.z + fz * dist - fx * lateral;
        const float y = base.y + row * spacing;
        if (Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_RS_INTERACTION, x, y, z, 40, 0, 60,
                        RS_INTERACTION_PARAMS(id)) != nullptr) {
            spawned++;
        }
    }
    Addf(lines, "op=spawn id=%d n=%d spawned=%d dist=%.0f cols=%d spacing=%.0f yaw=%d", id, n, spawned, dist, cols,
         spacing, static_cast<int>(yaw));
    return 0;
}

} // namespace

int32_t RsInteractionConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string& sub = args[0];
    if (sub == "status") {
        return Status(lines);
    }
    if (sub == "list") {
        return List(lines);
    }
    if (sub == "describe") {
        return Describe(args, lines);
    }
    if (sub == "badcheck") {
        return BadCheck(lines);
    }
    if (sub == "spawn") {
        return Spawn(args, lines);
    }
    if (sub == "clear") {
        return Clear(lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink ------------------------------------------------------------------------------
//
// `interaction` collides with nothing in debugger/debugconsole.cpp's CMD_REGISTER list.

namespace {

const ConsoleSink::Command interactionCommand(
    "interaction", RsInteractionConsole_Run,
    "Interactive props (sturdy-bassoon#183): status | list | describe <id> | badcheck | "
    "spawn <id> <n> [dist] [cols] [spacing] | clear. An interaction is what a prop does when Link "
    "checks it; its id is minted by the grid tool (I-0042) and its dialogue is a row in "
    "rs/interactions/InteractionTable.cpp. An id with no row says the placeholder.",
    { { "status|list|describe|badcheck|spawn|clear", Ship::ArgumentType::TEXT },
      { "argument", Ship::ArgumentType::TEXT, true },
      { "n", Ship::ArgumentType::TEXT, true },
      { "dist", Ship::ArgumentType::TEXT, true },
      { "cols", Ship::ArgumentType::TEXT, true },
      { "spacing", Ship::ArgumentType::TEXT, true } });

} // namespace
