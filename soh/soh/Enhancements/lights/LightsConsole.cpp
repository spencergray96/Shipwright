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
#include "soh/frame_interpolation.h"
#include "soh/Enhancements/console/ConsoleSink.h"

extern "C" {
#include <z64.h>
extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

const char* kUsage =
    "op=usage result=error usage=\"lights [status] | sunfirst [on|off|default] | nullbind [on|off|default] | "
    "pool [<n>|default] | shadow [clear] | "
    "shadowsmooth [on|off|default] | shadowrate [<k>|default] | shadowpulse [on|off|default|<amount> <seconds>] | "
    "shadowinterp [clear|on|off|default|pair <on|off|default>|probe <link|id>]\"";

bool SunFirstOn() {
    return CVarGetInteger(CVAR_LIGHTS_SUN_FIRST, LIGHTS_SUN_FIRST_DEFAULT) != 0;
}

bool NullBindFastOn() {
    return CVarGetInteger(CVAR_LIGHTS_NULL_BIND_FAST, LIGHTS_NULL_BIND_FAST_DEFAULT) != 0;
}

bool ShadowSmoothOn() {
    return CVarGetInteger(CVAR_LIGHTS_SHADOW_SMOOTH, LIGHTS_SHADOW_SMOOTH_DEFAULT) != 0;
}

bool ShadowPulseOn() {
    return CVarGetInteger(CVAR_LIGHTS_SHADOW_PULSE, LIGHTS_SHADOW_PULSE_DEFAULT) != 0;
}

int32_t Status(std::vector<std::string>& lines) {
    LightsStats s;
    Lights_GetStats(&s);
    Addf(lines, "op=status result=ok occupied=%d cap=%d size=%d peak=%d refused=%d sunfirst=%s gate=%d scene=0x%X",
         s.occupied, s.cap, LIGHTS_BUFFER_SIZE, s.peak, s.refused, SunFirstOn() ? "on" : "off", s.sunFirstGate,
         gPlayState != nullptr ? gPlayState->sceneNum : -1);
    Addf(lines, "tick frame=%u binds=%d walked=%d overflowed=%d max_in_range=%d null_binds=%d null_fast=%d nullbind=%s",
         s.tick, s.binds, s.walked, s.overflowed, s.maxInRange, s.nullBinds, s.nullFast,
         NullBindFastOn() ? "on" : "off");
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

// nullbind [on|off|default] (#253): a no-position bind visits only the directional lights. Saved, as sunfirst.
int32_t NullBind(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "default") {
            CVarClear(CVAR_LIGHTS_NULL_BIND_FAST);
        } else if (args[1] == "on" || args[1] == "off") {
            CVarSetInteger(CVAR_LIGHTS_NULL_BIND_FAST, args[1] == "on" ? 1 : 0);
        } else {
            Addf(lines, "op=nullbind result=error error=bad_value expects=on|off|default");
            return 1;
        }
        CVarSave();
    }
    Addf(lines, "op=nullbind result=ok nullbind=%s default=%s override=%d", NullBindFastOn() ? "on" : "off",
         LIGHTS_NULL_BIND_FAST_DEFAULT ? "on" : "off", CVarGetInteger(CVAR_LIGHTS_NULL_BIND_FAST, -1) != -1 ? 1 : 0);
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
    const int32_t width = LIGHTS_SHADOW_CASTS_MAX;
    for (int32_t i = 0; i < n; i++) {
        const LightsShadowTick& t = ticks[i];
        Addf(lines, "t=%u sm=%d n=%d ids=%s f0=%s f1=%s num=%s", t.frame, t.smoothed, t.numLights,
             SlotList(t.node, width, t.numLights, true).c_str(),
             SlotList(t.alpha[0], width, t.numLights, false).c_str(),
             SlotList(t.alpha[1], width, t.numLights, false).c_str(),
             SlotList(t.lightNum, width, t.numLights, false).c_str());
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

int32_t ShadowPulse(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 3) {
        char* end1 = nullptr;
        char* end2 = nullptr;
        const double amount = strtod(args[1].c_str(), &end1);
        const double seconds = strtod(args[2].c_str(), &end2);
        if (end1 == args[1].c_str() || *end1 != '\0' || end2 == args[2].c_str() || *end2 != '\0' || amount < 0.0 ||
            amount > 0.9 || seconds < 0.5 || seconds > 30.0) {
            Addf(lines, "op=shadowpulse result=error error=bad_value amount=0..0.9 seconds=0.5..30");
            return 1;
        }
        Lights_SetShadowPulse((f32)amount, (f32)seconds);
    } else if (args.size() == 2) {
        if (args[1] == "default") {
            CVarClear(CVAR_LIGHTS_SHADOW_PULSE);
            Lights_SetShadowPulse(LIGHTS_SHADOW_PULSE_AMOUNT_DEFAULT, LIGHTS_SHADOW_PULSE_SECONDS_DEFAULT);
        } else if (args[1] == "on" || args[1] == "off") {
            CVarSetInteger(CVAR_LIGHTS_SHADOW_PULSE, args[1] == "on" ? 1 : 0);
        } else {
            Addf(lines, "op=shadowpulse result=error error=bad_value expects=on|off|default|<amount> <seconds>");
            return 1;
        }
        CVarSave();
    }
    LightsShadowStats s;
    Lights_GetShadowStats(&s);
    Addf(lines, "op=shadowpulse result=ok shadowpulse=%s default=%s override=%d gate=%d amount=%.3f seconds=%.2f",
         ShadowPulseOn() ? "on" : "off", LIGHTS_SHADOW_PULSE_DEFAULT ? "on" : "off",
         CVarGetInteger(CVAR_LIGHTS_SHADOW_PULSE, -1) != -1 ? 1 : 0, s.pulseGate, s.pulseAmount, s.pulseSeconds);
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

// #region SOH [sturdy-bassoon#254] Shadows interpolated in world space, and the probe on one actor's.
bool ShadowInterpWorldOn() {
    return CVarGetInteger(CVAR_SHADOW_INTERP_WORLD, SHADOW_INTERP_WORLD_DEFAULT) != 0;
}

bool ShadowInterpPairOn() {
    return CVarGetInteger(CVAR_SHADOW_INTERP_PAIR, SHADOW_INTERP_PAIR_DEFAULT) != 0;
}

std::string ProbeName() {
    const s16 id = FrameInterpolation_GetProbeActor();
    char buf[16];
    if (id < 0) {
        return "link";
    }
    snprintf(buf, sizeof(buf), "0x%X", (u16)id);
    return buf;
}

int32_t ShadowInterp(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 3 && args[1] == "pair") {
        if (args[2] == "default") {
            CVarClear(CVAR_SHADOW_INTERP_PAIR);
        } else if (args[2] == "on" || args[2] == "off") {
            CVarSetInteger(CVAR_SHADOW_INTERP_PAIR, args[2] == "on" ? 1 : 0);
        } else {
            Addf(lines, "op=shadowinterp result=error error=bad_value expects=pair_on|pair_off|pair_default");
            return 1;
        }
        CVarSave();
    } else if (args.size() == 3) {
        if (args[1] != "probe") {
            lines.push_back(kUsage);
            return 1;
        }
        if (args[2] == "link") {
            FrameInterpolation_SetProbeActor(-1);
        } else {
            char* end = nullptr;
            const long value = strtol(args[2].c_str(), &end, 0);
            if (end == args[2].c_str() || *end != '\0' || value < 0 || value > 0x7FFF) {
                Addf(lines, "op=shadowinterp result=error error=bad_value expects=probe_link|probe_<actor_id>");
                return 1;
            }
            FrameInterpolation_SetProbeActor((s16)value);
        }
        FrameInterpolation_ClearProbeRecord();
    } else if (args.size() == 2) {
        if (args[1] == "clear") {
            FrameInterpolation_ClearProbeRecord();
            Addf(lines, "op=shadowinterp result=ok cleared=1");
            return 0;
        }
        if (args[1] == "default") {
            CVarClear(CVAR_SHADOW_INTERP_WORLD);
        } else if (args[1] == "on" || args[1] == "off") {
            CVarSetInteger(CVAR_SHADOW_INTERP_WORLD, args[1] == "on" ? 1 : 0);
        } else {
            Addf(lines, "op=shadowinterp result=error error=bad_value expects=clear|on|off|default|probe");
            return 1;
        }
        CVarSave();
    }
    if (args.size() >= 2) {
        Addf(lines,
             "op=shadowinterp result=ok world=%s default=%s override=%d pair=%s pair_default=%s pair_override=%d "
             "probe=%s",
             ShadowInterpWorldOn() ? "on" : "off", SHADOW_INTERP_WORLD_DEFAULT ? "on" : "off",
             CVarGetInteger(CVAR_SHADOW_INTERP_WORLD, -1) != -1 ? 1 : 0, ShadowInterpPairOn() ? "on" : "off",
             SHADOW_INTERP_PAIR_DEFAULT ? "on" : "off", CVarGetInteger(CVAR_SHADOW_INTERP_PAIR, -1) != -1 ? 1 : 0,
             ProbeName().c_str());
        return 0;
    }
    static FrameInterpolationProbeTick ticks[LIGHTS_SHADOW_RECORD_TICKS];
    const int32_t n = FrameInterpolation_GetProbeRecord(ticks, LIGHTS_SHADOW_RECORD_TICKS);
    float minSize = 1.0f;
    float minCos = 1.0f;
    float maxSlide = 0.0f;
    int32_t dips = 0;
    for (int32_t i = 0; i < n; i++) {
        minSize = fminf(minSize, ticks[i].minSize);
        minCos = fminf(minCos, ticks[i].minCos);
        maxSlide = fmaxf(maxSlide, ticks[i].maxSlide);
        dips += ticks[i].minSize < 0.9f ? 1 : 0;
    }
    Addf(lines,
         "op=shadowinterp result=ok ticks=%d min_size=%.3f min_cos=%.3f dips=%d max_slide=%.1f world=%s pair=%s "
         "probe=%s scene=0x%X",
         n, minSize, minCos, dips, maxSlide, ShadowInterpWorldOn() ? "on" : "off", ShadowInterpPairOn() ? "on" : "off",
         ProbeName().c_str(), gPlayState != nullptr ? gPlayState->sceneNum : -1);
    for (int32_t i = 0; i < n; i++) {
        const FrameInterpolationProbeTick& t = ticks[i];
        char dyaw[16] = "-";
        if (i > 0 && ticks[i - 1].frame + 1 == t.frame) {
            snprintf(dyaw, sizeof(dyaw), "%.1f", (s16)(t.yaw - ticks[i - 1].yaw) * (360.0f / 65536.0f));
        }
        Addf(lines, "t=%u id=0x%X yaw=0x%04X dyaw=%s m=%d ws=%d fr=%d cmp=%d size=%.3f cos=%.3f slide=%.1f jump=%.1f",
             t.frame, (u16)t.id, (u16)t.yaw, dyaw, t.matrices, t.worldSpace, t.frames, t.measured, t.minSize, t.minCos,
             t.maxSlide, t.maxJump);
    }
    return 0;
}
// #endregion

} // namespace

