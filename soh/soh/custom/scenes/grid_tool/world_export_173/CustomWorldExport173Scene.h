#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomWorldExport173Scene_IsCustomScene(s32 sceneId);
int  CustomWorldExport173Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomWorldExport173Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
