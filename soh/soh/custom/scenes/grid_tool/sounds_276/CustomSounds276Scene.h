#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomSounds276Scene_IsCustomScene(s32 sceneId);
int  CustomSounds276Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomSounds276Scene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
