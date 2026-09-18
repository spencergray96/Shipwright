#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomRsAreaP3Scene_IsCustomScene(s32 sceneId);
int  CustomRsAreaP3Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomRsAreaP3Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
