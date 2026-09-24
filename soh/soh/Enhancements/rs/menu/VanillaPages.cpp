/*
 * VanillaPages.cpp - what the three ported vanilla pause pages share (sturdy-bassoon#111 stage 8):
 * registration in ring order, the item-name tokens `menu dump` prints, the PauseAnyCursor read, each
 * page's HUD button states (#125), and read-only probes for the differential tests - kaleido's live
 * cursor, the save's equip fields and the HUD - and, since #132, the name panel's "To <page>" labels.
 * VanillaPages.h is the contract; the pages themselves are ItemsPage.cpp, EquipmentPage.cpp and
 * QuestStatusPage.cpp, and the C-button equip with its flying icon is EquipFlight.cpp.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-21
 */

#include "VanillaPages.h"
#include "VanillaPagesInternal.h"
#include "RsMenu.h"

#include <cstdio>
#include <string>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/SwitchAge.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "textures/icon_item_nes_static/icon_item_nes_static.h"
#include "textures/icon_item_ger_static/icon_item_ger_static.h"
#include "textures/icon_item_fra_static/icon_item_fra_static.h"
#include "textures/icon_item_jpn_static/icon_item_jpn_static.h"
extern PlayState* gPlayState;
}

static bool sRegistered = false;
static int32_t sEquipsDone = 0;
static int32_t sPlayerSyncs = 0;

// The ports, in ring order - one entry each, so adding a port is one line here and nothing else in
// this file. (Vanilla's cube turns Select Item, Map, Quest Status, Equipment; the map is not ported -
// stages 10-11 - so it closes up.)
struct RsVanillaPort {
    int32_t (*reg)();
    void (*describe)(RsMenuPortInfo*);
};
static const RsVanillaPort kPorts[] = {
    { RsMenuItemsPage_Register, RsMenuItemsPage_Describe },
    { RsMenuEquipmentPage_Register, RsMenuEquipmentPage_Describe },
    { RsMenuQuestStatusPage_Register, RsMenuQuestStatusPage_Describe },
};

void RsMenuVanillaPages_Register() {
    if (sRegistered) {
        return;
    }
    sRegistered = true;
    for (const RsVanillaPort& port : kPorts) {
        port.reg();
    }
}

std::vector<RsMenuPortInfo> RsMenuVanillaPages_Describe() {
    std::vector<RsMenuPortInfo> out;
    for (const RsVanillaPort& port : kPorts) {
        out.emplace_back();
        port.describe(&out.back());
    }
    return out;
}

std::string RsVanilla_ItemToken(int32_t item) {
    if (item == ITEM_NONE) {
        return "ITEM_NONE";
    }
    auto it = itemMapping.find((uint32_t)item);
    if (it != itemMapping.end()) {
        return it->second.name;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "id_%d", item);
    return buf;
}

bool RsVanilla_PauseAnyCursor() {
    const int32_t mode = CVarGetInteger(CVAR_ENHANCEMENT("PauseAnyCursor"), 0);
    return (mode == PAUSE_ANY_CURSOR_RANDO_ONLY && IS_RANDO) || mode == PAUSE_ANY_CURSOR_ALWAYS_ON;
}

void RsVanilla_CountEquip() {
    sEquipsDone++;
}

uint16_t RsVanilla_ClaimEquipDpad(int32_t pageIndex, uint16_t held, void* userData) {
    (void)pageIndex;
    (void)userData;
    return (uint16_t)(RsVanilla_EquipButtons(held) & (BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT));
}

uint16_t RsVanilla_EquipButtons(uint16_t cur) {
    uint16_t buttons = BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT;
    if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) &&
        (!CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) || CHECK_BTN_ALL(cur, BTN_CUP))) {
        buttons |= BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
    }
    return buttons;
}

