#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomScene235PuzzleFiresScene_IsCustomScene(s32 sceneId);
int  CustomScene235PuzzleFiresScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomScene235PuzzleFiresScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
