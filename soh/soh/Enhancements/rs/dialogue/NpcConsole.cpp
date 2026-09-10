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
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/Enhancements/rs/quest/QuestStore.h"
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

// One option line. `prefix` is "opt" for a rule's options and "node_opt" for a node's, so the two
// arrays never collide in a grep and every pre-#96 assertion on `opt[R.I]=` still matches only
// rule options.
//
// The fields after `reply=` are APPENDED for the same reason every other phase appended: earlier
// acceptance regexes keep holding verbatim. `next=` is where the option navigates
// (RS_DLG_NO_NEXT prints as -1, which is "close"); `when=` is how many predicates gate it and
// `visible=` is what those predicates say RIGHT NOW - which is what makes the dynamic reveal
// assertable from a console instead of from six screenshots.
std::string OptionLine(const char* prefix, int32_t screenIndex, int32_t optionIndex, const RsDialogueOption& option) {
    return std::string(prefix) + "[" + std::to_string(screenIndex) + "." + std::to_string(optionIndex) +
           "]=" + RsNpc_ActionName(option.kind) + " a=" + std::to_string(option.a) + " label=\"" +
           RsNpc_ComposeOptionLabel(option) + "\" reply=\"" +
           (option.reply != nullptr ? RsNpc_ComposeOptionReply(option) : std::string("-")) +
           "\" next=" + std::to_string(option.next) + " when=" + std::to_string(option.whenCount) +
           " visible=" + std::to_string(RsNpc_OptionVisible(&option));
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
        // `text=` is the COMPOSED body - what the player would actually read if this rule spoke
        // right now - so the console cannot validate a string the textbox never shows (D18). The
        // fields after it are appended rather than inserted, so every earlier phase's assertions
        // on this line keep matching.
        std::string line = "rule[" + std::to_string(r) + "]=" + std::to_string(rule.whenCount) + "when options=" +
                           std::to_string(rule.optionCount) + " match=" + std::to_string(RsNpc_RuleMatches(npcId, r)) +
                           " first=" + std::to_string(r == first ? 1 : 0) + " text=\"" +
                           RsNpc_ComposeRuleText(rule) + "\" missing_of=" + std::to_string(rule.missingOf);
        if (rule.missingOf != RS_DLG_NO_MISSING) {
            line += " missing=\"" + RsNpc_MissingList(rule) + "\"";
        }
        lines.push_back(line);
        for (int32_t i = 0; i < rule.whenCount; i++) {
            char desc[96];
            QuestPredicate_Describe(&rule.when[i], desc, sizeof(desc));
            lines.push_back("when[" + std::to_string(r) + "." + std::to_string(i) + "]=" + desc +
                            " value=" + std::to_string(QuestPredicate_Eval(&rule.when[i])));
        }
        for (int32_t i = 0; i < rule.optionCount; i++) {
            // COMPOSED, like `text=` above and for the same reason (D18): a label or a reply
            // carrying `{floor:N}` (#94) prints the words the player would read under the live
            // convention, so `region set us` then `npc dump` asserts the substitution directly.
            lines.push_back(OptionLine("opt", r, i, rule.options[i]));
        }
    }
    // The NODE array (#96), after every rule, so a dump reads in the order a conversation happens.
    // A node has no `match=`/`first=` - it is never matched - and no `when`, which registration
    // refuses on one; what it has instead is `reachable=`, the graph walk's verdict.
    for (int32_t n = 0; n < def->nodeCount; n++) {
        const RsDialogueNode& node = def->nodes[n];
        int32_t slots[RS_DIALOGUE_MAX_OPTIONS];
        const int32_t visible = RsNpc_VisibleOptions(&node, slots, RS_DIALOGUE_MAX_OPTIONS);
        lines.push_back("node[" + std::to_string(n) + "]=" + std::to_string(node.optionCount) + "options ungated=" +
                        std::to_string(RsNpc_UngatedOptionCount(&node)) + " visible=" + std::to_string(visible) +
                        " reachable=" + std::to_string(RsNpc_NodeReachable(npcId, n)) + " text=\"" +
                        RsNpc_ComposeRuleText(node) + "\"");
        for (int32_t i = 0; i < node.optionCount; i++) {
            lines.push_back(OptionLine("node_opt", n, i, node.options[i]));
        }
    }
}

