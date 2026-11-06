#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomLumbridgeRoofs265AgentCopyScene_IsCustomScene(s32 sceneId);
int  CustomLumbridgeRoofs265AgentCopyScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomLumbridgeRoofs265AgentCopyScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
