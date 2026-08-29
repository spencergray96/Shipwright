#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomCastleCamCeilingScene_IsCustomScene(s32 sceneId);
int  CustomCastleCamCeilingScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomCastleCamCeilingScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
