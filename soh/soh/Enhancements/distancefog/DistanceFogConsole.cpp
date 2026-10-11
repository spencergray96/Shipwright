#include "DistanceFogConsole.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "macros.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
extern s32 gPlayFogMax;
}

namespace {

using ConsoleSink::Addf;

// Fog-space is 0..1000 across zNear..zFar (Gfx_SetFog, z_rcp.c): below 997 it is a real start position,
// 997..999 a fixed factor that starts just past 996, and 1000 turns fog off. Environment_Update clamps a
// scene's own value to 996 and its far to 12800 (z_kankyo.c); the override takes the same range as the
// agenttest fog it replaced.
constexpr int32_t kSceneNearMax = 996;
constexpr int32_t kNearMax = 1000;
constexpr int32_t kFarMin = 100;
constexpr int32_t kFarMax = 12800;
// gSPFogPosition(near, max) packs 128000 / (max - near) into a signed 16-bit multiplier, so a band narrower
// than 4 fog-space units overflows it (sturdy-bassoon#169). 1000 is vanilla's max: fog completes at the clip.
constexpr int32_t kMaxWidthMin = 4;
constexpr int32_t kVanillaMax = 1000;

const char* const kSettingNames[4] = { "dawn", "day", "dusk", "night" };

struct Override {
    bool active = false;
    bool pinColor = false;
    s16 near = 0;
    s16 far = 0;
    s16 max = kVanillaMax;
    u8 color[3] = { 0, 0, 0 };
};

Override sOverride;

// The band the scene's own light settings give this frame, weather and lightning included - what
// Environment_Update writes into lightCtx (z_kankyo.c, "Adjust fog near and far"). Read from envCtx rather
// than lightCtx, which holds the override's values while the game is paused and Environment_Update idles.
struct Band {
    s16 near;
    s16 far;
    s16 max;
    u8 color[3];
};

Band SceneBand(const PlayState* play) {
    const EnvironmentContext* env = &play->envCtx;
    Band band;
    for (int i = 0; i < 3; i++) {
        const s32 c = env->lightSettings.fogColor[i] + env->adjFogColor[i];
        band.color[i] = static_cast<u8>(CLAMP(c, 0, 255));
    }
    const s32 near = env->lightSettings.fogNear + env->adjFogNear;
    const s32 far = env->lightSettings.fogFar + env->adjFogFar;
    band.near = static_cast<s16>(MIN(near, kSceneNearMax));
    band.far = static_cast<s16>(MIN(far, kFarMax));
    band.max = kVanillaMax;
    return band;
}

// The blend-rate bits a scene packs above fogNear's low 10 (BLEND_RATE_AND_FOG_NEAR), so a pasted live band
// keeps the rate the scene's own settings use. Every custom scene so far uses 1.
s32 SceneBlendRate(const PlayState* play) {
    const EnvironmentContext* env = &play->envCtx;
    return env->lightSettingsList != nullptr && env->numLightSettings > 0
               ? (env->lightSettingsList[0].fogNear >> 10) & 0x3F
               : 1;
}

Band LiveBand(const PlayState* play) {
    if (!sOverride.active) {
        return SceneBand(play);
    }
    Band band = SceneBand(play);
    band.near = sOverride.near;
    band.far = sOverride.far;
    band.max = sOverride.max;
    if (sOverride.pinColor) {
        for (int i = 0; i < 3; i++) {
            band.color[i] = sOverride.color[i];
        }
    }
    return band;
}

// The depth, in world units along the view axis, at fog-space u. Fog is linear in post-divide depth, not
// distance (the interpreter's fog_z = z * winv * fog_mul + fog_offset), and fog-space u maps to depth d through
// the perspective as u = 1000 f / (f - n) * (1 - n / d); inverted, d = n / (1 - u (f - n) / (1000 f)).
float DepthAt(float zNear, float u, int32_t far) {
    const float f = static_cast<float>(far);
    const float denom = 1.0f - u * (f - zNear) / (1000.0f * f);
    return denom <= zNear / f ? f : zNear / denom;
}

// Where fog begins. 997..999 get Gfx_SetFog's fixed factor, which starts at u = 500 * (1 + 0x7F00 / 0x7FFF).
// -1 = no fog.
float StartDistance(float zNear, int32_t near, int32_t far) {
    if (near >= 1000) {
        return -1.0f;
    }
    return DepthAt(zNear, near >= 997 ? 500.0f * (1.0f + 32512.0f / 32767.0f) : static_cast<float>(near), far);
}

// Environment_DrawSkyboxFilters (z_kankyo.c) lays the fog colour over the skybox once near drops under
// 980, at alpha (1000 - near) / 50: tinted down to 951, a flat fog-coloured sky at 950 and below.
const char* SkyFilter(int32_t near) {
    if (near >= 980) {
        return "clear";
    }
    return near > 950 ? "tinted" : "replaced";
}

void Describe(const char* op, const PlayState* play, std::vector<std::string>& lines) {
    const Band live = LiveBand(play);
    const Band scene = SceneBand(play);
    const float start = StartDistance(play->view.zNear, live.near, live.far);
    const u32 minutes = static_cast<u32>(gSaveContext.dayTime) * 24 * 60 / 0x10000;
    char startText[16];
    if (start < 0.0f) {
        std::snprintf(startText, sizeof(startText), "off");
    } else {
        std::snprintf(startText, sizeof(startText), "%.0f", start);
    }
    // end= only for a max below 1000, where fog completes before the clip (sturdy-bassoon#169); at 1000 it
    // completes at the clip, which far= already says. The parser keeps a max under 1000 to bands below 997.
    char endText[24] = "";
    if (live.max < kVanillaMax) {
        std::snprintf(endText, sizeof(endText), " end=%.0f", DepthAt(play->view.zNear, live.max, live.far));
    }
    // rain= is envCtx.unk_F2[0], the rain's intensity (the Song of Storms raises it to 20); c= clamps near to
    // what a scene can hold, since Environment_Update would clamp a pasted 997+ anyway.
    Addf(lines,
         "op=%s result=ok mode=%s near=%d far=%d max=%d color=%u,%u,%u color_src=%s start=%s%s sky=%s "
         "time=%02u:%02u rain=%u "
         "scene_near=%d scene_far=%d scene_color=%u,%u,%u c={%u,%u,%u},(s16)(%d|(%d<<10)),%d",
         op, sOverride.active ? "override" : "scene", live.near, live.far, live.max, live.color[0], live.color[1],
         live.color[2], sOverride.active && sOverride.pinColor ? "pinned" : "scene", startText, endText,
         SkyFilter(live.near), minutes / 60, minutes % 60, play->envCtx.unk_F2[0], scene.near, scene.far,
         scene.color[0], scene.color[1], scene.color[2], live.color[0], live.color[1], live.color[2],
         MIN(live.near, kSceneNearMax), SceneBlendRate(play), live.far);
}

// One line per light setting, ending in the three initializer fields a scene's EnvLightSettings entry
// closes with - fogColor, BLEND_RATE_AND_FOG_NEAR's packed near, fogFar - so an approved look pastes in.
void DescribeSetting(const EnvLightSettings* l, u8 index, const char* group, std::vector<std::string>& lines) {
    const s32 near = l->fogNear & 0x3FF;
    const s32 blend = (l->fogNear >> 10) & 0x3F;
    Addf(lines, "op=status setting=%u group=%s name=%s fog=%u,%u,%u near=%d far=%d c={%u,%u,%u},(s16)(%d|(%d<<10)),%d",
         index, group, kSettingNames[index % 4], l->fogColor[0], l->fogColor[1], l->fogColor[2], near, l->fogFar,
         l->fogColor[0], l->fogColor[1], l->fogColor[2], near, blend, l->fogFar);
}

bool ParseInt(const std::string& text, int32_t lo, int32_t hi, int32_t* out) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value < lo || value > hi) {
        return false;
    }
    *out = static_cast<int32_t>(value);
    return true;
}

