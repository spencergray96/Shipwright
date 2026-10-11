#include "WorldFog.h"

#include <algorithm>

#include <fast/interpreter.h>
#include <spdlog/spdlog.h>

static_assert(WORLD_FOG_BOUNDARY_MAX_RECTS == Fast::FOG_BOUNDARY_MAX_RECTS,
              "WorldFog.h's rectangle cap is the renderer's (and sceneTemplate.ts's FOG_BOUNDARY_MAX_RECTS)");

namespace {

// Environment_Update clamps a scene's far clip to 12800 (z_kankyo.c, "Adjust fog near and far"); the
// console's far takes 100 and up.
constexpr int16_t kClipMin = 100;
constexpr int16_t kClipMax = 12800;

WorldFogSetting sRows[WORLD_FOG_SETTINGS];
bool sHasScene = false;

bool sLive = false;
int16_t sLiveStart = 0;
int16_t sLiveEnd = 0;

// The table is compiled-in scene data, so a bad row is an exporter bug: draw something sane and say so.
WorldFogSetting Sanitized(const WorldFogSetting& in, int index) {
    WorldFogSetting out = in;
    out.start = std::clamp<int16_t>(out.start, 0, INT16_MAX - 1); // leaves room for end = start + 1
    out.end = std::max<int16_t>(out.end, static_cast<int16_t>(out.start + 1));
    out.clip = std::clamp<int16_t>(out.clip, kClipMin, kClipMax);
    if (out.start != in.start || out.end != in.end || out.clip != in.clip) {
        SPDLOG_WARN("[worldfog] row {} ({}, {}, {}) is out of range; drawing ({}, {}, {})", index, in.start, in.end,
                    in.clip, out.start, out.end, out.clip);
    }
    return out;
}

// The scene's boundary (sturdy-bassoon#169).
WorldFogRect sBoundary[WORLD_FOG_BOUNDARY_MAX_RECTS];
uint8_t sBoundaryCount = 0;
int16_t sBoundaryB0 = 0;
int16_t sBoundaryB1 = 1;

// The renderer's form of a boundary: float rectangles, at most the cap.
uint8_t ToFloats(const WorldFogRect* rects, uint8_t count, float (*out)[4]) {
    count = rects == nullptr ? 0 : std::min<uint8_t>(count, WORLD_FOG_BOUNDARY_MAX_RECTS);
    for (uint8_t i = 0; i < count; i++) {
        out[i][0] = rects[i].x0;
        out[i][1] = rects[i].z0;
        out[i][2] = rects[i].x1;
        out[i][3] = rects[i].z1;
    }
    return count;
}

// Hands rectangles and a ramp to the renderer, which draws with them while the RSP is in world-unit fog.
void PushBoundary(const WorldFogRect* rects, uint8_t count, int16_t b0, int16_t b1) {
    float out[WORLD_FOG_BOUNDARY_MAX_RECTS][4];
    count = ToFloats(rects, count, out);
    Fast::FogBoundarySet(out, count, b0, b1);
}

} // namespace

