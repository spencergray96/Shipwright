#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomTerrainF2pRooms2x2Scene_IsCustomScene(s32 sceneId);
int  CustomTerrainF2pRooms2x2Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomTerrainF2pRooms2x2Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
