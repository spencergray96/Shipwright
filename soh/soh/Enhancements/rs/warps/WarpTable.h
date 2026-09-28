#ifndef SOH_RS_WARP_TABLE_H
#define SOH_RS_WARP_TABLE_H

#include <stdint.h>
#include "WarpDef.h"

// The malformed tables `warps badcheck` asks the validator about - never registered.
int32_t RsWarpTable_BadCount();
const RsWarpSceneDef* RsWarpTable_Bad(int32_t index);

#endif // SOH_RS_WARP_TABLE_H
