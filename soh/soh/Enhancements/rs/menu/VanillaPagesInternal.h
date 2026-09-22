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

// Kaleido's C-button (and D-pad slot) equip, flying icon included (EquipFlight.cpp, #125). Begin is
// KaleidoScope_SetupItemEquip: the button the press names, the sound and the flight - nothing is written
// until the icon lands, a few ticks later, when the write is kaleido's own (the magic-arrow -> bow
// fixup, the swap, the equip-dupe fix, Interface_LoadItemIcon1). `slotX/slotY` is the slot's top-left
// in the menu's 320x240 space as it is drawn now, `bowX/bowY` the Bow slot's (a magic arrow's first
// stop). False when `press` names no button or a flight is already up. The caller has already done the
// age and sold-out checks and fired VB_EQUIP_ITEM_TO_C_BUTTON, exactly where kaleido does.
bool RsVanilla_BeginEquip(uint16_t press, uint16_t item, uint16_t slot, int16_t slotX, int16_t slotY, int16_t bowX,
                          int16_t bowY);
// One game tick of the flight (KaleidoScope_UpdateItemEquip); the menu calls it while it is up.
void RsVanilla_UpdateEquipFlight(PlayState* play);
// While true the menu takes no input, as kaleido takes none in its sub-state 3.
bool RsVanilla_EquipFlightActive();
// Lands a flight at once (the menu is closing under it), or does nothing.
void RsVanilla_FinishEquipFlight(PlayState* play);
// The OnInterfaceDrawItemButtonsEnd callback: the flying icon, over the HUD's buttons.
void RsVanilla_DrawEquipFlight(void* play);

// The buttons that equip on these pages this tick: C-left/C-down/C-right, plus the D-pad when SoH's
// DpadEquips is on and either DPadOnPause is off or C-up is held - kaleido's `buttonsToCheck`
// (z_kaleido_item.c:696-701). `cur` is the held mask, for the C-up test.
uint16_t RsVanilla_EquipButtons(uint16_t cur);

// The ported pages' `claim` callback: when the D-pad is an equip button (above), it is the page's and
// the cursor does not walk on it - kaleido's DpadEquips never moves the cursor with it.
uint16_t RsVanilla_ClaimEquipDpad(int32_t pageIndex, uint16_t held, void* userData);

// The HUD button states vanilla pause shows on a page (RsMenuPageHudFn), measured on vanilla pause
// (sturdy-bassoon docs/test-runs/2026-09-22-issue-125-pause-hud) and matching D_8082AB6C
// (z_kaleido_scope_PAL.c:906-919) as KaleidoScope_SwitchPage applies it, AssignableTunicsAndBoots
// included (:1274-1284). `kaleidoPage` is PAUSE_ITEM, PAUSE_QUEST or PAUSE_EQUIP.
void RsVanilla_HudButtons(int32_t kaleidoPage, uint8_t status[9]);

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
