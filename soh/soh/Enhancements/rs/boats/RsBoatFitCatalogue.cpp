// The fit-out's data (sturdy-bassoon#263, RsBoatFit.h): which slots each kind has, what each tier does, the looks'
// options and the upgrades. Checked once at startup (CheckCatalogue): a row naming a slot its kind lacks, a missing
// default, an upgrade out of order or an unlock outside the production band is a bug here, and asserts.
//
// Adding a tier: a row in kTiers and, so it can be built, an upgrade in kUpgrades with its unlock flag
// (WorldFlagIds.h) and an enumerator in RsBoatUpgradeId. Adding a facility or core part: an RsBoatSlot enumerator,
// its name and class in kSlots, and the kinds that have it in kKindSlots. None of it touches the save's layout.

#include "RsBoatFit.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

namespace {

struct SlotInfo {
    const char* name;
    bool core;
};

const SlotInfo kSlots[RS_BOAT_SLOT_COUNT] = {
    { "hull", true },
    { "helm", true },
    { "sails", true },
    { "keel", true },
    { "salvaging_hook", false },
    { "wind_catcher", false },
    { "cannon", false },
};

// Which kind has which slot. The hotspots are the owner's pick (#263, 2026-10-30), far fewer than RS's 1/7/11 and
// each typed to one facility: RS's either/or hotspots (a cannon or a misc facility) are deliberately not modelled.
static_assert(RS_BOAT_KIND_SKIFF == 0 && RS_BOAT_KIND_SLOOP == 1 && RS_BOAT_KIND_RAFT == 2, "kKindSlots' row order");
static_assert(RS_BOAT_SLOT_COUNT == 7, "kSlots and kKindSlots' columns list every slot");
const bool kKindSlots[RS_BOAT_KIND_COUNT][RS_BOAT_SLOT_COUNT] = {
    //                   hull  helm  sails keel   hook  wind   cannon
    /* skiff */ { true, true, true, true, true, true, false },
    /* sloop */ { true, true, true, true, true, true, true },
    /* raft  */ { true, true, true, false, true, false, false },
};

// Every tier. A core part's tier 0 is its kind's free default and has a row; a facility's 0 is empty and has none.
// Stats at 0 leave the boat's own standing (RsBoatTier). The locs are the wiki's, in its tier order, and unchecked
// against the cache: the boat draws its kind's one model (RsBoat.c's sRsBoatKinds: skiff 59503, sloop 59508, raft
// 59496) whatever is fitted, until a tier draws its own (a later issue). The wiki's sloop bronze keel is 59517,
// which is also its iron skiff keel; 59523 is the sequence's.
const RsBoatTier kTiers[] = {
    // kind, slot, tier, name, loc, topSpeed, accel, hitpoints, waters
    { RS_BOAT_KIND_RAFT, RS_BOAT_SLOT_HULL, 0, "wooden", 59494, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_RAFT, RS_BOAT_SLOT_HELM, 0, "bronze", 59554, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_RAFT, RS_BOAT_SLOT_SAILS, 0, "wooden", 59530, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_RAFT, RS_BOAT_SLOT_SALVAGING_HOOK, 1, "bronze", 60483, 0.0f, 0.0f, 0, 0 },

    { RS_BOAT_KIND_SKIFF, RS_BOAT_SLOT_HULL, 0, "wooden", 59501, 0.0f, 0.0f, 0, 0 },
    // The speed upgrade the owner tuned on #262 (2026-10-26): 14 to 18. Turning rises with it (RsBoat_TurnRate).
    { RS_BOAT_KIND_SKIFF, RS_BOAT_SLOT_HULL, 1, "oak", 59502, 18.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SKIFF, RS_BOAT_SLOT_HELM, 0, "bronze", 59576, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SKIFF, RS_BOAT_SLOT_SAILS, 0, "wooden", 59537, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SKIFF, RS_BOAT_SLOT_KEEL, 0, "bronze", 59516, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SKIFF, RS_BOAT_SLOT_SALVAGING_HOOK, 1, "bronze", 60490, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SKIFF, RS_BOAT_SLOT_WIND_CATCHER, 1, "wind_catcher", 59683, 0.0f, 0.0f, 0, 0 },

    { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_HULL, 0, "wooden", 59508, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_HULL, 1, "oak", 59509, 14.0f, 0.0f, 0, 0 }, // 12 to 14
    { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_HELM, 0, "bronze", 59598, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_SAILS, 0, "wooden", 59544, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_KEEL, 0, "bronze", 59523, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_SALVAGING_HOOK, 1, "bronze", 60497, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_WIND_CATCHER, 1, "wind_catcher", 59683, 0.0f, 0.0f, 0, 0 },
    { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_CANNON, 1, "bronze", 59446, 0.0f, 0.0f, 0, 0 },
};

constexpr int kTierCount = static_cast<int>(sizeof(kTiers) / sizeof(kTiers[0]));

// A look's options, unordered: index 0 is the default. Each may carry requirements (a race's sail, later); none do.
// `recolours` 0 draws the model's own colour; 1 draws `prim` over the material the boat's list hands to the look
// (RsBoatFit_LookColour).
struct LookOption {
    const char* name;
    int32_t reqCount;
    QuestPredicate reqs[2];
    int32_t recolours;
    uint8_t prim[3];
};

struct LookInfo {
    const char* name;
    int32_t optionCount;
    LookOption options[8];
};

// RS's sail patterns (the wiki's Sail customisation): plain is the default, the seven colours need 41 Sailing and
// 27 Construction there. Free and ungated here. In the cache (build 240) each kind's ten "Sails" locs are one cloth:
// three plain fabrics, then one model whose cloth colour, HSL16 4510, the other seven recolour (raft 29509-29515,
// skiff 29519-29525, sloop 29529-29535, in the order black, yellow, purple, green, blue, pink, red). So a pattern is
// a prim colour over the cloth our still sail already draws: RS's HSL16 (in the comment) through the pipeline's own
// conversion, rs_palette.py's rs_prim, as every imported colour is. Plain keeps the archive's 226, 226, 222. The
// pennant stays red, as in RS. sturdy-bassoon#283.
const LookInfo kLooks[RS_BOAT_LOOK_COUNT] = {
    { "sail_pattern",
      8,
      { { "plain", 0, {}, 0, { 0, 0, 0 } },
        { "black", 0, {}, 1, { 39, 35, 35 } },     // 12
        { "blue", 0, {}, 1, { 47, 48, 100 } },     // -22116
        { "green", 0, {}, 1, { 35, 101, 37 } },    // 22042
        { "pink", 0, {}, 1, { 196, 147, 138 } },   // 1356
        { "purple", 0, {}, 1, { 109, 72, 122 } },  // -14041
        { "red", 0, {}, 1, { 135, 31, 7 } },       // 1950
        { "yellow", 0, {}, 1, { 249, 246, 15 } } } }, // 11200
};

// What can be built. An upgrade's fits on a boat imply owning it (RsBoatFit_Check), so they do not list it. The
// costs are the owner's placeholders for testing, 5 and 10 rupees (#263, 2026-10-30), marked so. A tier that
// should only follow the one below it says so with a BoatTierAtLeast requirement; none here has one below it.
const RsBoatUpgrade kUpgrades[RS_BOAT_UPGRADE_COUNT] = {
    { RS_BOAT_UPGRADE_RAFT,
      "raft",
      WORLD_FLAG_BOAT_UNLOCK_RAFT,
      1,
      { { RS_BOAT_KIND_RAFT, RS_BOAT_FIT_OWN, 0 } },
      0,
      {},
      0,
      {},
      0 },
    { RS_BOAT_UPGRADE_SKIFF,
      "skiff",
      WORLD_FLAG_BOAT_UNLOCK_SKIFF,
      1,
      { { RS_BOAT_KIND_SKIFF, RS_BOAT_FIT_OWN, 0 } },
      0,
      {},
      1,
      { { RS_BOAT_COST_RUPEES, 0, 10 } },
      1 },
    { RS_BOAT_UPGRADE_SLOOP,
      "sloop",
      WORLD_FLAG_BOAT_UNLOCK_SLOOP,
      1,
      { { RS_BOAT_KIND_SLOOP, RS_BOAT_FIT_OWN, 0 } },
      0,
      {},
      1,
      { { RS_BOAT_COST_RUPEES, 0, 10 } },
      1 },
    { RS_BOAT_UPGRADE_SKIFF_HULL_OAK,
      "skiff_hull_oak",
      WORLD_FLAG_BOAT_UNLOCK_SKIFF_HULL_OAK,
      1,
      { { RS_BOAT_KIND_SKIFF, RS_BOAT_SLOT_HULL, 1 } },
      0,
      {},
      1,
      { { RS_BOAT_COST_RUPEES, 0, 10 } },
      1 },
    { RS_BOAT_UPGRADE_SLOOP_HULL_OAK,
      "sloop_hull_oak",
      WORLD_FLAG_BOAT_UNLOCK_SLOOP_HULL_OAK,
      1,
      { { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_HULL, 1 } },
      0,
      {},
      1,
      { { RS_BOAT_COST_RUPEES, 0, 10 } },
      1 },
    { RS_BOAT_UPGRADE_RAFT_HOOK_BRONZE,
      "raft_hook_bronze",
      WORLD_FLAG_BOAT_UNLOCK_RAFT_HOOK_BRONZE,
      1,
      { { RS_BOAT_KIND_RAFT, RS_BOAT_SLOT_SALVAGING_HOOK, 1 } },
      0,
      {},
      1,
      { { RS_BOAT_COST_RUPEES, 0, 5 } },
      1 },
    { RS_BOAT_UPGRADE_SKIFF_HOOK_BRONZE,
      "skiff_hook_bronze",
      WORLD_FLAG_BOAT_UNLOCK_SKIFF_HOOK_BRONZE,
      1,
      { { RS_BOAT_KIND_SKIFF, RS_BOAT_SLOT_SALVAGING_HOOK, 1 } },
      0,
      {},
      1,
      { { RS_BOAT_COST_RUPEES, 0, 5 } },
      1 },
    { RS_BOAT_UPGRADE_SLOOP_HOOK_BRONZE,
      "sloop_hook_bronze",
      WORLD_FLAG_BOAT_UNLOCK_SLOOP_HOOK_BRONZE,
      1,
      { { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_SALVAGING_HOOK, 1 } },
      0,
      {},
      1,
      { { RS_BOAT_COST_RUPEES, 0, 5 } },
      1 },
    { RS_BOAT_UPGRADE_SKIFF_WIND_CATCHER,
      "skiff_wind_catcher",
      WORLD_FLAG_BOAT_UNLOCK_SKIFF_WIND_CATCHER,
      1,
      { { RS_BOAT_KIND_SKIFF, RS_BOAT_SLOT_WIND_CATCHER, 1 } },
      0,
      {},
      1,
      { { RS_BOAT_COST_RUPEES, 0, 5 } },
      1 },
    { RS_BOAT_UPGRADE_SLOOP_WIND_CATCHER,
      "sloop_wind_catcher",
      WORLD_FLAG_BOAT_UNLOCK_SLOOP_WIND_CATCHER,
      1,
      { { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_WIND_CATCHER, 1 } },
      0,
      {},
      1,
      { { RS_BOAT_COST_RUPEES, 0, 5 } },
      1 },
    { RS_BOAT_UPGRADE_SLOOP_CANNON_BRONZE,
      "sloop_cannon_bronze",
      WORLD_FLAG_BOAT_UNLOCK_SLOOP_CANNON_BRONZE,
      1,
      { { RS_BOAT_KIND_SLOOP, RS_BOAT_SLOT_CANNON, 1 } },
      0,
      {},
      1,
      { { RS_BOAT_COST_RUPEES, 0, 10 } },
      1 },
};

const char* const kResultNames[] = {
    "ok", "unknown", "already", "locked", "not_owned", "requirement", "cant_afford",
};
static_assert(sizeof(kResultNames) / sizeof(kResultNames[0]) == RS_BOAT_FIT_RESULT_COUNT,
              "kResultNames names every RsBoatFitResult");

bool KindValid(int32_t kind) {
    return kind >= 0 && kind < RS_BOAT_KIND_COUNT;
}

bool SlotValid(int32_t slot) {
    return slot >= 0 && slot < RS_BOAT_SLOT_COUNT;
}

bool Problem(const char* what, int32_t index) {
    SPDLOG_ERROR("RsBoatFit: catalogue {}[{}] is wrong", what, index);
    assert(false && "boat fit-out catalogue is wrong");
    return false;
}

// A requirement's operands in range: the boat words through the check quest registration uses, the others by
// kind only (a quest id or a flag past its store is the quest system's own refusal, reported at its registration).
bool RequirementValid(const QuestPredicate& p) {
    if (p.kind == QUEST_PRED_BOAT_OWNED || p.kind == QUEST_PRED_BOAT_TIER_AT_LEAST) {
        return RsBoatFit_PredicateOperandsValid(p.kind, p.a, p.b) != 0;
    }
    return p.kind >= 0 && p.kind < QUEST_PRED_KIND_COUNT;
}

// The save keys one kind's object holds (RsBoatFit.cpp): "owned", every slot's name, every look's name. A slot and
// a look sharing a name would overwrite each other in the file, silently.
bool SaveKeysDistinct() {
    std::vector<std::string> keys = { "owned" };
    for (int32_t slot = 0; slot < RS_BOAT_SLOT_COUNT; slot++) {
        keys.push_back(kSlots[slot].name);
    }
    for (int32_t look = 0; look < RS_BOAT_LOOK_COUNT; look++) {
        keys.push_back(kLooks[look].name);
    }
    for (size_t i = 0; i < keys.size(); i++) {
        for (size_t j = 0; j < i; j++) {
            if (keys[i] == keys[j]) {
                return false;
            }
        }
    }
    return true;
}

// The catalogue's own consistency, once at startup. A slip here is a bug in this file, not an outcome.
void CheckCatalogue() {
    static bool checked = false;
    if (checked) {
        return;
    }
    checked = true;
    for (int i = 0; i < kTierCount; i++) {
        const RsBoatTier& t = kTiers[i];
        if (!KindValid(t.kind) || !SlotValid(t.slot) || !kKindSlots[t.kind][t.slot] || t.tier < 0 ||
            t.tier > RS_BOAT_TIER_MAX || (t.tier == 0 && !kSlots[t.slot].core) || t.name == nullptr) {
            Problem("tier", i);
        }
        for (int j = 0; j < i; j++) {
            if (kTiers[j].kind == t.kind && kTiers[j].slot == t.slot && kTiers[j].tier == t.tier) {
                Problem("tier (a duplicate)", i);
            }
        }
    }
    for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
        for (int32_t slot = 0; slot < RS_BOAT_SLOT_COUNT; slot++) {
            if (kKindSlots[kind][slot] && kSlots[slot].core && RsBoatFit_Tier(kind, slot, 0) == nullptr) {
                Problem("default (a core part with no tier 0)", RS_BOAT_FIT_KEY(kind, slot));
            }
        }
    }
    for (int32_t i = 0; i < RS_BOAT_UPGRADE_COUNT; i++) {
        const RsBoatUpgrade& u = kUpgrades[i];
        if (u.id != i || u.name == nullptr || u.fitCount < 1 || u.fitCount > RS_BOAT_UPGRADE_MAX_FITS ||
            u.reqCount < 0 || u.reqCount > RS_BOAT_UPGRADE_MAX_REQS || u.costCount < 0 ||
            u.costCount > RS_BOAT_UPGRADE_MAX_COSTS) {
            Problem("upgrade", i);
            continue;
        }
        // A production unlock below the placement band: a quest may set it, and `quest debugwipe` never clears it.
        if (u.unlockFlag < 0 || u.unlockFlag >= WORLD_FLAG_PLACEMENT_FIRST) {
            Problem("upgrade (its unlock flag)", i);
        }
        for (int32_t f = 0; f < u.fitCount; f++) {
            const RsBoatUpgradeFit& fit = u.fits[f];
            const bool own = fit.slot == RS_BOAT_FIT_OWN && KindValid(fit.kind);
            if (!own && RsBoatFit_Tier(fit.kind, fit.slot, fit.tier) == nullptr) {
                Problem("upgrade (a fit with no tier)", i);
            }
        }
        for (int32_t c = 0; c < u.costCount; c++) {
            if (u.costs[c].kind < 0 || u.costs[c].kind >= RS_BOAT_COST_KIND_COUNT || u.costs[c].amount <= 0) {
                Problem("upgrade (a cost)", i);
            }
        }
        for (int32_t r = 0; r < u.reqCount; r++) {
            if (!RequirementValid(u.reqs[r])) {
                Problem("upgrade (a requirement)", i);
            }
        }
    }
    for (int32_t look = 0; look < RS_BOAT_LOOK_COUNT; look++) {
        for (int32_t o = 0; o < kLooks[look].optionCount; o++) {
            for (int32_t r = 0; r < kLooks[look].options[o].reqCount; r++) {
                if (!RequirementValid(kLooks[look].options[o].reqs[r])) {
                    Problem("look option (a requirement)", look * 256 + o);
                }
            }
        }
    }
    if (!SaveKeysDistinct()) {
        Problem("save keys (a slot and a look share a name)", 0);
    }
}

