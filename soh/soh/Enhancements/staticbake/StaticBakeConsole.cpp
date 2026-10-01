#include "StaticBakeConsole.h"

#include <cstdio>
#include <cstdlib>

#include <fast/StaticMeshCache.h>

#include "ArchivePropLists.h"
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
    // group=, scenes= and links= (#157) go after the fields older run scripts parse. group=none:
    // nothing held.
    const int group = StaticBake_Group();
    char groupText[16];
    if (group < 0) {
        std::snprintf(groupText, sizeof(groupText), "none");
    } else {
        std::snprintf(groupText, sizeof(groupText), "0x%X", group);
    }
    Addf(lines,
         "op=%s result=ok active=%d setting=%d registered=%u baked=%u rejected=%u supported=%d sort=%d group=%s "
         "scenes=%d links=%d",
         op, StaticBake_IsActive(), StaticBake_Setting(), registered, baked, rejected, StaticBake_BackendSupported(),
         Fast::StaticBakeSortsByMaterial() ? 1 : 0, groupText, StaticBake_HeldScenes(), StaticBake_Links());
}

// A scene id as typed: 0x96 or 150. False for anything else, or past an s16 sceneNum.
bool ParseScene(const std::string& text, int& out) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 0);
    if (text.empty() || end == nullptr || *end != '\0' || value < 0 || value > 0x7FFF) {
        return false;
    }
    out = (int)value;
    return true;
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
    // Both sinks: it changes no setting, only what is held.
    if (sub == "reset") {
        StaticBake_Reset();
        Describe("reset", lines);
        return 0;
    }
    // Session only from both sinks, like sort: a measurement aid, not a setting.
    if (sub == "link") {
        int a = 0;
        int b = 0;
        if (args.size() >= 3 && ParseScene(args[1], a) && ParseScene(args[2], b)) {
            StaticBake_Link(a, b);
            Describe("link", lines);
            return 0;
        }
        lines.push_back("op=link result=error error=bad_argument usage=link(<scene>,<scene>)");
        return 1;
    }
    // Session only from both sinks: an A/B and a way back, not a setting.
    if (sub == "sort") {
        if (args.size() >= 2 && (args[1] == "on" || args[1] == "off")) {
            Fast::StaticBakeSetSortByMaterial(args[1] == "on");
            Describe("sort", lines);
            return 0;
        }
        lines.push_back("op=sort result=error error=bad_argument usage=sort(on|off)");
        return 1;
    }
    // The archive prop lists the registry holds (#171), one line each after the status line. Read-only,
    // so the same from both sinks.
    if (sub == "props") {
        Describe("props", lines);
        ArchiveProps::Describe(lines);
        return 0;
    }
    // The typed word is not echoed: it is free text, and this line is parsed field by field - so no
    // spaces inside the usage value either: `sort on|off` is written sort(on|off).
    lines.push_back("op=unknown result=error error=unknown_subcommand "
                    "usage=status|on|off|rebake|reset|link(<scene>,<scene>)|sort(on|off)|props");
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
                      "rebake | reset | link <scene> <scene> | sort on|off | props. On by default. on/off here "
                      "also save the setting (Settings > Graphics), so the choice survives a restart; `agenttest "
                      "staticbake on|off` does not. Off interprets every room and "
                      "keeps the bakes, so on replays them again without a re-record - flip it to compare baked and "
                      "interpreted pictures at one camera in one session. rebake re-records every baked room on its "
                      "next draw. reset frees every bake the current group holds, other scenes' included, and "
                      "records the current room again (#157). link <scene> <scene> joins two scenes' bake groups for "
                      "this session, to measure a kept return where no step warp runs yet. sort on|off orders each "
                      "recording by material, or keeps list order, and re-records (#158; on by default, this "
                      "session only). props lists the archive prop lists the group holds and what the "
                      "bake made of each (#171).",
                      { { "status|on|off|rebake|reset|link|sort|props", Ship::ArgumentType::TEXT, true },
                        { "on|off|scene", Ship::ArgumentType::TEXT, true },
                        { "scene", Ship::ArgumentType::TEXT, true } });

} // namespace
