// The fit-out's store, its save section and its rules (sturdy-bassoon#263, RsBoatFit.h). The data is
// RsBoatFitCatalogue.cpp's.

#include "RsBoatFit.h"

#include <cstdio>
#include <string>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/rs/actors/RsActors.h"
#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
}

namespace {

// The store. A project-owned global, NOT a gSaveContext member, for the reasons WorldFlags.cpp and QuestStore.cpp
// record, with the same accepted costs: savestates do not capture it, and the threaded save reads it live. Written
// only from the game thread (the console's commands run there too), a byte at a time.
struct KindFit {
    bool owned;
    uint8_t tiers[RS_BOAT_SLOT_COUNT]; // 0 for a slot the kind does not have
    uint8_t looks[RS_BOAT_LOOK_COUNT];
};

KindFit sFit[RS_BOAT_KIND_COUNT] = {};

bool KindValid(int32_t kind) {
    return kind >= 0 && kind < RS_BOAT_KIND_COUNT;
}

// The tier whose stats stand for `tier`: its own row, else the highest written below it (a save from a build with
// more tiers, read by one with fewer, keeps its number and draws on the best this build knows).
const RsBoatTier* StatsTier(int32_t kind, int32_t slot, int32_t tier) {
    for (int32_t t = tier; t >= 0; t--) {
        if (const RsBoatTier* row = RsBoatFit_Tier(kind, slot, t)) {
            return row;
        }
    }
    return nullptr;
}

void Emit(const std::string& line) {
    RsAgent_Marker(line.c_str());
}

// Each fit raises its slot to its tier (never lowers it) or owns its boat.
void Apply(const RsBoatUpgrade* u) {
    for (int32_t f = 0; f < u->fitCount; f++) {
        const RsBoatUpgradeFit& fit = u->fits[f];
        if (fit.slot == RS_BOAT_FIT_OWN) {
            sFit[fit.kind].owned = true;
        } else if (sFit[fit.kind].tiers[fit.slot] < fit.tier) {
            sFit[fit.kind].tiers[fit.slot] = static_cast<uint8_t>(fit.tier);
        }
    }
}

// Every fit already holds.
bool AlreadyBuilt(const RsBoatUpgrade* u) {
    for (int32_t f = 0; f < u->fitCount; f++) {
        const RsBoatUpgradeFit& fit = u->fits[f];
        if (fit.slot == RS_BOAT_FIT_OWN ? !sFit[fit.kind].owned : sFit[fit.kind].tiers[fit.slot] < fit.tier) {
            return false;
        }
    }
    return true;
}

// A fit onto a boat the player does not own; owning it in the same upgrade counts.
bool OntoUnownedBoat(const RsBoatUpgrade* u) {
    for (int32_t f = 0; f < u->fitCount; f++) {
        const RsBoatUpgradeFit& fit = u->fits[f];
        if (fit.slot == RS_BOAT_FIT_OWN || sFit[fit.kind].owned) {
            continue;
        }
        bool ownedHere = false;
        for (int32_t g = 0; g < u->fitCount; g++) {
            ownedHere |= u->fits[g].slot == RS_BOAT_FIT_OWN && u->fits[g].kind == fit.kind;
        }
        if (!ownedHere) {
            return true;
        }
    }
    return false;
}

void MarkUpgrade(const char* event, const RsBoatUpgrade* u, int32_t result, int32_t failIndex) {
    char line[256];
    std::snprintf(line, sizeof(line), "rs_boat event=%s upgrade=%s result=%s fail_index=%d rupees=%d", event,
                  u != nullptr ? u->name : "?", RsBoatFit_ResultName(result), (int)failIndex,
                  (int)RsBoatFit_RupeesAfterPending());
    Emit(line);
}

// --- SaveManager section -----------------------------------------------------------------------------------
//
// JSON shape, version 1, one object per kind, every key by NAME:
//   "rsBoats": { "skiff": { "owned": 1, "hull": 1, "helm": 0, "sails": 0, "keel": 0, "salvaging_hook": 1,
//                           "wind_catcher": 0, "sail_pattern": "red" }, "sloop": { ... }, "raft": { ... } }
// Only the slots a kind has are written. Because everything is keyed by name, a new kind, slot or look is a new
// key at version 1 and reads its default from an older file; what needs version 2 is changing what a key MEANS.
// Renaming a slot, look option or kind is a save break: its old key would be dropped.
//
// A save written before this landed has no section and reads as nothing owned, every default. An older build
// reading a newer save warns and skips the unknown section. Neither is a save break.

// Runs on every new game and at the top of every LoadFile, before the JSON is parsed (RsPrefs.cpp's reason): a
// file with no `rsBoats` section reads the defaults, not the previous slot's boats.
void RsBoatFitInitFile(bool isDebug) {
    RsBoatFit_Reset();
}

void SaveRsBoats(SaveContext* saveContext, int sectionID, bool fullSave) {
    for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
        SaveManager::Instance->SaveStruct(RsBoat_KindName(kind), [kind]() {
            SaveManager::Instance->SaveData("owned", sFit[kind].owned ? 1 : 0);
            for (int32_t slot = 0; slot < RS_BOAT_SLOT_COUNT; slot++) {
                if (RsBoatFit_KindHasSlot(kind, slot)) {
                    SaveManager::Instance->SaveData(RsBoatFit_SlotName(slot), (int32_t)sFit[kind].tiers[slot]);
                }
            }
            for (int32_t look = 0; look < RS_BOAT_LOOK_COUNT; look++) {
                SaveManager::Instance->SaveData(RsBoatFit_LookName(look),
                                                std::string(RsBoatFit_LookOptionName(look, sFit[kind].looks[look])));
            }
        });
    }
}

