#include "GridToolSceneRegistry.h"
#include "global.h"

// Forward declarations for every grid-tool-exported scene's TrySpawn/InitRoom functions.
// Appended by tools/grid-scene-tool's exporter; do not edit by hand within the markers.
// BEGIN GRID TOOL SCENE INCLUDES
#include "generated/GridToolSceneIncludes.inc"
// END GRID TOOL SCENE INCLUDES

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
};

static const GridToolSceneEntry* GridToolSceneRegistry_Find(s32 sceneId) {
    for (size_t i = 0; i < ARRAY_COUNT(sGridToolScenes); i++) {
        if (sGridToolScenes[i].sceneId == sceneId) {
            return &sGridToolScenes[i];
        }
    }
    return NULL;
}

extern "C" int GridToolSceneRegistry_TrySpawn(PlayState* play, s32 sceneId, s32 spawn) {
    const GridToolSceneEntry* entry = GridToolSceneRegistry_Find(sceneId);
    if (entry == NULL || entry->trySpawn == NULL) {
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