RegisterShipInitFunc rsBoatFitCatalogueInitFunc(CheckCatalogue);

} // namespace

extern "C" const char* RsBoatFit_SlotName(int32_t slot) {
    return SlotValid(slot) ? kSlots[slot].name : "?";
}

extern "C" int32_t RsBoatFit_SlotByName(const char* name) {
    for (int32_t slot = 0; name != nullptr && slot < RS_BOAT_SLOT_COUNT; slot++) {
        if (std::strcmp(kSlots[slot].name, name) == 0) {
            return slot;
        }
    }
    return -1;
}

extern "C" int32_t RsBoatFit_SlotIsCore(int32_t slot) {
    return SlotValid(slot) && kSlots[slot].core ? 1 : 0;
}

extern "C" int32_t RsBoatFit_KindHasSlot(int32_t kind, int32_t slot) {
    return KindValid(kind) && SlotValid(slot) && kKindSlots[kind][slot] ? 1 : 0;
}

extern "C" const RsBoatTier* RsBoatFit_Tier(int32_t kind, int32_t slot, int32_t tier) {
    for (int i = 0; i < kTierCount; i++) {
        if (kTiers[i].kind == kind && kTiers[i].slot == slot && kTiers[i].tier == tier) {
            return &kTiers[i];
        }
    }
    return nullptr;
}

