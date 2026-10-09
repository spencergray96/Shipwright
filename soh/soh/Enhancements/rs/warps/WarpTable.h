#ifndef SOH_RS_WARP_TABLE_H
#define SOH_RS_WARP_TABLE_H

#include <stdint.h>
#include "WarpDef.h"

// The malformed tables `warps badcheck` asks the validator about - never registered.
int32_t RsWarpTable_BadCount();
const RsWarpSceneDef* RsWarpTable_Bad(int32_t index);

// The same for GENERATED rows (#173 F3): planted warp tables as an export would write them, and planted
// map->scene tables (SceneMaps.h), each with the problem the merge or validator must find - NONE for
// one it must accept.
typedef struct RsWarpBadGen {
    const RsWarpGenRow* rows;
    int32_t rowCount;
    const RsWarpGenDest* dests;
    int32_t destCount;
    int32_t expect; // RsWarpProblem
} RsWarpBadGen;
int32_t RsWarpTable_BadGenCount();
const RsWarpBadGen* RsWarpTable_BadGen(int32_t index);

#include "soh/Enhancements/rs/maps/SceneMaps.h"
typedef struct RsMapsBad {
    const RsGenSceneRow* scenes;
    int32_t sceneCount;
    const RsGenSceneMapRow* maps;
    int32_t mapCount;
    int32_t expect; // RsMapsProblem
} RsMapsBad;
int32_t RsWarpTable_BadMapsCount();
const RsMapsBad* RsWarpTable_BadMaps(int32_t index);

#endif // SOH_RS_WARP_TABLE_H
