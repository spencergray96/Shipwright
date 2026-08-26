#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomTerrainF2pGreyboxMergedScene_IsCustomScene(s32 sceneId);
int  CustomTerrainF2pGreyboxMergedScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomTerrainF2pGreyboxMergedScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
