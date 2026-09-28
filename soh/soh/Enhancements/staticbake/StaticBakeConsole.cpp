#include "StaticBakeConsole.h"

#include <fast/StaticMeshCache.h>

#include "StaticBakeRegistry.h"
#include "soh/Enhancements/console/ConsoleSink.h"

namespace {

using ConsoleSink::Addf;

// The switch and the registry: the fields every successful line carries.
void Describe(const char* op, std::vector<std::string>& lines) {
    uint32_t registered = 0;
    uint32_t baked = 0;
    uint32_t rejected = 0;
    Fast::StaticBakeGetStats(&registered, &baked, &rejected);
    Addf(lines, "op=%s result=ok active=%d registered=%u baked=%u rejected=%u", op, StaticBake_IsActive(), registered,
         baked, rejected);
}

} // namespace

int32_t StaticBakeConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? "status" : args[0];

    if (sub == "status") {
        Describe("status", lines);
        return 0;
    }
    if (sub == "on" || sub == "off") {
        StaticBake_SetActive(sub == "on" ? 1 : 0);
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

// --- the human sink: the `staticbake` console command -------------------------------------------
//
// `staticbake` collides with nothing in debugger/debugconsole.cpp's CMD_REGISTER list.

namespace {

const ConsoleSink::Command
    staticBakeCommand("staticbake", StaticBakeConsole_Run,
                      "The static geometry bake's runtime switch (sturdy-bassoon#142): status | on | off | rebake. "
                      "Off interprets every room and keeps the bakes, so on replays them again without a re-record - "
                      "flip it to compare baked and interpreted pictures at one camera in one session. rebake "
                      "re-records every baked room on its next draw.",
                      { { "status|on|off|rebake", Ship::ArgumentType::TEXT, true } });

} // namespace
