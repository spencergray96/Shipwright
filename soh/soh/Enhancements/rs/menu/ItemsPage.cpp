/*
 * ItemsPage.cpp - vanilla's Select Item page, ported onto the scroll (sturdy-bassoon#111 stage 8).
 * Positions from KaleidoScope_InitVertices, movement from KaleidoScope_DrawItemSelect, contents from
 * gSaveContext.inventory, equipping from KaleidoScope_SetupItemEquip/UpdateItemEquip. VanillaPages.h
 * has what the three ports share.
 *
 * Draws through the menu's helpers only (RsMenu.h's page seam) - no OPEN_DISPS, no Gfx macro here.
 *
 * NOT PORTED, on purpose: the cursor's own look (the menu's yellow box stands in for kaleido's corners
 * and its 2-unit icon zoom), the name plate, SoH's item-cycling extras (mask select, rando trade
 * cycling, Roc's Feather). The flying-icon equip animation IS ported since #125 (EquipFlight.cpp): the
 * equip lands, and the save changes, when the icon reaches its button.
 * Also kaleido's `cursorItem == PAUSE_ITEM_NONE` -> `stickRelX = 40` (z_kaleido_item.c:460-461), which
 * walks the cursor right with no input: that value only marks the page arrows, which here are hands.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-21
 */

#include "VanillaPagesInternal.h"
#include "RsMenu.h"

#include <cstdio>
#include <string>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "overlays/misc/ovl_kaleido_scope/z_kaleido_scope.h"
#include "textures/parameter_static/parameter_static.h"
extern PlayState* gPlayState;
extern const char* _gAmmoDigit0Tex[];
}

// --- geometry: KaleidoScope_InitVertices, z_kaleido_scope_PAL.c:3108-3143 --------------------------
// A 6 x 4 grid at a 32-unit pitch, x from -96 and the top row's top edge at 58, each icon inset 2 and
// 28 square. Drawn extent: x -94..94, y 56..-68 (188 x 124), which fits the 216 x 138 page rect at
// scale 1 with 14 units spare either side - the edge columns clear the hands by 20.
constexpr int32_t kCols = 6;
constexpr int32_t kRows = 4;
constexpr int32_t kSlots = kCols * kRows;
constexpr int16_t kPitch = 32;
constexpr int16_t kIcon = 28;
static int16_t SlotLeft(int32_t slot) {
    return (int16_t)(-96 + kPitch * (slot % kCols) + 2);
}
static int16_t SlotTop(int32_t slot) {
    return (int16_t)(58 - kPitch * (slot / kCols) - 2);
}
// The mapping (VanillaPages.h): the drawn extent's centre (0, -6) onto the rect's centre (160, 120).
constexpr RsVanillaMap kMap = { 160, 114 };

// The ammo counts, kaleido's gAmmoItems (z_kaleido_item.c:12) - which of the first 15 slots show a
// count - and SoH's BetterAmmoRendering, which counts every slot whose item uses ammo.
static bool SlotShowsAmmo(int32_t slot, bool better) {
    const int32_t item = gSaveContext.inventory.items[slot];
    if (item == ITEM_NONE) {
        return false;
    }
    if (better) {
        return item == ITEM_STICK || item == ITEM_NUT || item == ITEM_BOMB || item == ITEM_BOW ||
               item == ITEM_SLINGSHOT || item == ITEM_BOMBCHU || item == ITEM_BEAN;
    }
    return slot < 15 && gAmmoItems[slot] != ITEM_NONE;
}

// --- movement: KaleidoScope_DrawItemSelect, z_kaleido_item.c:457-671 -------------------------------
//
// Each function below is one of kaleido's cursor branches with pauseCtx's cursor replaced by locals,
// run from `start` - so it answers "where would kaleido's cursor go from here" without a pauseCtx.

static bool Occupied(int32_t slot) {
    return gSaveContext.inventory.items[slot] != ITEM_NONE;
}

// Left/right (:476-542). Scans the columns beyond the cursor on its own row first, then on each row
// below it (wrapping to the top), and gives up to the page arrow on that side when it comes back round
// to its own row. Returns a slot, or RS_MENU_LINK_HAND_LEFT/RIGHT.
static int32_t MoveHorizontal(int32_t start, int32_t dir) {
    const bool any = RsVanilla_PauseAnyCursor();
    const int32_t startX = start % kCols;
    const int32_t startY = start / kCols;
    int32_t x = startX;
    int32_t y = startY;
    int32_t point = start;
    while (true) {
        if (dir < 0) {
            if (x != 0) {
                x--;
                point--;
                if (Occupied(point) || any) {
                    return point;
                }
                continue;
            }
        } else if (x < kCols - 1) {
            x++;
            point++;
            if (Occupied(point) || any) {
                return point;
            }
            continue;
        }
        x = startX;
        y++;
        if (y >= kRows) {
            y = 0;
        }
        point = x + y * kCols;
        if (point >= kSlots) {
            point = x;
        }
        if (y == startY) {
            return dir < 0 ? RS_MENU_LINK_HAND_LEFT : RS_MENU_LINK_HAND_RIGHT;
        }
    }
}

