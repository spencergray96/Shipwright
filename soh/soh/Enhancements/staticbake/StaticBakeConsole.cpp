#include "StaticBakeConsole.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <fast/StaticMeshCache.h>

#include "ArchivePropLists.h"
#include "StaticBakeRegistry.h"
#include "soh/Enhancements/console/ConsoleSink.h"

// libultraship's interpreter (fast/interpreter.h): empties the texture cache, as an ocarina textbox does.
extern "C" void gfx_texture_cache_clear();

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
    // scrolls= (#187 A1) after those.
    Addf(lines,
         "op=%s result=ok active=%d setting=%d registered=%u baked=%u rejected=%u supported=%d sort=%d group=%s "
         "scenes=%d links=%d scrolls=%u",
         op, StaticBake_IsActive(), StaticBake_Setting(), registered, baked, rejected, StaticBake_BackendSupported(),
         Fast::StaticBakeSortsByMaterial() ? 1 : 0, groupText, StaticBake_HeldScenes(), StaticBake_Links(),
         (unsigned)Fast::StaticBakeGetTextureScrolls().size());
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

// A number as typed, all of it, finite and within [lo, hi]. False for anything else.
bool ParseNumber(const std::string& text, double lo, double hi, double& out) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end == nullptr || *end != '\0' || !std::isfinite(value) || value < lo || value > hi) {
        return false;
    }
    out = value;
    return true;
}

// An archive path as a list names it: printable, no spaces (the tokenizer splits on them anyway), and
// short enough to be one. Refused rather than passed on, so nothing typed reaches the registry odd.
bool ValidPath(const std::string& text) {
    if (text.empty() || text.size() > 255) {
        return false;
    }
    for (char c : text) {
        if (c <= ' ' || c > '~') {
            return false;
        }
    }
    return true;
}

// `scroll` (#187 A1): list, clear, or set one texture's rate.
int32_t RunScroll(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() <= 1 || (args.size() == 2 && args[1] == "list")) {
        Describe("scroll", lines);
        // The path goes last: a registration from the host (#187 A2) is not checked the way a typed
        // one is, and a field that could hold a space has to be the rest of the line.
        for (const Fast::StaticBakeTextureScroll& s : Fast::StaticBakeGetTextureScrolls()) {
            Addf(lines, "op=scroll du=%g dv=%g bound=%d path=%s", s.du, s.dv, s.bound ? 1 : 0, s.path.c_str());
        }
        return 0;
    }
    if (args.size() == 2 && args[1] == "clear") {
        const size_t n = Fast::StaticBakeGetTextureScrolls().size();
        Fast::StaticBakeClearTextureScrolls();
        Describe("scroll", lines);
        Addf(lines, "op=scroll cleared=%u", (unsigned)n);
        return 0;
    }
    double du = 0.0;
    double dv = 0.0;
    if (args.size() == 4 && ValidPath(args[1]) && ParseNumber(args[2], -1000.0, 1000.0, du) &&
        ParseNumber(args[3], -1000.0, 1000.0, dv)) {
        const bool changed = Fast::StaticBakeSetTextureScroll(args[1].c_str(), (float)du, (float)dv);
        Describe("scroll", lines);
        Addf(lines, "op=scroll du=%g dv=%g changed=%d set=%s", (float)du, (float)dv, changed ? 1 : 0, args[1].c_str());
        return 0;
    }
    lines.push_back("op=scroll result=error error=bad_argument "
                    "usage=scroll(list)|scroll(clear)|scroll(<path>,<du>,<dv>)");
    return 1;
}

// `clock` (#187 A1): report, pin, or let it run.
int32_t RunClock(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    double t = 0.0;
    if (args.size() == 2 && args[1] == "run") {
        Fast::StaticBakePinClock(-1.0);
    } else if (args.size() == 2 && ParseNumber(args[1], 0.0, 1000000.0, t)) {
        Fast::StaticBakePinClock(t);
    } else if (args.size() != 1) {
        lines.push_back("op=clock result=error error=bad_argument usage=clock|clock(<seconds>)|clock(run)");
        return 1;
    }
    Describe("clock", lines);
    Addf(lines, "op=clock pinned=%d t=%.6f", Fast::StaticBakeClockIsPinned() ? 1 : 0, Fast::StaticBakeClockSeconds());
    return 0;
}

