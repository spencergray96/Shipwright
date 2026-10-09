#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomScene202CutoutTexturesScene_IsCustomScene(s32 sceneId);
int  CustomScene202CutoutTexturesScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomScene202CutoutTexturesScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
