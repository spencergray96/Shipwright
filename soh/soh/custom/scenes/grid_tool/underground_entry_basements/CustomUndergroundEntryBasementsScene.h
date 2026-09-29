#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomUndergroundEntryBasementsScene_IsCustomScene(s32 sceneId);
int  CustomUndergroundEntryBasementsScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomUndergroundEntryBasementsScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
