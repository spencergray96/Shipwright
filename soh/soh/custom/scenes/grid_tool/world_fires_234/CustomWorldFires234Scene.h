#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomWorldFires234Scene_IsCustomScene(s32 sceneId);
int  CustomWorldFires234Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomWorldFires234Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
