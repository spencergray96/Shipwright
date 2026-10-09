#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomStairsWarpsF3173Scene_IsCustomScene(s32 sceneId);
int  CustomStairsWarpsF3173Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomStairsWarpsF3173Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
