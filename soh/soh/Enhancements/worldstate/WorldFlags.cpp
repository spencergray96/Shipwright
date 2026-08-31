#include "WorldFlags.h"

#include <array>
#include <cassert>
#include <spdlog/spdlog.h>

#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

// The store. Deliberately a project-owned global, NOT a gSaveContext member: no vanilla struct
// change, no SaveContext resize, invisible to code that memcpys gSaveContext (savestates, Anchor).
// Two accepted costs, recorded in sturdy-bassoon#54: savestates do not capture the store, and the
// threaded save reads it live rather than from the gSaveContext snapshot (aligned u16, game-thread
// writes only - worst case a flag lands slightly newer than the rest of the save).
static std::array<uint16_t, WORLD_FLAG_WORDS> sWorldFlags = {};

static bool CheckBounds(int32_t flag, const char* op) {
    if (flag < 0 || flag >= WORLD_FLAG_MAX) {
        SPDLOG_ERROR("WorldFlags: {} out-of-range flag {} (max {})", op, flag, WORLD_FLAG_MAX - 1);
        assert(false && "world flag out of range");
        return false;
    }
    return true;
}

extern "C" int32_t Flags_GetWorldFlag(int32_t flag) {
    if (!CheckBounds(flag, "get")) {
        return 0;
    }
    return (sWorldFlags[flag >> 4] & (1 << (flag & 0xF))) != 0;
}

extern "C" void Flags_SetWorldFlag(int32_t flag) {
    if (!CheckBounds(flag, "set")) {
        return;
    }
    sWorldFlags[flag >> 4] |= (1 << (flag & 0xF));
}

extern "C" void Flags_UnsetWorldFlag(int32_t flag) {
    if (!CheckBounds(flag, "unset")) {
        return;
    }
    sWorldFlags[flag >> 4] &= ~(1 << (flag & 0xF));
}

extern "C" int32_t WorldFlags_CountSet(void) {
    int32_t count = 0;
    for (uint16_t word : sWorldFlags) {
        while (word) {
            count += word & 1;
            word >>= 1;
        }
    }
    return count;
}

// --- SaveManager section ---------------------------------------------------------------------

// Runs on every new game AND at the top of every LoadFile (SaveManager.cpp LoadFile -> InitFile),
// before the JSON is parsed. A save without the section therefore reads all-zero, and switching
// slots can never leak flags. Mirrors the randomizerInf reset in InitFileNormal.
static void WorldFlagsInitFile(bool isDebug) {
    sWorldFlags.fill(0);
}

// Reads OUR global, not the SaveContext snapshot the threaded save hands us; `saveContext`,
// `sectionID` and `fullSave` are part of the SaveFunc signature and unused.
static void SaveWorldFlags(SaveContext* saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveArray("flags", sWorldFlags.size(), [&](size_t i) {
        SaveManager::Instance->SaveData("", sWorldFlags[i]);
    });
}

// Version 1. Growing WORLD_FLAG_MAX needs no new version: LoadArray default-constructs the tail.
// Any layout change DOES need version 2 plus keeping this v1 handler registered, because a known
// section with an unregistered version is an error + debug assert on load.
static void LoadWorldFlagsV1() {
    SaveManager::Instance->LoadArray("flags", sWorldFlags.size(), [](size_t i) {
        SaveManager::Instance->LoadData("", sWorldFlags[i]);
    });
}

static void RegisterWorldFlags() {
    // ShipInit "*" registrations re-run on preset apply (Presets.cpp -> ShipInit::InitAll) and on
    // config-file drop (OTRGlobals.cpp SoH_HandleConfigDrop -> ShipInit::Init("*")); per-CVar
    // ShipInit::Init(cvarName) can never reach a no-cvar registration. AddSaveFunction asserts on
    // a duplicate name, so register exactly once.
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    SaveManager::Instance->AddInitFunction(WorldFlagsInitFile);
    SaveManager::Instance->AddSaveFunction("worldFlags", 1, SaveWorldFlags, true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction("worldFlags", 1, LoadWorldFlagsV1);
}

static RegisterShipInitFunc worldFlagsInitFunc(RegisterWorldFlags);
