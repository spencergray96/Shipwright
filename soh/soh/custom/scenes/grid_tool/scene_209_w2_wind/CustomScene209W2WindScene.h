#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomScene209W2WindScene_IsCustomScene(s32 sceneId);
int  CustomScene209W2WindScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomScene209W2WindScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
