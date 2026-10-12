#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomKindBenchScene_IsCustomScene(s32 sceneId);
int  CustomKindBenchScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomKindBenchScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
