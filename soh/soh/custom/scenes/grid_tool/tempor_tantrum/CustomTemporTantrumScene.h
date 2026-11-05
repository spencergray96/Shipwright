#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomTemporTantrumScene_IsCustomScene(s32 sceneId);
int  CustomTemporTantrumScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomTemporTantrumScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