// --- `npc tree` (#96 P3) ------------------------------------------------------------------------
//
// The GRAPH, printed. `npc dump` shows every screen's contents; this shows how they connect, which
// is the thing loop-back and the dynamic reveal actually consist of - and printing it is what makes
// them assertable from a console rather than from six screenshots. Same bargain `npc dump` struck
// for first-match-wins.
//
// One `edge` line per option, whether or not it navigates: an option that closes is an edge to
// `close`, and saying so beats leaving the reader to infer it from an absence. `visible=` is live,
// so `npc tree` before and after the flag that reveals an option differ in exactly one field.
int32_t Tree(int32_t npcId, std::vector<std::string>& lines) {
    const RsNpcDef* def = RsNpc_GetDef(npcId);
    int32_t reachable = 0;
    int32_t edges = 0;
    for (int32_t n = 0; n < def->nodeCount; n++) {
        reachable += RsNpc_NodeReachable(npcId, n);
    }
    lines.push_back("op=tree id=" + std::to_string(npcId) + " name=" + def->name +
                    " rules=" + std::to_string(def->ruleCount) + " nodes=" + std::to_string(def->nodeCount) +
                    " reachable=" + std::to_string(reachable));

    const int32_t first = RsNpc_ResolveRule(npcId);
    for (int32_t r = 0; r < def->ruleCount; r++) {
        const RsDialogueRule& rule = def->rules[r];
        int32_t slots[RS_DIALOGUE_MAX_OPTIONS];
        const int32_t visible = RsNpc_VisibleOptions(&rule, slots, RS_DIALOGUE_MAX_OPTIONS);
        lines.push_back("screen[rule." + std::to_string(r) + "]=" + std::to_string(rule.optionCount) +
                        "options ungated=" + std::to_string(RsNpc_UngatedOptionCount(&rule)) +
                        " visible=" + std::to_string(visible) +
                        " match=" + std::to_string(RsNpc_RuleMatches(npcId, r)) +
                        " first=" + std::to_string(r == first ? 1 : 0));
        for (int32_t i = 0; i < rule.optionCount; i++) {
            const RsDialogueOption& option = rule.options[i];
            lines.push_back("edge[rule." + std::to_string(r) + "." + std::to_string(i) + "]=" +
                            (option.next == RS_DLG_NO_NEXT ? std::string("close")
                                                           : "node." + std::to_string(option.next)) +
                            " visible=" + std::to_string(RsNpc_OptionVisible(&option)) + " label=\"" +
                            RsNpc_ComposeOptionLabel(option) + "\"");
            edges++;
        }
    }
    for (int32_t n = 0; n < def->nodeCount; n++) {
        const RsDialogueNode& node = def->nodes[n];
        int32_t slots[RS_DIALOGUE_MAX_OPTIONS];
        const int32_t visible = RsNpc_VisibleOptions(&node, slots, RS_DIALOGUE_MAX_OPTIONS);
        lines.push_back("screen[node." + std::to_string(n) + "]=" + std::to_string(node.optionCount) +
                        "options ungated=" + std::to_string(RsNpc_UngatedOptionCount(&node)) +
                        " visible=" + std::to_string(visible) +
                        " reachable=" + std::to_string(RsNpc_NodeReachable(npcId, n)));
        for (int32_t i = 0; i < node.optionCount; i++) {
            const RsDialogueOption& option = node.options[i];
            lines.push_back("edge[node." + std::to_string(n) + "." + std::to_string(i) + "]=" +
                            (option.next == RS_DLG_NO_NEXT ? std::string("close")
                                                           : "node." + std::to_string(option.next)) +
                            " visible=" + std::to_string(RsNpc_OptionVisible(&option)) + " label=\"" +
                            RsNpc_ComposeOptionLabel(option) + "\"");
            edges++;
        }
    }
    lines.push_back("op=tree id=" + std::to_string(npcId) + " edges=" + std::to_string(edges) +
                    " nodes=" + std::to_string(def->nodeCount) + " reachable=" + std::to_string(reachable) +
                    " unreachable=" + std::to_string(def->nodeCount - reachable));
    // Nonzero only if a stranded node somehow registered, which the registration walk makes
    // impossible - so a nonzero rc here is a claim that the gate is broken, not that the NPC is.
    return reachable == def->nodeCount ? 0 : 1;
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
                // Quest_IsStepSet asserts on a step past the definition's stepCount, and `step`
                // here comes from a hand-authored params word - so it is read through the
                // definition and the raw store instead. A placement naming a step its quest does
                // not have is a mistake worth SEEING in this dump (set=0, and the actor's own Init
                // shouts), not one worth hanging the agent loop over.
                const QuestDef* itemDef = Quest_GetDef(questId); // NULL for invalid or unregistered
                const int32_t stepSet =
                    (itemDef != nullptr && step >= 0 && step < itemDef->stepCount) ? QuestStore_IsStepSet(questId, step)
                                                                                  : 0;
                std::snprintf(buf, sizeof(buf),
                              "actor[%d]=rs_quest_item quest=%d step=%d params=0x%04X rsvd=%d set=%d room=%d "
                              "pos=%d,%d,%d",
                              found, questId, step, static_cast<unsigned>(actor->params) & 0xFFFF,
                              RS_ITEM_PARAMS_GET_RSVD(actor->params), stepSet, actor->room,
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

const char* kUsage = "usage: npc list | dump <id> | tree <id> | resolve <id> | actors | badcheck";

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
    if (sub == "tree") {
        if (!ParseNpcId(args, 1, &npcId, lines)) {
            return 1;
        }
        return Tree(npcId, lines);
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
                        (rule >= 0 ? RsNpc_ComposeRuleText(def->rules[rule]) : std::string("-")) + "\"");
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
                                 "NPC dialogue (sturdy-bassoon#58 P3, #96): list | dump <id> | tree <id> | "
                                 "resolve <id> | actors | badcheck. dump prints every rule with each predicate's "
                                 "live value, which rule MATCHES and which one SPEAKS (first match wins), every "
                                 "option's `next` and whether its own gate makes it visible right now, and the "
                                 "NODE array a dialogue tree navigates through; tree prints the graph - every "
                                 "screen, every edge and each node's reachability; resolve prints the speaking "
                                 "rule's composed body alone; actors lists the live RS actor instances in the "
                                 "loaded scene; badcheck proves registration refuses malformed definitions.",
                                 { { "list|dump|tree|resolve|actors|badcheck", Ship::ArgumentType::TEXT },
                                   { "npc id", Ship::ArgumentType::TEXT, true } } });
}

RegisterShipInitFunc npcConsoleInitFunc(RegisterNpcConsole);

} // namespace
