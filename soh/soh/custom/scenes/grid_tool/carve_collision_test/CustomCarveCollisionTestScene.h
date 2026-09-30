#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomCarveCollisionTestScene_IsCustomScene(s32 sceneId);
int  CustomCarveCollisionTestScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomCarveCollisionTestScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
