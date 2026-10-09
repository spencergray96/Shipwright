#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomF2pOverworldScene_IsCustomScene(s32 sceneId);
int  CustomF2pOverworldScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomF2pOverworldScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
