#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomStoreyHeightsInGame178Scene_IsCustomScene(s32 sceneId);
int  CustomStoreyHeightsInGame178Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomStoreyHeightsInGame178Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
