/*
 * EquipmentPage.cpp - vanilla's Equipment page, ported onto the scroll (sturdy-bassoon#111 stage 8).
 * Positions from KaleidoScope_InitVertices, movement and equipping from KaleidoScope_DrawEquipment,
 * contents from gSaveContext, and Link's portrait - the pause-Link render lives in PauseLink.cpp and
 * is composited here through RsMenu_DrawPauseLink. VanillaPages.h has what the three ports share.
 *
 * Draws through the menu's helpers only - no OPEN_DISPS, no Gfx macro here.
 *
 * NOT PORTED: the cursor's own look, the A-button hint drawn over the strength slot
 * under SoH's ToggleStrength (the toggle itself is ported), and kaleido's ten-tick input lockout after
 * an equip (unk_1E4 = 7, sEquipTimer = 10, z_kaleido_equipment.c:643/:709-717) - an equip here takes
 * effect at once and the cursor stays live. Dropped on purpose (Spencer, #126). In kaleido those ten
 * ticks freeze input and hold the cursor in its equip colour (cursorColorSet 8); the scroll's cursor
 * is one yellow box with no colour sets, so there is nothing for the ticks to show.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-21
 */

#include "VanillaPagesInternal.h"
#include "RsMenu.h"

#include <cstdio>
#include <string>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "overlays/misc/ovl_kaleido_scope/z_kaleido_scope.h"
#include "textures/icon_item_static/icon_item_static.h"
#include "textures/parameter_static/parameter_static.h"
extern PlayState* gPlayState;
}

// --- geometry: KaleidoScope_InitVertices, z_kaleido_scope_PAL.c:3251-3377 --------------------------
// Four rows at a 32-unit pitch from a top edge of 58; four columns at D_8082B12C's x (-114, 12, 44,
// 76) - column 0 the upgrades, 1-3 the equipment - each icon inset 2 and 28 square. Link's portrait is
// the quad at x -64..0, y 50..-62: 64 x 112, the size of the pause-Link framebuffer. Drawn extent:
// x -112..106, y 56..-68 (218 x 124). The page rect is 216 wide, so at scale 1 the outer columns run
// one unit into the hands' 6-unit clearance strip on each side - five units clear of the hands
// themselves. Reported rather than scaled: VanillaPages.h says why the scale stays 1.
constexpr int32_t kCols = 4;
constexpr int32_t kRows = 4;
constexpr int32_t kSlots = kCols * kRows;
constexpr int16_t kColX[kCols] = { -114, 12, 44, 76 };
constexpr int16_t kIcon = 28;
constexpr int16_t kLinkLeft = -64;
constexpr int16_t kLinkTop = 50;
constexpr int16_t kLinkW = 64;  // PAUSE_EQUIP_PLAYER_WIDTH
constexpr int16_t kLinkH = 112; // PAUSE_EQUIP_PLAYER_HEIGHT
static int16_t SlotLeft(int32_t point) {
    return (int16_t)(kColX[point % kCols] + 2);
}
static int16_t SlotTop(int32_t point) {
    return (int16_t)(58 - 32 * (point / kCols) - 2);
}
// The drawn extent's centre (-3, -6) onto the rect's centre (160, 120).
constexpr RsVanillaMap kMap = { 163, 114 };

// Vanilla's upgrade tables (z_kaleido_equipment.c:8-14).
static const u8 sChildUpgrades[] = { UPG_BULLET_BAG, UPG_BOMB_BAG, UPG_STRENGTH, UPG_SCALE };
static const u8 sAdultUpgrades[] = { UPG_QUIVER, UPG_BOMB_BAG, UPG_STRENGTH, UPG_SCALE };
static const u8 sChildUpgradeItemBases[] = { ITEM_BULLET_BAG_30, ITEM_BOMB_BAG_20, ITEM_BRACELET, ITEM_SCALE_SILVER };
static const u8 sAdultUpgradeItemBases[] = { ITEM_QUIVER_30, ITEM_BOMB_BAG_20, ITEM_BRACELET, ITEM_SCALE_SILVER };
static const u8 sUpgradeItemOffsets[] = { 0x00, 0x03, 0x06, 0x09 };
static const u8 sEquipmentItemOffsets[] = {
    0x00, 0x00, 0x01, 0x02, 0x00, 0x03, 0x04, 0x05, 0x00, 0x06, 0x07, 0x08, 0x00, 0x09, 0x0A, 0x0B,
};

