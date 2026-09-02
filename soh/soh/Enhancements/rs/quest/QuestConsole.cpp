#include "QuestConsole.h"

#include <cstdio>
#include <memory>
#include <ship/Context.h>
#include <ship/debug/Console.h>

#include "Quest.h"
#include "QuestDef.h"
#include "QuestPredicate.h"
#include "WorldFlagIds.h"
#include "soh/ShipInit.hpp"

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

std::string Describe(int32_t questId) {
    char buf[256];
    Quest_Describe(questId, buf, sizeof(buf));
    return buf;
}

// Console-layer pre-validation, the P0 rule: a bad id never reaches an accessor that would assert.
bool ParseQuestId(const std::vector<std::string>& args, size_t index, int32_t* questId,
                  std::vector<std::string>& lines) {
    if (index >= args.size() || !ParseInt(args[index], questId) || !QUEST_ID_IS_VALID(*questId)) {
        lines.push_back("error=needs a quest id in 0.." + std::to_string(QUEST_MAX - 1));
        return false;
    }
    if (!Quest_IsRegistered(*questId)) {
        lines.push_back("op=" + args[0] + " id=" + std::to_string(*questId) + " result=" +
                        Quest_ResultName(QUEST_ERR_NOT_REGISTERED));
        lines.push_back(Describe(*questId));
        return false;
    }
    return true;
}

bool ParseStep(const std::vector<std::string>& args, size_t index, int32_t questId, int32_t* step,
               std::vector<std::string>& lines) {
    const QuestDef* def = Quest_GetDef(questId);
    if (index >= args.size() || !ParseInt(args[index], step) || *step < 0 || *step >= def->stepCount) {
        lines.push_back("op=" + args[0] + " id=" + std::to_string(questId) + " result=" +
                        Quest_ResultName(QUEST_ERR_INVALID_STEP) + " stepCount=" + std::to_string(def->stepCount));
        return false;
    }
    return true;
}

int32_t Report(const std::string& op, int32_t questId, int32_t step, int32_t result, std::vector<std::string>& lines) {
    std::string line = "op=" + op + " id=" + std::to_string(questId);
    if (step >= 0) {
        line += " step=" + std::to_string(step);
    }
    line += " result=" + std::string(Quest_ResultName(result));
    lines.push_back(line);
    lines.push_back(Describe(questId));
    return result == QUEST_OK ? 0 : 1;
}

void Dump(int32_t questId, std::vector<std::string>& lines) {
    const QuestDef* def = Quest_GetDef(questId);
    lines.push_back(Describe(questId));
    lines.push_back("title=\"" + std::string(def->title) + "\"");
    for (int32_t i = 0; i < def->stepCount; i++) {
        lines.push_back("step[" + std::to_string(i) + "]=" + (def->stepNames ? def->stepNames[i] : "-") +
                        " set=" + std::to_string(Quest_IsStepSet(questId, i)));
    }
    for (int32_t i = 0; i < def->requirementCount; i++) {
        char desc[96];
        QuestPredicate_Describe(&def->requirements[i], desc, sizeof(desc));
        lines.push_back("req[" + std::to_string(i) + "]=" + desc +
                        " value=" + std::to_string(QuestPredicate_Eval(&def->requirements[i])));
    }
    if (def->prereqFn != nullptr) {
        lines.push_back("prereq_fn=set value=" + std::to_string(def->prereqFn() != 0 ? 1 : 0));
    } else {
        lines.push_back("prereq_fn=none");
    }
    for (int32_t i = 0; i < def->rewardCount; i++) {
        lines.push_back("reward[" + std::to_string(i) + "]=" + Quest_RewardKindName(def->rewards[i].kind) +
                        " a=" + std::to_string(def->rewards[i].a));
    }
    lines.push_back(std::string("on_complete=") + (def->onComplete ? "set" : "none"));
    for (int32_t i = 0; i < def->hintCount; i++) {
        lines.push_back("hint[" + std::to_string(i) + "]=\"" + def->hints[i] + "\"");
    }
}

const char* kUsage = "usage: quest list | dump <id> | start <id> | setstep <id> <step> | clearstep <id> <step> | "
                     "check <id> <step> | complete <id> | force <id> | reset <id> | debugwipe";

} // namespace