// A bad value is logged and repaired, never asserted: it comes from a FILE, and an assert inside LoadFile hangs the
// agent loop (RsPrefs.cpp). Values are read through wide temps for nlohmann's narrowing (QuestStore.cpp).
void LoadRsBoatsV1() {
    for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
        SaveManager::Instance->LoadStruct(RsBoat_KindName(kind), [kind]() {
            int32_t owned = 0;
            SaveManager::Instance->LoadData("owned", owned, 0);
            sFit[kind].owned = owned != 0;
            for (int32_t slot = 0; slot < RS_BOAT_SLOT_COUNT; slot++) {
                if (!RsBoatFit_KindHasSlot(kind, slot)) {
                    continue;
                }
                int32_t tier = 0;
                SaveManager::Instance->LoadData(RsBoatFit_SlotName(slot), tier, 0);
                if (tier < 0 || tier > RS_BOAT_TIER_MAX) {
                    SPDLOG_ERROR("RsBoatFit: save file's {} {} tier {} is out of range; using 0", RsBoat_KindName(kind),
                                 RsBoatFit_SlotName(slot), tier);
                    tier = 0;
                } else if (tier > RsBoatFit_TopTier(kind, slot)) {
                    // Kept: a tier this build has no row for is still what the player built (StatsTier).
                    SPDLOG_WARN("RsBoatFit: save file's {} {} tier {} is past this build's highest ({})",
                                RsBoat_KindName(kind), RsBoatFit_SlotName(slot), tier,
                                RsBoatFit_TopTier(kind, slot));
                }
                sFit[kind].tiers[slot] = static_cast<uint8_t>(tier);
            }
            for (int32_t look = 0; look < RS_BOAT_LOOK_COUNT; look++) {
                std::string name;
                SaveManager::Instance->LoadData(RsBoatFit_LookName(look), name,
                                                std::string(RsBoatFit_LookOptionName(look, 0)));
                int32_t option = RsBoatFit_LookOptionByName(look, name.c_str());
                if (option < 0) {
                    SPDLOG_ERROR("RsBoatFit: save file's {} {} '{}' is no option; using the default",
                                 RsBoat_KindName(kind), RsBoatFit_LookName(look), name);
                    option = 0;
                }
                sFit[kind].looks[look] = static_cast<uint8_t>(option);
            }
        });
    }
}

void RegisterRsBoatFit() {
    // ShipInit "*" functions re-run on preset apply and config drop; AddSaveFunction asserts on a duplicate name.
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    SaveManager::Instance->AddInitFunction(RsBoatFitInitFile);
    SaveManager::Instance->AddSaveFunction("rsBoats", 1, SaveRsBoats, true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction("rsBoats", 1, LoadRsBoatsV1);
}

RegisterShipInitFunc rsBoatFitInitFunc(RegisterRsBoatFit);

constexpr QuestPredicate kPackedByMacro = QP_BOAT_TIER_AT_LEAST(2, 5, 1);
static_assert(rs::quest::BoatTierAtLeast(2, 5, 1).a == RS_BOAT_FIT_KEY(2, 5) &&
                  kPackedByMacro.a == RS_BOAT_FIT_KEY(2, 5),
              "QuestPredicate.h's BoatTierAtLeast and QP_BOAT_TIER_AT_LEAST pack as RS_BOAT_FIT_KEY does");

} // namespace