bool LooksNumeric(const std::string& text) {
    return !text.empty() && (std::isdigit(static_cast<unsigned char>(text[0])) || text[0] == '-');
}

constexpr const char* kSetUsage = "usage=<near(0..1000)>_<far(100..12800)>_[r_g_b(0..255)]_[max=<near+4..1000>]";

} // namespace

int32_t DistanceFogConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? "status" : args[0];
    const bool isSet = LooksNumeric(sub);
    const char* op = isSet ? "set" : sub.c_str();

    if (!isSet && sub != "status" && sub != "off") {
        // The typed word is not echoed: it is free text, and this line is parsed field by field.
        lines.push_back(
            "op=unknown result=error error=unknown_subcommand usage=status|off|<near>_<far>_[r_g_b]_[max=<m>]");
        return 1;
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=%s result=error error=no_scene", op);
        return 1;
    }

    if (sub == "status") {
        Describe("status", gPlayState, lines);
        const EnvironmentContext* env = &gPlayState->envCtx;
        if (env->lightSettingsList != nullptr) {
            for (u8 i = 0; i < 4 && i < env->numLightSettings; i++) {
                DescribeSetting(&env->lightSettingsList[i], i, "scene", lines);
            }
            for (u8 i = 8; i < 12 && i < env->numLightSettings; i++) {
                DescribeSetting(&env->lightSettingsList[i], i, "storm", lines);
            }
        }
        return 0;
    }
    if (sub == "off") {
        sOverride.active = false;
        Describe("off", gPlayState, lines);
        return 0;
    }

    // An optional trailing max=<m> (sturdy-bassoon#169): the fog-space value where fog reaches 100%, passed to
    // Gfx_SetFog in place of vanilla's 1000. Without it the band is exactly what it always was.
    std::vector<std::string> positional = args;
    const bool hasMax = !positional.empty() && positional.back().rfind("max=", 0) == 0;
    std::string maxText;
    if (hasMax) {
        maxText = positional.back().substr(4);
        positional.pop_back();
    }
    int32_t near = 0;
    int32_t far = 0;
    int32_t max = kVanillaMax;
    int32_t rgb[3] = { 0, 0, 0 };
    const bool pin = positional.size() >= 5;
    bool ok = (positional.size() == 2 || positional.size() == 5) && ParseInt(positional[0], 0, kNearMax, &near) &&
              ParseInt(positional[1], kFarMin, kFarMax, &far);
    for (size_t i = 0; ok && pin && i < 3; i++) {
        ok = ParseInt(positional[2 + i], 0, 255, &rgb[i]);
    }
    // A max below 1000 needs max - near >= 4, which also refuses a max <= near and keeps it off the 997..999
    // fixed-factor band, where Gfx_SetFog never reads it. An explicit max=1000 is vanilla, so any near takes it.
    if (ok && hasMax) {
        ok = ParseInt(maxText, 0, kVanillaMax, &max) && (max == kVanillaMax || max - near >= kMaxWidthMin);
    }
    if (!ok) {
        Addf(lines, "op=set result=error error=bad_argument %s", kSetUsage);
        return 1;
    }
    sOverride.active = true;
    sOverride.pinColor = pin;
    sOverride.near = static_cast<s16>(near);
    sOverride.far = static_cast<s16>(far);
    sOverride.max = static_cast<s16>(max);
    for (int i = 0; i < 3; i++) {
        sOverride.color[i] = static_cast<u8>(rgb[i]);
    }
    Describe("set", gPlayState, lines);
    return 0;
}

