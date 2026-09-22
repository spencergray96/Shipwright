/*
 * VanillaPages.cpp - what the three ported vanilla pause pages share (sturdy-bassoon#111 stage 8):
 * registration in ring order, the item-name tokens `menu dump` prints, the PauseAnyCursor read,
 * kaleido's C-button assignment with the animation taken out, and two read-only probes for the
 * differential tests - kaleido's live cursor and the save's equip fields. VanillaPages.h is the
 * contract; the pages themselves are ItemsPage.cpp, EquipmentPage.cpp and QuestStatusPage.cpp.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-21
 */

#include "VanillaPages.h"
#include "VanillaPagesInternal.h"

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

// KaleidoScope_SetupItemEquip's button choice (z_kaleido_item.c:830-846) and the tail of
// KaleidoScope_UpdateItemEquip (:1187-1247), which is where kaleido actually writes the save once the
// icon has flown to its button. The animation in between only moves a picture and, for a magic arrow,
// renames the item to its bow form on the way - which the "skipping the arrow animation" branch below
// does anyway, so both of kaleido's paths end in the same write and this is it.
bool RsVanilla_EquipToButton(PlayState* play, uint16_t press, uint16_t item, uint16_t slot) {
    int32_t target = -1; // 0-2 C-left/down/right, 3-6 D-pad up/down/left/right
    if (CHECK_BTN_ALL(press, BTN_CLEFT)) {
        target = 0;
    } else if (CHECK_BTN_ALL(press, BTN_CDOWN)) {
        target = 1;
    } else if (CHECK_BTN_ALL(press, BTN_CRIGHT)) {
        target = 2;
    } else if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0)) {
        if (CHECK_BTN_ALL(press, BTN_DUP)) {
            target = 3;
        } else if (CHECK_BTN_ALL(press, BTN_DDOWN)) {
            target = 4;
        } else if (CHECK_BTN_ALL(press, BTN_DLEFT)) {
            target = 5;
        } else if (CHECK_BTN_ALL(press, BTN_DRIGHT)) {
            target = 6;
        }
    }
    if (target < 0) {
        return false;
    }
    // The sound kaleido plays when the equip STARTS (KaleidoScope_SetupItemEquip); the arrow-specific
    // "set fire arrow" sounds belong to the animation, which is not ported.
    Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

    // A magic arrow goes on the button as the bow loaded with it, and - unless SoH's SeparateArrows is
    // on - takes the bow's slot, so the two are one equip (z_kaleido_item.c:1114-1117, :1189-1205).
    if (item == ITEM_ARROW_FIRE || item == ITEM_ARROW_ICE || item == ITEM_ARROW_LIGHT) {
        item = item == ITEM_ARROW_FIRE ? ITEM_BOW_ARROW_FIRE : item == ITEM_ARROW_ICE ? ITEM_BOW_ARROW_ICE
                                                                                      : ITEM_BOW_ARROW_LIGHT;
        if (!CVarGetInteger(CVAR_ENHANCEMENT("SeparateArrows"), 0)) {
            slot = SLOT_BOW;
        }
    }

    // If the slot is on another button already, that button takes the target's old item: a swap.
    // Kaleido's own loop, including its commented-out `break` - it assumes one pre-existing equip.
    const uint16_t targetButton = (uint16_t)(target + 1);
    for (uint16_t other = 0; other < ARRAY_COUNT(gSaveContext.equips.cButtonSlots); other++) {
        const uint16_t otherButton = (uint16_t)(other + 1);
        if (other == target) {
            continue;
        }
        if (slot == gSaveContext.equips.cButtonSlots[other]) {
            if (gSaveContext.equips.buttonItems[targetButton] != ITEM_NONE) {
                gSaveContext.equips.buttonItems[otherButton] = gSaveContext.equips.buttonItems[targetButton];
                gSaveContext.equips.cButtonSlots[other] = gSaveContext.equips.cButtonSlots[target];
                Interface_LoadItemIcon2(play, otherButton);
            } else {
                gSaveContext.equips.buttonItems[otherButton] = ITEM_NONE;
                gSaveContext.equips.cButtonSlots[other] = SLOT_NONE;
            }
        }
        // SoH's "fix for equip dupe": equipping the plain bow over a button that holds a loaded bow.
        if (item == ITEM_BOW) {
            if (gSaveContext.equips.buttonItems[otherButton] >= ITEM_BOW_ARROW_FIRE &&
                gSaveContext.equips.buttonItems[otherButton] <= ITEM_BOW_ARROW_LIGHT &&
                !CVarGetInteger(CVAR_ENHANCEMENT("SeparateArrows"), 0)) {
                gSaveContext.equips.buttonItems[otherButton] = gSaveContext.equips.buttonItems[targetButton];
                gSaveContext.equips.cButtonSlots[other] = gSaveContext.equips.cButtonSlots[target];
                Interface_LoadItemIcon2(play, otherButton);
            }
        }
    }

    gSaveContext.equips.buttonItems[targetButton] = (u8)item;
    gSaveContext.equips.cButtonSlots[target] = (u8)slot;
    Interface_LoadItemIcon1(play, targetButton);
    RsVanilla_CountEquip();
    return true;
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
    return cursor;
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