// Up/down (:627-670). Its own column only, skipping empties; at the top or bottom edge it stays put.
static int32_t MoveVertical(int32_t start, int32_t dir) {
    const bool any = RsVanilla_PauseAnyCursor();
    int32_t y = start / kCols;
    int32_t point = start;
    while (true) {
        if (dir < 0) {
            if (y == 0) {
                return RS_MENU_LINK_NONE;
            }
            y--;
            point -= kCols;
        } else {
            if (y >= kRows - 1) {
                return RS_MENU_LINK_NONE;
            }
            y++;
            point += kCols;
        }
        if (Occupied(point) || any) {
            return point;
        }
    }
}

// Stepping off a page arrow (:552-623): from the left arrow, column by column from the left, each top
// to bottom; from the right arrow, the same from the right. Nothing found reaches the other arrow.
// (These two ignore PauseAnyCursor, as kaleido's do.)
static int32_t EnterFromLeft() {
    for (int32_t x = 0; x < kCols; x++) {
        for (int32_t y = 0; y < kRows; y++) {
            if (Occupied(x + y * kCols)) {
                return x + y * kCols;
            }
        }
    }
    return RS_MENU_LINK_HAND_RIGHT;
}

static int32_t EnterFromRight() {
    for (int32_t x = kCols - 1; x >= 0; x--) {
        for (int32_t y = 0; y < kRows; y++) {
            if (Occupied(x + y * kCols)) {
                return x + y * kCols;
            }
        }
    }
    return RS_MENU_LINK_HAND_LEFT;
}

static bool IsNode(int32_t slot) {
    return Occupied(slot) || RsVanilla_PauseAnyCursor();
}

static std::string NodeId(int32_t slot) {
    char id[16];
    std::snprintf(id, sizeof(id), "items_%02d", slot);
    return id;
}

// -1 when the id is not one of this page's.
static int32_t SlotOfNode(const std::string& id) {
    if (id.size() != 8 || id.compare(0, 6, "items_") != 0) {
        return -1;
    }
    const int32_t slot = (id[6] - '0') * 10 + (id[7] - '0');
    return slot >= 0 && slot < kSlots ? slot : -1;
}

// The slot the cursor last stood on here, which is where it comes back to - kaleido keeps one
// cursorSlot per page the same way. 0 until the page is first visited.
static int32_t sLastSlot = 0;
static int32_t sPageIndex = -1;

// KaleidoScope_SetDefaultCursor (z_kaleido_scope_PAL.c:1226-1246): an empty remembered slot moves on
// to the next occupied one, wrapping; an empty page has none.
static int32_t EntrySlot() {
    if (Occupied(sLastSlot)) {
        return sLastSlot;
    }
    for (int32_t i = 1; i < kSlots; i++) {
        const int32_t slot = (sLastSlot + i) % kSlots;
        if (Occupied(slot)) {
            return slot;
        }
    }
    return -1;
}

static void ItemsPageNodes(int32_t pageIndex, void* userData) {
    (void)pageIndex;
    (void)userData;
    const int32_t cursorSlot = SlotOfNode(RsMenu_CursorId());
    if (cursorSlot >= 0) {
        sLastSlot = cursorSlot;
    }
    // Slot -> node index, for the edges.
    int32_t nodeOf[kSlots];
    for (int32_t slot = 0; slot < kSlots; slot++) {
        nodeOf[slot] = -1;
        if (IsNode(slot)) {
            nodeOf[slot] = RsMenu_AddCursorNode(NodeId(slot).c_str(), kMap.X(SlotLeft(slot)), kMap.Y(SlotTop(slot)),
                                                kIcon, kIcon);
        }
    }
    auto toNode = [&](int32_t target) {
        return target >= 0 ? nodeOf[target] : target;
    };
    for (int32_t slot = 0; slot < kSlots; slot++) {
        if (nodeOf[slot] < 0) {
            continue;
        }
        RsMenu_LinkCursorNode(nodeOf[slot], toNode(MoveHorizontal(slot, -1)), toNode(MoveHorizontal(slot, 1)),
                              toNode(MoveVertical(slot, -1)), toNode(MoveVertical(slot, 1)));
    }
    RsMenu_LinkHands(toNode(EnterFromLeft()), toNode(EnterFromRight()));
    const int32_t entry = EntrySlot();
    RsMenu_SetCursorGrid(entry >= 0 ? nodeOf[entry] : -1);
}