extern "C" {

WorldFogSetting gWorldFogScene = { 0, 1, kClipMax };

void WorldFog_SetScene(const WorldFogSetting* rows, uint8_t count) {
    sHasScene = false;
    if (rows == nullptr || (count != 4 && count != 8)) {
        SPDLOG_ERROR("[worldfog] a scene handed {} rows; it keeps vanilla fog", count);
        return;
    }
    // The light settings' own layout (CustomSceneLighting_AddDefaultStorm): config 0, a copy of it as
    // config 1, then the storm as config 2 - the scene's own four if it gave none.
    for (int i = 0; i < 4; i++) {
        sRows[i] = Sanitized(rows[i], i);
        sRows[4 + i] = sRows[i];
        sRows[8 + i] = count == 8 ? Sanitized(rows[4 + i], 4 + i) : sRows[i];
    }
    gWorldFogScene = sRows[1]; // until Environment_Update first blends it
    sHasScene = true;
}

void WorldFog_ClearScene(void) {
    sHasScene = false;
    sBoundaryCount = 0;
    Fast::FogBoundaryClear();
}

uint8_t WorldFog_HasScene(void) {
    return sHasScene ? 1 : 0;
}

const WorldFogSetting* WorldFog_SceneSetting(uint8_t index) {
    if (!sHasScene) {
        return nullptr;
    }
    return &sRows[index < WORLD_FOG_SETTINGS ? index : index % 4];
}

void WorldFog_UseScene(void) {
    sLive = sHasScene;
    sLiveStart = gWorldFogScene.start;
    sLiveEnd = gWorldFogScene.end;
}

void WorldFog_UseBand(int16_t start, int16_t end) {
    sLive = true;
    sLiveStart = start;
    sLiveEnd = end;
}

void WorldFog_UseVanilla(void) {
    sLive = false;
}

uint8_t WorldFog_Live(int16_t* start, int16_t* end) {
    if (!sLive) {
        return 0;
    }
    *start = sLiveStart;
    *end = sLiveEnd;
    return 1;
}

void WorldFog_SetBoundary(const WorldFogRect* rects, uint8_t count, int16_t b0, int16_t b1) {
    sBoundaryCount = 0;
    if (rects == nullptr) {
        count = 0;
    }
    WorldFogRect kept[UINT8_MAX]; // count is a uint8_t
    uint8_t n = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (rects[i].x0 < rects[i].x1 && rects[i].z0 < rects[i].z1) {
            kept[n++] = rects[i];
        } else {
            SPDLOG_ERROR("[worldfog] boundary rectangle {} ({}, {}, {}, {}) is empty or reversed; dropped", i,
                         rects[i].x0, rects[i].z0, rects[i].x1, rects[i].z1);
        }
    }
    if (n > WORLD_FOG_BOUNDARY_MAX_RECTS) {
        // The exporters' own rule (sceneTemplate.ts fogBoundaryRects): the bounding box never fogs playable ground.
        WorldFogRect box = kept[0];
        for (uint8_t i = 1; i < n; i++) {
            box.x0 = std::min(box.x0, kept[i].x0);
            box.z0 = std::min(box.z0, kept[i].z0);
            box.x1 = std::max(box.x1, kept[i].x1);
            box.z1 = std::max(box.z1, kept[i].z1);
        }
        SPDLOG_ERROR("[worldfog] a scene handed {} boundary rectangles, past the cap of {}; drawing their bounding box",
                     n, WORLD_FOG_BOUNDARY_MAX_RECTS);
        kept[0] = box;
        n = 1;
    }
    if (b0 < 0 || b1 <= b0) {
        const int16_t safeB0 = std::clamp<int16_t>(b0, 0, INT16_MAX - 1); // leaves room for b1 = b0 + 1
        SPDLOG_ERROR("[worldfog] boundary ramp ({}, {}) is out of range; drawing ({}, {})", b0, b1, safeB0, safeB0 + 1);
        b0 = safeB0;
        b1 = static_cast<int16_t>(safeB0 + 1);
    }
    std::copy(kept, kept + n, sBoundary);
    sBoundaryCount = n;
    sBoundaryB0 = b0;
    sBoundaryB1 = b1;
}

uint8_t WorldFog_SceneBoundary(const WorldFogRect** rects, int16_t* b0, int16_t* b1) {
    *rects = sBoundaryCount != 0 ? sBoundary : nullptr;
    *b0 = sBoundaryB0;
    *b1 = sBoundaryB1;
    return sBoundaryCount;
}

void WorldFog_UseSceneBoundary(void) {
    PushBoundary(sBoundary, sBoundaryCount, sBoundaryB0, sBoundaryB1);
}

void WorldFog_UseBoundary(const WorldFogRect* rects, uint8_t count, int16_t b0, int16_t b1) {
    PushBoundary(rects, count, b0, b1);
}

void WorldFog_UseNoBoundary(void) {
    Fast::FogBoundaryClear();
}

float WorldFog_DistancePast(const WorldFogRect* rects, uint8_t count, float x, float z) {
    Fast::FogBoundary boundary = {};
    boundary.count = ToFloats(rects, count, boundary.rects);
    return Fast::FogBoundaryDistance(boundary, x, z);
}

} // extern "C"
