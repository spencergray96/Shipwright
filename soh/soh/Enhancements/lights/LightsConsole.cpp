/*
 * The console surface for the scene's point lights (sturdy-bassoon#233). See LightsConsole.h for the
 * subcommands and LightsBudget.h for the limits and why they moved.
 */

#include "LightsConsole.h"

#include <cstdio>
#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/debug/Console.h>

#include "LightsBudget.h"
#include "soh/Enhancements/console/ConsoleSink.h"

extern "C" {
#include <z64.h>
extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

const char* kUsage =
    "op=usage result=error usage=\"lights [status] | sunfirst [on|off|default] | pool [<n>|default] | shadow [clear] | "
    "shadowsmooth [on|off|default] | shadowrate [<k>|default]\"";

bool SunFirstOn() {
    return CVarGetInteger(CVAR_LIGHTS_SUN_FIRST, LIGHTS_SUN_FIRST_DEFAULT) != 0;
}

bool ShadowSmoothOn() {
    return CVarGetInteger(CVAR_LIGHTS_SHADOW_SMOOTH, LIGHTS_SHADOW_SMOOTH_DEFAULT) != 0;
}

int32_t Status(std::vector<std::string>& lines) {
    LightsStats s;
    Lights_GetStats(&s);
    Addf(lines, "op=status result=ok occupied=%d cap=%d size=%d peak=%d refused=%d sunfirst=%s gate=%d scene=0x%X",
         s.occupied, s.cap, LIGHTS_BUFFER_SIZE, s.peak, s.refused, SunFirstOn() ? "on" : "off", s.sunFirstGate,
         gPlayState != nullptr ? gPlayState->sceneNum : -1);
    Addf(lines, "tick frame=%u binds=%d walked=%d overflowed=%d max_in_range=%d", s.tick, s.binds, s.walked,
         s.overflowed, s.maxInRange);
    Addf(lines, "link in_range=%d points=%d dirs=%d", s.linkInRange, s.linkPoints, s.linkDirs);
    return 0;
}

int32_t SunFirst(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "default") {
            CVarClear(CVAR_LIGHTS_SUN_FIRST);
        } else if (args[1] == "on" || args[1] == "off") {
            CVarSetInteger(CVAR_LIGHTS_SUN_FIRST, args[1] == "on" ? 1 : 0);
        } else {
            Addf(lines, "op=sunfirst result=error error=bad_value expects=on|off|default");
            return 1;
        }
        CVarSave();
    }
    LightsStats s;
    Lights_GetStats(&s);
    Addf(lines, "op=sunfirst result=ok sunfirst=%s default=%s override=%d gate=%d", SunFirstOn() ? "on" : "off",
         LIGHTS_SUN_FIRST_DEFAULT ? "on" : "off", CVarGetInteger(CVAR_LIGHTS_SUN_FIRST, -1) != -1 ? 1 : 0,
         s.sunFirstGate);
    return 0;
}

int32_t Pool(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "default") {
            Lights_SetPoolCap(LIGHTS_BUFFER_SIZE);
        } else {
            char* end = nullptr;
            const long value = strtol(args[1].c_str(), &end, 10);
            if (end == args[1].c_str() || *end != '\0' || value < 1 || value > LIGHTS_BUFFER_SIZE) {
                Addf(lines, "op=pool result=error error=bad_value min=1 max=%d", LIGHTS_BUFFER_SIZE);
                return 1;
            }
            Lights_SetPoolCap((s32)value);
        }
    }
    LightsStats s;
    Lights_GetStats(&s);
    Addf(lines, "op=pool result=ok cap=%d size=%d occupied=%d", s.cap, LIGHTS_BUFFER_SIZE, s.occupied);
    return 0;
}

// A comma list of n values, `-` for a slot that is not there.
template <typename T> std::string SlotList(const T* values, int32_t n, int32_t bound, bool nodes) {
    std::string out;
    char buf[16];
    for (int32_t i = 0; i < n; i++) {
        if (i > 0) {
            out += ',';
        }
        if (i >= bound) {
            out += '-';
        } else if (nodes && values[i] == LIGHTS_SHADOW_NODE_DIR) {
            out += 's';
        } else {
            snprintf(buf, sizeof(buf), "%d", (int)values[i]);
            out += buf;
        }
    }
    return out;
}

