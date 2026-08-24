#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomLumbridgeCastleScene_IsCustomScene(s32 sceneId);
int  CustomLumbridgeCastleScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomLumbridgeCastleScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
