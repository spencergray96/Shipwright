#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomLedgeHopRetestScene_IsCustomScene(s32 sceneId);
int  CustomLedgeHopRetestScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomLedgeHopRetestScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
