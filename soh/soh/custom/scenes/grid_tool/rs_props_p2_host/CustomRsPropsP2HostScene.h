// HAND-MERGED COPY for sturdy-bassoon#78 (RS export P2b). Copied from the grid-tool scene
// grid_test_map_10 and extended by docs/test-runs/2026-09-17-rs-export-p2-props/merge_props.py
// with RS props from fast64-export/rs_props_p2. Not owned by any grid-tool project: no
// re-export touches it. Re-run the script rather than editing by hand.
#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int  CustomRsPropsP2HostScene_IsCustomScene(s32 sceneId);
int  CustomRsPropsP2HostScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn);
void CustomRsPropsP2HostScene_InitRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif
