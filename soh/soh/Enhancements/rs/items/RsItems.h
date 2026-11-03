#ifndef SOH_RS_ITEMS_H
#define SOH_RS_ITEMS_H

#include <stddef.h>
#include <stdint.h>

#include "soh/Enhancements/rs/RsAssert.h"
#include "soh/cvar_prefixes.h"

// THE INVENTORY (sturdy-bassoon#286; docs/decisions/2026-11-02-inventory-counted-and-key-items.md). Two kinds of
// thing the player carries, and they are stored in two different places on purpose:
//
//   COUNTED ITEMS - planks, logs, nails: a number per item, gained and spent the way bombs or arrows are. Always
//   carried (there is no bank), capped per item, never lost except by spending. The `rsItems` save section keeps
//   one count per item, keyed by the item's TOKEN, so this enum is never serialized and may be reordered.
//
//   KEY ITEMS - a hammer, a saw, a schematic: permanent finds, never spent or lost. Each is a WORLD FLAG
//   (WorldFlagIds.h), so "has the hammer" is already a predicate (WorldFlagSet) and an upgrade requirement. This
//   file adds the row beside the flag: its token, name, category, icon and pickup line. The flag is the truth;
//   the row is presentation.
//
// What the player sees: a counted gain bounces the item's icon over Link's head with its count beside it, the way
// vanilla's drops do (RsItemPickup.c's float); a key item plays vanilla's get-item ceremony. AT THE CAP NOTHING IS
// REFUSED OR SAID (the owner, 2026-11-02, as vanilla takes a drop into a full quiver): a pickup, a gift and a reward
// fill to the cap, lose the rest, and still float and sound. A PURCHASE is refused only when Link is already full,
// as vanilla's shop refuses arrows to a full quiver; short of that it fills to the cap at the full price. The float
// and the ceremony are presentation only - the count or the flag is written first, and a ceremony lost to a scene
// change loses nothing.
//
// Icons are RuneScape's own inventory sprites, rendered from the cache by tools/items/ in sturdy-bassoon
// (docs/reference/RS_ITEM_ICONS.md) and packed into `mods/rs-items.o2r`, two an item, at 8x RS's 36 x 32, so
// 288 x 256 RGBA16: `rs/items/<token>`, the inventory icon with RS's outline and shadow, and
// `rs/items/ground/<token>`, the ground icon, outline and no shadow (the owner's choice: a pickup reads like
// vanilla's drops). Which RS item each token is rendered from lives in that tool's manifest (rs-items.json), keyed
// by the same token, and nowhere here. An item whose icon is missing draws a placeholder: nothing about the store
// depends on the art.

// --- counted items ------------------------------------------------------------------------------------
//
// The save keys a count by TOKEN, so renaming a token is a save break and the number is not in the save. The number
// IS in scene data: a placed pickup's params carry it (RsItemPickup.h), and so do the definitions' packed operands.
// So: explicit `= N`, append only, never renumber or reuse; retire an item in place.
typedef enum RsItemId {
    RS_ITEM_LOGS = 0,
    RS_ITEM_OAK_LOGS = 1,
    RS_ITEM_PLANK = 2,
    RS_ITEM_OAK_PLANK = 3,
    RS_ITEM_STEEL_NAILS = 4,
    RS_ITEM_DRAGON_NAILS = 5,
    RS_ITEM_COUNT,
} RsItemId;

// What a packed operand can hold: 64 items, a count up to 1023. Every item's cap stays at or under this, which
// RsItemCatalogue.cpp checks at startup.
#define RS_ITEM_ID_MAX 64
#define RS_ITEM_COUNT_MAX 1023
RS_STATIC_ASSERT(RS_ITEM_COUNT <= RS_ITEM_ID_MAX, "a counted item's id must fit its 6-bit operand field");

// One int naming an item and a count: a quest reward's `a`, a dialogue option's `a`. Six bits of item, ten of
// count. A priced one (a sale or a purchase) carries the rupee price above them, fifteen bits, so every legal
// price leaves the int32 positive.
#define RS_ITEMS(item, count) ((int32_t)(((item)&0x3F) | (((count)&0x3FF) << 6)))
#define RS_ITEMS_GET_ITEM(a) ((int32_t)((a)&0x3F))
#define RS_ITEMS_GET_COUNT(a) ((int32_t)(((a) >> 6) & 0x3FF))
#define RS_ITEMS_PRICED(item, count, price) ((int32_t)(RS_ITEMS(item, count) | (((price)&0x7FFF) << 16)))
#define RS_ITEMS_GET_PRICE(a) ((int32_t)(((a) >> 16) & 0x7FFF))

typedef struct RsItemDef {
    int32_t id;         // RsItemId, equal to its index (checked at startup)
    const char* token;  // the save's key, the console's word and the icon's archive name: "oak_plank"
    const char* name;   // RS's own name, for display: "Oak plank"
    const char* plural; // the name in a sentence about several: "oak planks"
    int32_t cap;        // the most the player may carry, 1..RS_ITEM_COUNT_MAX
} RsItemDef;

