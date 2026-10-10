#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomF5Cellar173Scene_IsCustomScene(s32 sceneId);
int  CustomF5Cellar173Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomF5Cellar173Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
