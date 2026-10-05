/*
 * The console surface for the indoor camera pull-in (sturdy-bassoon#108). See CameraIndoorConsole.h
 * for the subcommand list and why the command exists at all, and CameraIndoorTuning.h for the knobs.
 */

#include "CameraIndoorConsole.h"
#include "CameraIndoorTuning.h"

#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/debug/Console.h>

#include "soh/Enhancements/console/ConsoleSink.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

// Unqualified because this file has ~20 call sites. A using-declaration in the unnamed
// namespace reaches the whole translation unit, including the renderer at global scope below.
using ConsoleSink::Addf;

// Every numeric argument goes through one of these two, so a typo is refused by the console rather
// than written into a CVar the camera then reads every frame. A rejected value leaves the old one
// in place: there is no path here that half-applies a change.
bool ParseFloat(const std::string& s, float min, float max, float* out) {
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !(v >= min) || !(v <= max)) {
        return false;
    }
    *out = v;
    return true;
}

bool ParseInt(const std::string& s, int32_t min, int32_t max, int32_t* out) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || v < min || v > max) {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

int32_t SetFloat(const std::vector<std::string>& args, std::vector<std::string>& lines, const char* cvar,
                 const char* label, float min, float max) {
    if (args.size() < 2) {
        Addf(lines, "op=%s result=error error=missing_value (expects %.2f..%.2f)", label, min, max);
        return 1;
    }
    float value = 0.0f;
    if (!ParseFloat(args[1], min, max, &value)) {
        Addf(lines, "op=%s result=error error=bad_value (expects %.2f..%.2f)", label, min, max);
        return 1;
    }
    CVarSetFloat(cvar, value);
    CVarSave();
    Addf(lines, "op=%s value=%.3f result=ok", label, value);
    return 0;
}

int32_t SetInt(const std::vector<std::string>& args, std::vector<std::string>& lines, const char* cvar,
               const char* label, int32_t min, int32_t max) {
    if (args.size() < 2) {
        Addf(lines, "op=%s result=error error=missing_value (expects %d..%d)", label, min, max);
        return 1;
    }
    int32_t value = 0;
    if (!ParseInt(args[1], min, max, &value)) {
        Addf(lines, "op=%s result=error error=bad_value (expects %d..%d)", label, min, max);
        return 1;
    }
    CVarSetInteger(cvar, value);
    CVarSave();
    Addf(lines, "op=%s value=%d result=ok", label, value);
    return 0;
}

// `<name> on|off` for a 0/1 CVar. Anything else is refused, so `ceilclamp of` cannot read as off.
int32_t SetSwitch(const std::vector<std::string>& args, std::vector<std::string>& lines, const char* cvar,
                  const char* label) {
    if (args.size() < 2 || (args[1] != "on" && args[1] != "off")) {
        Addf(lines, "op=%s result=error error=bad_value (expects on|off)", label);
        return 1;
    }
    CVarSetInteger(cvar, args[1] == "on" ? 1 : 0);
    CVarSave();
    Addf(lines, "op=%s value=%s result=ok", label, args[1].c_str());
    return 0;
}

// `heightt <f>` sets t; a leading sign makes it a step from the current value (`heightt +0.1`),
// clamped into range, so a stepped walk never has to know where it is.
int32_t SetHeightT(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2 && !args[1].empty() && (args[1][0] == '+' || args[1][0] == '-')) {
        float step = 0.0f;
        if (!ParseFloat(args[1], -1.0f, 1.0f, &step)) {
            Addf(lines, "op=heightt result=error error=bad_step (expects -1..+1)");
            return 1;
        }
        float t = CVarGetFloat(CVAR_CAM_ADULT_HEIGHT_T, CAM_ADULT_HEIGHT_T_DEFAULT) + step;
        if (t < CAM_ADULT_HEIGHT_T_MIN) {
            t = CAM_ADULT_HEIGHT_T_MIN;
        }
        if (t > CAM_ADULT_HEIGHT_T_MAX) {
            t = CAM_ADULT_HEIGHT_T_MAX;
        }
        CVarSetFloat(CVAR_CAM_ADULT_HEIGHT_T, t);
        CVarSave();
        Addf(lines, "op=heightt value=%.3f result=ok", t);
        return 0;
    }
    return SetFloat(args, lines, CVAR_CAM_ADULT_HEIGHT_T, "heightt", CAM_ADULT_HEIGHT_T_MIN,
                    CAM_ADULT_HEIGHT_T_MAX);
}