// --- key items ----------------------------------------------------------------------------------------
//
// Held is the world flag, which is what the save carries. The number is in scene data - a placed pickup's params -
// and in a text id, so the same rule as RsItemId: explicit `= N`, append only.
typedef enum RsKeyItemId {
    RS_KEY_ITEM_HAMMER = 0,
    RS_KEY_ITEM_SAW = 1,
    RS_KEY_ITEM_SKIFF_SCHEMATIC = 2,
    RS_KEY_ITEM_DEBUG = 3, // debug band: what fixtures grant, so `quest debugwipe` takes it back
    RS_KEY_ITEM_COUNT,
} RsKeyItemId;

#define RS_KEY_ITEM_ID_MAX 64
RS_STATIC_ASSERT(RS_KEY_ITEM_COUNT <= RS_KEY_ITEM_ID_MAX, "a key item's id must fit its text-id and params fields");

// What a list of key items is grouped by. Display only.
typedef enum RsKeyItemCategory {
    RS_KEY_CAT_TOOLS,
    RS_KEY_CAT_SCHEMATICS,
    RS_KEY_CAT_KEEPSAKES,
    RS_KEY_CAT_QUEST,
    RS_KEY_CAT_COUNT,
} RsKeyItemCategory;

typedef struct RsKeyItemDef {
    int32_t id;              // RsKeyItemId, equal to its index (checked at startup)
    const char* token;       // the console's word and the icon's archive name: "hammer"
    const char* name;        // "Hammer"
    int32_t category;        // RsKeyItemCategory
    int32_t worldFlag;       // WorldFlagIds.h: set means held
    const char* pickupText;  // the get-item box, `&` for a line break; NULL says "You got a <name>!"
} RsKeyItemDef;

// Why a gain, a take or a grant did or would not happen. Console words through RsItems_ResultName.
typedef enum RsItemsResult {
    RS_ITEMS_OK,
    RS_ITEMS_UNKNOWN,    // no such item
    RS_ITEMS_BAD_AMOUNT, // a count below 1 or past RS_ITEM_COUNT_MAX
    RS_ITEMS_FULL,       // the gain would pass the item's cap: nothing was added
    RS_ITEMS_SHORT,      // the player holds fewer than that: nothing was taken
    RS_ITEMS_HELD,       // a key item already held
    RS_ITEMS_RESULT_COUNT,
} RsItemsResult;

// Where a gain came from, for the `rs_items` markers. Console words through RsItems_SourceName.
typedef enum RsItemsSource {
    RS_ITEMS_SRC_CONSOLE,
    RS_ITEMS_SRC_REWARD,
    RS_ITEMS_SRC_DIALOGUE,
    RS_ITEMS_SRC_PICKUP,
    RS_ITEMS_SRC_BOAT,
    RS_ITEMS_SRC_COUNT,
} RsItemsSource;

// The readout beside the boat's (`items hud`): every counted item and every key item, at the right of the screen.
#define CVAR_RS_ITEMS_HUD CVAR_ENHANCEMENT("RsItemsHud")
// Whether a counted pickup lying in the world shows its count (`items groundcount`, on by default). The float over
// Link's head always does. The owner's switch while he decides which reads better (2026-11-02).
#define CVAR_RS_ITEMS_GROUND_COUNT CVAR_ENHANCEMENT("RsItemsGroundCount")

