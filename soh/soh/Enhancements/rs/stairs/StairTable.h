#ifndef SOH_RS_STAIR_TABLE_H
#define SOH_RS_STAIR_TABLE_H

#include <stdint.h>
#include "StairDef.h"

// The table of deliberately malformed staircases `stairs badcheck` runs the validator over - the
// staircase twin of `npc badcheck` and `quest badcheck`. Append-only, for the reason given where
// it is defined (StairTable.cpp). C++ only: the console is its one reader.
int32_t RsStairTable_BadCount();
const RsStairDef* RsStairTable_Bad(int32_t index);

#endif // SOH_RS_STAIR_TABLE_H
