#include "WorldFog.h"

#include <algorithm>

#include <spdlog/spdlog.h>

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

} // extern "C"