#ifdef __cplusplus
extern "C" {
#endif

// --- the catalogue (RsItemCatalogue.cpp) ---------------------------------------------------------------

const RsItemDef* RsItems_Def(int32_t item);  // NULL out of range
int32_t RsItems_ByToken(const char* token);  // -1 for none
const RsKeyItemDef* RsKeyItems_Def(int32_t key); // NULL out of range
int32_t RsKeyItems_ByToken(const char* token);   // -1 for none
int32_t RsKeyItems_ByFlag(int32_t worldFlag);    // the key item that flag is, or -1
const char* RsKeyItems_CategoryName(int32_t category); // "tools", "schematics", ...; "?" out of range

// The icon's archive path, "__OTR__rs/items/<token>" or with `ground` "__OTR__rs/items/ground/<token>", aligned for
// the renderer (RsItemArt.h says why it must be), and whether it resolved: 1 found, 0 missing (the placeholder
// draws). Resolved once, at boot.
#define RS_ITEM_ICON_W 288
#define RS_ITEM_ICON_H 256
const char* RsItems_IconPath(int32_t item, int32_t ground);
const char* RsKeyItems_IconPath(int32_t key, int32_t ground);
int32_t RsItems_IconFound(int32_t item, int32_t ground);
int32_t RsKeyItems_IconFound(int32_t key, int32_t ground);

// The key item's RuneScape 3D model, held up in its get-item ceremony (sturdy-bassoon#286): the display list
// "objects/rs_props/rs_item_<token>/props" of `mods/rs-props-rs_item_<token>.o2r`, which the build packages from
// soh/soh/custom/rs_items/rs_item_<token>_props.json with sturdy-bassoon's prop generator. The model is the item's
// inventory model, turned as its icon views it and centred on its middle (tools/props, the `item` source). A Gfx*,
// as void* so this header needs no gbi.h; NULL when no mounted archive has it, and the ceremony draws the icon.
// Loaded on first ask and held. The extent is its largest span on any axis, in model units, read from its own
// vertices (0 with no model), so the ceremony can size every model alike.
void* RsKeyItems_ModelList(int32_t key);
float RsKeyItems_ModelExtent(int32_t key);

// --- the store (RsItems.cpp) ------------------------------------------------------------------------------

int32_t RsItems_Count(int32_t item); // 0 for an unknown item

// Would `count` more fit? RS_ITEMS_OK, or UNKNOWN / BAD_AMOUNT / FULL. Writes nothing.
int32_t RsItems_CheckGain(int32_t item, int32_t count);
// Adds `count` if it all fits, else nothing (RsItems_CheckGain's answer). `show` bounces the icon over Link's head.
int32_t RsItems_Gain(int32_t item, int32_t count, int32_t show, int32_t source);
// The gain the world uses - a pickup, a gift, a reward: adds what fits under the cap and logs the rest as lost,
// never refusing. `show` floats the whole `count` over Link's head, even when none of it fitted, as vanilla shows a
// drop taken into a full quiver. Returns how many were added. RsItems_Gain, which refuses, is the console's.
int32_t RsItems_GainClamped(int32_t item, int32_t count, int32_t show, int32_t source);
// Does the player hold `count`? RS_ITEMS_OK, or UNKNOWN / BAD_AMOUNT / SHORT. Writes nothing.
int32_t RsItems_CheckTake(int32_t item, int32_t count);
int32_t RsItems_Take(int32_t item, int32_t count, int32_t source);
// Test aid: the count outright, 0..cap, no rules. UNKNOWN or BAD_AMOUNT otherwise.
int32_t RsItems_Set(int32_t item, int32_t count);
// Every count to 0, as a new save. Key items are world flags and are not touched.
void RsItems_Reset(void);

int32_t RsKeyItems_Held(int32_t key);
// Sets the key item's flag. `ceremony` queues the get-item ceremony, played once Link is free. HELD when it was
// already set (no second ceremony), UNKNOWN for no such key item.
int32_t RsKeyItems_Grant(int32_t key, int32_t ceremony, int32_t source);
// Test aid: clears the flag. A key item is never lost in play.
int32_t RsKeyItems_Clear(int32_t key);

const char* RsItems_ResultName(int32_t result);
const char* RsItems_SourceName(int32_t source);

// A QuestPredicate's ItemCountAtLeast operands in range: an item that exists, a count 0..RS_ITEM_COUNT_MAX.
int32_t RsItems_PredicateOperandsValid(int32_t item, int32_t count);
// An unpriced packed operand (RS_ITEMS) in range: an item that exists, a count 1..that item's cap, and nothing above
// the count - a price there would be a promise never kept. A priced one is checked as `packed & 0xFFFF`.
int32_t RsItems_PackedValid(int32_t packed);
// 1 when the key item's flag sits in the debug band: a debug definition grants only these, a production one only
// the others (the world-flag reward's rule, for `quest debugwipe`).
int32_t RsKeyItems_IsDebug(int32_t key);

// --- presentation (RsItems.cpp) -----------------------------------------------------------------------------

// A REFUSED DIALOGUE ACTION SAYS WHY (the owner, 2026-11-03, after vanilla's shop: EnOssan plays NA_SE_SY_ERROR and
// opens a "can't buy" box). `result` is the QuestResult the items action returned - QUEST_ERR_CANNOT_CARRY,
// QUEST_ERR_CANNOT_AFFORD or QUEST_ERR_NOT_ENOUGH_ITEMS; anything else does nothing. Plays the error sound and sets
// the words through RsInteraction_ActionSays with `instead` 1, so a prop and a character both say them in place of
// the option's reply, which is written for the action having happened: "You can't carry any more steel nails.",
// "You don't have enough rupees.", "You haven't got 3 planks." Marked `rs_items event=refused reason= text=`.
void RsItems_SayRefusal(int32_t result, int32_t item, int32_t count);

// Bounces the item's icon and `count` over Link's head (an RsItemPickup float), changing nothing. What a gain with
// `show` calls; the console's `items float` calls it alone. Does nothing without a PlayState.
void RsItems_ShowGain(int32_t item, int32_t count);
// Queues the key item's get-item ceremony (RsKeyItems_Grant with `ceremony` calls it). Played in order, one at a
// time, once no textbox is up and Link is free; dropped by a scene change, which costs nothing (the flag is set).
void RsKeyItems_QueueCeremony(int32_t key);
int32_t RsKeyItems_CeremoniesQueued(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_ITEMS_H
