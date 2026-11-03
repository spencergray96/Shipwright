// The inventory's store, its save section, its rules and what the player sees of a gain (sturdy-bassoon#286,
// RsItems.h). The tables are RsItemCatalogue.cpp's.

#include "RsItems.h"

#include <cstdio>
#include <string>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/actors/RsActors.h"
#include "soh/Enhancements/rs/actors/RsItemPickup.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/interactions/Interaction.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

// The store. A project-owned global, NOT a gSaveContext member, for the reasons WorldFlags.cpp and RsBoatFit.cpp
// record, with the same accepted costs: savestates do not capture it, and the threaded save reads it live. Written
// only from the game thread (the console's commands run there too).
int32_t sCounts[RS_ITEM_COUNT] = {};

bool ItemValid(int32_t item) {
    return item >= 0 && item < RS_ITEM_COUNT;
}

bool KeyValid(int32_t key) {
    return key >= 0 && key < RS_KEY_ITEM_COUNT;
}

void Mark(const char* event, const char* what, int32_t count, int32_t total, int32_t result, int32_t source) {
    char line[192];
    std::snprintf(line, sizeof(line), "rs_items event=%s item=%s count=%d total=%d result=%s source=%s", event, what,
                  (int)count, (int)total, RsItems_ResultName(result), RsItems_SourceName(source));
    RsAgent_Marker(line);
}

// --- the key items' ceremonies ---------------------------------------------------------------------------
//
// A queue rather than an offer from whoever granted it, because most grants have no one to offer from: a quest
// reward dispatched from a console line, a prop that cannot wait for Link to take anything (Interaction.cpp says
// why), several key items from one reward. So the GRANT sets the flag at once and queues the ceremony, and this
// plays the queue: once no textbox is up and Link is free, it hands Player the head's entry through SoH's
// GiveItemEntryWithoutActor - the path SoH's own time-saver queue takes - every frame until Player is seen
// holding it, then the next. The queue lives for the scene; losing it loses nothing but the show.
constexpr int32_t kQueueMax = 8;
int32_t sQueue[kQueueMax];
int32_t sQueued = 0;
int16_t sQueueScene = -1;

void PopCeremony() {
    for (int32_t i = 1; i < sQueued; i++) {
        sQueue[i - 1] = sQueue[i];
    }
    sQueued--;
}

void PlayCeremonies() {
    if (sQueued == 0 || gPlayState == nullptr) {
        return;
    }
    if (sQueueScene != gPlayState->sceneNum) {
        SPDLOG_INFO("RsItems: a scene change dropped {} key-item ceremonies", sQueued);
        sQueued = 0;
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }
    const int32_t key = sQueue[0];
    // Taken: Player is holding it up with our text id. The box and the fanfare are Player's from here.
    if ((player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) != 0) {
        if (player->getItemEntry.textId == RS_TEXT_KEY_ITEM_ID(key)) {
            char line[96];
            std::snprintf(line, sizeof(line), "rs_items event=ceremony item=%s", RsKeyItems_Def(key)->token);
            RsAgent_Marker(line);
            PopCeremony();
        }
        return;
    }
    if (Message_GetState(&gPlayState->msgCtx) != TEXT_STATE_NONE ||
        (player->stateFlags1 & (PLAYER_STATE1_TALKING | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_CARRYING_ACTOR)) !=
            0 ||
        Player_InBlockingCsMode(gPlayState, player)) {
        return;
    }
    GiveItemEntryWithoutActor(gPlayState, RsItemPickup_KeyEntry(key));
}

// --- SaveManager section ----------------------------------------------------------------------------------------
//
// JSON shape, version 1, one count per counted item, keyed by TOKEN:
//   "rsItems": { "logs": 0, "oak_logs": 0, "plank": 12, "oak_plank": 0, "steel_nails": 40, "dragon_nails": 0 }
// A new item is a new key at version 1 and reads 0 from an older file; renaming a token is a save break (its count
// would be dropped). Key items are not here: they are world flags, saved with the `worldFlags` section.
//
// A save written before this landed has no section and reads as nothing carried. An older build reading a newer
// save warns and skips the unknown section. Neither is a save break.

