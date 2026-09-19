#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomLumbridgeScale44Scene_IsCustomScene(s32 sceneId);
int  CustomLumbridgeScale44Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomLumbridgeScale44Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
