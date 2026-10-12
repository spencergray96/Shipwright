#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomScene217E2RemoveFacesScene_IsCustomScene(s32 sceneId);
int  CustomScene217E2RemoveFacesScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomScene217E2RemoveFacesScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
