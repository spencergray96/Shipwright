#include "NpcConsole.h"

#include <cstdio>
#include <memory>
#include <ship/Context.h>
#include <ship/debug/Console.h>

#include "NpcDialogue.h"
#include "NpcDialogueDef.h"
#include "NpcIds.h"
#include "soh/Enhancements/rs/actors/RsActorParams.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

// The malformed-definition table, defined in npcs/DebugNpcs.cpp next to the good definitions it
// contrasts with - the same arrangement QuestConsole.cpp uses for `quest badcheck`.
int32_t RsNpcDebug_BadDefCount();
const char* RsNpcDebug_BadDefLabel(int32_t index);
const RsNpcDef* RsNpcDebug_BadDef(int32_t index);

namespace {

bool ParseInt(const std::string& text, int32_t* value) {
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        *value = parsed;
        return true;
    } catch (...) { return false; }
}

std::string Describe(int32_t npcId) {
    char buf[256];
    RsNpc_Describe(npcId, buf, sizeof(buf));
    return buf;
}

// Console-layer pre-validation, the P0 rule: a bad id never reaches an accessor that would assert.
bool ParseNpcId(const std::vector<std::string>& args, size_t index, int32_t* npcId, std::vector<std::string>& lines) {
    if (index >= args.size() || !ParseInt(args[index], npcId) || !NPC_ID_IS_VALID(*npcId)) {
        lines.push_back("error=needs an npc id in 0.." + std::to_string(NPC_MAX - 1));
        return false;
    }
    if (!RsNpc_IsRegistered(*npcId)) {
        lines.push_back("op=" + args[0] + " id=" + std::to_string(*npcId) +
                        " result=" + RsNpc_ResultName(RS_NPC_ERR_NOT_REGISTERED));
        lines.push_back(Describe(*npcId));
        return false;
    }
    return true;
}

void Dump(int32_t npcId, std::vector<std::string>& lines) {
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    const int32_t first = RsNpc_ResolveRule(npcId);
    lines.push_back(Describe(npcId));
    for (int32_t r = 0; r < def->ruleCount; r++) {
        const RsDialogueRule& rule = def->rules[r];
        // The rule line carries `match=` AND `first=`, because "this rule is true" and "this rule
        // is the one that speaks" are different facts - and the gap between them is exactly what
        // first-match-wins means. A table where rule 2 is true while rule 1 speaks is the proof.
        lines.push_back("rule[" + std::to_string(r) + "]=" + std::to_string(rule.whenCount) + "when options=" +
                        std::to_string(rule.optionCount) + " match=" + std::to_string(RsNpc_RuleMatches(npcId, r)) +
                        " first=" + std::to_string(r == first ? 1 : 0) + " text=\"" + rule.text + "\"");
        for (int32_t i = 0; i < rule.whenCount; i++) {
            char desc[96];
            QuestPredicate_Describe(&rule.when[i], desc, sizeof(desc));
            lines.push_back("when[" + std::to_string(r) + "." + std::to_string(i) + "]=" + desc +
                            " value=" + std::to_string(QuestPredicate_Eval(&rule.when[i])));
        }
        for (int32_t i = 0; i < rule.optionCount; i++) {
            const RsDialogueOption& option = rule.options[i];
            lines.push_back("opt[" + std::to_string(r) + "." + std::to_string(i) +
                            "]=" + RsNpc_ActionName(option.kind) + " a=" + std::to_string(option.a) + " label=\"" +
                            option.label + "\" reply=\"" + (option.reply != nullptr ? option.reply : "-") + "\"");
        }
    }
}

// Every live RS actor instance in the loaded scene. This is what shows that two PLACEMENTS of one
// NpcId are two distinct actor instances resolving to the SAME rule - which a per-NPC dump cannot
// show, because it does not know how many of them are standing there.
int32_t Actors(std::vector<std::string>& lines) {
    if (gPlayState == nullptr) {
        // Every other subcommand works from the title screen (the stores are pure gSaveContext
        // reads), and the console is reachable before any scene loads. Answer, do not crash.
        lines.push_back("op=actors scene=none actors=0");
        return 0;
    }
    char buf[224];
    int32_t found = 0;
    for (int32_t cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* actor = gPlayState->actorCtx.actorLists[cat].head; actor != nullptr; actor = actor->next) {
            if (actor->id == ACTOR_RS_NPC) {
                const int32_t npcId = RS_NPC_PARAMS_GET_ID(actor->params);
                std::snprintf(buf, sizeof(buf),
                              "actor[%d]=rs_npc npc=%d params=0x%04X rsvd=%d registered=%d rule=%d room=%d "
                              "pos=%d,%d,%d",
                              found, npcId, static_cast<unsigned>(actor->params) & 0xFFFF,
                              RS_NPC_PARAMS_GET_RSVD(actor->params), RsNpc_IsRegistered(npcId),
                              RsNpc_ResolveRule(npcId), actor->room, static_cast<int>(actor->world.pos.x),
                              static_cast<int>(actor->world.pos.y), static_cast<int>(actor->world.pos.z));
                lines.push_back(buf);
                found++;
            } else if (actor->id == ACTOR_RS_QUEST_ITEM) {
                const int32_t questId = RS_ITEM_PARAMS_GET_QUEST(actor->params);
                const int32_t step = RS_ITEM_PARAMS_GET_STEP(actor->params);
                std::snprintf(buf, sizeof(buf),
                              "actor[%d]=rs_quest_item quest=%d step=%d params=0x%04X rsvd=%d set=%d room=%d "
                              "pos=%d,%d,%d",
                              found, questId, step, static_cast<unsigned>(actor->params) & 0xFFFF,
                              RS_ITEM_PARAMS_GET_RSVD(actor->params),
                              Quest_IsRegistered(questId) ? Quest_IsStepSet(questId, step) : 0, actor->room,
                              static_cast<int>(actor->world.pos.x), static_cast<int>(actor->world.pos.y),
                              static_cast<int>(actor->world.pos.z));
                lines.push_back(buf);
                found++;
            }
        }
    }
    std::snprintf(buf, sizeof(buf), "op=actors scene=0x%X actors=%d", gPlayState->sceneNum, found);
    lines.push_back(buf);
    return 0;
}

