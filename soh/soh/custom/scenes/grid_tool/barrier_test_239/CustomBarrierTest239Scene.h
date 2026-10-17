#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomBarrierTest239Scene_IsCustomScene(s32 sceneId);
int  CustomBarrierTest239Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomBarrierTest239Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