// --- movement: KaleidoScope_DrawEquipment, z_kaleido_equipment.c:212-451 ---------------------------
//
// THE UPGRADE COLUMN'S TOP CELL IS DIRECTION-DEPENDENT IN VANILLA, and the port keeps it: moving left
// or up into it, or stepping off the LEFT page arrow onto it, tests the bullet bag; stepping off the
// RIGHT page arrow tests CUR_UPG_VALUE(0), the quiver. Every other upgrade cell tests its own row's
// upgrade (bomb bag, strength, scale). The equipment cells test the owned-equipment bit.

static bool OwnsCell(int32_t point) {
    return (gBitFlags[point - 1] & gSaveContext.inventory.equipment) != 0;
}

// The upgrade test for a move landing on (0, y) by moving left or up - kaleido's :231-240 / :321-328.
static bool UpgradeLeftUp(int32_t y) {
    return y == 0 ? CUR_UPG_VALUE(UPG_BULLET_BAG) != 0 : CUR_UPG_VALUE(y) != 0;
}

// Left/right (:225-307). The same shape as the item page's: the columns beyond the cursor on its own
// row, then each row below, wrapping, then the page arrow.
static int32_t MoveHorizontal(int32_t start, int32_t dir) {
    const bool any = RsVanilla_PauseAnyCursor();
    const int32_t startX = start % kCols;
    const int32_t startY = start / kCols;
    int32_t x = startX;
    int32_t y = startY;
    int32_t point = start;
    while (true) {
        if (dir < 0 && x != 0) {
            x--;
            point--;
            if (x == 0) {
                if (UpgradeLeftUp(y)) {
                    return point;
                }
            } else if (OwnsCell(point) || any) {
                return point;
            }
            continue;
        }
        if (dir > 0 && x < kCols - 1) {
            x++;
            point++;
            // x is at least 1 here, so kaleido's `cursorX == 0` upgrade branch (:273-276) never runs.
            if (OwnsCell(point) || any) {
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

// Up/down (:315-361): its own column, skipping cells it cannot stand on; stops at the edges.
static int32_t MoveVertical(int32_t start, int32_t dir) {
    const bool any = RsVanilla_PauseAnyCursor();
    const int32_t x = start % kCols;
    int32_t y = start / kCols;
    int32_t point = start;
    while (true) {
        if (dir < 0) {
            if (y == 0) {
                return RS_MENU_LINK_NONE;
            }
            y--;
            point -= kCols;
            if (x == 0) {
                if (UpgradeLeftUp(y)) {
                    return point;
                }
            } else if (OwnsCell(point) || any) {
                return point;
            }
        } else {
            if (y >= kRows - 1) {
                return RS_MENU_LINK_NONE;
            }
            y++;
            point += kCols;
            if (x == 0) {
                if (CUR_UPG_VALUE(y) != 0) {
                    return point;
                }
            } else if (OwnsCell(point) || any) {
                return point;
            }
        }
    }
}

// Off the left arrow (:362-409), column-major from the left; the top-left cell tests the bullet bag.
static int32_t EnterFromLeft() {
    for (int32_t x = 0; x < kCols; x++) {
        for (int32_t y = 0; y < kRows; y++) {
            const int32_t point = x + y * kCols;
            if (x == 0 ? UpgradeLeftUp(y) : OwnsCell(point)) {
                return point;
            }
        }
    }
    return RS_MENU_LINK_HAND_RIGHT;
}

// Off the right arrow (:410-450), column-major from the right; the top-left cell tests the QUIVER.
static int32_t EnterFromRight() {
    for (int32_t x = kCols - 1; x >= 0; x--) {
        for (int32_t y = 0; y < kRows; y++) {
            const int32_t point = x + y * kCols;
            if (x == 0 ? CUR_UPG_VALUE(y) != 0 : OwnsCell(point)) {
                return point;
            }
        }
    }
    return RS_MENU_LINK_HAND_LEFT;
}

// A cell is a node if any of the rules above can land on it - which for the top upgrade cell is
// either the bullet bag or the quiver.
static bool IsNode(int32_t point) {
    const int32_t x = point % kCols;
    const int32_t y = point / kCols;
    if (x == 0) {
        return y == 0 ? (CUR_UPG_VALUE(UPG_BULLET_BAG) != 0 || CUR_UPG_VALUE(UPG_QUIVER) != 0)
                      : CUR_UPG_VALUE(y) != 0;
    }
    return OwnsCell(point) || RsVanilla_PauseAnyCursor();
}

// kaleido's cursorItem for a cell (:453-488) - what the name panel says (#132) and what an equip puts on
// the B button. The Biggoron cell names the heart piece when the sword is the BGS (a vanilla quirk
// the B-button write below undoes) and the knife when the broken knife is owned.
static int32_t CellItem(int32_t point) {
    const int32_t x = point % kCols;
    const int32_t y = point / kCols;
    int32_t item;
    if (x == 0) {
        const bool bulletRow = LINK_AGE_IN_YEARS == YEARS_CHILD ? (y == 0 && CUR_UPG_VALUE(UPG_BULLET_BAG) != 0)
                                                                : (y == 0 && CUR_UPG_VALUE(UPG_QUIVER) == 0);
        if (bulletRow) {
            item = ITEM_BULLET_BAG_30 + CUR_UPG_VALUE(UPG_BULLET_BAG) - 1;
        } else {
            item = ITEM_QUIVER_30 + sUpgradeItemOffsets[y] + CUR_UPG_VALUE(y) - 1;
        }
    } else {
        item = ITEM_SWORD_KOKIRI + sEquipmentItemOffsets[point];
    }
    if (y == 0 && x == 3) {
        if (gSaveContext.bgsFlag != 0) {
            item = ITEM_HEART_PIECE_2;
        } else if (CHECK_OWNED_EQUIP_ALT(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_BROKENGIANTKNIFE)) {
            item = ITEM_SWORD_KNIFE;
        }
    }
    return item;
}

static std::string NodeId(int32_t point) {
    char id[16];
    std::snprintf(id, sizeof(id), "equip_%02d", point);
    return id;
}

static int32_t PointOfNode(const std::string& id) {
    if (id.size() != 8 || id.compare(0, 6, "equip_") != 0) {
        return -1;
    }
    const int32_t point = (id[6] - '0') * 10 + (id[7] - '0');
    return point >= 0 && point < kSlots ? point : -1;
}

static int32_t sLastPoint = -1;
static int32_t sPageIndex = -1;

static void EquipmentPageNodes(int32_t pageIndex, void* userData) {
    (void)pageIndex;
    (void)userData;
    const int32_t cursorPoint = PointOfNode(RsMenu_CursorId());
    if (cursorPoint >= 0) {
        sLastPoint = cursorPoint;
    }
    int32_t nodeOf[kSlots];
    for (int32_t point = 0; point < kSlots; point++) {
        nodeOf[point] = -1;
        if (IsNode(point)) {
            nodeOf[point] = RsMenu_AddCursorNode(NodeId(point).c_str(), kMap.X(SlotLeft(point)),
                                                 kMap.Y(SlotTop(point)), kIcon, kIcon);
        }
    }
    auto toNode = [&](int32_t target) {
        return target >= 0 ? nodeOf[target] : target;
    };
    for (int32_t point = 0; point < kSlots; point++) {
        if (nodeOf[point] < 0) {
            continue;
        }
        RsMenu_LinkCursorNode(nodeOf[point], toNode(MoveHorizontal(point, -1)), toNode(MoveHorizontal(point, 1)),
                              toNode(MoveVertical(point, -1)), toNode(MoveVertical(point, 1)));
    }
    RsMenu_LinkHands(toNode(EnterFromLeft()), toNode(EnterFromRight()));
    // Where the menu opens on this page: the cell last stood on if it is still a node, else where the
    // left arrow's scan lands - kaleido has no default-cursor rule for this page, so this is ours.
    int32_t entry = sLastPoint >= 0 && nodeOf[sLastPoint] >= 0 ? nodeOf[sLastPoint] : -1;
    if (entry < 0) {
        const int32_t first = EnterFromLeft();
        entry = first >= 0 ? nodeOf[first] : -1;
    }
    RsMenu_SetCursorGrid(entry);
}

// --- drawing: z_kaleido_equipment.c:194-201, :769-858 -----------------------------------------------

static void EquipmentPageDraw(struct PlayState* play, int32_t pageIndex, void* userData) {
    (void)play;
    (void)pageIndex;
    (void)userData;
    // The equipped outlines: around the current sword, shield, tunic and boots, 2 outside the icon.
    for (int32_t row = 0; row < kRows; row++) {
        const int32_t value = CUR_EQUIP_VALUE(row);
        if (value == 0) {
            continue;
        }
        const int32_t point = row * kCols + value;
        RsMenu_DrawIcon(gEquippedItemOutlineTex, RS_MENU_TEX_IA8, 32, 32, (int16_t)(kMap.X(SlotLeft(point)) - 2),
                        (int16_t)(kMap.Y(SlotTop(point)) - 2), 32, 32, 255, 255, 255, 255, false);
    }

    const bool drawGreyItems = !CVarGetInteger(CVAR_CHEAT("TimelessEquipment"), 0);
    const bool toggleStrength = CVarGetInteger(CVAR_ENHANCEMENT("ToggleStrength"), 0) != 0;
    const bool strengthOff = CVarGetInteger(CVAR_ENHANCEMENT("StrengthDisabled"), 0) != 0;
    for (int32_t row = 0; row < kRows; row++) {
        // The upgrade in column 0, with kaleido's greying rules (:777-822).
        const int16_t ux = kMap.X(SlotLeft(row * kCols));
        const int16_t uy = kMap.Y(SlotTop(row * kCols));
        if (LINK_AGE_IN_YEARS == YEARS_CHILD) {
            const int32_t value = CUR_UPG_VALUE(sChildUpgrades[row]);
            if (value != 0) {
                const int32_t item = sChildUpgradeItemBases[row] + value - 1;
                const bool grey = (drawGreyItems && (item == ITEM_GAUNTLETS_SILVER || item == ITEM_GAUNTLETS_GOLD)) ||
                                  (toggleStrength && strengthOff && sChildUpgrades[row] == UPG_STRENGTH);
                RsMenu_DrawIcon(gItemIcons[item], RS_MENU_TEX_RGBA32, 32, 32, ux, uy, kIcon, kIcon, 255, 255, 255,
                                255, grey);
            }
        } else if (row == 0 && CUR_UPG_VALUE(sAdultUpgrades[row]) == 0) {
            // No bow: the bullet bag, greyed (:795-805).
            const int32_t item = sChildUpgradeItemBases[row] + CUR_UPG_VALUE(sChildUpgrades[row]) - 1;
            RsMenu_DrawIcon(gItemIcons[item], RS_MENU_TEX_RGBA32, 32, 32, ux, uy, kIcon, kIcon, 255, 255, 255, 255,
                            drawGreyItems);
        } else if (CUR_UPG_VALUE(sAdultUpgrades[row]) != 0) {
            const int32_t item = sAdultUpgradeItemBases[row] + CUR_UPG_VALUE(sAdultUpgrades[row]) - 1;
            const bool grey = (drawGreyItems && item == ITEM_BRACELET && !IS_RANDO && !toggleStrength) ||
                              (toggleStrength && strengthOff && sAdultUpgrades[row] == UPG_STRENGTH);
            RsMenu_DrawIcon(gItemIcons[item], RS_MENU_TEX_RGBA32, 32, 32, ux, uy, kIcon, kIcon, 255, 255, 255, 255,
                            grey);
        }
        // The three equipment cells (:824-840): BGS and the broken knife share the Biggoron cell.
        for (int32_t k = 0; k < 3; k++) {
            const int32_t point = row * kCols + k + 1;
            const int32_t bit = row * 4 + k;
            const int32_t item = ITEM_SWORD_KOKIRI + row * 3 + k;
            const bool grey = !CHECK_AGE_REQ_ITEM(item);
            const void* tex = nullptr;
            if (row == 0 && k == 2 && gSaveContext.bgsFlag != 0) {
                tex = gItemIconSwordBiggoronTex;
            } else if (row == 0 && k == 2 && (gBitFlags[bit + 1] & gSaveContext.inventory.equipment)) {
                tex = gItemIconBrokenGiantsKnifeTex;
            } else if (gBitFlags[bit] & gSaveContext.inventory.equipment) {
                tex = gItemIcons[item];
            }
            if (tex != nullptr) {
                RsMenu_DrawIcon(tex, RS_MENU_TEX_RGBA32, 32, 32, kMap.X(SlotLeft(point)), kMap.Y(SlotTop(point)),
                                kIcon, kIcon, 255, 255, 255, 255, grey);
            }
        }
    }

    // Link's portrait, last - KaleidoScope_DrawEquipmentImage is the last thing kaleido draws here.
    RsMenu_DrawPauseLink(kMap.X(kLinkLeft), kMap.Y(kLinkTop), kLinkW, kLinkH);
}

// --- equipping (half 2): z_kaleido_equipment.c:523-703 ----------------------------------------------
//
// A on an equipment cell: kaleido's age check, SoH's EquipmentCanBeRemoved toggle-off, the owned check,
// Inventory_ChangeEquipment, and for a sword the B button - infTable[29] zeroed (which clears
// INFTABLE_SWORDLESS), the Biggoron/knife naming, Interface_LoadItemIcon1(play, 0). A on the strength
// upgrade flips SoH's ToggleStrength. A C button (or D-pad slot) on a shield, tunic or boots cell
// assigns it to that button under SoH's AssignableTunicsAndBoots.
static void EquipWithA(PlayState* play, int32_t point, int32_t cursorItem) {
    const int32_t x = point % kCols;
    const int32_t y = point / kCols;
    bool swordButtonOnly = false;
    if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentCanBeRemoved"), 0)) {
        const int32_t toggle = CVarGetInteger(CVAR_ENHANCEMENT("SwordToggle"), SWORD_TOGGLE_NONE);
        if (toggle == SWORD_TOGGLE_BOTH_AGES || (toggle == SWORD_TOGGLE_CHILD && LINK_IS_CHILD)) {
            if (y == 0 && x == CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD)) {
                Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_NONE);
                gSaveContext.equips.buttonItems[0] = ITEM_NONE;
                Flags_SetInfTable(INFTABLE_SWORDLESS);
                swordButtonOnly = true;
            }
        } else if (y == 0 && x == 3 && CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD) == EQUIP_VALUE_SWORD_BIGGORON &&
                   CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_MASTER)) {
            Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER);
            gSaveContext.equips.buttonItems[0] = ITEM_SWORD_MASTER;
            Flags_UnsetInfTable(INFTABLE_SWORDLESS);
            swordButtonOnly = true;
        }
        if (!swordButtonOnly) {
            bool removed = false;
            if (y == 1 && x == CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD)) {
                Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_NONE);
                removed = true;
            } else if (y == 2 && x == CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC)) {
                Inventory_ChangeEquipment(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_KOKIRI);
                removed = true;
            } else if (y == 3 && x == CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS)) {
                Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_KOKIRI);
                removed = true;
            }
            if (removed) {
                // kaleido's RESUME_EQUIPMENT, which for these rows is only the sound.
                RsMenu_PlaySfxId(NA_SE_SY_DECIDE);
                RsVanilla_CountEquip();
                return;
            }
        }
    }
    if (!swordButtonOnly) {
        if (!CHECK_OWNED_EQUIP(y, x - 1)) {
            RsMenu_PlaySfxId(NA_SE_SY_ERROR);
            return;
        }
        Inventory_ChangeEquipment(y, x);
        if (y == 0) {
            gSaveContext.infTable[29] = 0;
            gSaveContext.equips.buttonItems[0] = cursorItem;
            if (x == 3 && gSaveContext.bgsFlag != 0) {
                gSaveContext.equips.buttonItems[0] = ITEM_SWORD_BGS;
                gSaveContext.swordHealth = 8;
            } else {
                if (gSaveContext.equips.buttonItems[0] == ITEM_HEART_PIECE_2) {
                    gSaveContext.equips.buttonItems[0] = ITEM_SWORD_BGS;
                }
                if (gSaveContext.equips.buttonItems[0] == ITEM_SWORD_BGS && gSaveContext.bgsFlag == 0 &&
                    CHECK_OWNED_EQUIP_ALT(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_BROKENGIANTKNIFE)) {
                    gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KNIFE;
                }
            }
        }
    }
    if (y == 0) {
        Interface_LoadItemIcon1(play, 0); // RESUME_EQUIPMENT_SWORD
    }
    RsMenu_PlaySfxId(NA_SE_SY_DECIDE);
    RsVanilla_CountEquip();
}

