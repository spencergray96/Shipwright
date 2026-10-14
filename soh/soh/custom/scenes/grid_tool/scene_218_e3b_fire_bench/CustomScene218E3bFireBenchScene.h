#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomScene218E3bFireBenchScene_IsCustomScene(s32 sceneId);
int  CustomScene218E3bFireBenchScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomScene218E3bFireBenchScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
