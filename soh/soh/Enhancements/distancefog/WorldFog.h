#ifndef SOH_WORLD_FOG_H
#define SOH_WORLD_FOG_H

#include <stdint.h>

// World-unit distance fog (sturdy-bassoon#167): fog linear in view depth, clear out to `start` and complete
// at `end`, both in world units, per scene. Vanilla fog is linear in post-divide depth instead, starts at
// most ~2,090 units out and only completes at the far clip (docs/reference/ENGINE_BUDGETS.md).
//
// The pieces:
//   - a scene's side table: a WorldFogSetting per light setting, written by the grid tool's exporters
//     (sceneTemplate.ts) and handed over from the scene's InitScene with WorldFog_SetScene. A scene that
//     never calls it - every vanilla scene - keeps vanilla fog. EnvLightSettings is not touched;
//   - Environment_Update (z_kankyo.c) blends the scene's rows by time of day and weather exactly as it
//     blends fogNear/fogFar, into gWorldFogScene, and takes the far clip from it;
//   - Play_SetFog (z_play.c) asks WorldFog_Live which band to draw this frame and emits gSPFogWorld
//     (libultraship gbi.h) in place of gSPFogPosition when there is one. The renderer computes the factor
//     in GfxSpVertex and in the bake's patched vertex shader alike.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WorldFogSetting {
    int16_t start; // view depth, world units, where fog begins (0%)
    int16_t end;   // where it is complete (100%); start < end
    int16_t clip;  // the far clip (zFar) under this setting, 100..12800
} WorldFogSetting;

// The table is indexed like the light settings a custom scene has once it has spawned
// (CustomSceneLighting_AddDefaultStorm, sturdy-bassoon#164): 0-3 config 0 (dawn, day, dusk, night), 4-7 a
// copy of them, 8-11 config 2, the Song of Storms' gloomy sky. So the light-setting indices the blend reads
// index both tables.
#define WORLD_FOG_SETTINGS 12

// A scene's rows, from its InitScene. count 4 (dawn, day, dusk, night; the storm reuses them) or 8 (those,
// then the storm's four). Any other count, or a NULL table, leaves the scene on vanilla fog.
void WorldFog_SetScene(const WorldFogSetting* rows, uint8_t count);
// Every scene spawn starts here (Play_SpawnScene), so only a scene that sets rows has them.
void WorldFog_ClearScene(void);
// 1 when the loaded scene has rows.
uint8_t WorldFog_HasScene(void);
// The row for light setting `index`, or NULL without rows. An index past the table reads its time-of-day
// row (index % 4), the fallback Environment_UsableLightConfig gives the light settings themselves.
const WorldFogSetting* WorldFog_SceneSetting(uint8_t index);

// The scene's band this frame, which Environment_Update blends; meaningful while WorldFog_HasScene().
extern WorldFogSetting gWorldFogScene;

// What Play_SetFog draws this frame, resolved at OnPlayDrawBegin (DistanceFogConsole.cpp's ApplyOverride):
// WorldFog_UseScene for the scene's own fog, and a console override layered on top with WorldFog_UseBand
// (a world band in any scene) or WorldFog_UseVanilla (vanilla fog even in a scene with rows).
void WorldFog_UseScene(void);
void WorldFog_UseBand(int16_t start, int16_t end);
void WorldFog_UseVanilla(void);
// 1 and the band to emit when this frame draws world-unit fog, 0 for vanilla fog.
uint8_t WorldFog_Live(int16_t* start, int16_t* end);

#ifdef __cplusplus
}
#endif

#endif // SOH_WORLD_FOG_H