// --- drawing: KaleidoScope_DrawItemSelect, z_kaleido_item.c:736-818 --------------------------------

// The equipped outline sits 2 units outside the icon, 32 square (:3145-3182).
static void DrawEquippedOutlines() {
    const bool dpad = CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) != 0;
    for (int32_t i = 0; i < (int32_t)ARRAY_COUNT(gSaveContext.equips.cButtonSlots); i++) {
        const int32_t slot = gSaveContext.equips.cButtonSlots[i];
        const int32_t item = gSaveContext.equips.buttonItems[i + 1];
        if (slot == SLOT_NONE || slot >= kSlots || (i >= 3 && !dpad)) {
            continue;
        }
        if (item == ITEM_NONE || (item >= ITEM_SHIELD_DEKU && item <= ITEM_BOOTS_HOVER)) {
            continue;
        }
        RsMenu_DrawIcon(gEquippedItemOutlineTex, RS_MENU_TEX_IA8, 32, 32, (int16_t)(kMap.X(SlotLeft(slot)) - 2),
                        (int16_t)(kMap.Y(SlotTop(slot)) - 2), 32, 32, 255, 255, 255, 255, false);
    }
}

// KaleidoScope_DrawAmmoCount (:37-104): the count's two 8 x 8 digits sit 22 below the icon's top, the
// tens digit only from 10, and the colour says greyed-for-age, empty or full.
static void DrawAmmo(int32_t slot) {
    s16 item = gSaveContext.inventory.items[slot];
    if (!GameInteractor_Should(VB_DRAW_AMMO_COUNT, true, &item)) {
        return;
    }
    int32_t ammo = AMMO(item);
    u8 r = 255, g = 255, b = 255;
    if (!CHECK_AGE_REQ_SLOT(SLOT(item))) {
        r = g = b = 100;
    } else if (ammo == 0) {
        r = g = b = 130;
    } else if ((item == ITEM_BOMB && AMMO(item) == CUR_CAPACITY(UPG_BOMB_BAG)) ||
               (item == ITEM_BOW && AMMO(item) == CUR_CAPACITY(UPG_QUIVER)) ||
               (item == ITEM_SLINGSHOT && AMMO(item) == CUR_CAPACITY(UPG_BULLET_BAG)) ||
               (item == ITEM_STICK && AMMO(item) == CUR_CAPACITY(UPG_STICKS)) ||
               (item == ITEM_NUT && AMMO(item) == CUR_CAPACITY(UPG_NUTS)) || (item == ITEM_BOMBCHU && ammo == 50) ||
               (item == ITEM_BEAN && ammo == 15) || GameInteractor_Should(VB_COLOR_AMMO_GREEN, false, item)) {
        r = 120;
        g = 255;
        b = 0;
    }
    int32_t tens = 0;
    while (ammo >= 10) {
        tens++;
        ammo -= 10;
    }
    const int16_t x = kMap.X(SlotLeft(slot));
    const int16_t y = (int16_t)(kMap.Y(SlotTop(slot)) + 22);
    if (tens != 0) {
        RsMenu_DrawIcon(_gAmmoDigit0Tex[tens], RS_MENU_TEX_IA8, 8, 8, x, y, 8, 8, r, g, b, 255, false);
    }
    RsMenu_DrawIcon(_gAmmoDigit0Tex[ammo], RS_MENU_TEX_IA8, 8, 8, (int16_t)(x + 6), y, 8, 8, r, g, b, 255, false);
}

static void ItemsPageDraw(struct PlayState* play, int32_t pageIndex, void* userData) {
    (void)play;
    (void)pageIndex;
    (void)userData;
    DrawEquippedOutlines();
    for (int32_t slot = 0; slot < kSlots; slot++) {
        const int32_t item = gSaveContext.inventory.items[slot];
        if (item == ITEM_NONE) {
            continue;
        }
        RsMenu_DrawIcon(gItemIcons[item], RS_MENU_TEX_RGBA32, 32, 32, kMap.X(SlotLeft(slot)), kMap.Y(SlotTop(slot)),
                        kIcon, kIcon, 255, 255, 255, 255, !CHECK_AGE_REQ_ITEM(item));
    }
    const bool better = CVarGetInteger(CVAR_ENHANCEMENT("BetterAmmoRendering"), 0) != 0;
    for (int32_t slot = 0; slot < kSlots; slot++) {
        if (SlotShowsAmmo(slot, better)) {
            DrawAmmo(slot);
        }
    }
}