// `props`' second half (#187 A1): every baked list with a scrolling draw, keyed as the list lines are.
// A list with no line here has no scrolling draw.
void DescribeScrollingLists(std::vector<std::string>& lines) {
    const std::vector<Fast::StaticBakeScrollingEntry> entries = Fast::StaticBakeGetScrollingEntries();
    Addf(lines, "op=props result=ok scroll_lists=%u", (unsigned)entries.size());
    for (const Fast::StaticBakeScrollingEntry& e : entries) {
        Addf(lines, "op=props scroll_key=%p draws=%u tris=%u scroll_draws=%u scroll_tris=%u", e.key, e.info.draws,
             e.info.tris, e.info.scrollingDraws, e.info.scrollingTris);
    }
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
        DescribeScrollingLists(lines);
        return 0;
    }
    // Texture scroll (#187 A1). Session only from both sinks: the registry is never saved.
    if (sub == "scroll") {
        return RunScroll(args, lines);
    }
    if (sub == "clock") {
        return RunClock(args, lines);
    }
    if (sub == "texclear") {
        gfx_texture_cache_clear();
        Describe("texclear", lines);
        return 0;
    }
    // The typed word is not echoed: it is free text, and this line is parsed field by field - so no
    // spaces inside the usage value either: `sort on|off` is written sort(on|off).
    lines.push_back("op=unknown result=error error=unknown_subcommand "
                    "usage=status|on|off|rebake|reset|link(<scene>,<scene>)|sort(on|off)|props|"
                    "scroll(list|clear|<path>,<du>,<dv>)|clock(<seconds>|run)|texclear");
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

const ConsoleSink::Command staticBakeCommand(
    "staticbake", StaticBakeConsole_Run,
    "The static geometry bake's runtime switch (sturdy-bassoon#142, #153): status | on | off | "
    "rebake | reset | link <scene> <scene> | sort on|off | props | scroll [list|clear|<path> <du> "
    "<dv>] | clock [<seconds>|run] | texclear. On by default. on/off here "
    "also save the setting (Settings > Graphics), so the choice survives a restart; `agenttest "
    "staticbake on|off` does not. Off interprets every room and "
    "keeps the bakes, so on replays them again without a re-record - flip it to compare baked and "
    "interpreted pictures at one camera in one session. rebake re-records every baked room on its "
    "next draw. reset frees every bake the current group holds, other scenes' included, and "
    "records the current room again (#157). link <scene> <scene> joins two scenes' bake groups for "
    "this session, to measure a kept return where no step warp runs yet. sort on|off orders each "
    "recording by material, or keeps list order, and re-records (#158; on by default, this "
    "session only). props lists the archive prop lists the group holds and what the "
    "bake made of each (#171), and every list with scrolling draws (#187). scroll registers a "
    "texture, by the archive path its list names, to scroll at du, dv texture widths a second, "
    "baked or interpreted (0 0 removes it); the rate is read when a list records, so rebake after "
    "changing one. clock pins the clock every scroll reads, for same-picture comparisons, or lets "
    "it run. texclear empties the texture cache, as an ocarina textbox does; bakes and scrolls "
    "keep. scroll, clock and texclear are this session only.",
    { { "status|on|off|rebake|reset|link|sort|props|scroll|clock|texclear", Ship::ArgumentType::TEXT, true },
      { "on|off|scene|list|clear|path|seconds|run", Ship::ArgumentType::TEXT, true },
      { "scene|du", Ship::ArgumentType::TEXT, true },
      { "dv", Ship::ArgumentType::TEXT, true } });

} // namespace