// Rupees_ChangeBy feeds the accumulator, which Interface_Update drains one a frame.
extern "C" int32_t RsBoatFit_RupeesAfterPending(void) {
    return gSaveContext.rupees + gSaveContext.rupeeAccumulator;
}

extern "C" int32_t RsBoatFit_Owned(int32_t kind) {
    return KindValid(kind) && sFit[kind].owned ? 1 : 0;
}

extern "C" int32_t RsBoatFit_TierOf(int32_t kind, int32_t slot) {
    return RsBoatFit_KindHasSlot(kind, slot) ? sFit[kind].tiers[slot] : 0;
}

extern "C" int32_t RsBoatFit_LookOf(int32_t kind, int32_t look) {
    return (KindValid(kind) && look >= 0 && look < RS_BOAT_LOOK_COUNT) ? sFit[kind].looks[look] : 0;
}

extern "C" void RsBoatFit_Stats(int32_t kind, RsBoatFitStats* out) {
    *out = {};
    if (!KindValid(kind)) {
        return;
    }
    for (int32_t slot = 0; slot < RS_BOAT_SLOT_COUNT; slot++) {
        if (!RsBoatFit_KindHasSlot(kind, slot)) {
            continue;
        }
        const RsBoatTier* t = StatsTier(kind, slot, sFit[kind].tiers[slot]);
        if (t == nullptr) {
            continue; // an empty hotspot
        }
        if (t->topSpeed > 0.0f) {
            out->topSpeed = t->topSpeed;
        }
        if (t->accel > 0.0f) {
            out->accel = t->accel;
        }
        out->hitpoints += t->hitpoints;
        out->waters |= t->waters;
    }
}

extern "C" void RsBoatFit_Summary(int32_t kind, char* buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return;
    }
    std::string out = std::string(RsBoat_KindName(kind)) + (RsBoatFit_Owned(kind) ? ": owned" : ": not owned");
    for (int pass = 0; pass < 2; pass++) { // the core parts, then the hotspots
        bool first = true;
        for (int32_t slot = 0; slot < RS_BOAT_SLOT_COUNT; slot++) {
            if (!RsBoatFit_KindHasSlot(kind, slot) || (RsBoatFit_SlotIsCore(slot) != 0) != (pass == 0)) {
                continue;
            }
            const int32_t tier = RsBoatFit_TierOf(kind, slot);
            const RsBoatTier* row = RsBoatFit_Tier(kind, slot, tier);
            out += first ? " | " : ", ";
            out += std::string(RsBoatFit_SlotName(slot)) + " ";
            out += row != nullptr ? row->name : (tier == 0 ? "-" : std::to_string(tier));
            first = false;
        }
    }
    for (int32_t look = 0; look < RS_BOAT_LOOK_COUNT; look++) {
        out += std::string(" | ") + RsBoatFit_LookName(look) + " " +
               RsBoatFit_LookOptionName(look, RsBoatFit_LookOf(kind, look));
    }
    std::snprintf(buf, len, "%s", out.c_str());
}

extern "C" int32_t RsBoatFit_SetOwned(int32_t kind, int32_t owned) {
    if (!KindValid(kind)) {
        return RS_BOAT_FIT_UNKNOWN;
    }
    sFit[kind].owned = owned != 0;
    return RS_BOAT_FIT_OK;
}

extern "C" int32_t RsBoatFit_SetTier(int32_t kind, int32_t slot, int32_t tier) {
    if (!RsBoatFit_KindHasSlot(kind, slot) || tier < 0 || tier > RS_BOAT_TIER_MAX) {
        return RS_BOAT_FIT_UNKNOWN;
    }
    sFit[kind].tiers[slot] = static_cast<uint8_t>(tier);
    return RS_BOAT_FIT_OK;
}

extern "C" void RsBoatFit_Reset(void) {
    for (KindFit& fit : sFit) {
        fit = {};
    }
}

