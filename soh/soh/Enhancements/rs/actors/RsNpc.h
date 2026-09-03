#ifndef SOH_RS_NPC_ACTOR_H
#define SOH_RS_NPC_ACTOR_H

#include "z64actor.h"

struct RsNpc;

typedef void (*RsNpcActionFunc)(struct RsNpc*, PlayState*);

// The quest-giver actor (sturdy-bassoon#58 P3 / #64). One actor TYPE serves every character: which
// character an instance is comes from `params` (RsActorParams.h), and what it says comes from that
// character's dialogue rule table (dialogue/NpcDialogue.h). Registered at runtime through
// ActorDB::AddEntry from RsActors.cpp, deliberately NOT added to the 428-overlay tree in
// soh/src/overlays/actors (D6).
//
// NOTHING IN THIS STRUCT SURVIVES A SCENE TRANSITION, AND NOTHING IN IT MAY NEED TO. Every actor
// is deleted on a scene change (func_80031C3C -> Actor_Delete), so an instance is rebuilt from
// scratch on the other side. That is correct and load-bearing, not a limitation: the character's
// state lives in the GLOBAL quest and world-flag stores (D22), and the instance rebuilds what it
// says from them on every frame it idles. Storing conversation progress here would give one
// character two different memories in two scenes - which is exactly vanilla's per-scene flag trap
// that this system exists to avoid.
//
// `ruleIndex` looks like a counter-example and is not: it is a CACHE of RsNpc_ResolveRule, rewritten
// from the stores every frame in the idle state, never read across a transition.
typedef struct RsNpc {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ ColliderCylinder collider;
    /* */ RsNpcActionFunc actionFunc;
    /* */ int32_t npcId;    // decoded from params once, at Init
    /* */ int32_t ruleIndex; // the rule currently speaking; recomputed every idle frame
} RsNpc;

#endif // SOH_RS_NPC_ACTOR_H