// Runs on every new game and at the top of every LoadFile, before the JSON is parsed (RsPrefs.cpp's reason): a file
// with no `rsItems` section reads empty, not the previous slot's counts.
void RsItemsInitFile(bool isDebug) {
    RsItems_Reset();
    sQueued = 0;
}

void SaveRsItems(SaveContext* saveContext, int sectionID, bool fullSave) {
    for (int32_t item = 0; item < RS_ITEM_COUNT; item++) {
        SaveManager::Instance->SaveData(RsItems_Def(item)->token, sCounts[item]);
    }
}

// A bad count is logged and repaired, never asserted: it comes from a FILE, and an assert inside LoadFile hangs the
// agent loop (RsPrefs.cpp). Past the cap it is kept at the cap - an older build with a lower cap reading a newer
// save loses the excess, which is the cap's whole meaning.
void LoadRsItemsV1() {
    for (int32_t item = 0; item < RS_ITEM_COUNT; item++) {
        const RsItemDef* def = RsItems_Def(item);
        int32_t count = 0;
        SaveManager::Instance->LoadData(def->token, count, 0);
        if (count < 0) {
            SPDLOG_ERROR("RsItems: save file's {} count {} is negative; using 0", def->token, count);
            count = 0;
        } else if (count > def->cap) {
            SPDLOG_WARN("RsItems: save file's {} count {} is past its cap {}; keeping the cap", def->token, count,
                        def->cap);
            count = def->cap;
        }
        sCounts[item] = count;
    }
}

void RegisterRsItems() {
    // ShipInit "*" functions re-run on preset apply and config drop; AddSaveFunction asserts on a duplicate name.
    static bool registered = false;
    if (!registered) {
        registered = true;
        SaveManager::Instance->AddInitFunction(RsItemsInitFile);
        SaveManager::Instance->AddSaveFunction("rsItems", 1, SaveRsItems, true, SECTION_PARENT_NONE);
        SaveManager::Instance->AddLoadFunction("rsItems", 1, LoadRsItemsV1);
    }
    COND_HOOK(OnPlayerUpdate, true, PlayCeremonies);
}

RegisterShipInitFunc rsItemsInitFunc(RegisterRsItems);

// The text-id band and the params field are as wide as these limits (NpcDialogueDef.h, RsItemPickup.h).
RS_STATIC_ASSERT(RS_TEXT_IS_KEY_ITEM(RS_TEXT_KEY_ITEM_ID(RS_KEY_ITEM_ID_MAX - 1)) &&
                     !RS_TEXT_IS_KEY_ITEM(RS_TEXT_KEY_ITEM_ID(RS_KEY_ITEM_ID_MAX)),
                 "the key-item text band holds exactly RS_KEY_ITEM_ID_MAX ids");
RS_STATIC_ASSERT(RS_PICKUP_PARAMS_ITEM_MASK + 1 == RS_ITEM_ID_MAX && RS_ITEM_ID_MAX == RS_KEY_ITEM_ID_MAX,
                 "a pickup's params item field holds every counted and key item id");

} // namespace

extern "C" int32_t RsItems_Count(int32_t item) {
    return ItemValid(item) ? sCounts[item] : 0;
}

extern "C" int32_t RsItems_CheckGain(int32_t item, int32_t count) {
    if (!ItemValid(item)) {
        return RS_ITEMS_UNKNOWN;
    }
    if (count < 1 || count > RS_ITEM_COUNT_MAX) {
        return RS_ITEMS_BAD_AMOUNT;
    }
    return sCounts[item] + count > RsItems_Def(item)->cap ? RS_ITEMS_FULL : RS_ITEMS_OK;
}

