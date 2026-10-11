#include "StaticBakeConsole.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>

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
    // scrolls= (#187 A1) after those, then wind_amp= (#209 W1): the frame's amplitude, 0 when nothing bends;
    // then props_xlu= (#216 T2): whether archive prop lists' translucent halves are submitted.
    Addf(lines,
         "op=%s result=ok active=%d setting=%d registered=%u baked=%u rejected=%u supported=%d sort=%d group=%s "
         "scenes=%d links=%d scrolls=%u wind_amp=%g props_xlu=%d",
         op, StaticBake_IsActive(), StaticBake_Setting(), registered, baked, rejected, StaticBake_BackendSupported(),
         Fast::StaticBakeSortsByMaterial() ? 1 : 0, groupText, StaticBake_HeldScenes(), StaticBake_Links(),
         (unsigned)Fast::StaticBakeGetTextureScrolls().size(), Fast::StaticBakeGetWind().amplitude,
         ArchiveProps::XluSubmitted() ? 1 : 0);
}

// `xlu` (#216 T2): report, or switch the room draw's submission of archive prop lists' translucent halves.
// `save` (the human sink) also saves it, as on/off do; from the agent loop it is this session's only.
int32_t RunXlu(const std::vector<std::string>& args, std::vector<std::string>& lines, bool save) {
    if (args.size() == 2 && (args[1] == "on" || args[1] == "off")) {
        ArchiveProps::SetXluSubmitted(args[1] == "on", save);
    } else if (args.size() != 1) {
        lines.push_back("op=xlu result=error error=bad_argument usage=xlu|xlu(on|off)");
        return 1;
    }
    Describe("xlu", lines);
    Addf(lines, "op=xlu result=ok xlu_lists=%u", (unsigned)ArchiveProps::XluLists());
    return 0;
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
    const std::vector<Fast::StaticBakeKeyedEntry> entries = Fast::StaticBakeGetScrollingEntries();
    Addf(lines, "op=props result=ok scroll_lists=%u", (unsigned)entries.size());
    for (const Fast::StaticBakeKeyedEntry& e : entries) {
        Addf(lines, "op=props scroll_key=%p draws=%u tris=%u scroll_draws=%u scroll_tris=%u", e.key, e.info.draws,
             e.info.tris, e.info.scrollingDraws, e.info.scrollingTris);
    }
}

// And its third (#209 W1): every baked list that recorded a weighted vertex, keyed the same way. A list
// with no line here has nothing that sways.
void DescribeWindLists(std::vector<std::string>& lines) {
    const std::vector<Fast::StaticBakeKeyedEntry> entries = Fast::StaticBakeGetWindEntries();
    Addf(lines, "op=props result=ok wind_lists=%u", (unsigned)entries.size());
    for (const Fast::StaticBakeKeyedEntry& e : entries) {
        Addf(lines, "op=props wind_key=%p draws=%u tris=%u wind_vertices=%u wind_tris=%u", e.key, e.info.draws,
             e.info.tris, e.info.windVertices, e.info.windTris);
    }
}

// `wind` (#209 W1): the frame's wind and what it did in the last frame drawn. saved=1 when what bends
// now is the saved wind (not a session-only or scripted one).
void DescribeWind(std::vector<std::string>& lines) {
    const Fast::StaticBakeWind w = Fast::StaticBakeGetWind();
    const Fast::StaticBakeWindStats st = Fast::StaticBakeGetWindStats();
    Addf(lines,
         "op=wind result=ok amp=%g freq=%g wavelength=%g yaw=%g ripple=%g on=%d saved=%d replay_entries=%u "
         "interp_vertices=%u interp_vectors=%u",
         w.amplitude, w.frequency, w.wavelength, w.yawDeg, w.ripple, w.amplitude != 0.0f ? 1 : 0,
         w == StaticBake_WindSettings() ? 1 : 0, st.replayEntries, st.interpVertices, st.interpVectors);
}

// The keys `wind` sets, the field each writes (a pointer to a member of StaticBakeWind: `w.*field` is
// that member of w) and the range a typed value must fall in. The usage line is built from it too.
struct WindKey {
    const char* name;
    float Fast::StaticBakeWind::* field;
    double lo;
    double hi;
};
constexpr WindKey kWindKeys[] = {
    { "amp", &Fast::StaticBakeWind::amplitude, 0.0, 100.0 },
    { "freq", &Fast::StaticBakeWind::frequency, 0.0, 20.0 },
    { "wavelength", &Fast::StaticBakeWind::wavelength, 0.0, 100000.0 },
    { "yaw", &Fast::StaticBakeWind::yawDeg, -360.0, 360.0 },
    { "ripple", &Fast::StaticBakeWind::ripple, -20.0, 20.0 },
};