int32_t Shadow(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] != "clear") {
            Addf(lines, "op=shadow result=error error=bad_value expects=clear");
            return 1;
        }
        Lights_ClearShadowRecord();
        Addf(lines, "op=shadow result=ok cleared=1");
        return 0;
    }
    static LightsShadowTick ticks[LIGHTS_SHADOW_RECORD_TICKS];
    const int32_t n = Lights_GetShadowRecord(ticks, LIGHTS_SHADOW_RECORD_TICKS);
    Addf(lines, "op=shadow result=ok ticks=%d scene=0x%X", n, gPlayState != nullptr ? gPlayState->sceneNum : -1);
    const int32_t w = LIGHTS_SHADOW_CASTS_MAX;
    for (int32_t i = 0; i < n; i++) {
        const LightsShadowTick& t = ticks[i];
        Addf(lines, "t=%u sm=%d n=%d ids=%s f0=%s f1=%s num=%s", t.frame, t.smoothed, t.numLights,
             SlotList(t.node, w, t.numLights, true).c_str(), SlotList(t.alpha[0], w, t.numLights, false).c_str(),
             SlotList(t.alpha[1], w, t.numLights, false).c_str(), SlotList(t.lightNum, w, t.numLights, false).c_str());
    }
    return 0;
}

int32_t ShadowSmooth(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "default") {
            CVarClear(CVAR_LIGHTS_SHADOW_SMOOTH);
        } else if (args[1] == "on" || args[1] == "off") {
            CVarSetInteger(CVAR_LIGHTS_SHADOW_SMOOTH, args[1] == "on" ? 1 : 0);
        } else {
            Addf(lines, "op=shadowsmooth result=error error=bad_value expects=on|off|default");
            return 1;
        }
        CVarSave();
    }
    LightsShadowStats s;
    Lights_GetShadowStats(&s);
    Addf(lines,
         "op=shadowsmooth result=ok shadowsmooth=%s default=%s override=%d gate=%d rate=%.3f actors=%d snaps=%d "
         "full=%d nobind=%d cut=%d",
         ShadowSmoothOn() ? "on" : "off", LIGHTS_SHADOW_SMOOTH_DEFAULT ? "on" : "off",
         CVarGetInteger(CVAR_LIGHTS_SHADOW_SMOOTH, -1) != -1 ? 1 : 0, s.gate, s.rate, s.actors, s.snaps, s.full,
         s.nobind, s.cut);
    return 0;
}

int32_t ShadowRate(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "default") {
            Lights_SetShadowRate(LIGHTS_SHADOW_RATE_DEFAULT);
        } else {
            char* end = nullptr;
            const double value = strtod(args[1].c_str(), &end);
            if (end == args[1].c_str() || *end != '\0' || value < 0.01 || value > 1.0) {
                Addf(lines, "op=shadowrate result=error error=bad_value min=0.01 max=1");
                return 1;
            }
            Lights_SetShadowRate((f32)value);
        }
    }
    LightsShadowStats s;
    Lights_GetShadowStats(&s);
    Addf(lines, "op=shadowrate result=ok rate=%.3f default=%.3f", s.rate, LIGHTS_SHADOW_RATE_DEFAULT);
    return 0;
}

} // namespace

int32_t LightsConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() > 2) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string sub = args.empty() ? std::string("status") : args[0];
    if (sub == "sunfirst") {
        return SunFirst(args, lines);
    }
    if (sub == "pool") {
        return Pool(args, lines);
    }
    if (sub == "shadow") {
        return Shadow(args, lines);
    }
    if (sub == "shadowsmooth") {
        return ShadowSmooth(args, lines);
    }
    if (sub == "shadowrate") {
        return ShadowRate(args, lines);
    }
    if (sub == "status" && args.size() <= 1) {
        return Status(lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `lights` console command -----------------------------------------------
//
// The mechanical half is ConsoleSink (sturdy-bassoon#112).

namespace {

const ConsoleSink::Command lightsCommand(
    "lights", LightsConsole_Run,
    "The scene's point lights (sturdy-bassoon#233): status | sunfirst [on|off|default] | pool [<n>|default]. "
    "status counts the light nodes held against the pool's cap, the inserts refused, what the last tick's "
    "binds walked, and how many lights Link bound and of which kind (dirs=2 means he keeps the sun). "
    "sunfirst switches the grid-tool scenes' bind rule: when an actor has more lights in range than its "
    "7 slots, the scene's directional lights keep theirs and the nearest point lights take the rest, "
    "instead of the newest seven crowding the sun out. pool holds new lights to n nodes for this session "
    "only, so `pool 32` plants vanilla's starvation beside the raised pool. shadow lists Link's feet shadows "
    "over the last 120 ticks (#250): each slot's light and the alpha it drew at; `shadow clear` starts afresh. "
    "shadowsmooth switches the grid-tool scenes' smoothed feet shadows: each light's shadow fades toward its "
    "strength instead of jumping with the flame's flicker, and fades in and out as it joins or leaves the bound "
    "lights. shadowrate sets the fraction a tick for this session only (default 0.12; 1 is unsmoothed).",
    { { "status|sunfirst|pool|shadow|shadowsmooth|shadowrate", Ship::ArgumentType::TEXT },
      { "value", Ship::ArgumentType::TEXT, true } });

} // namespace
