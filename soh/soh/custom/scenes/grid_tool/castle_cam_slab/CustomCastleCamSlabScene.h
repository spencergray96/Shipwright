#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomCastleCamSlabScene_IsCustomScene(s32 sceneId);
int  CustomCastleCamSlabScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomCastleCamSlabScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
