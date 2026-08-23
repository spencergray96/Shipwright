#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomLumbridgeCastleTest1Scene_IsCustomScene(s32 sceneId);
int  CustomLumbridgeCastleTest1Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomLumbridgeCastleTest1Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