extern "C" int32_t RsBoatFit_TopTier(int32_t kind, int32_t slot) {
    int32_t top = 0;
    for (int i = 0; i < kTierCount; i++) {
        if (kTiers[i].kind == kind && kTiers[i].slot == slot && kTiers[i].tier > top) {
            top = kTiers[i].tier;
        }
    }
    return top;
}

extern "C" const char* RsBoatFit_LookName(int32_t look) {
    return (look >= 0 && look < RS_BOAT_LOOK_COUNT) ? kLooks[look].name : "?";
}

extern "C" int32_t RsBoatFit_LookByName(const char* name) {
    for (int32_t look = 0; name != nullptr && look < RS_BOAT_LOOK_COUNT; look++) {
        if (std::strcmp(kLooks[look].name, name) == 0) {
            return look;
        }
    }
    return -1;
}

extern "C" int32_t RsBoatFit_LookOptionCount(int32_t look) {
    return (look >= 0 && look < RS_BOAT_LOOK_COUNT) ? kLooks[look].optionCount : 0;
}

extern "C" const char* RsBoatFit_LookOptionName(int32_t look, int32_t option) {
    if (option < 0 || option >= RsBoatFit_LookOptionCount(look)) {
        return "?";
    }
    return kLooks[look].options[option].name;
}

