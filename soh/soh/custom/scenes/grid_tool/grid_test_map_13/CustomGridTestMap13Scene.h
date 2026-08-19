#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomGridTestMap13Scene_IsCustomScene(s32 sceneId);
int  CustomGridTestMap13Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomGridTestMap13Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