// --- equipping (half 2): z_kaleido_item.c:694-715 ---------------------------------------------------
//
// A C button (or a D-pad slot, under DpadEquips) with the cursor on an item: the age and sold-out
// checks, VB_EQUIP_ITEM_TO_C_BUTTON - through which SoH's ItemUnequip takes a press on the button that
// already holds the item - and then the write. The error sound where kaleido plays it.
static void ItemsPageInput(int32_t pageIndex, int32_t level, uint16_t press, int32_t navY, void* userData) {
    (void)pageIndex;
    (void)navY;
    (void)userData;
    if (level != 0 || gPlayState == nullptr) {
        return;
    }
    const uint16_t buttons = RsVanilla_EquipButtons(gPlayState->state.input[0].cur.button);
    if ((press & buttons) == 0) {
        return;
    }
    const RsMenuCursorNode* node = RsMenu_CursorAt(RsMenu_CursorIndex());
    const int32_t slot = node != nullptr ? SlotOfNode(node->id) : -1;
    if (slot < 0) {
        return;
    }
    const u16 item = gSaveContext.inventory.items[slot];
    if (CHECK_AGE_REQ_SLOT(slot) && item != ITEM_SOLD_OUT && item != ITEM_NONE) {
        if (GameInteractor_Should(VB_EQUIP_ITEM_TO_C_BUTTON, true, gPlayState, (u16)slot, item)) {
            // From the slot's drawn top-left - kaleido's itemVtx corner, inset 2 like it - and via the
            // Bow's slot (3, kaleido's itemVtx[12]) if a magic arrow's effect runs.
            RsVanilla_BeginEquip(press, item, (u16)slot, kMap.X(SlotLeft(slot)), kMap.Y(SlotTop(slot)),
                                 kMap.X(SlotLeft(SLOT_BOW)), kMap.Y(SlotTop(SLOT_BOW)));
        }
    } else {
        RsMenu_PlaySfxId(NA_SE_SY_ERROR);
    }
}

// #125: the HUD buttons vanilla shows on this page.
static void ItemsPageHud(int32_t pageIndex, uint8_t status[9], void* userData) {
    (void)pageIndex;
    (void)userData;
    RsVanilla_HudButtons(PAUSE_ITEM, status);
}

int32_t RsMenuItemsPage_Register() {
    RsMenuPage page;
    page.id = "items";
    page.title = "Items";
    page.draw = ItemsPageDraw;
    page.nodes = ItemsPageNodes;
    page.input = ItemsPageInput;
    page.claim = RsVanilla_ClaimEquipDpad;
    page.stickModel = RS_MENU_STICK_KALEIDO_SEQUENTIAL;
    page.hud = ItemsPageHud;
    page.ownsItemHighlight = false; // the menu's yellow box, not kaleido's corner cursor
    sPageIndex = RsMenu_RegisterPageStruct(page);
    return sPageIndex;
}

void RsMenuItemsPage_Describe(RsMenuPortInfo* info) {
    info->pageId = "items";
    info->pageIndex = sPageIndex;
    info->scale = 1.0f;
    info->offsetX = kMap.offsetX;
    info->offsetY = kMap.offsetY;
    info->x0 = kMap.X(SlotLeft(0));
    info->y0 = kMap.Y(SlotTop(0));
    info->x1 = (int16_t)(kMap.X(SlotLeft(kCols - 1)) + kIcon);
    info->y1 = (int16_t)(kMap.Y(SlotTop(kSlots - 1)) + kIcon);
    info->slots.clear();
    for (int32_t slot = 0; slot < kSlots; slot++) {
        const int32_t item = gSaveContext.inventory.items[slot];
        RsMenuSlotInfo s;
        s.node = NodeId(slot);
        s.slot = slot;
        s.x = kMap.X(SlotLeft(slot));
        s.y = kMap.Y(SlotTop(slot));
        s.w = kIcon;
        s.h = kIcon;
        s.item = item;
        s.itemName = RsVanilla_ItemToken(item);
        s.owned = item != ITEM_NONE;
        s.isNode = IsNode(slot);
        s.grey = item != ITEM_NONE && !CHECK_AGE_REQ_ITEM(item);
        info->slots.push_back(s);
    }
}
