#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomVertProbeScene_IsCustomScene(s32 sceneId);
int  CustomVertProbeScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomVertProbeScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
