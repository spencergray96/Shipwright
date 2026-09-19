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
    "Indoor camera pull-in on grid-tool scenes (sturdy-bassoon#108): status | on | off | scale <f> | "
    "height <f> | ease <f> | ring <n> | radius <f> | bias <f> | k <n> | defaults. The follow distance "
    "is multiplied by `scale` while a ceiling is found within `height` of the floor the player is on, "
    "and Camera_ClampDist's own easing makes that glide. `ease` is the fraction of the normal step "
    "taken while pulling IN, so a low value means a short pass under an archway barely moves the "
    "camera; pushing back out is never slowed. `ring`/`radius`/`bias`/`k` widen the check from one "
    "sample at the player's feet to a ring biased ahead of his facing, which ignores thin overheads "
    "and steadies the flip on a doorway threshold. Every value applies on the next frame and "
    "persists. `status` prints what the check is reading right now, which is how you tell "
    "'not indoors' from 'the follow camera is not the one running'.",
    { { "status|on|off|scale|height|ease|ring|radius|bias|k|defaults", Ship::ArgumentType::TEXT },
      { "value", Ship::ArgumentType::TEXT, true } });

} // namespace
