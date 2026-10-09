#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomStoreyClasses178S2Scene_IsCustomScene(s32 sceneId);
int  CustomStoreyClasses178S2Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomStoreyClasses178S2Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