// --- the override itself -------------------------------------------------------------------------

namespace {

// OnPlayDrawBegin runs after the frame's Environment_Update and before Play_Draw sets the fog and builds the
// perspective from lightCtx.fogFar (z_play.c), so the band reaches the fog, the far clip and the cullable
// room shape's fog test alike, while the lights Environment_Update just wrote are left alone.
void ApplyOverride() {
    // Play_SetFog's max is written every frame, so it never outlives the override: off, a scene load or an
    // inactive override all draw with vanilla's 1000 from the next frame on (sturdy-bassoon#169).
    gPlayFogMax = sOverride.active ? sOverride.max : kVanillaMax;
    if (!sOverride.active || gPlayState == nullptr) {
        return;
    }
    const Band band = LiveBand(gPlayState);
    gPlayState->lightCtx.fogNear = band.near;
    gPlayState->lightCtx.fogFar = band.far;
    for (int i = 0; i < 3; i++) {
        gPlayState->lightCtx.fogColor[i] = band.color[i];
    }
}

void RegisterDistanceFog() {
    COND_HOOK(OnPlayDrawBegin, true, ApplyOverride);
    // A scene load hands fog back, as the reg-editor override this replaced did (Environment_Init): a band
    // tuned for one map is not assumed for the next.
    COND_HOOK(OnSceneInit, true, [](int16_t) { sOverride.active = false; });
}

RegisterShipInitFunc initFunc(RegisterDistanceFog, {});

// --- the human sink: the `fog` console command ---------------------------------------------------

const ConsoleSink::Command distanceFogCommand(
    "fog", DistanceFogConsole_Run,
    "Tune the scene's distance fog live (sturdy-bassoon#144): <near> <far> [r g b] [max=<m>] | status | off. "
    "near is fog-space 0..1000 (nonlinear: 996, the most a scene can have, starts ~2,000 units out at far 12800; 1000 "
    "is no fog), far is world units 100..12800 and also the far clip. Without r g b the colour keeps following "
    "time of day and weather; lights are never touched. max is the fog-space value where fog reaches 100% "
    "(sturdy-bassoon#169): 1000, the default, completes it at the clip; lower completes it sooner, down to "
    "near+4. Below near 980 the sky fades to the fog colour. status "
    "prints the live band, where it starts, and each light setting's fog as C to paste into a scene. A scene "
    "load hands fog back; nothing is saved.",
    { { "near|status|off", Ship::ArgumentType::TEXT, true },
      { "far", Ship::ArgumentType::TEXT, true },
      { "r", Ship::ArgumentType::TEXT, true },
      { "g", Ship::ArgumentType::TEXT, true },
      { "b", Ship::ArgumentType::TEXT, true },
      { "max=<m>", Ship::ArgumentType::TEXT, true } });

} // namespace
