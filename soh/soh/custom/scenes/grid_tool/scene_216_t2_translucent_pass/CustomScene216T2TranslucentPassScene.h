#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomScene216T2TranslucentPassScene_IsCustomScene(s32 sceneId);
int  CustomScene216T2TranslucentPassScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomScene216T2TranslucentPassScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