// AssignableTunicsAndBoots (:645-691): a shield, tunic or boots cell onto a C button.
static void AssignToButton(PlayState* play, uint16_t press, int32_t point, int32_t cursorItem) {
    const int32_t x = point % kCols;
    const int32_t y = point / kCols;
    if (y == 0) {
        return;
    }
    if (!CHECK_OWNED_EQUIP(y, x - 1)) {
        RsMenu_PlaySfxId(NA_SE_SY_ERROR);
        return;
    }
    u16 slot = 0;
    switch (cursorItem) {
        case ITEM_TUNIC_KOKIRI:
            slot = SLOT_TUNIC_KOKIRI;
            break;
        case ITEM_TUNIC_GORON:
            slot = SLOT_TUNIC_GORON;
            break;
        case ITEM_TUNIC_ZORA:
            slot = SLOT_TUNIC_ZORA;
            break;
        case ITEM_BOOTS_KOKIRI:
            slot = SLOT_BOOTS_KOKIRI;
            break;
        case ITEM_BOOTS_IRON:
            slot = SLOT_BOOTS_IRON;
            break;
        case ITEM_BOOTS_HOVER:
            slot = SLOT_BOOTS_HOVER;
            break;
        case ITEM_SHIELD_DEKU:
            slot = SLOT_SHIELD_DEKU;
            break;
        case ITEM_SHIELD_HYLIAN:
            slot = SLOT_SHIELD_HYLIAN;
            break;
        case ITEM_SHIELD_MIRROR:
            slot = SLOT_SHIELD_MIRROR;
            break;
        default:
            break;
    }
    if (GameInteractor_Should(VB_EQUIP_ITEM_TO_C_BUTTON, true, play, slot, (u16)cursorItem)) {
        // From the cell's drawn top-left (kaleido's equipVtx[cursorSlot * 4], z_kaleido_equipment.c:682-684).
        const int16_t x = kMap.X(SlotLeft(point));
        const int16_t y = kMap.Y(SlotTop(point));
        RsVanilla_BeginEquip(press, (u16)cursorItem, slot, x, y, x, y);
    }
}

