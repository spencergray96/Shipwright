#include "QuestStore.h"

#include <array>
#include <cassert>
#include <spdlog/spdlog.h>

#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

// The store. A project-owned global, NOT a gSaveContext member, for the reasons WorldFlags.cpp
// records (no vanilla struct change, no SaveContext resize, invisible to savestates/Anchor
// memcpys). The same two accepted costs follow: savestates do not capture the store, and the
// threaded save reads it live from this global rather than from the gSaveContext snapshot.
// Game-thread writes only; an 8-byte entry can in principle tear between status and stepMask if
// a save is in flight during a write - worst case one quest lands slightly newer than the rest.
static std::array<QuestSaveEntry, QUEST_MAX> sQuests = {};

static bool CheckQuestId(int32_t questId, const char* op) {
    if (!QUEST_ID_IS_VALID(questId)) {
        SPDLOG_ERROR("QuestStore: {} out-of-range quest id {} (max {})", op, questId, QUEST_MAX - 1);
        assert(false && "quest id out of range");
        return false;
    }
    return true;
}

static bool CheckStatus(int32_t status, const char* op) {
    if (status < 0 || status >= QUEST_STATUS_COUNT) {
        SPDLOG_ERROR("QuestStore: {} out-of-range status {} (max {})", op, status, QUEST_STATUS_COUNT - 1);
        assert(false && "quest status out of range");
        return false;
    }
    return true;
}

static bool CheckStep(int32_t step, const char* op) {
    if (step < 0 || step >= QUEST_STEP_MAX) {
        SPDLOG_ERROR("QuestStore: {} out-of-range step {} (max {})", op, step, QUEST_STEP_MAX - 1);
        assert(false && "quest step out of range");
        return false;
    }
    return true;
}

extern "C" int32_t QuestStore_GetStatus(int32_t questId) {
    if (!CheckQuestId(questId, "get status")) {
        return QUEST_STATUS_NOT_STARTED;
    }
    return sQuests[questId].status;
}

extern "C" void QuestStore_SetStatus(int32_t questId, int32_t status) {
    if (!CheckQuestId(questId, "set status") || !CheckStatus(status, "set status")) {
        return;
    }
    sQuests[questId].status = static_cast<uint8_t>(status);
}

extern "C" uint32_t QuestStore_GetStepMask(int32_t questId) {
    if (!CheckQuestId(questId, "get steps")) {
        return 0;
    }
    return sQuests[questId].stepMask;
}

extern "C" void QuestStore_SetStepMask(int32_t questId, uint32_t stepMask) {
    if (!CheckQuestId(questId, "set steps")) {
        return;
    }
    sQuests[questId].stepMask = stepMask;
}

// Shifts are unsigned on purpose: `1 << 31` on an int is signed overflow.
extern "C" int32_t QuestStore_IsStepSet(int32_t questId, int32_t step) {
    if (!CheckQuestId(questId, "is step set") || !CheckStep(step, "is step set")) {
        return 0;
    }
    return (sQuests[questId].stepMask & (1u << step)) != 0;
}

extern "C" void QuestStore_SetStep(int32_t questId, int32_t step) {
    if (!CheckQuestId(questId, "set step") || !CheckStep(step, "set step")) {
        return;
    }
    sQuests[questId].stepMask |= (1u << step);
}

extern "C" void QuestStore_ClearStep(int32_t questId, int32_t step) {
    if (!CheckQuestId(questId, "clear step") || !CheckStep(step, "clear step")) {
        return;
    }
    sQuests[questId].stepMask &= ~(1u << step);
}

extern "C" void QuestStore_Reset(int32_t questId) {
    if (!CheckQuestId(questId, "reset")) {
        return;
    }
    sQuests[questId] = {};
}

extern "C" int32_t QuestStore_CountTouched(void) {
    int32_t count = 0;
    for (const QuestSaveEntry& entry : sQuests) {
        if (entry.status != 0 || entry.stepMask != 0) {
            count++;
        }
    }
    return count;
}

// --- SaveManager section ---------------------------------------------------------------------
//
// JSON shape, version 1:
//   "quests": { "entries": [ { "status": 0, "steps": 0 }, ... QUEST_MAX of them ] }
//
// Growing QUEST_MAX needs no new version: LoadArray default-constructs the tail (an entry past
// the file's data loads through an empty object, so every LoadData falls back to its default).
// Any LAYOUT change does need version 2 plus keeping this v1 loader registered, because a known
// section with an unregistered version is an error + debug assert on load.

// Runs on every new game AND at the top of every LoadFile (SaveManager.cpp LoadFile -> InitFile),
// before the JSON is parsed. A save without the section therefore reads all-zero, and switching
// slots can never leak quest state. Also runs for the debug save (isDebug), which is what the
// agent-test auto-boot lands on.
static void QuestsInitFile(bool isDebug) {
    sQuests.fill({});
}

// Reads OUR global, not the SaveContext snapshot the threaded save hands us; `saveContext`,
// `sectionID` and `fullSave` are part of the SaveFunc signature and unused.
static void SaveQuests(SaveContext* saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveArray("entries", sQuests.size(), [&](size_t i) {
        SaveManager::Instance->SaveStruct("", [&]() {
            SaveManager::Instance->SaveData("status", sQuests[i].status);
            SaveManager::Instance->SaveData("steps", sQuests[i].stepMask);
        });
    });
}

static void LoadQuestsV1() {
    SaveManager::Instance->LoadArray("entries", sQuests.size(), [](size_t i) {
        SaveManager::Instance->LoadStruct("", [i]() {
            // Load status through a wide temp: nlohmann's get_to narrows with a bare static_cast,
            // so reading straight into the u8 would turn a hand-edited 256 into 0 before any
            // range check could see it. A bad value is logged and asserted, then stored as-is
            // (clamped only to fit the u8) - no silent repair.
            int32_t status = 0;
            SaveManager::Instance->LoadData("status", status);
            if (status < 0 || status >= QUEST_STATUS_COUNT) {
                SPDLOG_ERROR("QuestStore: quest {} loaded with out-of-range status {} (max {})", i, status,
                             QUEST_STATUS_COUNT - 1);
                assert(false && "quest status in save file out of range");
                status = status < 0 ? 0 : 255;
            }
            sQuests[i].status = static_cast<uint8_t>(status);
            SaveManager::Instance->LoadData("steps", sQuests[i].stepMask);
        });
    });
}

static void RegisterQuestStore() {
    // ShipInit "*" registrations re-run on preset apply (Presets.cpp -> ShipInit::InitAll) and on
    // config-file drop (OTRGlobals.cpp SoH_HandleConfigDrop -> ShipInit::Init("*")).
    // AddSaveFunction asserts on a duplicate name, so register exactly once.
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    SaveManager::Instance->AddInitFunction(QuestsInitFile);
    SaveManager::Instance->AddSaveFunction("quests", 1, SaveQuests, true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction("quests", 1, LoadQuestsV1);
}

static RegisterShipInitFunc questStoreInitFunc(RegisterQuestStore);
