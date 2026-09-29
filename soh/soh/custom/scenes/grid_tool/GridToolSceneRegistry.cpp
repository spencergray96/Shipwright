#include "GridToolSceneRegistry.h"
#include "global.h"
#include <spdlog/spdlog.h>

// Forward declarations for every grid-tool-exported scene's TrySpawn/InitRoom functions.
// Appended by tools/grid-scene-tool's exporter; do not edit by hand within the markers.
// BEGIN GRID TOOL SCENE INCLUDES
#include "generated/GridToolSceneIncludes.inc"
// END GRID TOOL SCENE INCLUDES

// Local fixtures (sturdy-bassoon tools/fixtures): generated scenes too big to commit - #143's 4.1M-triangle
// map is the first - kept in a tree without living in git. Each occupies one of the reserved
// SCENE_LOCAL_FIXTURE_n slots in scene_table.h, so its scene id and entrance never move and the tracked
// tables never change. Its folder (grid_tool/local_<slug>/) and these two files are gitignored and
// written by `tools/fixtures/fixture.py install`; a tree with no fixture installed has neither file.
#if __has_include("generated/LocalFixtureIncludes.inc")
#include "generated/LocalFixtureIncludes.inc"
#endif

typedef int (*GridToolTrySpawnFn)(PlayState*, s32, s32);
typedef void (*GridToolInitRoomFn)(PlayState*, RoomContext*);

typedef struct {
    s32 sceneId;
    GridToolTrySpawnFn trySpawn;
    GridToolInitRoomFn initRoom;
} GridToolSceneEntry;

// One row per scene exported by the grid tool. Appended by the exporter; do not edit by
// hand within the markers - re-running an export for the same project replaces its row.
static const GridToolSceneEntry sGridToolScenes[] = {
    { -1, NULL, NULL }, // sentinel: keeps the array non-empty before any scene is exported
    // BEGIN GRID TOOL EXPORTS
#include "generated/GridToolSceneManifest.inc"
    // END GRID TOOL EXPORTS
#if __has_include("generated/LocalFixtureManifest.inc")
#include "generated/LocalFixtureManifest.inc"
#endif
};

static const GridToolSceneEntry* GridToolSceneRegistry_Find(s32 sceneId) {
    for (size_t i = 0; i < ARRAY_COUNT(sGridToolScenes); i++) {
        if (sGridToolScenes[i].sceneId == sceneId) {
            return &sGridToolScenes[i];
        }
    }
    return NULL;
}

static bool IsLocalFixtureSlot(s32 sceneId) {
    return sceneId >= SCENE_LOCAL_FIXTURE_0 && sceneId <= SCENE_LOCAL_FIXTURE_7;
}

// A reserved slot counts even when it is empty: Play_Init reads this to skip the vanilla four-entrance
// layer offset, and without it an adult warping to an empty slot's entrance lands three rows on, in
// another slot.
extern "C" int GridToolSceneRegistry_IsCustomScene(s32 sceneId) {
    return GridToolSceneRegistry_Find(sceneId) != NULL || IsLocalFixtureSlot(sceneId);
}

extern "C" int GridToolSceneRegistry_TrySpawn(PlayState* play, s32 sceneId, s32 spawn) {
    const GridToolSceneEntry* entry = GridToolSceneRegistry_Find(sceneId);
    if (entry == NULL || entry->trySpawn == NULL) {
        // An empty local-fixture slot. OTRPlay_SpawnScene then finds no scene resource by that name
        // and falls back to Dodongo's Cavern, which is survivable but says nothing about why.
        if (IsLocalFixtureSlot(sceneId)) {
            SPDLOG_ERROR("Local fixture slot {} is empty in this build: install one with sturdy-bassoon's "
                         "tools/fixtures/fixture.py and rebuild",
                         sceneId - SCENE_LOCAL_FIXTURE_0);
        }
        return 0;
    }
    return entry->trySpawn(play, sceneId, spawn);
}

extern "C" int GridToolSceneRegistry_TryInitRoom(PlayState* play, RoomContext* roomCtx) {
    const GridToolSceneEntry* entry = GridToolSceneRegistry_Find(play->sceneNum);
    if (entry == NULL || entry->initRoom == NULL) {
        return 0;
    }
    entry->initRoom(play, roomCtx);
    return 1;
}
