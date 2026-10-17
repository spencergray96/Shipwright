#include "DistanceFogConsole.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "HorizonHaze.h"
#include "WorldFog.h"

#include <fast/interpreter.h>

extern "C" {
#include <z64.h>
#include "macros.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
extern s32 gPlayFogMax;
extern s32 gSkyColourFrameBuffer; // soh/framebuffer_effects.h (sturdy-bassoon#229)
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
// World-unit fog (sturdy-bassoon#167): start and end are view depths in world units, carried as u16 by
// gSPFogWorld and as s16 by the side table, so they stay under 32768.
constexpr int32_t kWorldMax = 32767;
// A world-unit band's far clip goes past vanilla's 12800 (sturdy-bassoon#227; WorldFog.h). The vanilla band
// keeps kFarMax.
constexpr int32_t kWorldClipMax = WORLD_FOG_CLIP_MAX;
// `fog znear` (sturdy-bassoon#227): the near clip plane, a judging knob for distant z-fighting. View_Init's 10 is
// what the game uses everywhere else; 60 stays inside the skybox's cube.
constexpr int32_t kZNearDefault = 10;
constexpr int32_t kZNearMin = 1;
constexpr int32_t kZNearMax = 60;

const char* const kSettingNames[4] = { "dawn", "day", "dusk", "night" };

struct Override {
    bool active = false;
    bool pinColor = false;
    bool world = false;     // `fog world ...` (sturdy-bassoon#167): a world-unit band, not near/far/max
    bool clipGiven = false; // world only: clip= was typed; otherwise the clip is the scene's, live
    s16 near = 0;
    s16 far = 0;
    s16 max = kVanillaMax;
    s16 start = 0;
    s16 end = 0;
    u8 color[3] = { 0, 0, 0 };
};

Override sOverride;

// `fog edge ...` (sturdy-bassoon#169): boundary fog's override, layered on the scene's boundary. Separate from the
// band's, so `fog off` hands back the band and `fog edge scene` the boundary; a scene load hands back both.
struct EdgeOverride {
    bool off = false;     // `fog edge off`: no boundary term, even in a scene with one
    bool hasRamp = false; // `fog edge <b0> <b1>`, or a ramp given with `rect`
    bool hasRect = false; // `fog edge rect ...`: one hand rectangle in place of the scene's
    s16 b0 = 0;
    s16 b1 = 1;
    WorldFogRect rect = { 0, 0, 1, 1 };
};

EdgeOverride sEdge;

// `fog haze ...` (sturdy-bassoon#168): the horizon haze band's override, layered on the scene's height and fade.
// Its own, like the edge's: `fog off` leaves it alone, `fog haze scene` hands it back, a scene load hands back all.
struct HazeOverride {
    bool off = false;       // `fog haze off`: no band, even under world-unit fog
    bool hasValues = false; // `fog haze <H> <F>`
    WorldFogHaze values = { 0.0f, 0.0f };
};

HazeOverride sHaze;

// `fog znear <n>` (sturdy-bassoon#227): 0 leaves the near plane alone. sZNearApplied remembers that it was written,
// so `fog znear off` and a scene load put 10 back once.
int32_t sZNear = 0;
bool sZNearApplied = false;

// Play_Draw builds the perspective from view.zNear and keeps whatever is there, so the override is written every
// frame (ApplyOverride) and 10 put back once when it ends. The console calls it too, so its line reads the new plane.
void ApplyZNear() {
    if (gPlayState != nullptr && (sZNear != 0 || sZNearApplied)) {
        gPlayState->view.zNear = static_cast<f32>(sZNear != 0 ? sZNear : kZNearDefault);
        sZNearApplied = sZNear != 0;
    }
}

// The boundary to draw this frame: the scene's, with the override's rectangle and ramp over it.
struct Edge {
    bool off;
    const WorldFogRect* rects; // NULL with none
    u8 count;
    bool hasRamp; // false only when there are no rectangles either
    s16 b0;
    s16 b1;
    // Whether the term has anything to draw: rectangles, a ramp, and not switched off.
    bool Draws() const {
        return !off && count != 0 && hasRamp;
    }
};

Edge LiveEdge() {
    Edge edge;
    const u8 sceneCount = WorldFog_SceneBoundary(&edge.rects, &edge.b0, &edge.b1);
    edge.count = sceneCount;
    edge.hasRamp = sceneCount != 0;
    edge.off = sEdge.off;
    if (sEdge.hasRect) {
        edge.rects = &sEdge.rect;
        edge.count = 1;
    }
    if (sEdge.hasRamp) {
        edge.hasRamp = true;
        edge.b0 = sEdge.b0;
        edge.b1 = sEdge.b1;
    }
    return edge;
}

// The band the scene's own light settings give this frame, weather and lightning included - what
// Environment_Update writes into lightCtx (z_kankyo.c, "Adjust fog near and far"). Read from envCtx rather
// than lightCtx, which holds the override's values while the game is paused and Environment_Update idles.
//
// With world-unit rows (sturdy-bassoon#167) the band is world: start/end from gWorldFogScene, which
// Environment_Update blends, and far its clip. near is still the scene's, which keeps driving the sky filter.
struct Band {
    bool world;
    s16 near;
    s16 far;
    s16 max;
    s16 start; // world only
    s16 end;   // world only
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
    band.world = WorldFog_HasScene() != 0;
    band.start = gWorldFogScene.start;
    band.end = gWorldFogScene.end;
    if (band.world) {
        band.far = static_cast<s16>(CLAMP(gWorldFogScene.clip, kFarMin, kWorldClipMax));
    }
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
    band.world = sOverride.world;
    if (sOverride.world) {
        band.start = sOverride.start;
        band.end = sOverride.end;
        band.max = kVanillaMax;
        if (sOverride.clipGiven) {
            band.far = sOverride.far;
        }
    } else {
        band.near = sOverride.near;
        band.far = sOverride.far;
        band.max = sOverride.max;
    }
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

// World-unit fog's factor at view depth d (sturdy-bassoon#167): linear from start to end, as GfxSpVertex.
// The blend truncates start and end separately, so it can bring them together; the renderer's G_FOG_WORLD
// handler then uses end = start + 1, and so does this.
float WorldFactor(const Band& band, float d) {
    const float t = (d - band.start) / static_cast<float>(MAX(band.end - band.start, 1));
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

// What the scene's own fog is, on every line: scene_kind=, and for world rows their band right now.
std::string SceneKindText(const Band& scene) {
    char text[64];
    if (scene.world) {
        std::snprintf(text, sizeof(text), "scene_kind=world scene_start=%d scene_end=%d", scene.start, scene.end);
    } else {
        std::snprintf(text, sizeof(text), "scene_kind=vanilla");
    }
    return text;
}

// The line for a world-unit band (sturdy-bassoon#167). The vanilla line's fields keep their meanings: start=
// and end= where the fog begins and completes, far= the clip, near= the fog-space value still driving the sky
// filter. at_far= is the factor at the clip itself - 1 means nothing the clip cuts can show - and c= the band
// as a scene's WorldFogSetting row, {start,end,clip}.
void DescribeWorld(const char* op, const PlayState* play, const Band& live, const Band& scene,
                   std::vector<std::string>& lines) {
    const u32 minutes = static_cast<u32>(gSaveContext.dayTime) * 24 * 60 / 0x10000;
    Addf(lines,
         "op=%s result=ok mode=%s kind=world near=%d far=%d max=%d color=%u,%u,%u color_src=%s start=%d end=%d "
         "at_far=%.3f sky=%s time=%02u:%02u rain=%u scene_near=%d scene_far=%d scene_color=%u,%u,%u %s "
         "c={%d,%d,%d}",
         op, sOverride.active ? "override" : "scene", live.near, live.far, live.max, live.color[0], live.color[1],
         live.color[2], sOverride.active && sOverride.pinColor ? "pinned" : "scene", live.start, live.end,
         WorldFactor(live, static_cast<float>(live.far)), SkyFilter(live.near), minutes / 60, minutes % 60,
         play->envCtx.unk_F2[0], scene.near, scene.far, scene.color[0], scene.color[1], scene.color[2],
         SceneKindText(scene).c_str(), live.start, live.end, live.far);
}

void Describe(const char* op, const PlayState* play, std::vector<std::string>& lines) {
    const Band live = LiveBand(play);
    const Band scene = SceneBand(play);
    if (live.world) {
        DescribeWorld(op, play, live, scene, lines);
        return;
    }
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
         "op=%s result=ok mode=%s kind=vanilla near=%d far=%d max=%d color=%u,%u,%u color_src=%s start=%s%s sky=%s "
         "time=%02u:%02u rain=%u "
         "scene_near=%d scene_far=%d scene_color=%u,%u,%u %s c={%u,%u,%u},(s16)(%d|(%d<<10)),%d",
         op, sOverride.active ? "override" : "scene", live.near, live.far, live.max, live.color[0], live.color[1],
         live.color[2], sOverride.active && sOverride.pinColor ? "pinned" : "scene", startText, endText,
         SkyFilter(live.near), minutes / 60, minutes % 60, play->envCtx.unk_F2[0], scene.near, scene.far,
         scene.color[0], scene.color[1], scene.color[2], SceneKindText(scene).c_str(), live.color[0], live.color[1],
         live.color[2], MIN(live.near, kSceneNearMax), SceneBlendRate(play), live.far);
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
constexpr const char* kWorldUsage =
    "usage=world_<start(0..32766)>_<end(start+1..32767)>_[r_g_b(0..255)]_[clip=<100..32767>]";

// `fog world <start> <end> [r g b] [clip=<c>]` (sturdy-bassoon#167): a world-unit band in any scene, vanilla
// ones included. Without clip= the clip stays the scene's own, live; max= is vanilla's and is refused.
int32_t RunWorld(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    std::vector<std::string> positional(args.begin() + 1, args.end());
    bool clipGiven = false;
    int32_t clip = 0;
    bool ok = true;
    while (ok && !positional.empty() && positional.back().find('=') != std::string::npos) {
        const std::string word = positional.back();
        positional.pop_back();
        ok = !clipGiven && word.rfind("clip=", 0) == 0 &&
             ParseInt(word.substr(5), kFarMin, kWorldClipMax, &clip);
        clipGiven = true;
    }
    int32_t start = 0;
    int32_t end = 0;
    int32_t rgb[3] = { 0, 0, 0 };
    const bool pin = positional.size() >= 5;
    ok = ok && (positional.size() == 2 || positional.size() == 5) && ParseInt(positional[0], 0, kWorldMax - 1, &start) &&
         ParseInt(positional[1], start + 1, kWorldMax, &end);
    for (size_t i = 0; ok && pin && i < 3; i++) {
        ok = ParseInt(positional[2 + i], 0, 255, &rgb[i]);
    }
    if (!ok) {
        Addf(lines, "op=set result=error error=bad_argument %s", kWorldUsage);
        return 1;
    }
    sOverride.active = true;
    sOverride.world = true;
    sOverride.pinColor = pin;
    sOverride.clipGiven = clipGiven;
    sOverride.start = static_cast<s16>(start);
    sOverride.end = static_cast<s16>(end);
    sOverride.far = static_cast<s16>(clip);
    sOverride.max = kVanillaMax;
    for (int i = 0; i < 3; i++) {
        sOverride.color[i] = static_cast<u8>(rgb[i]);
    }
    Describe("set", gPlayState, lines);
    return 0;
}

// The boundary-fog line (sturdy-bassoon#169), for `fog edge ...` and `fog status`. edge= on|off|none (none: no
// rectangles anywhere), active= whether the term draws this frame (world-unit fog, a boundary, not off), past=
// Link's distance past the boundary and b= the ramp there, 0..1. c= is the boundary as a scene's C: its
// WorldFogRect rows, then b0 and b1.
void DescribeEdge(const char* op, const char* action, const PlayState* play, std::vector<std::string>& lines) {
    const Edge edge = LiveEdge();
    s16 start = 0;
    s16 end = 0;
    const bool world = WorldFog_Live(&start, &end) != 0;
    const bool on = edge.Draws();
    std::string rects;
    std::string rows;
    for (u8 i = 0; i < edge.count; i++) {
        const WorldFogRect& r = edge.rects[i];
        char text[96];
        std::snprintf(text, sizeof(text), " rect%u=%d,%d,%d,%d", i, r.x0, r.z0, r.x1, r.z1);
        rects += text;
        std::snprintf(text, sizeof(text), "%s{%d,%d,%d,%d}", i == 0 ? "" : ",", r.x0, r.z0, r.x1, r.z1);
        rows += text;
    }
    // Link's distance past the rectangles the line names, by the renderer's own function (the renderer's copy is
    // cleared while the edge is off, so it is measured from these rather than read back).
    char pastText[64] = "link=none past=none b=none";
    const Player* player = GET_PLAYER(play);
    if (player != nullptr) {
        const float x = player->actor.world.pos.x;
        const float z = player->actor.world.pos.z;
        if (edge.count != 0) {
            const float best = WorldFog_DistancePast(edge.rects, edge.count, x, z);
            const float t = (best - edge.b0) / static_cast<float>(MAX(edge.b1 - edge.b0, 1));
            std::snprintf(pastText, sizeof(pastText), "link=%.0f,%.0f past=%.0f b=%.3f", x, z, best,
                          edge.hasRamp ? CLAMP(t, 0.0f, 1.0f) : 0.0f);
        } else {
            std::snprintf(pastText, sizeof(pastText), "link=%.0f,%.0f past=none b=none", x, z);
        }
    }
    char ramp[48] = "b0=none b1=none";
    if (edge.hasRamp) {
        std::snprintf(ramp, sizeof(ramp), "b0=%d b1=%d", edge.b0, edge.b1);
    }
    Addf(lines,
         "op=%s result=ok action=%s edge=%s kind=%s active=%d rects=%u%s rect_src=%s %s ramp_src=%s %s "
         "c={%s},%d,%d",
         op, action, edge.off ? "off" : (on ? "on" : "none"), world ? "world" : "vanilla", world && on ? 1 : 0,
         edge.count, rects.c_str(), sEdge.hasRect ? "hand" : "scene", ramp, sEdge.hasRamp ? "override" : "scene",
         pastText, rows.c_str(), edge.b0, edge.b1);
}

constexpr const char* kEdgeUsage =
    "usage=edge_[status]|edge_off|edge_scene|edge_<b0(0..32766)>_<b1(b0+1..32767)>|"
    "edge_rect_<x0>_<z0>_<x1>_<z1>_[<b0>_<b1>]";

void SetEdgeRamp(int32_t b0, int32_t b1) {
    sEdge.off = false;
    sEdge.hasRamp = true;
    sEdge.b0 = static_cast<s16>(b0);
    sEdge.b1 = static_cast<s16>(b1);
}

// `fog edge ...` (sturdy-bassoon#169): tune boundary fog live. The rectangle and ramp are world units.
int32_t RunEdge(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string action = args.size() < 2 ? "status" : args[1];
    if (action == "status" && args.size() <= 2) {
        DescribeEdge("edge", "status", gPlayState, lines);
        return 0;
    }
    if (action == "off" && args.size() == 2) {
        sEdge.off = true;
        DescribeEdge("edge", "off", gPlayState, lines);
        return 0;
    }
    if (action == "scene" && args.size() == 2) {
        sEdge = EdgeOverride();
        DescribeEdge("edge", "scene", gPlayState, lines);
        return 0;
    }
    if (action == "rect") {
        int32_t v[4] = { 0, 0, 0, 0 };
        int32_t ramp[2] = { 0, 0 };
        const bool withRamp = args.size() == 8;
        bool ok = (args.size() == 6 || withRamp) && ParseInt(args[2], INT16_MIN, INT16_MAX - 1, &v[0]) &&
                  ParseInt(args[3], INT16_MIN, INT16_MAX - 1, &v[1]) && ParseInt(args[4], v[0] + 1, INT16_MAX, &v[2]) &&
                  ParseInt(args[5], v[1] + 1, INT16_MAX, &v[3]);
        if (ok && withRamp) {
            ok = ParseInt(args[6], 0, kWorldMax - 1, &ramp[0]) && ParseInt(args[7], ramp[0] + 1, kWorldMax, &ramp[1]);
        }
        if (!ok) {
            Addf(lines, "op=edge result=error error=bad_argument %s", kEdgeUsage);
            return 1;
        }
        if (!withRamp && !LiveEdge().hasRamp) {
            // No scene boundary and no ramp typed yet: nothing says how far past the rectangle the fog ramps.
            Addf(lines, "op=edge result=error error=no_ramp %s", kEdgeUsage);
            return 1;
        }
        sEdge.off = false;
        sEdge.hasRect = true;
        sEdge.rect = { static_cast<s16>(v[0]), static_cast<s16>(v[1]), static_cast<s16>(v[2]), static_cast<s16>(v[3]) };
        if (withRamp) {
            SetEdgeRamp(ramp[0], ramp[1]);
        }
        DescribeEdge("edge", "rect", gPlayState, lines);
        return 0;
    }
    if (!LooksNumeric(action)) {
        // Not one of the words and not a number: the typed word is not echoed.
        Addf(lines, "op=edge result=error error=unknown_subcommand %s", kEdgeUsage);
        return 1;
    }
    int32_t b0 = 0;
    int32_t b1 = 0;
    if (args.size() != 3 || !ParseInt(args[1], 0, kWorldMax - 1, &b0) || !ParseInt(args[2], b0 + 1, kWorldMax, &b1)) {
        Addf(lines, "op=edge result=error error=bad_argument %s", kEdgeUsage);
        return 1;
    }
    SetEdgeRamp(b0, b1);
    DescribeEdge("edge", "ramp", gPlayState, lines);
    return 0;
}

// Sky-colour fog (sturdy-bassoon#226; WorldFog.h) and its edge layer (sturdy-bassoon#230). The console chooses the
// scope - auto (the default), edge, all or off - and the method, which `mirror` and `clamp` set. ApplySky resolves
// them every frame. The choice is kept across scene loads, as #226's was; nothing is saved.
enum class SkyChoice : uint8_t { Auto, Edge, All, Off };
SkyChoice sSkyChoice = SkyChoice::Auto;
uint8_t sSkyMethod = WORLD_FOG_SKY_MIRROR;
uint32_t sSkyDumpCount = 0;
// Why the live mode is what it is, for status: console (a choice other than auto), or auto's reason -
// world_and_boundary (the edge layer draws), no_world_fog or no_boundary (it is off).
const char* sSkyWhy = "no_world_fog";

const char* SkyName(uint8_t mode) {
    return mode == WORLD_FOG_SKY_MIRROR ? "mirror" : (mode == WORLD_FOG_SKY_CLAMP ? "clamp" : "off");
}

const char* SkyChoiceName(SkyChoice choice) {
    switch (choice) {
        case SkyChoice::Edge:
            return "edge";
        case SkyChoice::All:
            return "all";
        case SkyChoice::Off:
            return "off";
        case SkyChoice::Auto:
            break;
    }
    return "auto";
}

// The live mode and scope for this frame, from the choice. Auto is the edge layer wherever world-unit fog draws
// with a boundary - the beyond ring dissolving into the live sky - and off everywhere else, so vanilla scenes, scenes
// without a boundary and `fog edge off` take no copy at all. Run at OnPlayDrawBegin after the band and the boundary
// are resolved (ApplyOverride), and straight after a console change so status reads it at once.
void ApplySky() {
    bool on = true;
    uint8_t scope = WORLD_FOG_SKY_SCOPE_EDGE;
    switch (sSkyChoice) {
        case SkyChoice::Edge:
            sSkyWhy = "console";
            break;
        case SkyChoice::All:
            scope = WORLD_FOG_SKY_SCOPE_ALL;
            sSkyWhy = "console";
            break;
        case SkyChoice::Off:
            on = false;
            sSkyWhy = "console";
            break;
        default: {
            s16 start = 0;
            s16 end = 0;
            const bool world = WorldFog_Live(&start, &end) != 0;
            const bool boundary = LiveEdge().Draws();
            on = world && boundary;
            sSkyWhy = !world ? "no_world_fog" : (!boundary ? "no_boundary" : "world_and_boundary");
            break;
        }
    }
    WorldFog_SetSky(on ? sSkyMethod : WORLD_FOG_SKY_OFF, scope);
}

// The sky line, for `fog sky ...` and `fog status`. live= what draws (edge, all or off), from choice= (auto, edge,
// all, off) for the reason why=; sky= the live method or off and method= the chosen one. armed= whether the last
// rendered frame took the copy, size= and origin= the copy and where it came from in the render target, horizon_row=
// the horizon's row in the copy at its centre column (clamp's), horizon= the line over render-target pixels at float
// precision (a check rebuilds clamp's sample rows from it, and 6 digits put a row off by one), dumps= the
// `fog sky dump`s written. edge_tris= and edge_baked= are the edge layer's witness (sturdy-bassoon#230): interpreted
// triangles and baked draws the last rendered frame drew with it. drawn= the copy texels the sky drew (x0, y0, x1,
// y1: a letterbox's bars are left out), which every read of the copy stays inside (sturdy-bassoon#241).
void DescribeSky(const char* op, const char* action, std::vector<std::string>& lines) {
    const Fast::SkyFogStatus& now = Fast::SkyFogNow();
    const Fast::SkyFogParams& p = now.last;
    char row[32] = "none";
    if (now.copies != 0 && std::abs(p.horizon[1]) > Fast::SKY_FOG_HORIZON_EPSILON) {
        const float cx = p.origin[0] + p.size[0] * 0.5f;
        std::snprintf(row, sizeof(row), "%.1f", -(p.horizon[0] * cx + p.horizon[2]) / p.horizon[1] - p.origin[1]);
    }
    const char* live = WorldFog_Sky() == WORLD_FOG_SKY_OFF
                           ? "off"
                           : (WorldFog_SkyScope() == WORLD_FOG_SKY_SCOPE_EDGE ? "edge" : "all");
    Addf(lines,
         "op=%s result=ok action=%s live=%s choice=%s why=%s sky=%s method=%s fb=%d copies=%llu armed=%d size=%dx%d "
         "origin=%d,%d msaa=%u horizon_row=%s horizon=%.9g,%.9g,%.9g dumps=%llu dump_ok=%d dump_pending=%d hold=%d "
         "edge_tris=%llu edge_baked=%llu drawn=%d,%d,%d,%d",
         op, action, live, SkyChoiceName(sSkyChoice), sSkyWhy, SkyName(WorldFog_Sky()), SkyName(sSkyMethod), now.fb,
         (unsigned long long)now.copies, now.copies != 0 && now.lastCopyRun == now.runs ? 1 : 0, p.size[0], p.size[1],
         p.origin[0], p.origin[1], now.msaa, row, p.horizon[0], p.horizon[1], p.horizon[2],
         (unsigned long long)now.dumps, now.lastDumpOk ? 1 : 0, now.dumpPending ? 1 : 0, now.hold ? 1 : 0,
         (unsigned long long)now.lastEdgeTris, (unsigned long long)now.lastEdgeBakedDraws, p.drawn[0], p.drawn[1],
         p.drawn[2], p.drawn[3]);
}

constexpr const char* kSkyUsage = "usage=sky_[status]|sky_auto|sky_edge|sky_all|sky_on|sky_off|sky_mirror|sky_clamp|"
                                  "sky_dump_[<path-prefix>]|sky_hold_on|sky_hold_off";

// `fog sky ...` (sturdy-bassoon#226): switch sky-colour fog live.
int32_t RunSky(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string action = args.size() < 2 ? "status" : args[1];
    if (action == "dump" && args.size() <= 3) {
        if (WorldFog_Sky() == WORLD_FOG_SKY_OFF && !WorldFog_ColourLive()) {
            // The dump is taken from a frame that copied the sky; with sky fog and the sky's colour off none does.
            Addf(lines, "op=sky result=error error=sky_off %s", kSkyUsage);
            return 1;
        }
        std::string prefix;
        if (args.size() == 3) {
            prefix = args[2];
        } else {
            const char* temp = std::getenv("TEMP");
            prefix = std::string(temp != nullptr ? temp : ".") + "/skyfog-" + std::to_string(++sSkyDumpCount);
        }
        Fast::SkyFogRequestDump(prefix);
        DescribeSky("sky", "dump", lines);
        return 0;
    }
    if (action == "hold") {
        if (args.size() != 3 || (args[2] != "on" && args[2] != "off")) {
            Addf(lines, "op=sky result=error error=bad_argument %s", kSkyUsage);
            return 1;
        }
        WorldFog_SetSkyHold(args[2] == "on" ? 1 : 0);
        DescribeSky("sky", "hold", lines);
        return 0;
    }
    if (args.size() > 2) {
        Addf(lines, "op=sky result=error error=bad_argument %s", kSkyUsage);
        return 1;
    }
    if (action == "status") {
        DescribeSky("sky", "status", lines);
        return 0;
    }
    // The scope words choose what draws; mirror and clamp only choose the method (sturdy-bassoon#230: under #226 they
    // also switched it on, which the default now does where it matters). `on` is `all`, as #226's scripts used it.
    if (action == "auto") {
        sSkyChoice = SkyChoice::Auto;
    } else if (action == "edge") {
        sSkyChoice = SkyChoice::Edge;
    } else if (action == "all" || action == "on") {
        sSkyChoice = SkyChoice::All;
    } else if (action == "off") {
        sSkyChoice = SkyChoice::Off;
    } else if (action == "mirror" || action == "clamp") {
        sSkyMethod = action == "mirror" ? WORLD_FOG_SKY_MIRROR : WORLD_FOG_SKY_CLAMP;
    } else {
        // The typed word is not echoed.
        Addf(lines, "op=sky result=error error=unknown_subcommand %s", kSkyUsage);
        return 1;
    }
    ApplySky();
    DescribeSky("sky", action.c_str(), lines);
    return 0;
}

// The sky's colour (sturdy-bassoon#229; WorldFog.h WorldFog_SetColour): depth fog toward the live sky's colour just
// above the horizon in place of the scene's fog colour. The console chooses auto (the default) or scene, and the band
// and smoothing; ApplyColour resolves them every frame. Kept across scene loads, like the sky's choice; nothing is
// saved.
enum class ColourChoice : uint8_t { Auto, Scene };
ColourChoice sColourChoice = ColourChoice::Auto;
float sColourLo = WORLD_FOG_COLOUR_DEFAULT_LO;
float sColourHi = WORLD_FOG_COLOUR_DEFAULT_HI;
float sColourTau = WORLD_FOG_COLOUR_DEFAULT_TAU;
// Why the live colour is what it is, for status: world_fog (auto, live), or why it is the scene's - console (`fog
// colour scene`), no_world_fog, pinned (a `fog world ... r g b` colour), sky_all (#226's per-pixel colour wins) or
// unsupported (the renderer has no reduction).
const char* sColourWhy = "no_world_fog";
uint32_t sColourLogCount = 0;
// `fog haze site before|after` (sturdy-bassoon#229): where the haze band draws under the sky's colour. Before the sun
// and moon (the default, the owner's pick on 2026-10-14: "definitely looks better"), as #168 drew it, with the
// previous frame's colour and the colour sampled above it; or after the sky copy, never in it. Kept across scene
// loads like the choice above.
bool sHazeSiteBefore = true;

WorldFogHaze LiveHaze();

// The live colour for this frame. Auto takes the sky's under world-unit fog unless the colour is pinned, sky fog's
// scope is all, or the renderer cannot. The sampled band is the chosen one, except with the haze band drawn before
// the copy, where it would be in the copy: then the sampled band starts that far above the haze's top instead.
// Run at OnPlayDrawBegin after ApplySky and the band (ApplyOverride), and straight after a console change.
void ApplyColour() {
    s16 start = 0;
    s16 end = 0;
    const bool world = WorldFog_Live(&start, &end) != 0;
    bool live = false;
    if (sColourChoice == ColourChoice::Scene) {
        sColourWhy = "console";
    } else if (!world) {
        sColourWhy = "no_world_fog";
    } else if (sOverride.active && sOverride.pinColor) {
        sColourWhy = "pinned";
    } else if (WorldFog_Sky() != WORLD_FOG_SKY_OFF && WorldFog_SkyScope() == WORLD_FOG_SKY_SCOPE_ALL) {
        sColourWhy = "sky_all";
    } else if (!WorldFog_ColourSupported()) {
        sColourWhy = "unsupported";
    } else {
        live = true;
        sColourWhy = "world_fog";
    }
    float lo = sColourLo;
    float hi = sColourHi;
    if (live && sHazeSiteBefore && !sHaze.off) {
        const WorldFogHaze haze = LiveHaze();
        lo = std::min(haze.height + haze.fade + sColourLo, 80.0f);
        hi = lo + (sColourHi - sColourLo);
    }
    static const u8 kNoFog[3] = { 0, 0, 0 };
    WorldFog_SetColour(live ? 1 : 0, gPlayState != nullptr ? gPlayState->lightCtx.fogColor : kNoFog, lo, hi,
                       sColourTau);
    // The band samples the colour only once the renderer is drawing it (WorldFog_ColourLive can still say no) and,
    // before the copy, only once a reduction has written the framebuffer at all: never a texel nothing wrote.
    const bool bandSky = WorldFog_ColourLive() && (!sHazeSiteBefore || Fast::SkyColourNow().reductions != 0);
    HorizonHaze_UseColour(bandSky ? 1 : 0, sHazeSiteBefore ? 0 : 1);
}

// The colour line, for `fog colour ...` and `fog status`. live= auto|scene what draws, choice= the console's, why= the
// reason; drawn= whether the last rendered frame's depth fog took the sky's colour. rgb= raw= samples= held= are the
// last readback (0..255, two decimals; none before one), taken at the end of the next rendered frame after a request,
// readout_run= its rendered frame and run= the newest - so read status twice for a fresh one. band= the chosen
// degrees, band_px= the live band in pixels above the horizon, rect= the copy texels the last reduction read (x0, y0,
// x1, y1: the 3D viewport cut to the scissor, so a letterbox's bars are left out - sturdy-bassoon#241), smooth= tau,
// k= and dt= the last reduction's step.
void DescribeColour(const char* op, const char* action, std::vector<std::string>& lines) {
    const Fast::SkyColourStatus& now = Fast::SkyColourNow();
    const Fast::SkyFogStatus& sky = Fast::SkyFogNow();
    char rgb[48] = "none";
    char raw[48] = "none";
    if (now.readoutOk) {
        std::snprintf(rgb, sizeof(rgb), "%.2f,%.2f,%.2f", now.readout.colour[0], now.readout.colour[1],
                      now.readout.colour[2]);
        std::snprintf(raw, sizeof(raw), "%.2f,%.2f,%.2f", now.readout.raw[0], now.readout.raw[1], now.readout.raw[2]);
    }
    const u8* scene = gPlayState != nullptr ? gPlayState->lightCtx.fogColor : nullptr;
    Addf(lines,
         "op=%s result=ok action=%s live=%s choice=%s why=%s drawn=%d rgb=%s raw=%s samples=%u held=%d "
         "readout_run=%llu run=%llu scene_rgb=%u,%u,%u band=%g,%g band_px=%.2f,%.2f rect=%d,%d,%d,%d smooth=%g "
         "k=%.5f dt=%.5f reductions=%llu fb=%d log=%s logged=%llu",
         op, action, WorldFog_ColourLive() ? "auto" : "scene", sColourChoice == ColourChoice::Auto ? "auto" : "scene",
         sColourWhy, now.live && now.lastRun == sky.runs ? 1 : 0, rgb, raw, now.readoutOk ? now.readout.samples : 0,
         now.readoutOk && now.readout.samples == 0 ? 1 : 0, (unsigned long long)now.readoutRun,
         (unsigned long long)sky.runs, scene != nullptr ? scene[0] : 0, scene != nullptr ? scene[1] : 0,
         scene != nullptr ? scene[2] : 0, sColourLo, sColourHi, now.last.loPx, now.last.hiPx, now.last.viewport[0],
         now.last.viewport[1], now.last.viewport[2], now.last.viewport[3], sColourTau, now.last.k,
         now.lastDt, (unsigned long long)now.reductions, gSkyColourFrameBuffer, now.logging ? "on" : "off",
         (unsigned long long)now.logged);
    Fast::SkyColourRequestReadout();
}

constexpr const char* kColourUsage = "usage=colour_[status]|colour_auto|colour_scene|colour_band_<lo>_<hi>(0..15)|"
                                     "colour_smooth_<seconds(0..5)>|colour_snap|colour_log_on_[path]|colour_log_off";

bool ParseFloat(const std::string& text, float lo, float hi, float* out) {
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(value) || value < lo || value > hi) {
        return false;
    }
    *out = value;
    return true;
}

// `fog colour ...` (sturdy-bassoon#229): the depth fog's colour source, live.
int32_t RunColour(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string action = args.size() < 2 ? "status" : args[1];
    if ((action == "status" || action == "auto" || action == "scene" || action == "snap") && args.size() <= 2) {
        if (action == "auto" || action == "scene") {
            sColourChoice = action == "auto" ? ColourChoice::Auto : ColourChoice::Scene;
        } else if (action == "snap") {
            Fast::SkyColourSnap();
        }
        ApplyColour();
        DescribeColour("colour", action.c_str(), lines);
        return 0;
    }
    if (action == "band") {
        float lo = 0.0f;
        float hi = 0.0f;
        if (args.size() != 4 || !ParseFloat(args[2], 0.0f, 15.0f, &lo) || !ParseFloat(args[3], 0.0f, 15.0f, &hi) ||
            hi <= lo) {
            Addf(lines, "op=colour result=error error=bad_argument %s", kColourUsage);
            return 1;
        }
        sColourLo = lo;
        sColourHi = hi;
        ApplyColour();
        DescribeColour("colour", "band", lines);
        return 0;
    }
    if (action == "smooth") {
        float tau = 0.0f;
        if (args.size() != 3 || !ParseFloat(args[2], 0.0f, 5.0f, &tau)) {
            Addf(lines, "op=colour result=error error=bad_argument %s", kColourUsage);
            return 1;
        }
        sColourTau = tau;
        ApplyColour();
        DescribeColour("colour", "smooth", lines);
        return 0;
    }
    if (action == "log") {
        const bool on = args.size() >= 3 && args[2] == "on";
        if (args.size() < 3 || args.size() > 4 || (!on && (args[2] != "off" || args.size() != 3))) {
            Addf(lines, "op=colour result=error error=bad_argument %s", kColourUsage);
            return 1;
        }
        std::string path;
        if (on) {
            if (args.size() == 4) {
                path = args[3];
            } else {
                const char* temp = std::getenv("TEMP");
                path = std::string(temp != nullptr ? temp : ".") + "/skycolour-" + std::to_string(++sColourLogCount) +
                       ".txt";
            }
        }
        if (!Fast::SkyColourLog(path)) {
            Addf(lines, "op=colour result=error error=log_open %s", kColourUsage);
            return 1;
        }
        DescribeColour("colour", "log", lines);
        return 0;
    }
    // The typed word is not echoed.
    Addf(lines, "op=colour result=error error=unknown_subcommand %s", kColourUsage);
    return 1;
}

// A degree value as a C float literal, as sceneTemplate.ts writes it: 4.0f, 2.25f.
std::string FloatLiteral(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", value);
    std::string out = text;
    if (out.find_first_of(".e") == std::string::npos) {
        out += ".0";
    }
    return out + "f";
}

// The height and fade the band draws with: the scene's, or the override's.
WorldFogHaze LiveHaze() {
    return sHaze.hasValues ? sHaze.values : WorldFog_SceneHaze();
}

// The haze line (sturdy-bassoon#168), for `fog haze ...` and `fog status`. haze= on|off is the switch, kind= whether
// world-unit fog is live (the band draws only then), drawn= whether the last frame drew it and draws= how many ticks
// have since boot, color= the colour it last drew with (none before the first), fog= the live fog colour, c= the
// scene's InitScene call for the live height and fade. drawn= and color= are the last frame's, so read them a frame
// after a change.
void DescribeHaze(const char* op, const char* action, const PlayState* play, std::vector<std::string>& lines) {
    s16 start = 0;
    s16 end = 0;
    const bool world = WorldFog_Live(&start, &end) != 0;
    const WorldFogHaze live = LiveHaze();
    const WorldFogHaze scene = WorldFog_SceneHaze();
    const HorizonHazeFrame* frame = HorizonHaze_LastFrame();
    char color[16] = "none";
    if (frame->draws != 0 && frame->skyColour) {
        std::snprintf(color, sizeof(color), "auto"); // the sky's colour, on the GPU (sturdy-bassoon#229)
    } else if (frame->draws != 0) {
        std::snprintf(color, sizeof(color), "%u,%u,%u", frame->color[0], frame->color[1], frame->color[2]);
    }
    Addf(lines,
         "op=%s result=ok action=%s haze=%s kind=%s drawn=%u draws=%u h=%g f=%g top=%g src=%s scene_h=%g "
         "scene_f=%g color=%s fog=%u,%u,%u c=WorldFog_SetHaze(%s,%s) colour_src=%s site=%s site_choice=%s",
         op, action, sHaze.off ? "off" : "on", world ? "world" : "vanilla", frame->drawn, frame->draws, live.height,
         live.fade, live.height + live.fade, sHaze.hasValues ? "override" : "scene", scene.height, scene.fade, color,
         play->lightCtx.fogColor[0], play->lightCtx.fogColor[1], play->lightCtx.fogColor[2],
         FloatLiteral(live.height).c_str(), FloatLiteral(live.fade).c_str(), frame->skyColour ? "sky" : "fog",
         frame->afterCopy ? "after_copy" : "before_sun", sHazeSiteBefore ? "before" : "after");
}

constexpr const char* kHazeUsage =
    "usage=haze_[status]|haze_off|haze_on|haze_scene|haze_<H(0..45)>_<F(0..45)>|haze_site_after|haze_site_before";

bool ParseHazeDegrees(const std::string& text, float* out) {
    return ParseFloat(text, 0.0f, WORLD_FOG_HAZE_MAX, out);
}

// `fog haze ...` (sturdy-bassoon#168): tune the horizon haze band live. Height and fade are degrees above the
// eye's horizontal plane.
int32_t RunHaze(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string action = args.size() < 2 ? "status" : args[1];
    if (action == "status" && args.size() <= 2) {
        DescribeHaze("haze", "status", gPlayState, lines);
        return 0;
    }
    if (action == "off" && args.size() == 2) {
        sHaze.off = true;
        DescribeHaze("haze", "off", gPlayState, lines);
        return 0;
    }
    if (action == "on" && args.size() == 2) {
        sHaze.off = false;
        DescribeHaze("haze", "on", gPlayState, lines);
        return 0;
    }
    if (action == "scene" && args.size() == 2) {
        sHaze = HazeOverride();
        DescribeHaze("haze", "scene", gPlayState, lines);
        return 0;
    }
    // Where the band draws under the sky's colour (sturdy-bassoon#229): after the copy (the default), or before the
    // sun and moon with the previous frame's colour, the colour then sampled above the band (ApplyColour).
    if (action == "site") {
        if (args.size() != 3 || (args[2] != "after" && args[2] != "before")) {
            Addf(lines, "op=haze result=error error=bad_argument %s", kHazeUsage);
            return 1;
        }
        sHazeSiteBefore = args[2] == "before";
        ApplyColour();
        DescribeHaze("haze", "site", gPlayState, lines);
        return 0;
    }
    if (!LooksNumeric(action)) {
        // Not one of the words and not a number: the typed word is not echoed.
        Addf(lines, "op=haze result=error error=unknown_subcommand %s", kHazeUsage);
        return 1;
    }
    WorldFogHaze values = { 0.0f, 0.0f };
    if (args.size() != 3 || !ParseHazeDegrees(args[1], &values.height) || !ParseHazeDegrees(args[2], &values.fade)) {
        Addf(lines, "op=haze result=error error=bad_argument %s", kHazeUsage);
        return 1;
    }
    sHaze.off = false;
    sHaze.hasValues = true;
    sHaze.values = values;
    DescribeHaze("haze", "set", gPlayState, lines);
    return 0;
}

constexpr const char* kRowsUsage = "usage=rows_<start(0..32766)>_<end(start+1..32767)>_<clip(100..32767)>";

// `fog rows <start> <end> <clip>` (sturdy-bassoon#227, a test aid): every row of the loaded scene's table becomes
// this one, in memory, until the next scene load. Unlike `fog world`, which writes the band and clip after
// Environment_Update, this goes through the scene's own path - the rows' range checks, the blend and
// Environment_Update's clamp - as an export of that row would. The usual line follows, with the override's
// band if one is active (`fog off` shows the rows).
int32_t RunRows(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t start = 0;
    int32_t end = 0;
    int32_t clip = 0;
    if (args.size() != 4 || !ParseInt(args[1], 0, kWorldMax - 1, &start) ||
        !ParseInt(args[2], start + 1, kWorldMax, &end) || !ParseInt(args[3], kFarMin, kWorldClipMax, &clip)) {
        Addf(lines, "op=rows result=error error=bad_argument %s", kRowsUsage);
        return 1;
    }
    if (!WorldFog_SetSceneRows(static_cast<s16>(start), static_cast<s16>(end), static_cast<s16>(clip))) {
        Addf(lines, "op=rows result=error error=no_world_rows %s", kRowsUsage);
        return 1;
    }
    Describe("rows", gPlayState, lines);
    return 0;
}

constexpr const char* kZNearUsage = "usage=znear_[status]|znear_off|znear_<n(1..60)>";

// `fog znear ...` (sturdy-bassoon#227): the line. znear= the view's near plane now, src= override|game.
void DescribeZNear(const char* action, std::vector<std::string>& lines) {
    Addf(lines, "op=znear result=ok action=%s znear=%.0f src=%s default=%d", action, gPlayState->view.zNear,
         sZNear != 0 ? "override" : "game", kZNearDefault);
}

// `fog znear <n>|off|status` (sturdy-bassoon#227): move the near clip plane, to judge distant z-fighting against
// close-up clipping. Written at OnPlayDrawBegin, before Play_Draw builds the perspective; nothing is saved.
int32_t RunZNear(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string action = args.size() < 2 ? "status" : args[1];
    if (action == "status" && args.size() <= 2) {
        DescribeZNear("status", lines);
        return 0;
    }
    if (action == "off" && args.size() == 2) {
        sZNear = 0;
        ApplyZNear();
        DescribeZNear("off", lines);
        return 0;
    }
    int32_t value = 0;
    if (args.size() != 2 || !ParseInt(action, kZNearMin, kZNearMax, &value)) {
        Addf(lines, "op=znear result=error error=bad_argument %s", kZNearUsage);
        return 1;
    }
    sZNear = value;
    ApplyZNear();
    DescribeZNear("set", lines);
    return 0;
}

} // namespace

int32_t DistanceFogConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? "status" : args[0];
    const bool isSet = LooksNumeric(sub);
    const char* op = isSet || sub == "world" ? "set" : sub.c_str();

    if (!isSet && sub != "status" && sub != "off" && sub != "world" && sub != "edge" && sub != "haze" &&
        sub != "sky" && sub != "colour" && sub != "rows" && sub != "znear") {
        // The typed word is not echoed: it is free text, and this line is parsed field by field.
        lines.push_back("op=unknown result=error error=unknown_subcommand "
                        "usage=status|off|<near>_<far>_[r_g_b]_[max=<m>]|world_<start>_<end>_[r_g_b]_[clip=<c>]|"
                        "edge_...|haze_...|sky_...|colour_...|rows_<start>_<end>_<clip>|znear_...");
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
        // The scene's world-unit rows (sturdy-bassoon#167), laid out like the light settings above: config 0
        // (0-3) and the storm (8-11), each c= pasteable as a WorldFogSetting row.
        if (WorldFog_HasScene()) {
            for (u8 i = 0; i < WORLD_FOG_SETTINGS; i = (i == 3 ? 8 : i + 1)) {
                const WorldFogSetting* row = WorldFog_SceneSetting(i);
                Addf(lines, "op=status world_setting=%u group=%s name=%s start=%d end=%d clip=%d c={%d,%d,%d}", i,
                     i < 4 ? "scene" : "storm", kSettingNames[i % 4], row->start, row->end, row->clip, row->start,
                     row->end, row->clip);
            }
        }
        // Boundary fog (sturdy-bassoon#169): the edge's own line, as `fog edge status` prints it.
        DescribeEdge("status", "status", gPlayState, lines);
        // The horizon haze (sturdy-bassoon#168): its own line, as `fog haze status` prints it.
        DescribeHaze("status", "status", gPlayState, lines);
        // Sky-colour fog (sturdy-bassoon#226): the sky's own line, as `fog sky status` prints it.
        DescribeSky("status", "status", lines);
        // The sky's colour (sturdy-bassoon#229): its own line, as `fog colour status` prints it.
        DescribeColour("status", "status", lines);
        return 0;
    }
    if (sub == "off") {
        sOverride.active = false;
        Describe("off", gPlayState, lines);
        return 0;
    }

    if (sub == "world") {
        return RunWorld(args, lines);
    }
    if (sub == "edge") {
        return RunEdge(args, lines);
    }
    if (sub == "haze") {
        return RunHaze(args, lines);
    }
    if (sub == "sky") {
        return RunSky(args, lines);
    }
    if (sub == "colour") {
        return RunColour(args, lines);
    }
    if (sub == "rows") {
        return RunRows(args, lines);
    }
    if (sub == "znear") {
        return RunZNear(args, lines);
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
    sOverride.world = false;
    sOverride.clipGiven = false;
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
    gPlayFogMax = sOverride.active && !sOverride.world ? sOverride.max : kVanillaMax;
    // Boundary fog (sturdy-bassoon#169): the scene's boundary unless `fog edge` overrides it, handed to the
    // renderer every frame like the band. It draws only under world-unit fog, which the renderer checks itself.
    const Edge edge = LiveEdge();
    if (!edge.Draws()) {
        WorldFog_UseNoBoundary();
    } else if (!sEdge.hasRect && !sEdge.hasRamp) {
        WorldFog_UseSceneBoundary();
    } else {
        WorldFog_UseBoundary(edge.rects, edge.count, edge.b0, edge.b1);
    }
    // The horizon haze (sturdy-bassoon#168): the scene's height and fade unless `fog haze` overrides them, handed
    // over every frame like the boundary. It draws only under world-unit fog, which HorizonHaze_Draw checks itself.
    HorizonHaze_Use(sHaze.off ? 0 : 1, LiveHaze());
    // Which kind of fog Play_SetFog emits this frame (sturdy-bassoon#167). The scene's own resolves in
    // WorldFog.cpp - its blended rows if it has them, else vanilla - and the override layers on top: a world
    // band in any scene, or vanilla fog even in a scene with rows. Written every frame, like the max.
    if (!sOverride.active || gPlayState == nullptr) {
        WorldFog_UseScene();
    } else {
        const Band band = LiveBand(gPlayState);
        if (band.world) {
            WorldFog_UseBand(band.start, band.end);
        } else {
            WorldFog_UseVanilla();
            gPlayState->lightCtx.fogNear = band.near;
        }
        gPlayState->lightCtx.fogFar = band.far;
        for (int i = 0; i < 3; i++) {
            gPlayState->lightCtx.fogColor[i] = band.color[i];
        }
    }
    // Sky-colour fog and its edge layer (sturdy-bassoon#226, #230), from the band and boundary just resolved, before
    // Play_Draw decides whether to take the copy (WorldFog_SkyCopy).
    ApplySky();
    // The sky's colour (sturdy-bassoon#229), from the band, the colour and the sky just resolved: lightCtx.fogColor is
    // final here, and is what Play_SetFog emits next.
    ApplyColour();
    // `fog znear` (sturdy-bassoon#227), before Play_Draw builds the perspective from view.zNear.
    ApplyZNear();
}

void RegisterDistanceFog() {
    COND_HOOK(OnPlayDrawBegin, true, ApplyOverride);
    // A scene load hands fog back, as the reg-editor override this replaced did (Environment_Init): a band
    // tuned for one map is not assumed for the next.
    COND_HOOK(OnSceneInit, true, [](int16_t) {
        sOverride.active = false;
        sEdge = EdgeOverride();
        sHaze = HazeOverride();
        sZNear = 0; // sZNearApplied stays, so the next frame puts 10 back
        // The sky's colour starts from this scene's own fog colour if none of its band is on screen at first (a
        // letterboxed or downward first frame), never from the last scene's (sturdy-bassoon#241).
        Fast::SkyColourReset();
    });
}

RegisterShipInitFunc initFunc(RegisterDistanceFog, {});

// --- the human sink: the `fog` console command ---------------------------------------------------

const ConsoleSink::Command distanceFogCommand(
    "fog", DistanceFogConsole_Run,
    "Tune the scene's distance fog live (sturdy-bassoon#144): <near> <far> [r g b] [max=<m>] | "
    "world <start> <end> [r g b] [clip=<c>] | edge ... | haze ... | rows ... | znear ... | status | off. "
    "near is fog-space 0..1000 (nonlinear: 996, the most a scene can have, starts ~2,000 units out at far 12800; 1000 "
    "is no fog), far is world units 100..12800 and also the far clip. Without r g b the colour keeps following "
    "time of day and weather; lights are never touched. max is the fog-space value where fog reaches 100% "
    "(sturdy-bassoon#169): 1000, the default, completes it at the clip; lower completes it sooner, down to "
    "near+4. Below near 980 the sky fades to the fog colour. world (sturdy-bassoon#167) draws fog linear in "
    "view depth instead, clear to start and complete at end, both world units, in any scene; clip is the far "
    "clip (100..32767, default the scene's). edge (sturdy-bassoon#169) tunes boundary fog, which under world fog "
    "also fogs geometry by how far it lies past the playable area: edge <b0> <b1> ramps it from b0 units past the "
    "edge to full at b1; edge rect <x0> <z0> <x1> <z1> [b0 b1] sets one rectangle by hand, in any scene; edge off "
    "turns it off, edge scene hands it back, edge status reads it. haze (sturdy-bassoon#168) tunes the horizon haze "
    "band, which under world fog lays the fog colour over the sky's lowest part, under the world: haze <H> <F> makes "
    "it opaque up to H degrees above the horizon and fades it out over F more; haze off / on / scene / status. sky "
    "(sturdy-bassoon#226, #230, DX11) makes fogged pixels blend toward the sky behind them instead of the fog "
    "colour: sky edge (only boundary fog's share, so the ring past the playable area dissolves into the sky while "
    "depth fog keeps its colour), sky all (every fogged pixel; on is the same), sky off, sky auto (the default: edge "
    "under world fog with a boundary, off elsewhere); sky mirror (fogged ground takes the skybox's mirrored sky) or "
    "sky clamp (the sky just above the horizon) picks the method; sky status, sky dump [path-prefix] (writes the sky "
    "copy and the frame as PPMs, by default in the TEMP folder), sky hold on|off (keep fogging toward the copy "
    "already taken, for captures a moment apart); the choice is kept across scene loads. colour (sturdy-bassoon#229, "
    "DX11) gives depth fog the live sky's colour just above the horizon in place of the scene's fog colour: colour "
    "auto (the default, under world fog) or colour scene; colour band <lo> <hi> (degrees above the horizon), colour "
    "smooth <seconds>, colour snap, colour log on [path] | off, colour status. haze site after|before chooses where "
    "the band draws under it. rows <start> <end> <clip> (sturdy-bassoon#227, a test aid) makes every world-unit row "
    "of the loaded scene this one until the next scene load, through the scene's own path rather than over it. znear "
    "<n> (1..60) moves the near clip plane from 10, to judge distant z-fighting against close-up clipping; znear off "
    "/ status. status prints the live "
    "band, where it starts, and each light setting's fog as C to paste into a scene. A scene load hands fog back "
    "(not sky); nothing is saved.",
    { { "near|world|edge|haze|sky|colour|rows|znear|status|off", Ship::ArgumentType::TEXT, true },
      { "far|start|H|n|auto|edge|all|on|off|mirror|clamp|dump|scene|band|smooth|snap|log|site",
        Ship::ArgumentType::TEXT, true },
      { "r|end|F|path-prefix|lo|seconds|after|before", Ship::ArgumentType::TEXT, true },
      { "g|r", Ship::ArgumentType::TEXT, true },
      { "b|g", Ship::ArgumentType::TEXT, true },
      { "max=<m>|b", Ship::ArgumentType::TEXT, true },
      { "clip=<c>|b0", Ship::ArgumentType::TEXT, true },
      { "b1", Ship::ArgumentType::TEXT, true } });

} // namespace
