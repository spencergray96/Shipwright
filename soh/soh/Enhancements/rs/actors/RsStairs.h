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
//
// WALK INTO IT (sturdy-bassoon#151): the second way in, beside target-and-talk. Pushing into the
// collider - touching it, stick held, stick and facing both aimed at the shaft - for the hold opens
// the menu with no A press, through vanilla's own auto-accepted talk offer. RsStairs.c says what
// each check is for; the state below is all of it, and all of it is per placement.
typedef struct RsStairsBump {
    int16_t count;      // consecutive ticks the push has held; 0 = not counting
    uint8_t offered;    // the hold was reached and the auto-accepted offer is being made
    uint8_t latched;    // a conversation here ended, or a move ran with the stick held, and the push
                        // has not been let go since
    uint8_t ticksApart; // ticks since Link was last in contact, capped - ends a contact for the markers
    int8_t ignoredWhy;  // the RsStairsBumpWhy last reported as `bump_ignored` this contact, -1 for none
} RsStairsBump;

typedef struct RsStairs {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ ColliderCylinder collider;
    /* */ RsStairsActionFunc actionFunc;
    /* */ int32_t stairId; // decoded from params once, at Init
    /* */ int32_t row;     // which of that staircase's rows this placement stands on
    /* */ RsStairsBump bump;
} RsStairs;

#endif // SOH_RS_STAIRS_ACTOR_H
