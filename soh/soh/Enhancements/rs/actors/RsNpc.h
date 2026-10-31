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
//
// DIALOGUE TREES DID NOT ADD A FIELD (sturdy-bassoon#96), and that is deliberate. "Which node of
// the conversation am I on" would have been the first real exception to the paragraph above; it is
// not needed, because the OPEN TEXTBOX'S ID already says - a node box's id carries the node exactly
// as the entry box's carries the rule, and a reply that leads somewhere opens on an id naming its
// destination. The actor decodes both from `play->msgCtx.textId`. The consequence is the right one:
// walking away mid-tree and re-talking restarts at entry resolution, which is what OoT does.
//
// `giving` (sturdy-bassoon#281) is the one field that outlives a textbox, and it keeps the paragraph
// above true: it lives from a GIVE_ITEM pick, through the reply and any journal notices, to Link
// taking the item a few seconds after the box closes, and it holds nothing the save needs. The step is set at acceptance, so a scene change mid-give loses an
// offer that was never taken and nothing else - re-talking offers it again, because the step is
// still unset. It is not a position in the conversation either: the `next` it carries is followed
// the moment the item is taken, and the re-opened box's own id says where the conversation is from
// then on, as every other node box's does.
typedef struct RsNpc {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ ColliderCylinder collider;
    /* */ RsNpcActionFunc actionFunc;
    /* */ int32_t npcId;    // decoded from params once, at Init
    /* */ int32_t ruleIndex; // the rule currently speaking; recomputed every idle frame
    /* */ const struct RsDialogueOption* giving; // the GIVE_ITEM option being handed over, or NULL
} RsNpc;

#endif // SOH_RS_NPC_ACTOR_H