static void EquipmentPageInput(int32_t pageIndex, int32_t level, uint16_t press, int32_t navY, void* userData) {
    (void)pageIndex;
    (void)navY;
    (void)userData;
    if (level != 0 || gPlayState == nullptr) {
        return;
    }
    const bool assignable = CVarGetInteger(CVAR_ENHANCEMENT("AssignableTunicsAndBoots"), 0) != 0;
    const uint16_t cButtons = RsVanilla_EquipButtons(gPlayState->state.input[0].cur.button);
    const RsMenuCursorNode* node = RsMenu_CursorAt(RsMenu_CursorIndex());
    const int32_t point = node != nullptr ? PointOfNode(node->id) : -1;
    if (point < 0) {
        return;
    }
    const int32_t x = point % kCols;
    const int32_t y = point / kCols;
    const int32_t cursorItem = CellItem(point);
    if (CHECK_BTN_ALL(press, BTN_A) && x == 0 && y == 2 && CVarGetInteger(CVAR_ENHANCEMENT("ToggleStrength"), 0)) {
        CVarSetInteger(CVAR_ENHANCEMENT("StrengthDisabled"), !CVarGetInteger(CVAR_ENHANCEMENT("StrengthDisabled"), 0));
        RsMenu_PlaySfxId(NA_SE_SY_DECIDE);
        return;
    }
    const uint16_t buttons = (uint16_t)(BTN_A | cButtons);
    if (x == 0 || (press & buttons) == 0) {
        return;
    }
    if (!CHECK_AGE_REQ_EQUIP(y, x)) {
        // EQUIP_FAIL (:693-701): the error sound for A, and for a C button on a tunic or boots row.
        if (CHECK_BTN_ALL(press, BTN_A) || (assignable && y > 1)) {
            RsMenu_PlaySfxId(NA_SE_SY_ERROR);
        }
        return;
    }
    if (CHECK_BTN_ALL(press, BTN_A)) {
        EquipWithA(gPlayState, point, cursorItem);
    } else if (assignable) {
        AssignToButton(gPlayState, press, point, cursorItem);
    }
}

