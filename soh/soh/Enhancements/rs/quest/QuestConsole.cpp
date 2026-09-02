#include "QuestConsole.h"

#include <cstdio>
#include <memory>
#include <ship/Context.h>
#include <ship/debug/Console.h>

#include "Quest.h"
#include "QuestDef.h"
#include "QuestJournal.h"
#include "QuestPredicate.h"
#include "WorldFlagIds.h"
#include "soh/ShipInit.hpp"

// The malformed-definition table, defined in quests/DebugJournalQuest.cpp next to the good
// definition it contrasts with.
int32_t QuestDebug_BadDefCount();
const char* QuestDebug_BadDefLabel(int32_t index);
const QuestDef* QuestDebug_BadDef(int32_t index);

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

// One display string through the parser and back out as a line. Used for the fields `dump` shows;
// the journal proper goes through QuestJournal_Build, which parses the same way.
std::string RenderMarkup(const char* text) {
    std::vector<QuestRun> runs;
    const QuestMarkupResult result = QuestMarkup_Parse(text, &runs);
    if (result.error != QUEST_MARKUP_OK) {
        // Unreachable for a registered quest - Quest_Register refuses a definition whose markup
        // does not parse. Kept because a diagnostic is the only acceptable fallback: rendering the
        // raw prose is the exact silent failure the parser exists to prevent.
        return std::string("<markup ") + QuestMarkup_ErrorName(result.error) + " at " + std::to_string(result.pos) +
               ">";
    }
    return QuestJournal_RenderInline(runs);
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
    lines.push_back("title=\"" + RenderMarkup(def->title) + "\"");
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
    // Hints are markup too (D9/D23), so they render as runs like everything else - which is how
    // a `#hint:...#` SPAN inside a hint STRING shows up as `[hint:...]` rather than as raw prose.
    for (int32_t i = 0; i < def->hintCount; i++) {
        lines.push_back("hint[" + std::to_string(i) + "]=\"" + RenderMarkup(def->hints[i]) + "\"");
    }
}

// The resolved entry, one line per rendered line, plus a header. `~text~` marks a struck-through
// checklist row: visible on a surface with no colour, and greppable.
void RenderEntry(const QuestJournalEntry& entry, bool showRuns, std::vector<std::string>& lines) {
    lines.push_back("journal id=" + std::to_string(entry.questId) + " name=" + entry.name +
                    " status=" + Quest_StatusName(entry.status) + " blocks=" + std::to_string(entry.blockCount) +
                    " visible=" + std::to_string(entry.visibleCount) +
                    " lines=" + std::to_string(entry.lines.size()) + " title=\"" +
                    QuestJournal_RenderInline(entry.title) + "\"");
    for (size_t i = 0; i < entry.lines.size(); i++) {
        const QuestJournalLine& line = entry.lines[i];
        std::string rendered = QuestJournal_RenderInline(line.runs);
        std::string out = "line[" + std::to_string(i) + "]=";
        if (line.kind == QUEST_LINE_CHECK_ITEM) {
            out += "item block=" + std::to_string(line.blockIndex) + " step=" + std::to_string(line.step) +
                   " checked=" + std::to_string(line.checked ? 1 : 0) + " \"" +
                   (line.checked ? "~" + rendered + "~" : rendered) + "\"";
        } else {
            out += "para block=" + std::to_string(line.blockIndex) + " \"" + rendered + "\"";
        }
        lines.push_back(out);
        if (!showRuns) {
            continue;
        }
        // The run list itself - the thing D23 says the console exists to validate. A renderer that
        // printed only the joined line could be hiding a single plain run holding the whole string.
        for (size_t r = 0; r < line.runs.size(); r++) {
            const QuestRun& run = line.runs[r];
            lines.push_back("run[" + std::to_string(i) + "." + std::to_string(r) + "]=" +
                            QuestJournal_StyleName(run.style) + " emphasis=" +
                            QuestJournal_EmphasisName(QuestJournal_StyleEmphasis(run.style)) + " \"" + run.text + "\"");
        }
    }
}

