#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomGridTestMap6Scene_IsCustomScene(s32 sceneId);
int  CustomGridTestMap6Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomGridTestMap6Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
