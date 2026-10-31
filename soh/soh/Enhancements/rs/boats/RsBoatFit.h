#ifndef SOH_RS_BOAT_FIT_H
#define SOH_RS_BOAT_FIT_H

// Boat ownership and fit-out (sturdy-bassoon#263): which boats the player owns, and what each one carries. The base
// layer of Sailing's progression - data, the save and the rules - with nothing in the world to drive it yet; the
// design and what is left for later are sturdy-bassoon docs/decisions/2026-10-30-boat-ownership-and-fit-out.md.
//
// THREE KINDS OF SLOT on each boat kind (RsBoatKindId):
//   - a CORE PART (hull, helm, sails, keel): always filled, from tier 0, the free default. The raft has no keel.
//   - a FACILITY HOTSPOT, typed to one facility (a salvaging hook, a wind catcher, a cannon): tier 0 is empty.
//   - a LOOK (the sail pattern): one of an unordered set of options, swapped freely back and forth, free. The sail
//     pattern recolours the sail's cloth as the boat draws (RsBoatFit_LookColour, sturdy-bassoon#283).
// Core parts and hotspots share one slot list (RsBoatSlot) and one rule: a slot's tier only ever goes UP. A look is
// not a tier, so it is never one-way.
//
// TWO TABLES (RsBoatFitCatalogue.cpp):
//   - TIERS: what each version of a part or facility does on each kind: its stats and its RS loc.
//   - UPGRADES: what can be built. Each names its fits (a slot to a tier, or owning the boat itself), the world flag
//     that unlocks it, its requirements (quest predicates) and its costs. Owning a boat is an upgrade, so boats,
//     parts and facilities all work one way: unlocked, then built (requirements met, costs paid) or granted free.
//
// THE SPLIT between the stores. What the player has EARNED - permission to build an upgrade, a key item, a schematic
// - is a world flag, so a quest reward sets it and any rule can read it. What the player has BUILT or CHOSEN - owned
// boats, each slot's tier, each look - is the `rsBoats` save section (RsBoatFit.cpp). Nothing built is ever lost: a
// boat is never destroyed, a tier is only ever replaced by a higher one.
//
// EVERY boat wears its kind's fit-out, the test fixtures (Lake Hylia's, `boat spawn`) included, owned or not: an
// unowned kind's fit-out is its defaults in play, since nothing can be built onto a boat the player does not own
// (the console's `own` and `fit` test aids can leave it otherwise, and the boat then wears what they set).
//
// Console: `boat own|fit|build|grant|look|fitout|upgrades` (RsBoatFitConsole.cpp). Markers: `rs_boat event=build|
// grant|look ...` (agent mode).

#include <stddef.h>
#include <stdint.h>

#include "soh/Enhancements/rs/quest/QuestPredicate.h"

// The debug readout under the steering label (RsBoatHud.cpp): one line per kind from the save. On by default;
// `boat fitout hud off`. CVAR_ENHANCEMENT is soh/cvar_prefixes.h's; it expands where this is used.
#define CVAR_RS_BOAT_FIT_HUD CVAR_ENHANCEMENT("RsBoatFitHud")

