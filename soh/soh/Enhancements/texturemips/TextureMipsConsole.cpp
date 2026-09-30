#include "TextureMipsConsole.h"

#include <cstdlib>

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

const char* const kLodNames[] = { "max", "mean", "aniso" };

// The switch, its saved setting, the level choice and the counters: the fields every successful line
// carries.
void Describe(const char* op, std::vector<std::string>& lines) {
    uint32_t lists = 0;
    uint32_t addrs = 0;
    uint64_t mipped = 0;
    Fast::TextureMipsGetStats(&lists, &addrs, &mipped);
    int mode = 0;
    float bias = 0.0f;
    Fast::TextureMipsGetLod(&mode, &bias);
    Addf(lines,
         "op=%s result=ok active=%d setting=%d supported=%d lod=%s bias=%.2f lists=%u addrs=%u mipped=%llu filter=%s",
         op, TextureMips_IsActive(), TextureMips_Setting(), TextureMips_BackendSupported(), kLodNames[mode], bias,
         lists, addrs, (unsigned long long)mipped, FilterName(Fast::TextureMipsFilterMode()));
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
    if (sub == "lod" || sub == "bias") {
        int mode = 0;
        float bias = 0.0f;
        Fast::TextureMipsGetLod(&mode, &bias);
        bool ok = args.size() >= 2;
        if (ok && sub == "lod") {
            ok = false;
            for (int i = 0; i < 3; i++) {
                if (args[1] == kLodNames[i]) {
                    mode = i;
                    ok = true;
                }
            }
        } else if (ok) {
            char* end = nullptr;
            const float value = std::strtof(args[1].c_str(), &end);
            ok = end != args[1].c_str() && *end == '\0' && value >= -4.0f && value <= 4.0f;
            bias = value;
        }
        if (!ok) {
            lines.push_back(sub == "lod" ? "op=lod result=error error=bad_argument usage=lod(max|mean|aniso)"
                                         : "op=bias result=error error=bad_argument usage=bias(-4..4)");
            return 1;
        }
        TextureMips_SetLod(mode, bias, save ? 1 : 0);
        Describe(sub.c_str(), lines);
        return 0;
    }
    // The typed word is not echoed: it is free text, and this line is parsed field by field.
    lines.push_back(
        "op=unknown result=error error=unknown_subcommand usage=status|on|off|lod(max|mean|aniso)|bias(-4..4)");
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

const ConsoleSink::Command textureMipsCommand(
    "mipmaps", TextureMipsConsole_Run,
    "Mipmaps for this mod's own scene textures (sturdy-bassoon#146): status | on | off | "
    "lod max|mean|aniso | bias <-4..4>. On by default. Distant walls, barrels and crates stop "
    "shimmering as the camera moves; vanilla scenes and texture packs are untouched. lod picks "
    "how sharp a wall seen at a grazing angle stays: max is the softest, aniso the sharpest that "
    "still does not crawl; bias nudges it (negative = sharper). Everything here is saved; "
    "`agenttest mipmaps` is session only. Takes effect on the next frame, so flipping compares "
    "looks at one camera.",
    { { "status|on|off|lod|bias", Ship::ArgumentType::TEXT, true }, { "value", Ship::ArgumentType::TEXT, true } });

} // namespace