// The knobs' live values, with no camera needed. `status` prints this even on the title screen,
// where there is nothing to probe - that is when a run script checks its own setup.
void AddTunables(std::vector<std::string>& lines) {
    Addf(lines,
         "tunables on=%d scale=%.3f height=%.1f ease=%.3f",
         CVarGetInteger(CVAR_CAM_INDOOR_ON, CAM_INDOOR_ON_DEFAULT),
         CVarGetFloat(CVAR_CAM_INDOOR_SCALE, CAM_INDOOR_SCALE_DEFAULT),
         CVarGetFloat(CVAR_CAM_INDOOR_HEIGHT, CAM_INDOOR_HEIGHT_DEFAULT),
         CVarGetFloat(CVAR_CAM_INDOOR_EASE_IN, CAM_INDOOR_EASE_IN_DEFAULT));
    Addf(lines,
         "ring samples=%d radius=%.1f bias=%.1f k=%d",
         CVarGetInteger(CVAR_CAM_INDOOR_RING_SAMPLES, CAM_INDOOR_RING_SAMPLES_DEFAULT),
         CVarGetFloat(CVAR_CAM_INDOOR_RING_RADIUS, CAM_INDOOR_RING_RADIUS_DEFAULT),
         CVarGetFloat(CVAR_CAM_INDOOR_RING_BIAS, CAM_INDOOR_RING_BIAS_DEFAULT),
         CVarGetInteger(CVAR_CAM_INDOOR_RING_K, CAM_INDOOR_RING_K_DEFAULT));
    Addf(lines, "switches ceilclamp=%d corner=%.1f hold=%.1f floorahead=%d probeceil=%d",
         CVarGetInteger(CVAR_CAM_CEIL_CLAMP_ON, CAM_CEIL_CLAMP_ON_DEFAULT),
         CVarGetFloat(CVAR_CAM_CEIL_CORNER_BACK, CAM_CEIL_CORNER_BACK_DEFAULT),
         CVarGetFloat(CVAR_CAM_CEIL_HOLD, CAM_CEIL_HOLD_DEFAULT),
         CVarGetInteger(CVAR_CAM_FLOOR_AHEAD_ON, CAM_FLOOR_AHEAD_ON_DEFAULT),
         CVarGetInteger(CVAR_CAM_PROBE_CEIL_ON, CAM_PROBE_CEIL_ON_DEFAULT));
    Addf(lines, "ledge cap=%.1f rail=%d", CVarGetFloat(CVAR_CAM_LEDGE_DROP_CAP, CAM_LEDGE_DROP_CAP_DEFAULT),
         CVarGetInteger(CVAR_CAM_LEDGE_RAIL, CAM_LEDGE_RAIL_DEFAULT));
    Addf(lines, "height t=%.3f", CVarGetFloat(CVAR_CAM_ADULT_HEIGHT_T, CAM_ADULT_HEIGHT_T_DEFAULT));
}

int32_t Status(std::vector<std::string>& lines) {
    AddTunables(lines);
    if (gPlayState == nullptr) {
        Addf(lines, "probe result=unavailable reason=no_play_state");
        return 0;
    }

    Camera* camera = GET_ACTIVE_CAM(gPlayState);
    if (camera == nullptr) {
        Addf(lines, "probe result=unavailable reason=no_active_camera");
        return 0;
    }

    CameraIndoorProbe probe = {};
    Camera_IndoorPullInProbe(camera, &probe);
    Addf(lines, "gate scene=0x%X grid_tool=%d enabled=%d", probe.sceneId, probe.gridToolScene, probe.enabled);
    Addf(lines, "detect hits=%d of=%d needed=%d indoors=%d check_height=%.1f", probe.hits, probe.samples, probe.needed,
         probe.indoors, probe.checkHeight);
    // `scale` is what this frame would apply; `applied` is what Normal1 last really used, and the
    // frame it did. They differ whenever another camera mode is running, which is the whole reason
    // both are printed - a run that reads only one of them cannot tell "off" from "not running".
    // `min`/`max` and the whole `applied` line come from the camera's own last pass, so they read
     // as 0.0 / 1.000 before it has ever run - on the title screen, or in a scene whose camera
     // setting never reaches Normal1. Say so instead of printing a zero that looks like a
     // measurement; this is the same trap `indoors` had.
    if (!probe.appliedValid) {
        Addf(lines, "dist now=%.1f scale=%.3f", probe.dist, probe.scale);
        Addf(lines, "applied never=1 reason=normal1_has_not_run setting=%d mode=%d", camera->setting, camera->mode);
        return 0;
    }
    Addf(lines, "dist now=%.1f min=%.1f max=%.1f scale=%.3f", probe.dist, probe.distMin, probe.distMax, probe.scale);
    // #136: `eff` is the height the camera frames from now; `y_offset` with min/max above is what
    // Normal1 resolved from it at its last reload, so a t that has not reached the cached
    // parameters yet shows as eff moving while these do not.
    Addf(lines, "framing t=%.3f eff=%.1f y_offset=%.1f", probe.heightT, probe.height, probe.yOffset);
    Addf(lines, "applied scale=%.3f frame=%u now_frame=%u setting=%d mode=%d", probe.appliedScale, probe.appliedFrame,
         probe.frame, camera->setting, camera->mode);
    return 0;
}

