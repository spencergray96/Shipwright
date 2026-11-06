#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomRimmingtonChurch265AgentScene_IsCustomScene(s32 sceneId);
int  CustomRimmingtonChurch265AgentScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomRimmingtonChurch265AgentScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
