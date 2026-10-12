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
// Every scene spawn starts here (Play_SpawnScene), so only a scene that sets rows has them - and the same for a
// boundary (sturdy-bassoon#169, below), which this clears in the renderer too.
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

// Boundary fog (sturdy-bassoon#169): under world-unit fog, a vertex's fog is the larger of its depth fog and a
// ramp by how far its world position lies past the playable area - clear out to b0 world units outside it,
// complete at b1. The area is the grid tool's playable rectangles (playableArea.ts); one ramp per scene, not
// per time of day. The renderer computes it in GfxSpVertex and in the bake's patched vertex shader alike
// (libultraship interpreter.h, Fast::FogBoundarySet); it never draws under vanilla fog.
typedef struct WorldFogRect {
    int16_t x0, z0, x1, z1; // world units, x0 < x1, z0 < z1
} WorldFogRect;

// The most rectangles the renderer draws with (libultraship's FOG_BOUNDARY_MAX_RECTS). The exporters write the
// playable area's bounding box past it, and WorldFog_SetBoundary does the same.
#define WORLD_FOG_BOUNDARY_MAX_RECTS 8

// A scene's boundary, from its InitScene after the light settings. 0 <= b0 < b1. WorldFog_ClearScene clears it.
// A bad rectangle is dropped and a bad ramp drawn as b1 = b0 + 1, with an error in the log: compiled-in scene data,
// so an exporter bug.
void WorldFog_SetBoundary(const WorldFogRect* rects, uint8_t count, int16_t b0, int16_t b1);
// The scene's rectangles (NULL and 0 without a boundary), and its ramp.
uint8_t WorldFog_SceneBoundary(const WorldFogRect** rects, int16_t* b0, int16_t* b1);

// Which boundary the renderer draws this frame, set at OnPlayDrawBegin (DistanceFogConsole.cpp's ApplyOverride)
// like the band: WorldFog_UseSceneBoundary for the scene's own, and a `fog edge` override on top with
// WorldFog_UseBoundary (any rectangles and ramp) or WorldFog_UseNoBoundary (none, even in a scene with one).
void WorldFog_UseSceneBoundary(void);
void WorldFog_UseBoundary(const WorldFogRect* rects, uint8_t count, int16_t b0, int16_t b1);
void WorldFog_UseNoBoundary(void);
// The distance past `rects` at world (x, z), by the renderer's own function (Fast::FogBoundaryDistance): the
// least over the rectangles of each axis's gap outside, hypot-ed; 0 inside, and 0 with none.
float WorldFog_DistancePast(const WorldFogRect* rects, uint8_t count, float x, float z);

// Sky-colour fog (sturdy-bassoon#226, an experiment): fogged pixels blend toward the sky behind them - a copy of the
// frame taken just before the rooms draw - instead of toward the fog colour, so fully fogged geometry is the sky,
// clouds and all. Only the colour changes; the fog factor (vanilla or world-unit, the boundary term, shade alpha) is
// untouched. DX11 only (libultraship interpreter.h, Fast::SkyFogSet). A console setting (`fog sky`), off at launch and
// kept across scene loads; nothing is saved.
//   mirror: the copy as it is, so fogged ground below the horizon takes the skybox's mirrored sky
//   clamp:  a pixel below the horizon takes the sky just above the horizon, in its own column
#define WORLD_FOG_SKY_OFF 0
#define WORLD_FOG_SKY_MIRROR 1
#define WORLD_FOG_SKY_CLAMP 2
void WorldFog_SetSky(uint8_t mode);
uint8_t WorldFog_Sky(void);
// `fog sky hold` (a measurement aid): keep fogging toward the copy already taken instead of a fresh one each frame,
// so two captures a moment apart see the same sky in their fog (Fast::SkyFogHold).
void WorldFog_SetSkyHold(uint8_t hold);
// Play_Draw's copy point, after the lightning flash and before the rooms (z_play.c): while sky fog is on, emits the
// copy of the frame into gSkyFogFrameBuffer - a bare gDPCopyFB, which changes no RDP state the rooms inherit. With it
// off it emits nothing, so the display list is exactly today's.
union Gfx;
void WorldFog_SkyCopy(union Gfx** gfx);

#ifdef __cplusplus
}
#endif

#endif // SOH_WORLD_FOG_H
