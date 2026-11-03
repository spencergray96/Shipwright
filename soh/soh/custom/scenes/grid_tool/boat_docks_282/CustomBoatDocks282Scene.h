#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomBoatDocks282Scene_IsCustomScene(s32 sceneId);
int  CustomBoatDocks282Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomBoatDocks282Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
