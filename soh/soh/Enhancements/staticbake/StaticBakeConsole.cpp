#include "StaticBakeConsole.h"

#include <fast/StaticMeshCache.h>

#include "StaticBakeRegistry.h"
#include "soh/Enhancements/console/ConsoleSink.h"

namespace {

using ConsoleSink::Addf;

// The switch, its saved setting and the registry: the fields every successful line carries.
void Describe(const char* op, std::vector<std::string>& lines) {
    uint32_t registered = 0;
    uint32_t baked = 0;
    uint32_t rejected = 0;
    Fast::StaticBakeGetStats(&registered, &baked, &rejected);
    Addf(lines, "op=%s result=ok active=%d setting=%d registered=%u baked=%u rejected=%u", op, StaticBake_IsActive(),
         StaticBake_Setting(), registered, baked, rejected);
}

// Both sinks' renderer. `save` is the one difference between them: the human command saves the
// setting, the agent loop's switch is for its session only.
int32_t Run(const std::vector<std::string>& args, std::vector<std::string>& lines, bool save) {
    const std::string sub = args.empty() ? "status" : args[0];

    if (sub == "status") {
        Describe("status", lines);
        return 0;
    }
    if (sub == "on" || sub == "off") {
        if (save) {
            StaticBake_SetSetting(sub == "on" ? 1 : 0);
        } else {
            StaticBake_SetActive(sub == "on" ? 1 : 0);
        }
        Describe(sub.c_str(), lines);
        return 0;
    }
    if (sub == "rebake") {
        Fast::StaticBakeInvalidateAll();
        Describe("rebake", lines);
        return 0;
    }
    // The typed word is not echoed: it is free text, and this line is parsed field by field.
    lines.push_back("op=unknown result=error error=unknown_subcommand usage=status|on|off|rebake");
    return 1;
}

} // namespace

int32_t StaticBakeConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    return Run(args, lines, true);
}

int32_t StaticBakeConsole_RunSession(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    return Run(args, lines, false);
}

// --- the human sink: the `staticbake` console command -------------------------------------------
//
// `staticbake` collides with nothing in debugger/debugconsole.cpp's CMD_REGISTER list.

namespace {

const ConsoleSink::Command
    staticBakeCommand("staticbake", StaticBakeConsole_Run,
                      "The static geometry bake's runtime switch (sturdy-bassoon#142, #153): status | on | off | "
                      "rebake. On by default. on/off here also save the setting (Settings > Graphics), so the choice "
                      "survives a restart; `agenttest staticbake on|off` does not. Off interprets every room and "
                      "keeps the bakes, so on replays them again without a re-record - flip it to compare baked and "
                      "interpreted pictures at one camera in one session. rebake re-records every baked room on its "
                      "next draw.",
                      { { "status|on|off|rebake", Ship::ArgumentType::TEXT, true } });

} // namespace