#ifdef __cplusplus
extern "C" {
#endif

// Every slot a boat kind may have. Which kind has which is the catalogue's (RsBoatFit_KindHasSlot). Never
// serialized by number - the save keys each slot by its name - so a slot may be appended anywhere in the list
// (the eternal brazier, a core part, is the one expected next) without a save break.
typedef enum RsBoatSlot {
    RS_BOAT_SLOT_HULL,           // core: top speed (RS: also its health and speed cap)
    RS_BOAT_SLOT_HELM,           // core: nothing yet (RS: rapids and kelp)
    RS_BOAT_SLOT_SAILS,          // core, RS's "mast and sails": acceleration
    RS_BOAT_SLOT_KEEL,           // core, not on the raft: nothing yet (RS: health and armour)
    RS_BOAT_SLOT_SALVAGING_HOOK, // facility
    RS_BOAT_SLOT_WIND_CATCHER,   // facility
    RS_BOAT_SLOT_CANNON,         // facility
    RS_BOAT_SLOT_COUNT,
} RsBoatSlot;

// A slot's highest tier, for the fit-out's storage. RS has seven of each; tier 0 is the default (or empty).
#define RS_BOAT_TIER_MAX 15

// Every look a boat may have, and RS's options for each. Saved by name, like the slots.
typedef enum RsBoatLook {
    RS_BOAT_LOOK_SAIL_PATTERN, // RS's sail colours; plain is the default
    RS_BOAT_LOOK_COUNT,
} RsBoatLook;

// What each tier does, on one kind. A stat at 0 is one this tier leaves alone: the boat's own (RsBoat.c's
// sRsBoatKinds, or a `boat tune` knob) stands.
typedef struct RsBoatTier {
    int32_t kind; // RsBoatKindId
    int32_t slot; // RsBoatSlot
    int32_t tier; // 1.. for a facility (0 is empty and has no row); 0.. for a core part
    const char* name; // RS's material, as a console token: "wooden", "oak", "bronze"
    int32_t loc;      // the RS loc it is. A core part's is checked against the cache (#283): the boat draws its kind's
                      // one model in this loc's colours, which the boat lists' `tiers` read (RsBoatModel.cpp logs a
                      // list whose locs differ). A facility's is still the wiki's, unchecked.
    float topSpeed;   // units a tick ahead (a hull's)
    float accel;      // speed gained a tick toward the throttle's target (a sail's)
    int32_t hitpoints; // reserved for boat health (designed, not built)
    uint32_t waters;   // reserved: the hazardous waters it lets the boat enter (designed, not built)
} RsBoatTier;

// The boats' stats from a fit-out: every fitted tier's stats in slot order, a later slot's nonzero stat winning.
// A stat at 0 is the boat's own, as in RsBoatTier.
typedef struct RsBoatFitStats {
    float topSpeed;
    float accel;
    int32_t hitpoints;
    uint32_t waters;
} RsBoatFitStats;

// --- upgrades -----------------------------------------------------------------------------------
//
// NEVER serialized - the save records what upgrades built, not which ones ran - so this list may be reordered and
// appended to freely. A quest reward names one (QUEST_REWARD_BOAT_UPGRADE).
typedef enum RsBoatUpgradeId {
    RS_BOAT_UPGRADE_RAFT,                 // owning the raft: a quest's gift (granted)
    RS_BOAT_UPGRADE_SKIFF,                // owning the skiff
    RS_BOAT_UPGRADE_SLOOP,                // owning the sloop
    RS_BOAT_UPGRADE_SKIFF_HULL_OAK,       // the skiff's speed upgrade (14 to 18)
    RS_BOAT_UPGRADE_SLOOP_HULL_OAK,       // the sloop's (12 to 14)
    RS_BOAT_UPGRADE_RAFT_HOOK_BRONZE,     // the first tier of each facility the owner picked
    RS_BOAT_UPGRADE_SKIFF_HOOK_BRONZE,
    RS_BOAT_UPGRADE_SLOOP_HOOK_BRONZE,
    RS_BOAT_UPGRADE_SKIFF_WIND_CATCHER,
    RS_BOAT_UPGRADE_SLOOP_WIND_CATCHER,
    RS_BOAT_UPGRADE_SLOOP_CANNON_BRONZE,
    RS_BOAT_UPGRADE_COUNT,
} RsBoatUpgradeId;

// A fit's slot for owning the boat itself rather than fitting it.
#define RS_BOAT_FIT_OWN (-1)

typedef struct RsBoatUpgradeFit {
    int32_t kind; // RsBoatKindId
    int32_t slot; // RsBoatSlot, or RS_BOAT_FIT_OWN
    int32_t tier; // the tier it fits (unused for RS_BOAT_FIT_OWN)
} RsBoatUpgradeFit;

// What an upgrade costs. Rupees only, until an inventory exists: counted items (planks, logs, nails, like bombs or
// arrows) arrive as an appended kind whose `a` names the item. Key items are not costs - they are never spent -
// but requirements (a world flag).
typedef enum RsBoatCostKind {
    RS_BOAT_COST_RUPEES, // amount = rupees; a unused
    RS_BOAT_COST_KIND_COUNT,
} RsBoatCostKind;

typedef struct RsBoatCost {
    int32_t kind; // RsBoatCostKind
    int32_t a;
    int32_t amount;
} RsBoatCost;

#define RS_BOAT_UPGRADE_MAX_FITS 4
#define RS_BOAT_UPGRADE_MAX_REQS 4
#define RS_BOAT_UPGRADE_MAX_COSTS 4

typedef struct RsBoatUpgrade {
    int32_t id;       // RsBoatUpgradeId, equal to its index (checked at startup)
    const char* name; // console token: "skiff", "skiff_hull_oak"
    int32_t unlockFlag; // the world flag that permits building it; a grant ignores it
    int32_t fitCount;
    RsBoatUpgradeFit fits[RS_BOAT_UPGRADE_MAX_FITS];
    int32_t reqCount; // every one must hold, besides the unlock; owning the boat a slot is on is implied
    QuestPredicate reqs[RS_BOAT_UPGRADE_MAX_REQS];
    int32_t costCount;
    RsBoatCost costs[RS_BOAT_UPGRADE_MAX_COSTS];
    int32_t placeholder; // 1: its costs are the owner's test values (5 and 10 rupees), not tuned
} RsBoatUpgrade;

// Why a build (or a grant, or a look) did or would not happen.
typedef enum RsBoatFitResult {
    RS_BOAT_FIT_OK,
    RS_BOAT_FIT_UNKNOWN,       // no such upgrade, kind, slot, look or option
    RS_BOAT_FIT_ALREADY,       // every fit already holds: nothing to build
    RS_BOAT_FIT_LOCKED,        // its unlock flag is not set
    RS_BOAT_FIT_NOT_OWNED,     // a fit is on a boat the player does not own
    RS_BOAT_FIT_REQUIREMENT,   // a requirement does not hold (`*failIndex` names it)
    RS_BOAT_FIT_CANT_AFFORD,   // a cost is more than the player has (`*failIndex` names it)
    RS_BOAT_FIT_RESULT_COUNT,
} RsBoatFitResult;

// --- the catalogue (RsBoatFitCatalogue.cpp) --------------------------------------------------------

// "hull", "salvaging_hook" (the save's key and the console's token), or "?"; and back, -1 for none.
const char* RsBoatFit_SlotName(int32_t slot);
int32_t RsBoatFit_SlotByName(const char* name);
// 1 for a core part (always filled, from tier 0), 0 for a facility hotspot (empty at tier 0).
int32_t RsBoatFit_SlotIsCore(int32_t slot);
// 1 when the kind has the slot: every kind its core parts but the raft's keel, and its own hotspots.
int32_t RsBoatFit_KindHasSlot(int32_t kind, int32_t slot);

// The tier's row on that kind, or NULL: none written (a facility's 0, or past the highest).
const RsBoatTier* RsBoatFit_Tier(int32_t kind, int32_t slot, int32_t tier);
// The highest tier written for the slot on that kind (0 for a slot with none past its default).
int32_t RsBoatFit_TopTier(int32_t kind, int32_t slot);

const char* RsBoatFit_LookName(int32_t look); // "sail_pattern"
int32_t RsBoatFit_LookByName(const char* name);
int32_t RsBoatFit_LookOptionCount(int32_t look);
const char* RsBoatFit_LookOptionName(int32_t look, int32_t option); // "plain", "red"; "?" out of range
int32_t RsBoatFit_LookOptionByName(int32_t look, const char* name);
// 1 when every requirement the option carries holds (none do yet).
int32_t RsBoatFit_LookOptionAllowed(int32_t look, int32_t option);
// 1 and the option's prim colour in rgb when it recolours (a sail pattern's colour); 0 when it draws the model's
// own colour (plain) or is out of range. RsBoat_Draw puts it in the segment the boat's list calls after the cloth.
int32_t RsBoatFit_LookColour(int32_t look, int32_t option, uint8_t rgb[3]);

const RsBoatUpgrade* RsBoatFit_Upgrade(int32_t id); // NULL out of range
int32_t RsBoatFit_UpgradeByName(const char* name);  // -1 for none

const char* RsBoatFit_ResultName(int32_t result); // "ok", "locked", "cant_afford", ...

// --- the store (RsBoatFit.cpp) --------------------------------------------------------------------

// The player's rupees once the HUD's counter has drained (rupees plus the accumulator): what a cost is checked
// against, so two builds a frame apart cannot spend the same rupees twice.
int32_t RsBoatFit_RupeesAfterPending(void);

int32_t RsBoatFit_Owned(int32_t kind);
// The slot's tier on the kind: 0 for a slot the kind does not have.
int32_t RsBoatFit_TierOf(int32_t kind, int32_t slot);
int32_t RsBoatFit_LookOf(int32_t kind, int32_t look);
// The kind's stats from its fit-out (see RsBoatFitStats). RsBoat.c reads it every tick.
void RsBoatFit_Stats(int32_t kind, RsBoatFitStats* out);

// The kind's block for the debug readout, one entry a line: "skiff: owned", then indented "hull oak", "helm bronze",
// ... the core parts, the hotspots ("-" while empty) and the looks. A tier with no row reads as its number.
void RsBoatFit_Summary(int32_t kind, char* buf, size_t len);

// Test aids for the console: set ownership or a tier outright, up or down, with no rules and no costs. A tier
// outside 0..RS_BOAT_TIER_MAX or on a slot the kind lacks is refused (RS_BOAT_FIT_UNKNOWN).
int32_t RsBoatFit_SetOwned(int32_t kind, int32_t owned);
int32_t RsBoatFit_SetTier(int32_t kind, int32_t slot, int32_t tier);
// Every kind back to unowned and its defaults, as a new save.
void RsBoatFit_Reset(void);

// --- the rules (RsBoatFit.cpp) --------------------------------------------------------------------

// Whether the upgrade could be built now, without building it: RS_BOAT_FIT_OK or the first reason it cannot, in
// the order the enum lists them. `failIndex` (may be NULL) gets the requirement's or the cost's index.
int32_t RsBoatFit_Check(int32_t id, int32_t* failIndex);
// Builds it: checks as above, then pays its costs and applies its fits. A dialogue option or the console calls it.
int32_t RsBoatFit_Build(int32_t id, int32_t* failIndex);
// Applies its fits free, ignoring the unlock, the requirements and the costs: a quest's gift. Still refuses a fit
// onto a boat the player does not own, so a reward list grants the boat before its parts.
int32_t RsBoatFit_Grant(int32_t id);
// Swaps a look, free, either way. Refuses an unowned boat.
int32_t RsBoatFit_SetLook(int32_t kind, int32_t look, int32_t option);

// --- quest predicates (QuestPredicate.h's BoatOwned and BoatTierAtLeast) --------------------------

// BoatTierAtLeast's `a`: the kind and the slot in one operand. A slot fits in 8 bits.
#define RS_BOAT_FIT_KEY(kind, slot) ((kind) * 256 + (slot))
#define RS_BOAT_FIT_KEY_KIND(key) ((key) / 256)
#define RS_BOAT_FIT_KEY_SLOT(key) ((key) % 256)

// 1 when the predicate's operands name a real kind (and slot on it, and tier in range): registration's check.
int32_t RsBoatFit_PredicateOperandsValid(int32_t predKind, int32_t a, int32_t b);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_BOAT_FIT_H
