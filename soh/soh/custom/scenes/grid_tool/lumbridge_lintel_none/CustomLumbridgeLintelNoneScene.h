#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomLumbridgeLintelNoneScene_IsCustomScene(s32 sceneId);
int  CustomLumbridgeLintelNoneScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomLumbridgeLintelNoneScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