// args[1..] as <key> <value> pairs onto w: each key once, every value in its range. False (w half
// written; the caller drops it) for anything else.
bool ParseWindPairs(const std::vector<std::string>& args, Fast::StaticBakeWind& w) {
    if (args.size() < 3 || (args.size() - 1) % 2 != 0) {
        return false;
    }
    bool seen[std::size(kWindKeys)] = {};
    for (size_t i = 1; i + 1 < args.size(); i += 2) {
        size_t k = 0;
        while (k < std::size(kWindKeys) && args[i] != kWindKeys[k].name) {
            k++;
        }
        double v = 0.0;
        if (k == std::size(kWindKeys) || seen[k] || !ParseNumber(args[i + 1], kWindKeys[k].lo, kWindKeys[k].hi, v)) {
            return false;
        }
        seen[k] = true;
        w.*kWindKeys[k].field = (float)v;
    }
    return true;
}

// `wind`: report; set some of the five (all or nothing); reset to the owner's defaults; or go back to the
// saved wind. `save` (the human sink) also saves a set, and clears the saved values on a reset. Every
// form that succeeds prints the same two lines.
int32_t RunWind(const std::vector<std::string>& args, std::vector<std::string>& lines, bool save) {
    const bool report = args.size() <= 1 || (args.size() == 2 && args[1] == "list");
    if (args.size() == 2 && args[1] == "reset") {
        Fast::StaticBakeSetWind(Fast::StaticBakeWind{});
        if (save) {
            StaticBake_ClearWindSettings();
        }
    } else if (args.size() == 2 && args[1] == "saved") {
        StaticBake_ApplyWindSettings();
    } else if (!report) {
        Fast::StaticBakeWind w = Fast::StaticBakeGetWind();
        if (!ParseWindPairs(args, w) || !Fast::StaticBakeSetWind(w)) {
            std::string usage = "op=wind result=error error=bad_argument "
                                "usage=wind(list)|wind(reset)|wind(saved)|wind(<key>,<value>...):";
            for (const WindKey& k : kWindKeys) {
                char range[64];
                std::snprintf(range, sizeof(range), "%s%s[%g,%g]", &k == kWindKeys ? "" : ",", k.name, k.lo, k.hi);
                usage += range;
            }
            lines.push_back(usage);
            return 1;
        }
        if (save) {
            StaticBake_SaveWindSettings(w);
        }
    }
    Describe("wind", lines);
    DescribeWind(lines);
    return 0;
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
        DescribeWindLists(lines);
        return 0;
    }
    // Wind in the replay (#209 W1). From the human command a set or a reset is also saved; from the
    // agent loop it is this session's only, like on/off.
    if (sub == "wind") {
        return RunWind(args, lines, save);
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
    // Translucent prop lists (#216 T2). From the human command a switch is also saved; from the agent loop
    // it is this session's only, like on/off.
    if (sub == "xlu") {
        return RunXlu(args, lines, save);
    }
    // The typed word is not echoed: it is free text, and this line is parsed field by field - so no
    // spaces inside the usage value either: `sort on|off` is written sort(on|off).
    lines.push_back("op=unknown result=error error=unknown_subcommand "
                    "usage=status|on|off|rebake|reset|link(<scene>,<scene>)|sort(on|off)|props|"
                    "scroll(list|clear|<path>,<du>,<dv>)|clock(<seconds>|run)|texclear|"
                    "wind(list|reset|saved|<key>,<value>...)|xlu(on|off)");
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
    "<dv>] | clock [<seconds>|run] | texclear | wind [list|reset|saved|<key> <value>...] | xlu [on|off]. "
    "On by default. on/off here "
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
    "changing one. clock pins the clock every scroll and the wind read, for same-picture comparisons, or lets "
    "it run. texclear empties the texture cache, as an ocarina textbox does; bakes and scrolls "
    "keep. scroll, clock and texclear are this session only. wind (#209) reports or sets the wind "
    "that sways archive props marked for it: amp (units of swing at the hem; 0 stills them), freq "
    "(Hz), wavelength, yaw (where it blows to) and ripple, as <key> <value> pairs; reset puts the "
    "owner's defaults back and saved the saved wind. Here a set or reset is saved; from agenttest it "
    "is not. No rebake needed: the wind is read every frame. xlu on|off (#216) draws archive prop lists' "
    "translucent halves in the room's translucent pass, or not, to compare a map with and without them in one "
    "session; on by default, saved here, this session only from agenttest. No rebake needed.",
    { { "status|on|off|rebake|reset|link|sort|props|scroll|clock|texclear|wind|xlu", Ship::ArgumentType::TEXT, true },
      { "on|off|scene|list|clear|path|seconds|run|reset|saved|key", Ship::ArgumentType::TEXT, true },
      { "scene|du|value", Ship::ArgumentType::TEXT, true },
      { "dv|key", Ship::ArgumentType::TEXT, true },
      { "value", Ship::ArgumentType::TEXT, true },
      { "key", Ship::ArgumentType::TEXT, true },
      { "value", Ship::ArgumentType::TEXT, true },
      { "key", Ship::ArgumentType::TEXT, true },
      { "value", Ship::ArgumentType::TEXT, true },
      { "key", Ship::ArgumentType::TEXT, true },
      { "value", Ship::ArgumentType::TEXT, true } });

} // namespace
