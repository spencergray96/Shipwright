#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomLumbridgeSettlementX3Scene_IsCustomScene(s32 sceneId);
int  CustomLumbridgeSettlementX3Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomLumbridgeSettlementX3Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