extern "C" int32_t RsBoatFit_Check(int32_t id, int32_t* failIndex) {
    if (failIndex != nullptr) {
        *failIndex = -1;
    }
    const RsBoatUpgrade* u = RsBoatFit_Upgrade(id);
    if (u == nullptr) {
        return RS_BOAT_FIT_UNKNOWN;
    }
    if (AlreadyBuilt(u)) {
        return RS_BOAT_FIT_ALREADY;
    }
    if (!Flags_GetWorldFlag(u->unlockFlag)) {
        return RS_BOAT_FIT_LOCKED;
    }
    if (OntoUnownedBoat(u)) {
        return RS_BOAT_FIT_NOT_OWNED;
    }
    for (int32_t i = 0; i < u->reqCount; i++) {
        if (!QuestPredicate_Eval(&u->reqs[i])) {
            if (failIndex != nullptr) {
                *failIndex = i;
            }
            return RS_BOAT_FIT_REQUIREMENT;
        }
    }
    int32_t rupees = 0;
    for (int32_t i = 0; i < u->costCount; i++) {
        switch (u->costs[i].kind) {
            case RS_BOAT_COST_RUPEES:
                rupees += u->costs[i].amount;
                if (rupees > RsBoatFit_RupeesAfterPending()) {
                    if (failIndex != nullptr) {
                        *failIndex = i;
                    }
                    return RS_BOAT_FIT_CANT_AFFORD;
                }
                break;
            default:
                break; // unreachable: CheckCatalogue refuses an unknown kind
        }
    }
    return RS_BOAT_FIT_OK;
}

extern "C" int32_t RsBoatFit_Build(int32_t id, int32_t* failIndex) {
    int32_t index = -1;
    const int32_t result = RsBoatFit_Check(id, &index);
    const RsBoatUpgrade* u = RsBoatFit_Upgrade(id);
    if (result == RS_BOAT_FIT_OK) {
        for (int32_t i = 0; i < u->costCount; i++) {
            if (u->costs[i].kind == RS_BOAT_COST_RUPEES) {
                Rupees_ChangeBy(static_cast<s16>(-u->costs[i].amount));
            }
        }
        Apply(u);
        SPDLOG_INFO("RsBoatFit: built {}", u->name);
    }
    MarkUpgrade("build", u, result, index);
    if (failIndex != nullptr) {
        *failIndex = index;
    }
    return result;
}

extern "C" int32_t RsBoatFit_Grant(int32_t id) {
    const RsBoatUpgrade* u = RsBoatFit_Upgrade(id);
    int32_t result = RS_BOAT_FIT_OK;
    if (u == nullptr) {
        result = RS_BOAT_FIT_UNKNOWN;
    } else if (AlreadyBuilt(u)) {
        result = RS_BOAT_FIT_ALREADY;
    } else if (OntoUnownedBoat(u)) {
        result = RS_BOAT_FIT_NOT_OWNED;
    } else {
        Apply(u);
        SPDLOG_INFO("RsBoatFit: granted {}", u->name);
    }
    MarkUpgrade("grant", u, result, -1);
    return result;
}

extern "C" int32_t RsBoatFit_SetLook(int32_t kind, int32_t look, int32_t option) {
    int32_t result = RS_BOAT_FIT_OK;
    if (!KindValid(kind) || option < 0 || option >= RsBoatFit_LookOptionCount(look)) {
        result = RS_BOAT_FIT_UNKNOWN;
    } else if (!sFit[kind].owned) {
        result = RS_BOAT_FIT_NOT_OWNED;
    } else if (!RsBoatFit_LookOptionAllowed(look, option)) {
        result = RS_BOAT_FIT_REQUIREMENT;
    } else {
        sFit[kind].looks[look] = static_cast<uint8_t>(option);
    }
    char line[160];
    std::snprintf(line, sizeof(line), "rs_boat event=look kind=%s look=%s option=%s result=%s",
                  RsBoat_KindName(kind), RsBoatFit_LookName(look), RsBoatFit_LookOptionName(look, option),
                  RsBoatFit_ResultName(result));
    Emit(line);
    return result;
}

extern "C" int32_t RsBoatFit_PredicateOperandsValid(int32_t predKind, int32_t a, int32_t b) {
    switch (predKind) {
        case QUEST_PRED_BOAT_OWNED:
            return KindValid(a) ? 1 : 0;
        case QUEST_PRED_BOAT_TIER_AT_LEAST:
            return a >= 0 && RsBoatFit_KindHasSlot(RS_BOAT_FIT_KEY_KIND(a), RS_BOAT_FIT_KEY_SLOT(a)) && b >= 0 &&
                           b <= RS_BOAT_TIER_MAX
                       ? 1
                       : 0;
        default:
            return 0;
    }
}
