/*
 * The console surface for the frame's display-list arenas (sturdy-bassoon#247). See GraphConsole.h for
 * the subcommands and GraphBudget.h for the sizes and the fail-safe.
 */

#include "GraphConsole.h"

#include <cstdlib>

#include <ship/debug/Console.h>

extern "C" {
#include <z64.h>
}

#include "GraphBudget.h"
#include "soh/Enhancements/console/ConsoleSink.h"

namespace {

using ConsoleSink::Addf;

const char* kUsage = "op=usage result=error usage=\"graph [status] | reset | cap <opa|xlu|overlay> [<words>|default] | "
                     "failsafe [on|off]\"";

const char* kArenaNames[GRAPH_ARENA_COUNT] = { "opa", "xlu", "overlay" };

bool ParseArena(const std::string& word, GraphArena* arena) {
    for (int32_t i = 0; i < GRAPH_ARENA_COUNT; i++) {
        if (word == kArenaNames[i]) {
            *arena = (GraphArena)i;
            return true;
        }
    }
    return false;
}

int32_t Status(std::vector<std::string>& lines) {
    GraphStats s;
    Graph_GetStats(&s);
    Addf(lines,
         "op=status result=ok frame=%u failsafe=%s drawn=%d skipped=%d skipped_total=%u skipped_frames=%u "
         "repaired_frames=%u last_cut=0x%X overrun_frames=%u reserve=%d",
         s.frame, s.failsafe ? "on" : "off", s.actorsDrawn, s.actorsSkipped, s.skippedTotal, s.skippedFrames,
         s.repairedFrames, s.lastRepairMask, s.overrunFrames, GRAPH_ACTOR_RESERVE_WORDS);
    for (int32_t i = 0; i < GRAPH_ARENA_COUNT; i++) {
        const GraphArenaStats& a = s.arena[i];
        Addf(lines,
             "arena name=%s used=%d words=%d physical=%d peak=%d max_actor=%d max_actor_id=0x%X after_actors=%d "
             "after_peak=%d markers=%d",
             kArenaNames[i], a.used, a.words, a.physical, a.peak, a.maxActor, (uint16_t)a.maxActorId, a.afterActors,
             a.afterPeak, a.markers);
    }
    return 0;
}

int32_t Cap(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    GraphArena arena;
    if (args.size() < 2 || !ParseArena(args[1], &arena)) {
        Addf(lines, "op=cap result=error error=bad_value expects=opa|xlu|overlay");
        return 1;
    }
    GraphStats s;
    Graph_GetStats(&s);
    int32_t words = s.arena[arena].words;
    if (args.size() == 3) {
        int32_t size = arena == GRAPH_ARENA_OPA   ? GFXPOOL_POLY_OPA_WORDS
                       : arena == GRAPH_ARENA_XLU ? GFXPOOL_POLY_XLU_WORDS
                                                  : GFXPOOL_OVERLAY_WORDS;
        if (args[2] == "default") {
            words = Graph_SetArenaCap(arena, 0);
        } else {
            char* end = nullptr;
            const long value = strtol(args[2].c_str(), &end, 0);
            if (end == args[2].c_str() || *end != '\0' || value < 64 || value > size) {
                Addf(lines, "op=cap result=error error=bad_value min=64 max=%d", size);
                return 1;
            }
            words = Graph_SetArenaCap(arena, (int32_t)value);
        }
    }
    Addf(lines, "op=cap result=ok arena=%s words=%d size=%d", kArenaNames[arena], words, s.arena[arena].physical);
    return 0;
}

int32_t Failsafe(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] != "on" && args[1] != "off") {
            Addf(lines, "op=failsafe result=error error=bad_value expects=on|off");
            return 1;
        }
        Graph_SetFailsafe(args[1] == "on");
    }
    GraphStats s;
    Graph_GetStats(&s);
    Addf(lines, "op=failsafe result=ok failsafe=%s", s.failsafe ? "on" : "off");
    return 0;
}

} // namespace

int32_t GraphConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() > 3) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string sub = args.empty() ? std::string("status") : args[0];
    if (sub == "cap") {
        return Cap(args, lines);
    }
    if (sub == "failsafe" && args.size() <= 2) {
        return Failsafe(args, lines);
    }
    if (sub == "reset" && args.size() == 1) {
        Graph_ResetStats();
        Addf(lines, "op=reset result=ok");
        return 0;
    }
    if (sub == "status" && args.size() <= 1) {
        return Status(lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `graph` console command ------------------------------------------------
//
// The mechanical half is ConsoleSink (sturdy-bassoon#112).

namespace {

const ConsoleSink::Command graphCommand(
    "graph", GraphConsole_Run,
    "The frame's display-list arenas (sturdy-bassoon#247): status | reset | cap <opa|xlu|overlay> "
    "[<words>|default] | failsafe [on|off]. status reads each arena's use, peak and size, the largest single "
    "actor draw, what is written after the actor loop, and what the fail-safe did: actors not drawn because "
    "the lists were nearly full, and frames drawn without an arena that overran. cap and failsafe are for "
    "planted controls, this session only: `cap xlu 4096` + `failsafe off` is the pre-#247 layout that hangs "
    "at about 160 fires in view.",
    { { "status|reset|cap|failsafe", Ship::ArgumentType::TEXT },
      { "arena|value", Ship::ArgumentType::TEXT, true },
      { "words", Ship::ArgumentType::TEXT, true } });

} // namespace
