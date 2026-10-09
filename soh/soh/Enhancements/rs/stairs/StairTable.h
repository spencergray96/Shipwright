#ifndef SOH_RS_STAIR_TABLE_H
#define SOH_RS_STAIR_TABLE_H

#include <stdint.h>
#include "StairDef.h"

// The table of deliberately malformed staircases `stairs badcheck` runs the validator over - the
// staircase twin of `npc badcheck` and `quest badcheck`. Append-only, for the reason given where
// it is defined (StairTable.cpp). C++ only: the console is its one reader.
int32_t RsStairTable_BadCount();
const RsStairDef* RsStairTable_Bad(int32_t index);

// The same for GENERATED rows (#173 F3): planted tables as an export would write them, each with the
// problem the merge must find (RsStairProblem; RS_STAIR_PROBLEM_NONE for one it must accept).
typedef struct RsStairBadGen {
    const RsStairGenRow* rows;
    int32_t rowCount;
    const RsStairGenOption* options;
    int32_t optionCount;
    int32_t expect;
} RsStairBadGen;
// Staircases whose names or overrides do not all fit, each with how many words must fall back
// (decision 23). They register cleanly; `stairs badcheck` counts what RsStair_FallbacksOf drops.
typedef struct RsStairWordsCase {
    const RsStairDef* def;
    int32_t fallbacks;
} RsStairWordsCase;
int32_t RsStairTable_WordsCount();
const RsStairWordsCase* RsStairTable_Words(int32_t index);

int32_t RsStairTable_BadGenCount();
const RsStairBadGen* RsStairTable_BadGen(int32_t index);

#endif // SOH_RS_STAIR_TABLE_H
