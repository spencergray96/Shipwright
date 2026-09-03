#ifndef SOH_RS_QUEST_ITEM_ACTOR_H
#define SOH_RS_QUEST_ITEM_ACTOR_H

#include "z64actor.h"

struct RsQuestItem;

typedef void (*RsQuestItemActionFunc)(struct RsQuestItem*, PlayState*);

// The quest-item actor (sturdy-bassoon#58 P3 / #64, D16): a collision cylinder that, on touch,
// sets a quest step, shows a custom textbox and despawns. NOTHING ENTERS THE VANILLA INVENTORY, so
// there is zero coupling to the vanilla item id space - which matters under the
// strand-vanilla-content-don't-delete-it plan.
//
// Which step it sets comes from `params` (RsActorParams.h): the (quest, step) pair, and nothing
// else. There is deliberately no third id space - the pair is already frozen data under D3.
//
// THE FLAG IS SET ON COLLECTION, NEVER ON SPAWN. A one-off spawn that sets the flag lets the
// player leave the zone without picking the item up and be soft-locked out of a key item forever.
// Init here reads params and validates; it writes nothing.
//
// Suppressing an ALREADY-COLLECTED item's spawn (ShouldActorInit) is P4's, not this phase's.
typedef struct RsQuestItem {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ ColliderCylinder collider;
    /* */ RsQuestItemActionFunc actionFunc;
    /* */ int32_t questId; // decoded from params once, at Init
    /* */ int32_t step;
    /* */ int32_t valid; // 0 when params named a quest/step this build cannot honour
} RsQuestItem;

#endif // SOH_RS_QUEST_ITEM_ACTOR_H