// #132: what the name panel says on this page. The item is kaleido's cursorItem (CellItem); the grey is its
// nameColorSet (z_kaleido_equipment.c:497-517, and SoH's strength toggle at :721-729); the timer runs off the
// upgrade column only (UpdateNamePanel, :2510); the A prompt shows unless the cell is equipment not owned
// (DrawInfoPanel, :2413-2416). An unowned cell is named nothing only under PauseAnyCursor (:2452-2458) - without
// it the cursor never stands on one.
static void EquipmentPageName(int32_t pageIndex, const RsMenuCursorNode* node, RsMenuNameInfo* info,
                              void* userData) {
    (void)pageIndex;
    (void)userData;
    const int32_t point = PointOfNode(node->id);
    if (point < 0) {
        return;
    }
    const int32_t x = point % kCols;
    const int32_t y = point / kCols;
    const int32_t item = CellItem(point);
    const bool unownedEquipment = x != 0 && !CHECK_OWNED_EQUIP(y, x - 1);
    info->item = RsVanilla_PauseAnyCursor() && unownedEquipment ? -1 : item;
    bool grey = !CHECK_AGE_REQ_EQUIP(y, x);
    if (item == ITEM_BRACELET) {
        grey = !(LINK_AGE_IN_YEARS == YEARS_CHILD || IS_RANDO);
    }
    if (x == 0 && y == 0) {
        grey = LINK_AGE_IN_YEARS != YEARS_CHILD && item >= ITEM_BULLET_BAG_30 && item <= ITEM_BULLET_BAG_50;
    }
    if (x == 0 && y == 2 && CVarGetInteger(CVAR_ENHANCEMENT("ToggleStrength"), 0)) {
        grey = CVarGetInteger(CVAR_ENHANCEMENT("StrengthDisabled"), 0) != 0;
    }
    info->grey = grey;
    info->alternates = x != 0;
    info->subState = RsVanilla_EquipSubState();
    info->prompt = unownedEquipment ? RS_MENU_PROMPT_NONE : RS_MENU_PROMPT_A_EQUIP;
}

