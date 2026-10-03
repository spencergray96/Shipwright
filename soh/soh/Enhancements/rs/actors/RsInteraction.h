#ifndef SOH_RS_INTERACTION_ACTOR_H
#define SOH_RS_INTERACTION_ACTOR_H

// Rs_Interaction (sturdy-bassoon#183): the invisible trigger over a baked prop that names an
// interaction. It has NO draw function - the prop stays in its map's baked list, with its own
// collision and climb flag, and this actor only supplies the check. Measured at 0.20 us a tick
// against 1.15 us for an actor whose draw function draws nothing (#117 stage C), which is why the
// null draw is a registration choice and not a detail.
//
// The scene's ActorEntry row carries everything (RsActorParams.h, the grid tool's emitter):
//   params  the interaction id, 15 bits
//   rot.x   the focus height above the prop's base, units
//   rot.z   the talk range in x/z, units, from the prop's mesh centre
// None of it needs to survive a scene transition: a conversation's position is on the open box's
// text id, as it is for a character (RsNpc.h).

#include "z64actor.h"

struct RsInteraction;

typedef void (*RsInteractionActionFunc)(struct RsInteraction*, PlayState*);

typedef struct RsInteraction {
    /* 0x0000 */ Actor actor;
    /* */ RsInteractionActionFunc actionFunc;
    /* */ int32_t interactionId; // decoded from params once, at Init
    /* */ int32_t ruleIndex;     // the entry rule resolved when the current check was accepted
    /* */ int32_t checks;        // times this placement has been checked since it spawned
    /* */ float range;           // talk range, x/z units
} RsInteraction;

#endif // SOH_RS_INTERACTION_ACTOR_H
