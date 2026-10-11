#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomScene216WWobbleImportScene_IsCustomScene(s32 sceneId);
int  CustomScene216WWobbleImportScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomScene216WWobbleImportScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