RsMenuKaleidoCursor RsMenu_KaleidoCursor() {
    RsMenuKaleidoCursor cursor = {};
    if (gPlayState == nullptr) {
        return cursor;
    }
    const PauseContext* pauseCtx = &gPlayState->pauseCtx;
    cursor.valid = true;
    cursor.state = pauseCtx->state;
    cursor.debugState = pauseCtx->debugState;
    cursor.page = pauseCtx->pageIndex;
    // pageIndex indexes the per-page arrays, which are PAUSE_MAX long; the save prompt's page (5) is
    // not one of them.
    const int32_t page = cursor.page >= 0 && cursor.page <= PAUSE_EQUIP ? cursor.page : 0;
    cursor.point = pauseCtx->cursorPoint[page];
    cursor.x = pauseCtx->cursorX[page];
    cursor.y = pauseCtx->cursorY[page];
    cursor.special = pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT    ? 1
                     : pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_RIGHT ? 2
                                                                             : 0;
    cursor.item = pauseCtx->cursorItem[page];
    cursor.slot = pauseCtx->cursorSlot[page];
    cursor.sub = pauseCtx->unk_1E4;
    cursor.named = pauseCtx->namedItem == PAUSE_ITEM_NONE ? -1 : pauseCtx->namedItem;
    cursor.namedName = cursor.named >= 0 ? RsVanilla_ItemToken(cursor.named) : "-";
    cursor.nameTimer = pauseCtx->nameDisplayTimer;
    cursor.nameGrey = pauseCtx->nameColorSet;
    return cursor;
}

// #132: D_8082AD78/D_8082ADA8's labels (z_kaleido_scope_PAL.c:1932-1943), one row per page they turn TO.
static const void* const kToSelectItem[4] = { gPauseToSelectItemENGTex, gPauseToSelectItemGERTex,
                                              gPauseToSelectItemFRATex, gPauseToSelectItemJPNTex };
static const void* const kToEquipment[4] = { gPauseToEquipmentENGTex, gPauseToEquipmentGERTex,
                                             gPauseToEquipmentFRATex, gPauseToEquipmentJPNTex };
static const void* const kToQuestStatus[4] = { gPauseToQuestStatusENGTex, gPauseToQuestStatusGERTex,
                                               gPauseToQuestStatusFRATex, gPauseToQuestStatusJPNTex };

const void* const* RsVanilla_ToPageLabel(int32_t kaleidoPage) {
    switch (kaleidoPage) {
        case PAUSE_ITEM:
            return kToSelectItem;
        case PAUSE_EQUIP:
            return kToEquipment;
        case PAUSE_QUEST:
            return kToQuestStatus;
        default:
            return nullptr;
    }
}

int32_t RsVanilla_EquipSubState() {
    return RsVanilla_EquipFlightActive() ? 3 : 0;
}

void RsVanilla_HudButtons(int32_t kaleidoPage, uint8_t status[9]) {
    // What vanilla pause leaves on each page (v1-out.txt): B always; Select Item the C buttons and the
    // D-pad slots but not A; Quest Status and Equipment A only - unless AssignableTunicsAndBoots, which
    // turns every button on for Equipment.
    const bool items = kaleidoPage == PAUSE_ITEM;
    const bool allOn =
        kaleidoPage == PAUSE_EQUIP && CVarGetInteger(CVAR_ENHANCEMENT("AssignableTunicsAndBoots"), 0) != 0;
    status[0] = BTN_ENABLED;
    for (int32_t i = 1; i < 9; i++) {
        status[i] = (allOn || items) ? BTN_ENABLED : BTN_DISABLED;
    }
    status[4] = (allOn || !items) ? BTN_ENABLED : BTN_DISABLED;
}

RsMenuHudState RsMenu_HudState() {
    RsMenuHudState hud = {};
    if (gPlayState == nullptr) {
        return hud;
    }
    const InterfaceContext* ic = &gPlayState->interfaceCtx;
    hud.valid = true;
    hud.mode = gSaveContext.hudVisibilityMode;
    hud.prevMode = gSaveContext.prevHudVisibilityMode;
    for (int32_t i = 0; i < 9; i++) {
        hud.status[i] = gSaveContext.buttonStatus[i];
    }
    const int32_t alpha[13] = { ic->bAlpha,         ic->aAlpha,          ic->cLeftAlpha,     ic->cDownAlpha,
                                ic->cRightAlpha,    ic->dpadUpAlpha,     ic->dpadDownAlpha,  ic->dpadLeftAlpha,
                                ic->dpadRightAlpha, ic->healthAlpha,     ic->magicAlpha,     ic->minimapAlpha,
                                ic->startAlpha };
    for (int32_t i = 0; i < 13; i++) {
        hud.alpha[i] = alpha[i];
    }
    hud.bLabel = ic->unk_1FC;      // Interface_LoadActionLabelB (z_parameter.c:2872)
    hud.bLabelShown = ic->unk_1FA; // 1: B draws that label, not its item (:5455)
    RsMenu_HudRaise(&hud.marginTop, &hud.raisedOn, &hud.raiseElements);
    hud.magicLevel = gSaveContext.magicLevel;
    hud.magicCapacity = gSaveContext.magicCapacity;
    hud.magic = gSaveContext.magic;
    s16 dx = 0;
    s16 dy = 0;
    if (GameInteractor_Should(VB_SHIFT_HUD_B_BUTTON, false, &dx, &dy)) {
        hud.bShiftX = dx;
        hud.bShiftY = dy;
    }
    return hud;
}