int32_t Defaults(std::vector<std::string>& lines) {
    CVarClear(CVAR_CAM_INDOOR_ON);
    CVarClear(CVAR_CAM_INDOOR_SCALE);
    CVarClear(CVAR_CAM_INDOOR_HEIGHT);
    CVarClear(CVAR_CAM_INDOOR_EASE_IN);
    CVarClear(CVAR_CAM_INDOOR_RING_SAMPLES);
    CVarClear(CVAR_CAM_INDOOR_RING_RADIUS);
    CVarClear(CVAR_CAM_INDOOR_RING_BIAS);
    CVarClear(CVAR_CAM_INDOOR_RING_K);
    CVarClear(CVAR_CAM_CEIL_CLAMP_ON);
    CVarClear(CVAR_CAM_CEIL_CORNER_BACK);
    CVarClear(CVAR_CAM_CEIL_HOLD);
    CVarClear(CVAR_CAM_FLOOR_AHEAD_ON);
    CVarClear(CVAR_CAM_PROBE_CEIL_ON);
    CVarClear(CVAR_CAM_LEDGE_DROP_CAP);
    CVarClear(CVAR_CAM_LEDGE_RAIL);
    CVarClear(CVAR_CAM_ADULT_HEIGHT_T);
    CVarSave();
    Addf(lines, "op=defaults result=ok");
    AddTunables(lines);
    return 0;
}

} // namespace

int32_t CameraIndoorConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? std::string("status") : args[0];

    if (sub == "status") {
        return Status(lines);
    }
    if (sub == "on" || sub == "off") {
        CVarSetInteger(CVAR_CAM_INDOOR_ON, sub == "on" ? 1 : 0);
        CVarSave();
        Addf(lines, "op=%s result=ok", sub.c_str());
        return 0;
    }
    // Every bound comes from CameraIndoorTuning.h, which is also where the camera clamps to it.
    // Typing the numbers again here is how a console comes to refuse a value the camera would have
    // accepted, or accept one it silently clamps.
    if (sub == "scale") {
        return SetFloat(args, lines, CVAR_CAM_INDOOR_SCALE, "scale", CAM_INDOOR_SCALE_MIN, CAM_INDOOR_SCALE_MAX);
    }
    if (sub == "height") {
        return SetFloat(args, lines, CVAR_CAM_INDOOR_HEIGHT, "height", CAM_INDOOR_HEIGHT_MIN,
                        CAM_INDOOR_HEIGHT_MAX);
    }
    if (sub == "ease") {
        return SetFloat(args, lines, CVAR_CAM_INDOOR_EASE_IN, "ease", CAM_INDOOR_EASE_IN_MIN,
                        CAM_INDOOR_EASE_IN_MAX);
    }
    if (sub == "ring") {
        return SetInt(args, lines, CVAR_CAM_INDOOR_RING_SAMPLES, "ring", 0, CAM_INDOOR_RING_MAX);
    }
    if (sub == "radius") {
        return SetFloat(args, lines, CVAR_CAM_INDOOR_RING_RADIUS, "radius", 0.0f, CAM_INDOOR_RING_RADIUS_MAX);
    }
    if (sub == "bias") {
        return SetFloat(args, lines, CVAR_CAM_INDOOR_RING_BIAS, "bias", 0.0f, CAM_INDOOR_RING_BIAS_MAX);
    }
    if (sub == "k") {
        // Not clamped to the current ring size here: `k 3` then `ring 4` is a reasonable order to
        // type them in. The camera clamps k to the sample count each frame, so an over-large k
        // behaves as "all of them" rather than as "never".
        return SetInt(args, lines, CVAR_CAM_INDOOR_RING_K, "k", 1, CAM_INDOOR_RING_MAX + 1);
    }
    // #152 bisect switches: each turns one of our own camera corrections off, engine-wide.
    if (sub == "ceilclamp") {
        return SetSwitch(args, lines, CVAR_CAM_CEIL_CLAMP_ON, "ceilclamp");
    }
    if (sub == "corner") {
        return SetFloat(args, lines, CVAR_CAM_CEIL_CORNER_BACK, "corner", 0.0f, CAM_CEIL_CORNER_BACK_MAX);
    }
    if (sub == "hold") {
        return SetFloat(args, lines, CVAR_CAM_CEIL_HOLD, "hold", 0.0f, CAM_CEIL_HOLD_MAX);
    }
    if (sub == "floorahead") {
        return SetSwitch(args, lines, CVAR_CAM_FLOOR_AHEAD_ON, "floorahead");
    }
    if (sub == "probeceil") {
        return SetSwitch(args, lines, CVAR_CAM_PROBE_CEIL_ON, "probeceil");
    }
    // #155 ledge look-down, grid-tool scenes only.
    if (sub == "ledgecap") {
        return SetFloat(args, lines, CVAR_CAM_LEDGE_DROP_CAP, "ledgecap", 0.0f, CAM_LEDGE_DROP_CAP_MAX);
    }
    if (sub == "ledgerail") {
        return SetSwitch(args, lines, CVAR_CAM_LEDGE_RAIL, "ledgerail");
    }
    // #136 adult camera height, grid-tool scenes only.
    if (sub == "heightt") {
        return SetHeightT(args, lines);
    }
    if (sub == "defaults") {
        return Defaults(lines);
    }

    Addf(lines, "op=%s result=error error=unknown_subcommand", sub.c_str());
    return 1;
}

