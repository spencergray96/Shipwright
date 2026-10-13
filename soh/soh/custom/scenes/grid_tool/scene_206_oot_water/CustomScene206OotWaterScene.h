#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomScene206OotWaterScene_IsCustomScene(s32 sceneId);
int  CustomScene206OotWaterScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomScene206OotWaterScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
