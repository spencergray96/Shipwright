#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 if sceneId is a registered grid-tool scene, 0 otherwise.
int GridToolSceneRegistry_IsCustomScene(s32 sceneId);

// Dispatches to a grid-tool-exported scene's spawn intercept, if `sceneId` is registered.
// Returns 1 (handled) or 0 (not a grid-tool scene, caller should fall through to OTR loading).
int GridToolSceneRegistry_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);

// Dispatches to a grid-tool-exported scene's compiled-in room init, if the current scene
// (play->sceneNum) is registered. Returns 1 (handled) or 0 (not a grid-tool scene).
int GridToolSceneRegistry_TryInitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