int32_t QuestConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string& sub = args[0];
    int32_t questId = 0;
    int32_t step = 0;

    if (sub == "list") {
        lines.push_back("registered=" + std::to_string(Quest_RegisteredCount()) + " max=" + std::to_string(QUEST_MAX) +
                        " debug_first=" + std::to_string(QUEST_ID_DEBUG_FIRST));
        for (int32_t id = 0; id < QUEST_MAX; id++) {
            if (Quest_IsRegistered(id)) {
                lines.push_back(Describe(id));
            }
        }
        return 0;
    }
    if (sub == "debugwipe") {
        int32_t quests = 0;
        int32_t flags = 0;
        Quest_DebugWipe(&quests, &flags);
        lines.push_back("op=debugwipe quests=" + std::to_string(quests) + " flags=" + std::to_string(flags) +
                        " quest_band=" + std::to_string(QUEST_ID_DEBUG_FIRST) + ".." + std::to_string(QUEST_MAX - 1) +
                        " flag_band=" + std::to_string(WORLD_FLAG_DEBUG_FIRST) + ".." + std::to_string(WORLD_FLAG_MAX - 1));
        return 0;
    }
    if (sub == "dump") {
        if (!ParseQuestId(args, 1, &questId, lines)) {
            return 1;
        }
        Dump(questId, lines);
        return 0;
    }
    if (sub == "start" || sub == "complete" || sub == "force" || sub == "reset") {
        if (!ParseQuestId(args, 1, &questId, lines)) {
            return 1;
        }
        int32_t result;
        if (sub == "start") {
            // Pre-checked so an outcome-class refusal never even logs at error level; the write
            // path re-checks and would agree.
            result = Quest_CheckStart(questId);
            if (result == QUEST_OK) {
                result = Quest_Start(questId);
            }
        } else if (sub == "complete") {
            result = Quest_CheckComplete(questId);
            if (result == QUEST_OK) {
                result = Quest_Complete(questId);
            }
        } else if (sub == "force") {
            result = Quest_ForceComplete(questId); // its only refusal is ALREADY_COMPLETE (outcome class)
        } else {
            result = Quest_Reset(questId);
        }
        return Report(sub, questId, -1, result, lines);
    }
    if (sub == "setstep" || sub == "clearstep" || sub == "check") {
        if (!ParseQuestId(args, 1, &questId, lines) || !ParseStep(args, 2, questId, &step, lines)) {
            return 1;
        }
        int32_t result;
        if (sub == "check") {
            result = Quest_CheckSetStep(questId, step);
        } else if (sub == "setstep") {
            // Check first: an ORDER VIOLATION is bug class in Quest_SetStep (log + debug assert),
            // and a tripped assert would hang the agent loop. The console proves the refusal
            // through the same rule without the assert.
            result = Quest_CheckSetStep(questId, step);
            if (result == QUEST_OK) {
                result = Quest_SetStep(questId, step);
            }
        } else {
            result = Quest_CheckClearStep(questId, step);
            if (result == QUEST_OK) {
                result = Quest_ClearStep(questId, step);
            }
        }
        return Report(sub, questId, step, result, lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `quest` console command -----------------------------------------------

namespace {

int32_t QuestCommandHandler(std::shared_ptr<Ship::Console> console, const std::vector<std::string>& args,
                            std::string* output) {
    std::vector<std::string> sub(args.begin() + 1, args.end());
    std::vector<std::string> lines;
    const int32_t rc = QuestConsole_Run(sub, lines);
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

// ShipInit "*" functions re-run on preset apply and config drop; AddCommand only warns on a
// duplicate, but the guard keeps the log clean.
void RegisterQuestConsole() {
    auto console = Ship::Context::GetRawInstance()->GetConsole();
    if (console->HasCommand("quest")) {
        return;
    }
    console->AddCommand("quest",
                        { QuestCommandHandler,
                          "Quest system (sturdy-bassoon#58): list | dump <id> | start <id> | setstep <id> <step> | "
                          "clearstep <id> <step> | check <id> <step> | complete <id> | force <id> | reset <id> | "
                          "debugwipe. debugwipe clears only the debug bands of quests and world flags.",
                          { { "list|dump|start|setstep|clearstep|check|complete|force|reset|debugwipe",
                              Ship::ArgumentType::TEXT },
                            { "quest id", Ship::ArgumentType::NUMBER, true },
                            { "step", Ship::ArgumentType::NUMBER, true } } });
}

RegisterShipInitFunc questConsoleInitFunc(RegisterQuestConsole);

} // namespace