bool RsMenu_TestSetInventory(const std::string& kind, int32_t a, int32_t b) {
    if (kind == "item") {
        if (a < 0 || a >= 24 || b < 0 || b > 0xFF) {
            return false;
        }
        gSaveContext.inventory.items[a] = (u8)b;
        return true;
    }
    if (kind == "equip") {
        if (a < 0 || a >= 16 || (b != 0 && b != 1)) {
            return false;
        }
        if (b) {
            gSaveContext.inventory.equipment |= gBitFlags[a];
        } else {
            gSaveContext.inventory.equipment &= ~gBitFlags[a];
        }
        return true;
    }
    if (kind == "upgrade") {
        if (a < 0 || a >= 8 || b < 0 || b > 7) {
            return false;
        }
        Inventory_ChangeUpgrade(a, b);
        return true;
    }
    if (kind == "sword") {
        if ((a != 0 && a != 1) || b < 0 || b > 8) {
            return false;
        }
        gSaveContext.bgsFlag = (u8)a;
        gSaveContext.swordHealth = (s16)b;
        return true;
    }
    if (kind == "age") {
        if ((a != LINK_AGE_ADULT && a != LINK_AGE_CHILD) || b != 0 || gPlayState == nullptr) {
            return false;
        }
        if (gSaveContext.linkAge != a) {
            SwitchAge();
        }
        return true;
    }
    if (kind == "quest") {
        if (a < 0 || a >= 24 || (b != 0 && b != 1)) {
            return false;
        }
        if (b) {
            gSaveContext.inventory.questItems |= gBitFlags[a];
        } else {
            gSaveContext.inventory.questItems &= ~gBitFlags[a];
        }
        return true;
    }
    return false;
}

RsMenuEquipState RsMenu_EquipState() {
    RsMenuEquipState state = {};
    for (int32_t i = 0; i < 8; i++) {
        state.buttons[i] = gSaveContext.equips.buttonItems[i];
    }
    for (int32_t i = 0; i < 7; i++) {
        state.slots[i] = gSaveContext.equips.cButtonSlots[i];
    }
    state.equipment = gSaveContext.equips.equipment;
    state.swordless = Flags_GetInfTable(INFTABLE_SWORDLESS) != 0;
    state.inf29 = gSaveContext.infTable[29];
    state.swordHealth = (int32_t)gSaveContext.swordHealth;
    state.bgsFlag = gSaveContext.bgsFlag;
    state.dpadEquips = CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) != 0;
    state.equipsDone = sEquipsDone;
    const Player* player = gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
    state.player = player != nullptr;
    state.playerSword = player != nullptr ? player->currentSwordItemId : -1;
    state.playerShield = player != nullptr ? player->currentShield : -1;
    state.playerTunic = player != nullptr ? player->currentTunic : -1;
    state.playerBoots = player != nullptr ? player->currentBoots : -1;
    state.modelGroup = player != nullptr ? player->modelGroup : -1;
    state.modelAnimType = player != nullptr ? player->modelAnimType : -1;
    state.leftHandType = player != nullptr ? player->leftHandType : -1;
    state.rightHandType = player != nullptr ? player->rightHandType : -1;
    state.sheathType = player != nullptr ? player->sheathType : -1;
    state.linkAge = gSaveContext.linkAge;
    state.playerSyncs = sPlayerSyncs;
    return state;
}

void RsMenuVanillaPages_SyncPlayer(PlayState* play) {
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr) {
        return;
    }
    // Unconditional, as vanilla's close is: the one guard is inside it (a csAction of 0x56 keeps the
    // Player's own gear). Vanilla's close tail also restores buttonStatus and the rando's swordless
    // temp-B (:4869-4874); the scroll never dims a button, so it has neither to restore.
    Player_SetEquipmentData(play, player);
    sPlayerSyncs++;
}
