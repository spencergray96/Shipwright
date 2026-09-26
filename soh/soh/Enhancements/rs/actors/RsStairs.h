#ifndef SOH_RS_STAIRS_ACTOR_H
#define SOH_RS_STAIRS_ACTOR_H

#include "z64actor.h"

struct RsStairs;

typedef void (*RsStairsActionFunc)(struct RsStairs*, PlayState*);

// The staircase actor (sturdy-bassoon#147): one placement per storey a staircase serves. Link
// targets it and talks to it like a signpost; it opens that storey's menu and, on a pick, hands the
// move to the controller in stairs/Stairs.cpp and goes back to waiting. It does NOT run the move -
// see Stairs.h for why that would kill it half way through.
//
// It draws nothing. The shaft it stands in is the visible thing, and the attention arrow and the A
// prompt are what say it can be used; a ladder model is asset work (rs-asset-import), not this.
// Its cylinder is solid, which is the other half of the job: it fills the hole it stands in, so
// Link cannot walk off a storey's edge into the shaft.
//
// Like RsNpc, nothing here needs to survive a scene transition or a room change: the staircase is
// (id, row) from params, and everything else is in the staircase's table.
typedef struct RsStairs {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ ColliderCylinder collider;
    /* */ RsStairsActionFunc actionFunc;
    /* */ int32_t stairId; // decoded from params once, at Init
    /* */ int32_t row;     // which of that staircase's rows this placement stands on
} RsStairs;

#endif // SOH_RS_STAIRS_ACTOR_H
