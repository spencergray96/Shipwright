#include "TextureMipsConsole.h"

#include <fast/TextureMips.h>

#include "TextureMips.h"
#include "soh/Enhancements/console/ConsoleSink.h"

namespace {

using ConsoleSink::Addf;

const char* FilterName(int mode) {
    switch (mode) {
        case 0:
            return "three_point";
        case 1:
            return "linear";
        case 2:
            return "none";
        default:
            return "unknown";
    }
}

// The switch, its saved setting and the counters: the fields every successful line carries.
void Describe(const char* op, std::vector<std::string>& lines) {
    uint32_t lists = 0;
    uint32_t addrs = 0;
    uint64_t mipped = 0;
    Fast::TextureMipsGetStats(&lists, &addrs, &mipped);
    Addf(lines, "op=%s result=ok active=%d setting=%d supported=%d lists=%u addrs=%u mipped=%llu filter=%s", op,
         TextureMips_IsActive(), TextureMips_Setting(), TextureMips_BackendSupported(), lists, addrs,
         (unsigned long long)mipped, FilterName(Fast::TextureMipsFilterMode()));
}

// Both sinks' renderer. `save` is the one difference: the human command saves the setting, the agent
// loop's switch is for its session only.
int32_t Run(const std::vector<std::string>& args, std::vector<std::string>& lines, bool save) {
    const std::string sub = args.empty() ? "status" : args[0];

    if (sub == "status") {
        Describe("status", lines);
        return 0;
    }
    if (sub == "on" || sub == "off") {
        if (save) {
            TextureMips_SetSetting(sub == "on" ? 1 : 0);
        } else {
            TextureMips_SetActive(sub == "on" ? 1 : 0);
        }
        Describe(sub.c_str(), lines);
        return 0;
    }
    // The typed word is not echoed: it is free text, and this line is parsed field by field.
    lines.push_back("op=unknown result=error error=unknown_subcommand usage=status|on|off");
    return 1;
}

} // namespace

int32_t TextureMipsConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    return Run(args, lines, true);
}

int32_t TextureMipsConsole_RunSession(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    return Run(args, lines, false);
}

// --- the human sink: the `mipmaps` console command ----------------------------------------------

namespace {

const ConsoleSink::Command
    textureMipsCommand("mipmaps", TextureMipsConsole_Run,
                       "Mipmaps for this mod's own scene textures (sturdy-bassoon#146): status | on | off. On by "
                       "default. Distant walls, barrels and crates stop shimmering as the camera moves; vanilla "
                       "scenes and texture packs are untouched. on/off here also save the setting (Settings > "
                       "Graphics); `agenttest mipmaps on|off` does not. Takes effect on the next frame, in baked "
                       "and interpreted rooms alike, so flipping it compares the two looks at one camera.",
                       { { "status|on|off", Ship::ArgumentType::TEXT, true } });

} // namespace