// --- the human sink: the `camindoor` console command ---------------------------------------------
//
// The mechanical half is ConsoleSink (sturdy-bassoon#112).

namespace {

const ConsoleSink::Command cameraIndoorCommand(
    "camindoor", CameraIndoorConsole_Run,
    "Grid-tool camera tuning (sturdy-bassoon#108, #152, #155, #136): status | on | off | scale <f> | "
    "height <f> | ease <f> | ring <n> | radius <f> | bias <f> | k <n> | ceilclamp|floorahead|probeceil "
    "on|off | corner <f> | hold <f> | ledgecap <f> | ledgerail on|off | "
    "heightt <f>|+<f>|-<f> | "
    "defaults. The follow distance "
    "is multiplied by `scale` while a ceiling is found within `height` of the floor the player is on, "
    "and Camera_ClampDist's own easing makes that glide. `ease` is the fraction of the normal step "
    "taken while pulling IN, so a low value means a short pass under an archway barely moves the "
    "camera; pushing back out is never slowed. `ring`/`radius`/`bias`/`k` widen the check from one "
    "sample at the player's feet to a ring biased ahead of his facing, which ignores thin overheads "
    "and steadies the flip on a doorway threshold. Every value applies on the next frame and "
    "persists. `status` prints what the check is reading right now, which is how you tell "
    "'not indoors' from 'the follow camera is not the one running'. ceilclamp/floorahead/probeceil switch "
    "our #103/#38 corrections off, engine-wide, to bisect a camera fight; `corner` is how far back from a wall hit "
    "the ceiling clamp looks up for the ceiling over it (0 = the #103 rule: a wall first means no "
    "clamp); `hold` is how much further the clamp's test reaches on the frame after it found a "
    "ceiling (0 = none). ledgecap is the deepest a drop ahead counts as, in units (0 = no cap: the "
    "camera looks down harder the further the drop goes); ledgerail ignores a low top with a drop "
    "behind it (a parapet) rather than reading it as a step up. "
    "heightt moves adult Link's camera height toward Young Link's (0 vanilla, 1 child); a sign steps it.",
    { { "status|on|off|scale|height|ease|ring|radius|bias|k|ceilclamp|corner|hold|floorahead|probeceil|ledgecap|"
        "ledgerail|heightt|defaults",
        Ship::ArgumentType::TEXT },
      { "value", Ship::ArgumentType::TEXT, true } });

} // namespace
