#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomGridTestMap7Scene_IsCustomScene(s32 sceneId);
int  CustomGridTestMap7Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomGridTestMap7Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