extern "C" int32_t RsBoatFit_LookOptionByName(int32_t look, const char* name) {
    for (int32_t option = 0; name != nullptr && option < RsBoatFit_LookOptionCount(look); option++) {
        if (std::strcmp(kLooks[look].options[option].name, name) == 0) {
            return option;
        }
    }
    return -1;
}

// Here rather than in RsBoatFit.cpp so LookOption stays this file's.
extern "C" int32_t RsBoatFit_LookOptionAllowed(int32_t look, int32_t option) {
    if (option < 0 || option >= RsBoatFit_LookOptionCount(look)) {
        return 0;
    }
    const LookOption& opt = kLooks[look].options[option];
    for (int32_t i = 0; i < opt.reqCount; i++) {
        if (!QuestPredicate_Eval(&opt.reqs[i])) {
            return 0;
        }
    }
    return 1;
}

extern "C" int32_t RsBoatFit_LookColour(int32_t look, int32_t option, uint8_t rgb[3]) {
    if (option < 0 || option >= RsBoatFit_LookOptionCount(look) || !kLooks[look].options[option].recolours) {
        return 0;
    }
    std::memcpy(rgb, kLooks[look].options[option].prim, 3);
    return 1;
}

extern "C" const RsBoatUpgrade* RsBoatFit_Upgrade(int32_t id) {
    return (id >= 0 && id < RS_BOAT_UPGRADE_COUNT) ? &kUpgrades[id] : nullptr;
}

extern "C" int32_t RsBoatFit_UpgradeByName(const char* name) {
    for (int32_t id = 0; name != nullptr && id < RS_BOAT_UPGRADE_COUNT; id++) {
        if (std::strcmp(kUpgrades[id].name, name) == 0) {
            return id;
        }
    }
    return -1;
}

extern "C" const char* RsBoatFit_ResultName(int32_t result) {
    return (result >= 0 && result < RS_BOAT_FIT_RESULT_COUNT) ? kResultNames[result] : "?";
}
