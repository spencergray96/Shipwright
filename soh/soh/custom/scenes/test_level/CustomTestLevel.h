#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomTestLevel_IsCustomScene(s32 sceneId);
int  CustomTestLevel_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomTestLevel_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
