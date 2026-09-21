#ifndef SOH_RS_MENU_VANILLA_PAGES_INTERNAL_H
#define SOH_RS_MENU_VANILLA_PAGES_INTERNAL_H

// What the three ported pages share with each other and with VanillaPages.cpp, and nothing else
// includes. VanillaPages.h is the public, z64-free half; this one may assume z64.h has been included.

#include <stdint.h>
#include <string>

#include "VanillaPages.h"

struct PlayState;

// One page's mapping from kaleido's page-local units into the menu's 320x240 space (VanillaPages.h):
// uniform scale 1, one offset. `left`/`top` are a quad's page-local left edge and TOP edge (y up).
struct RsVanillaMap {
    int16_t offsetX;
    int16_t offsetY;
    int16_t X(int16_t left) const {
        return (int16_t)(offsetX + left);
    }
    int16_t Y(int16_t top) const {
        return (int16_t)(offsetY - top);
    }
};

// The enum token for an ITEM_ id (`ITEM_HOOKSHOT`), from SoH's own ImGui item table; `id_<n>` for
// anything it does not carry. Never contains a space, so it is safe mid-line in a key=value marker.
std::string RsVanilla_ItemToken(int32_t item);

// SoH's PauseAnyCursor enhancement - the cursor may stop on empty slots - exactly as kaleido reads it
// (z_kaleido_item.c:446, z_kaleido_equipment.c:205).
bool RsVanilla_PauseAnyCursor();

// Kaleido's C-button (and D-pad slot) assignment, KaleidoScope_SetupItemEquip plus the end of
// KaleidoScope_UpdateItemEquip, with the flying-item animation taken out: which button the press
// names, the magic-arrow -> bow fixup, the swap with a button that already holds the slot, the
// equip-dupe fix, the write and Interface_LoadItemIcon1. Returns false when `press` names no button
// (so the caller knows nothing happened). The caller has already done the age and sold-out checks
// and fired VB_EQUIP_ITEM_TO_C_BUTTON, exactly where kaleido does.
bool RsVanilla_EquipToButton(PlayState* play, uint16_t press, uint16_t item, uint16_t slot);

// The buttons that equip on these pages this tick: C-left/C-down/C-right, plus the D-pad when SoH's
// DpadEquips is on and either DPadOnPause is off or C-up is held - kaleido's `buttonsToCheck`
// (z_kaleido_item.c:696-701). `cur` is the held mask, for the C-up test.
uint16_t RsVanilla_EquipButtons(uint16_t cur);

// The ported pages' `claim` callback: when the D-pad is an equip button (above), it is the page's and
// the cursor does not walk on it - kaleido's DpadEquips never moves the cursor with it.
uint16_t RsVanilla_ClaimEquipDpad(int32_t pageIndex, uint16_t held, void* userData);

// Counts an equip the pages performed, for `menu equips`' `done=`.
void RsVanilla_CountEquip();

// The pages, one file each. Register returns the ring index; Describe fills one port line.
int32_t RsMenuItemsPage_Register();
int32_t RsMenuEquipmentPage_Register();
int32_t RsMenuQuestStatusPage_Register();
void RsMenuItemsPage_Describe(RsMenuPortInfo* info);
void RsMenuEquipmentPage_Describe(RsMenuPortInfo* info);
void RsMenuQuestStatusPage_Describe(RsMenuPortInfo* info);

#endif // SOH_RS_MENU_VANILLA_PAGES_INTERNAL_H