// #125: the HUD buttons vanilla shows on this page.
static void EquipmentPageHud(int32_t pageIndex, uint8_t status[9], void* userData) {
    (void)pageIndex;
    (void)userData;
    RsVanilla_HudButtons(PAUSE_EQUIP, status);
}

int32_t RsMenuEquipmentPage_Register() {
    RsMenuPage page;
    page.id = "equipment";
    page.title = "Equipment";
    page.draw = EquipmentPageDraw;
    page.nodes = EquipmentPageNodes;
    page.input = EquipmentPageInput;
    page.claim = RsVanilla_ClaimEquipDpad;
    page.stickModel = RS_MENU_STICK_KALEIDO_SEQUENTIAL;
    page.hud = EquipmentPageHud;
    page.ownsItemHighlight = false;
    page.name = EquipmentPageName;
    page.toLabel = RsVanilla_ToPageLabel(PAUSE_EQUIP);
    sPageIndex = RsMenu_RegisterPageStruct(page);
    return sPageIndex;
}

void RsMenuEquipmentPage_Describe(RsMenuPortInfo* info) {
    info->pageId = "equipment";
    info->pageIndex = sPageIndex;
    info->scale = 1.0f;
    info->offsetX = kMap.offsetX;
    info->offsetY = kMap.offsetY;
    info->x0 = kMap.X(SlotLeft(0));
    info->y0 = kMap.Y(SlotTop(0));
    info->x1 = (int16_t)(kMap.X(SlotLeft(kCols - 1)) + kIcon);
    info->y1 = (int16_t)(kMap.Y(SlotTop(kSlots - 1)) + kIcon);
    info->slots.clear();
    for (int32_t point = 0; point < kSlots; point++) {
        const int32_t x = point % kCols;
        const int32_t y = point / kCols;
        const bool owned = x == 0 ? IsNode(point) : OwnsCell(point);
        const int32_t item = owned ? CellItem(point) : (x == 0 ? ITEM_NONE : ITEM_SWORD_KOKIRI + y * 3 + x - 1);
        RsMenuSlotInfo s;
        s.node = NodeId(point);
        s.slot = point;
        s.x = kMap.X(SlotLeft(point));
        s.y = kMap.Y(SlotTop(point));
        s.w = kIcon;
        s.h = kIcon;
        s.item = item;
        s.itemName = RsVanilla_ItemToken(item);
        s.owned = owned;
        s.isNode = IsNode(point);
        s.grey = x != 0 && owned && !CHECK_AGE_REQ_ITEM(ITEM_SWORD_KOKIRI + y * 3 + x - 1);
        info->slots.push_back(s);
    }
    // Link's portrait, as a slot line of its own so its mapped box is on record; never a node.
    RsMenuSlotInfo link;
    link.node = "equip_link";
    link.slot = -1;
    link.x = kMap.X(kLinkLeft);
    link.y = kMap.Y(kLinkTop);
    link.w = kLinkW;
    link.h = kLinkH;
    link.item = ITEM_NONE;
    link.itemName = "PAUSE_LINK";
    link.owned = true;
    link.isNode = false;
    link.grey = false;
    info->slots.push_back(link);
}