// Proves the REGISTRATION GATE refuses malformed definitions, which a good-definition dump cannot
// show. rc is 0 when every entry was refused - a table entry that validated clean is the failure.
int32_t BadCheck(std::vector<std::string>& lines) {
    const int32_t count = RsNpcDebug_BadDefCount();
    int32_t clean = 0;
    for (int32_t i = 0; i < count; i++) {
        char problem[192];
        const int32_t rc = RsNpc_DefProblem(RsNpcDebug_BadDef(i), problem, sizeof(problem));
        if (rc == 0) {
            clean++;
        }
        lines.push_back("bad[" + std::to_string(i) + "]=" + RsNpcDebug_BadDefLabel(i) +
                        " refused=" + std::to_string(rc) + " problem=\"" + problem + "\"");
    }
    lines.push_back("op=badcheck defs=" + std::to_string(count) + " refused=" + std::to_string(count - clean) +
                    " accepted=" + std::to_string(clean));
    return clean == 0 ? 0 : 1;
}

const char* kUsage = "usage: npc list | dump <id> | resolve <id> | actors | badcheck";

} // namespace

int32_t RsNpcConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string& sub = args[0];
    int32_t npcId = 0;

    if (sub == "list") {
        lines.push_back("registered=" + std::to_string(RsNpc_RegisteredCount()) + " max=" + std::to_string(NPC_MAX) +
                        " debug_first=" + std::to_string(NPC_ID_DEBUG_FIRST));
        for (int32_t id = 0; id < NPC_MAX; id++) {
            if (RsNpc_IsRegistered(id)) {
                lines.push_back(Describe(id));
            }
        }
        return 0;
    }
    if (sub == "actors") {
        return Actors(lines);
    }
    if (sub == "badcheck") {
        return BadCheck(lines);
    }
    if (sub == "dump") {
        if (!ParseNpcId(args, 1, &npcId, lines)) {
            return 1;
        }
        Dump(npcId, lines);
        return 0;
    }
    if (sub == "resolve") {
        if (!ParseNpcId(args, 1, &npcId, lines)) {
            return 1;
        }
        const RsNpcDef* def = RsNpc_GetDef(npcId);
        const int32_t rule = RsNpc_ResolveRule(npcId);
        // The matched rule's text is printed here on purpose: it is what makes "the gate is unmet,
        // so the offer is never made" assertable as an ABSENCE - a run can require the offer's
        // wording not to appear in this output.
        lines.push_back("op=resolve id=" + std::to_string(npcId) + " rule=" + std::to_string(rule) + " options=" +
                        std::to_string(rule >= 0 ? def->rules[rule].optionCount : 0) + " text=\"" +
                        (rule >= 0 ? def->rules[rule].text : "-") + "\"");
        return rule >= 0 ? 0 : 1;
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `npc` console command --------------------------------------------------

namespace {

int32_t NpcCommandHandler(std::shared_ptr<Ship::Console> console, const std::vector<std::string>& args,
                          std::string* output) {
    std::vector<std::string> sub(args.begin() + 1, args.end());
    std::vector<std::string> lines;
    const int32_t rc = RsNpcConsole_Run(sub, lines);
    if (output) {
        for (size_t i = 0; i < lines.size(); i++) {
            if (i > 0) {
                *output += "\n";
            }
            // ConsoleWindow hands the output to vsnprintf as the FORMAT string; definition strings
            // are refused at registration if they carry '%', and this guards the rest.
            for (char c : lines[i]) {
                *output += c;
                if (c == '%') {
                    *output += '%';
                }
            }
        }
    }
    return rc;
}

// ShipInit "*" functions re-run on preset apply and config load; AddCommand only warns on a
// duplicate, but the guard keeps the log clean.
void RegisterNpcConsole() {
    auto console = Ship::Context::GetRawInstance()->GetConsole();
    if (console->HasCommand("npc")) {
        return;
    }
    console->AddCommand("npc", { NpcCommandHandler,
                                 "NPC dialogue (sturdy-bassoon#58 P3): list | dump <id> | resolve <id> | actors | "
                                 "badcheck. dump prints every rule with each predicate's live value, which rule "
                                 "MATCHES and which one SPEAKS (first match wins); resolve prints the speaking rule "
                                 "alone; actors lists the live RS actor instances in the loaded scene; badcheck "
                                 "proves registration refuses malformed definitions.",
                                 { { "list|dump|resolve|actors|badcheck", Ship::ArgumentType::TEXT },
                                   { "npc id", Ship::ArgumentType::TEXT, true } } });
}

RegisterShipInitFunc npcConsoleInitFunc(RegisterNpcConsole);

} // namespace
