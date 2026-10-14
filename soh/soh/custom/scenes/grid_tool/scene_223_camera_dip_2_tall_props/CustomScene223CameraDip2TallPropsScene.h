#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomScene223CameraDip2TallPropsScene_IsCustomScene(s32 sceneId);
int  CustomScene223CameraDip2TallPropsScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomScene223CameraDip2TallPropsScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
