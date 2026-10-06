#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomHandsClimb179BenchCopyScene_IsCustomScene(s32 sceneId);
int  CustomHandsClimb179BenchCopyScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomHandsClimb179BenchCopyScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