extern "C" int32_t RsItems_Gain(int32_t item, int32_t count, int32_t show, int32_t source) {
    const int32_t result = RsItems_CheckGain(item, count);
    if (result == RS_ITEMS_OK) {
        sCounts[item] += count;
        if (show) {
            RsItems_ShowGain(item, count);
        }
    }
    Mark("gain", ItemValid(item) ? RsItems_Def(item)->token : "?", count, RsItems_Count(item), result, source);
    return result;
}

extern "C" int32_t RsItems_GainClamped(int32_t item, int32_t count, int32_t show, int32_t source) {
    if (!ItemValid(item) || count < 1) {
        Mark("gain", "?", count, 0, ItemValid(item) ? RS_ITEMS_BAD_AMOUNT : RS_ITEMS_UNKNOWN, source);
        return 0;
    }
    const RsItemDef* def = RsItems_Def(item);
    const int32_t room = def->cap - sCounts[item];
    const int32_t added = count < room ? count : room;
    if (added < count) {
        SPDLOG_WARN("RsItems: {} more {} would pass the cap {}; {} lost", count, def->plural, def->cap,
                    count - added);
    }
    sCounts[item] += added > 0 ? added : 0;
    if (show) {
        RsItems_ShowGain(item, count);
    }
    Mark("gain", def->token, added, sCounts[item], added == count ? RS_ITEMS_OK : RS_ITEMS_FULL, source);
    return added;
}

extern "C" int32_t RsItems_CheckTake(int32_t item, int32_t count) {
    if (!ItemValid(item)) {
        return RS_ITEMS_UNKNOWN;
    }
    if (count < 1 || count > RS_ITEM_COUNT_MAX) {
        return RS_ITEMS_BAD_AMOUNT;
    }
    return sCounts[item] < count ? RS_ITEMS_SHORT : RS_ITEMS_OK;
}

extern "C" int32_t RsItems_Take(int32_t item, int32_t count, int32_t source) {
    const int32_t result = RsItems_CheckTake(item, count);
    if (result == RS_ITEMS_OK) {
        sCounts[item] -= count;
    }
    Mark("take", ItemValid(item) ? RsItems_Def(item)->token : "?", count, RsItems_Count(item), result, source);
    return result;
}

extern "C" int32_t RsItems_Set(int32_t item, int32_t count) {
    if (!ItemValid(item)) {
        return RS_ITEMS_UNKNOWN;
    }
    if (count < 0 || count > RsItems_Def(item)->cap) {
        return RS_ITEMS_BAD_AMOUNT;
    }
    sCounts[item] = count;
    return RS_ITEMS_OK;
}

extern "C" void RsItems_Reset(void) {
    for (int32_t& count : sCounts) {
        count = 0;
    }
}

extern "C" int32_t RsKeyItems_Held(int32_t key) {
    return KeyValid(key) && Flags_GetWorldFlag(RsKeyItems_Def(key)->worldFlag) ? 1 : 0;
}

extern "C" int32_t RsKeyItems_Grant(int32_t key, int32_t ceremony, int32_t source) {
    int32_t result = RS_ITEMS_OK;
    if (!KeyValid(key)) {
        result = RS_ITEMS_UNKNOWN;
    } else if (RsKeyItems_Held(key)) {
        result = RS_ITEMS_HELD;
    } else {
        Flags_SetWorldFlag(RsKeyItems_Def(key)->worldFlag);
        if (ceremony) {
            RsKeyItems_QueueCeremony(key);
        }
    }
    Mark("key", KeyValid(key) ? RsKeyItems_Def(key)->token : "?", 1, RsKeyItems_Held(key), result, source);
    return result;
}

extern "C" int32_t RsKeyItems_Clear(int32_t key) {
    if (!KeyValid(key)) {
        return RS_ITEMS_UNKNOWN;
    }
    Flags_UnsetWorldFlag(RsKeyItems_Def(key)->worldFlag);
    return RS_ITEMS_OK;
}

