#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomCastleCamBothScene_IsCustomScene(s32 sceneId);
int  CustomCastleCamBothScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomCastleCamBothScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