// `quest parse <text...>`: the grammar probe. The console tokenizer is a naive split on " ", so
// the remaining arguments are re-joined with single spaces; runs of spaces survive because the
// split keeps empty tokens. NEVER echoes its input - only runs (which cannot contain a refused
// character, because the parse failed first) or the error kind and offset.
int32_t Parse(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() < 2) {
        lines.push_back("error=parse needs text");
        return 1;
    }
    std::string text;
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) {
            text += " ";
        }
        text += args[i];
    }
    std::vector<QuestRun> runs;
    const QuestMarkupResult result = QuestMarkup_Parse(text.c_str(), &runs);
    if (result.error != QUEST_MARKUP_OK) {
        lines.push_back("op=parse result=error error=" + std::string(QuestMarkup_ErrorName(result.error)) +
                        " pos=" + std::to_string(result.pos) + " runs=" + std::to_string(runs.size()));
        return 1;
    }
    lines.push_back("op=parse result=ok runs=" + std::to_string(runs.size()) + " text=\"" +
                    QuestJournal_RenderInline(runs) + "\"");
    for (size_t r = 0; r < runs.size(); r++) {
        lines.push_back("run[" + std::to_string(r) + "]=" + QuestJournal_StyleName(runs[r].style) + " emphasis=" +
                        QuestJournal_EmphasisName(QuestJournal_StyleEmphasis(runs[r].style)) + " \"" + runs[r].text +
                        "\"");
    }
    return 0;
}

// `quest badcheck`: Quest_DefProblem over the malformed table. Proves the REGISTRATION GATE is
// wired to the parser, which `parse` alone cannot show. rc is 0 when every entry was refused -
// a table entry that validated clean is the failure.
int32_t BadCheck(std::vector<std::string>& lines) {
    const int32_t count = QuestDebug_BadDefCount();
    int32_t clean = 0;
    for (int32_t i = 0; i < count; i++) {
        char problem[192];
        const int32_t rc = Quest_DefProblem(QuestDebug_BadDef(i), problem, sizeof(problem));
        if (rc == 0) {
            clean++;
        }
        lines.push_back("bad[" + std::to_string(i) + "]=" + QuestDebug_BadDefLabel(i) +
                        " refused=" + std::to_string(rc) + " problem=\"" + problem + "\"");
    }
    lines.push_back("op=badcheck defs=" + std::to_string(count) + " refused=" + std::to_string(count - clean) +
                    " accepted=" + std::to_string(clean));
    return clean == 0 ? 0 : 1;
}

const char* kUsage = "usage: quest list | dump <id> | start <id> | setstep <id> <step> | clearstep <id> <step> | "
                     "check <id> <step> | complete <id> | force <id> | reset <id> | debugwipe | "
                     "journal <id|all> [runs] | parse <text...> | badcheck";

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
    if (sub == "parse") {
        return Parse(args, lines);
    }
    if (sub == "badcheck") {
        return BadCheck(lines);
    }
    if (sub == "journal") {
        const bool showRuns = args.size() >= 3 && args[2] == "runs";
        if (args.size() >= 2 && args[1] == "all") {
            // D15's snapshot builder. Unfiltered, in id order - a quest with nothing visible still
            // gets a header, so `entries=` is a count of registered quests, not of interesting ones.
            const std::vector<QuestJournalEntry> entries = QuestJournal_Snapshot();
            lines.push_back("op=journal scope=all entries=" + std::to_string(entries.size()));
            for (const QuestJournalEntry& entry : entries) {
                RenderEntry(entry, showRuns, lines);
            }
            return 0;
        }
        if (!ParseQuestId(args, 1, &questId, lines)) {
            return 1;
        }
        QuestJournalEntry entry;
        if (!QuestJournal_Build(questId, &entry)) {
            lines.push_back("op=journal id=" + std::to_string(questId) + " result=" +
                            Quest_ResultName(QUEST_ERR_NOT_REGISTERED));
            return 1;
        }
        RenderEntry(entry, showRuns, lines);
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
                          "debugwipe | journal <id|all> [runs] | parse <text...> | badcheck. debugwipe clears only "
                          "the debug bands of quests and world flags; journal renders the resolved entry with "
                          "spans as [item:Egg]; parse is the markup probe; badcheck proves registration refuses "
                          "malformed definitions.",
                          { { "list|dump|start|setstep|clearstep|check|complete|force|reset|debugwipe|journal|parse|"
                              "badcheck",
                              Ship::ArgumentType::TEXT },
                            { "quest id / text", Ship::ArgumentType::TEXT, true },
                            { "step / runs", Ship::ArgumentType::TEXT, true } } });
}

RegisterShipInitFunc questConsoleInitFunc(RegisterQuestConsole);

} // namespace