extern "C" const char* RsItems_ResultName(int32_t result) {
    switch (result) {
        case RS_ITEMS_OK:
            return "ok";
        case RS_ITEMS_UNKNOWN:
            return "unknown";
        case RS_ITEMS_BAD_AMOUNT:
            return "bad_amount";
        case RS_ITEMS_FULL:
            return "full";
        case RS_ITEMS_SHORT:
            return "short";
        case RS_ITEMS_HELD:
            return "held";
        default:
            return "?";
    }
}

extern "C" const char* RsItems_SourceName(int32_t source) {
    switch (source) {
        case RS_ITEMS_SRC_CONSOLE:
            return "console";
        case RS_ITEMS_SRC_REWARD:
            return "reward";
        case RS_ITEMS_SRC_DIALOGUE:
            return "dialogue";
        case RS_ITEMS_SRC_PICKUP:
            return "pickup";
        case RS_ITEMS_SRC_BOAT:
            return "boat";
        default:
            return "?";
    }
}

extern "C" int32_t RsItems_PredicateOperandsValid(int32_t item, int32_t count) {
    return ItemValid(item) && count >= 0 && count <= RS_ITEM_COUNT_MAX ? 1 : 0;
}

extern "C" int32_t RsItems_PackedValid(int32_t packed) {
    const int32_t item = RS_ITEMS_GET_ITEM(packed);
    const int32_t count = RS_ITEMS_GET_COUNT(packed);
    if (packed < 0 || (packed >> 16) != 0 || !ItemValid(item)) {
        return 0;
    }
    return count >= 1 && count <= RsItems_Def(item)->cap ? 1 : 0;
}

extern "C" int32_t RsKeyItems_IsDebug(int32_t key) {
    return KeyValid(key) && WORLD_FLAG_IS_DEBUG(RsKeyItems_Def(key)->worldFlag) ? 1 : 0;
}

extern "C" void RsItems_ShowGain(int32_t item, int32_t count) {
    if (gPlayState == nullptr || !ItemValid(item) || count < 1) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }
    RsItemPickup_SpawnFloat(gPlayState, item, count);
}

extern "C" void RsItems_SayRefusal(int32_t result, int32_t item, int32_t count) {
    const RsItemDef* def = RsItems_Def(item);
    if (def == nullptr) {
        return;
    }
    std::string words;
    const char* reason = "?";
    switch (result) {
        case QUEST_ERR_CANNOT_CARRY:
            words = std::string("You can't carry any more ") + def->plural + ".";
            reason = "full";
            break;
        case QUEST_ERR_CANNOT_AFFORD:
            words = "You don't have enough rupees.";
            reason = "rupees";
            break;
        case QUEST_ERR_NOT_ENOUGH_ITEMS:
            words = count == 1 ? std::string("You haven't got any ") + def->plural + "."
                               : "You haven't got " + std::to_string(count) + " " + def->plural + ".";
            reason = "short";
            break;
        default:
            return;
    }
    if (gPlayState != nullptr) {
        Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
    }
    RsInteraction_ActionSays(words.c_str(), 1);
    char line[192];
    std::snprintf(line, sizeof(line), "rs_items event=refused item=%s reason=%s text=\"%s\"", def->token, reason,
                  words.c_str());
    RsAgent_Marker(line);
}

extern "C" void RsKeyItems_QueueCeremony(int32_t key) {
    if (!KeyValid(key) || gPlayState == nullptr) {
        return;
    }
    if (sQueued > 0 && sQueueScene != gPlayState->sceneNum) {
        sQueued = 0;
    }
    if (sQueued >= kQueueMax) {
        SPDLOG_WARN("RsItems: the ceremony queue is full; {} is held without one", RsKeyItems_Def(key)->token);
        return;
    }
    sQueueScene = gPlayState->sceneNum;
    sQueue[sQueued++] = key;
}

extern "C" int32_t RsKeyItems_CeremoniesQueued(void) {
    return sQueued;
}
