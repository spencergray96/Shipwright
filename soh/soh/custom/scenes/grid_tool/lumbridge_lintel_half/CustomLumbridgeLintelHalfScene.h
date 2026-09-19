#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomLumbridgeLintelHalfScene_IsCustomScene(s32 sceneId);
int  CustomLumbridgeLintelHalfScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomLumbridgeLintelHalfScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
