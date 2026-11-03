#ifndef SOH_RS_ITEM_PICKUP_H
#define SOH_RS_ITEM_PICKUP_H

#include <stdbool.h>

#include "z64.h"

// The inventory's actor (sturdy-bassoon#286, items/RsItems.h): one actor, three uses, all drawing an item's icon
// as a sprite that faces the camera.
//
//   A COUNTED PICKUP - planks lying on the floor. Walked into, it adds its count (RsItems_Gain) and goes, and the
//   icon bounces over Link's head. When the count would pass the item's cap it stays, and says so once ("You can't
//   carry any more planks.") until Link steps away. It comes back on every load of the scene unless the placement
//   names a ONCE-ONLY world flag, set on collection; a set flag keeps it from being built at all.
//
//   A KEY-ITEM PICKUP - the hammer on a bench. Collected with vanilla's get-item ceremony (RsQuestItem's get-item
//   style, the same shape), which sets the key item's flag when Link has it. A held key item is never built.
//
//   A FLOAT - not placed: spawned by RsItems_ShowGain over Link's head when a count goes up, bouncing there for a
//   second the way vanilla's collected drops do (EnItem00's func_8001E5C8), with the count in RuneScape's yellow at
//   the icon's top-left (RS draws a stack's count there, and only past one).
//
// Placed like the quest item: one ActorEntry row in the scene's C, or `items spawn` from the console.
//   params   bits 0-5 the item (an RsItemId, or an RsKeyItemId with bit 6), bit 6 KEY, bit 7 FLOAT,
//            8-14 reserved (zero)
//   rot.x    a counted pickup's count, 1..its cap (0 reads as 1). Unused for a key item.
//   rot.z    a counted pickup's once-only world flag PLUS ONE; 0 means it comes back on every load. The flag must
//            be a PLACEMENT flag (WorldFlagIds.h: production 2048-3839, debug 3968-4095), one per placement; any
//            other is refused at Init. A key item's own flag is its once-ness.
// The rotations are read at Init and zeroed, as RsFire reads its extra parameters (a sprite has no facing).

#define RS_PICKUP_PARAMS_ITEM_MASK 0x003F
#define RS_PICKUP_PARAMS_KEY 0x0040
#define RS_PICKUP_PARAMS_FLOAT 0x0080
#define RS_PICKUP_PARAMS_RSVD_MASK 0x7F00
#define RS_PICKUP_PARAMS(item) ((int16_t)((item)&RS_PICKUP_PARAMS_ITEM_MASK))
#define RS_PICKUP_PARAMS_KEY_ITEM(key) ((int16_t)(((key)&RS_PICKUP_PARAMS_ITEM_MASK) | RS_PICKUP_PARAMS_KEY))
// rot.z for a once-only counted pickup: its world flag plus one.
#define RS_PICKUP_ONCE(flag) ((int16_t)((flag) + 1))

struct RsItemPickup;

typedef void (*RsItemPickupActionFunc)(struct RsItemPickup*, PlayState*);

typedef struct RsItemPickup {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ ColliderCylinder collider;
    /* */ RsItemPickupActionFunc actionFunc;
    /* */ int32_t item;     // RsItemId, or RsKeyItemId when `key`
    /* */ int32_t key;      // 1: a key item
    /* */ int32_t count;    // a counted pickup's or a float's count
    /* */ int32_t onceFlag; // a counted pickup's once-only world flag, or -1
    /* */ int32_t valid;    // 0 when params name nothing this build has
    /* */ int32_t timer;    // a float's ticks left
    /* */ int32_t refused;  // 1 after a FULL refusal said so, until Link steps away
} RsItemPickup;

#ifdef __cplusplus
extern "C" {
#endif

void RsItemPickup_Init(Actor* thisx, PlayState* play);
void RsItemPickup_Destroy(Actor* thisx, PlayState* play);
void RsItemPickup_Update(Actor* thisx, PlayState* play);
void RsItemPickup_Draw(Actor* thisx, PlayState* play);

// The ShouldActorInit hook (RsActors.cpp): false for a key item already held and for a once-only pickup whose flag
// is set. Reads only, the quest item's rule: a flag is set on collection, never on spawn.
void RsItemPickup_ShouldInit(void* actorRef, bool* should);

// The get-item entry for a key item - its icon held up, its line in the box (RS_TEXT_KEY_ITEM_ID) - for a pickup
// and for RsItems.cpp's ceremony queue alike. Nothing enters the vanilla inventory (itemId ITEM_NONE).
GetItemEntry RsItemPickup_KeyEntry(s32 key);

// A float over Link's head showing `count` of the item. What RsItems_ShowGain spawns.
void RsItemPickup_SpawnFloat(PlayState* play, s32 item, s32 count);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_ITEM_PICKUP_H