int32_t LightsConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    // Only `shadowpulse <amount> <seconds>` and `shadowinterp probe <id>` take two values.
    if (args.size() > 3 || (args.size() == 3 && args[0] != "shadowpulse" && args[0] != "shadowinterp")) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string sub = args.empty() ? std::string("status") : args[0];
    if (sub == "sunfirst") {
        return SunFirst(args, lines);
    }
    if (sub == "nullbind") {
        return NullBind(args, lines);
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
    if (sub == "shadowpulse") {
        return ShadowPulse(args, lines);
    }
    if (sub == "shadowrate") {
        return ShadowRate(args, lines);
    }
    if (sub == "shadowinterp") {
        return ShadowInterp(args, lines);
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
    "lights. shadowrate sets the fraction a tick for this session only (default 0.12; 1 is unsmoothed). "
    "shadowpulse switches a slow breathing of each smoothed shadow, each fire out of step (off is the plain "
    "fade); `shadowpulse <amount> <seconds>` tunes its depth and period for this session. "
    "shadowinterp (#254) lists, over the last 120 ticks, how far frame interpolation shrank, turned or slid one "
    "actor's shadow matrices in the frames between ticks (size 1, cos 1 and slide 0 is untouched): Link's, or "
    "`shadowinterp probe <id>`'s for this session; `shadowinterp clear` starts afresh. `shadowinterp on|off` "
    "switches the feet and circle shadows to world-space interpolation, which stops them shrinking and reversing "
    "when their actor turns sharply; `shadowinterp pair on|off` pairs each foot shadow with its own light's from "
    "tick to tick, so one joining or leaving does not swing the others. nullbind (#253) switches the "
    "no-position bind (IGNORE_POINTLIGHTS actors, the room, soft sprites) to visit only the directional lights "
    "instead of walking every node; it binds the same lights either way.",
    { { "status|sunfirst|nullbind|pool|shadow|shadowsmooth|shadowrate|shadowpulse|shadowinterp",
        Ship::ArgumentType::TEXT },
      { "value", Ship::ArgumentType::TEXT, true } });

} // namespace
