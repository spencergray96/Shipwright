#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomScene203StillTorchScene_IsCustomScene(s32 sceneId);
int  CustomScene203StillTorchScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomScene203StillTorchScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
