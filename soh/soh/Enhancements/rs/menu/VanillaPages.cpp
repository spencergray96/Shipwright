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
#include "RsMenuRaw.h"
#include "soh/Enhancements/rs/sprites/RsSprites.h"

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

// --- #301: slot tracks ----------------------------------------------------------------------------
//
// The pieces are tools/sprites/rs-sprites.json's approved `settings_slider` rows, each a 16 x 16 RS sprite (the art
// keeps its offset inside that canvas) xBR-upscaled to 128 x 128 texels (#312), drawn at ONE RS PIXEL TO ONE GAME UNIT: a piece
// is 16 units, half the 32-unit slot pitch, so 2n - 1 plain pieces span n sockets exactly and the end caps' art (the
// outer half of their canvas, 8 units) closes the row 2 units outside the edge icons. The band is the canvas's middle
// 14 rows, centred on the icons; a 28-unit icon covers its socket and overhangs the band by 7 above and below.
//
// WHY THIS ASSEMBLY (the owner's looks, rounds 1-3; measured in docs/test-runs/2026-11-07-issue-301-slider-rows).
// The segments are windows on one continuous stone texture, so two pieces meet cleanly only where RS cut them side
// by side; alternating notched and plain pieces put a line left of every notch. So:
//   - the row opens as RS cut it: the left cap 2852, then the notched piece 2853 under the first socket;
//   - then only the plain piece 2855: the first unmirrored (2853 -> 2855 is an RS cut), then mirrored on every other
//     step, so every later join meets an identical column. The last piece is always mirrored, so its left edge meets
//     the right cap 2857, the better of the two orientations;
//   - the lone notch dot 2863 (pixel-identical to 2853's dot) on top of every other socket.
// Column-mean steps at 4K: 4.1 (cap), 2.9 (2853 -> plain), 0.0 thereafter, 7.3 (right cap).
//
// THE LEFT END READS A LITTLE DARKER, and stays so (owner, round 3: fine as is). 2853's stone is ~10 levels darker
// than the plain piece's, and it is the only piece 2852 joins cleanly. Every arrangement that lifts it (2854/2856 in
// its place, 2857 mirrored as the left cap, a plain piece plus a dot) costs a join of 10.8-25.7 levels somewhere, a
// seam the owner had just signed off as gone. The old 679-683 set is cut the same way and starts just as dark
// (679's stone 62.7 against 72.7-78.1): the darker start is RS's own art.
constexpr int16_t kTrackPiece = 16;
static_assert(RsVanilla_kTrackPitch == 2 * kTrackPiece, "a socket is two pieces");

// Piece `i` of a row of n sockets: 0 the left cap, 1 the notched piece, 2 .. 2n-1 plain, 2n the right cap. A
// socket's piece is 2k + 1.
static bool TrackPieceMirrored(int32_t i, int32_t pieces) {
    return i >= 3 && i < pieces - 1 && (i % 2) == 1;
}

static const char* TrackPieceToken(int32_t i, int32_t pieces) {
    if (i == 0) {
        return "slider_end_left";
    }
    if (i == pieces - 1) {
        return "slider_end_right";
    }
    return i == 1 ? "slider_track_notch" : "slider_track";
}

void RsVanilla_DrawTracks(RsVanillaTracks* t) {
    t->quads = 0;
    t->missing = 0;
    t->draws++;
    GraphicsContext* gfxCtx = nullptr;
    std::vector<Gfx>* dl = nullptr;
    if (!RsMenu_BeginRawDraw(&gfxCtx, &dl)) {
        return;
    }
    // RsMenu.cpp's icon preset with the RGBA combiner: the texel times a white prim, so the art draws as it is;
    // XLU_SURF so the binary alpha's bled edge blends rather than cuts. Bilinear: a texel of the x8 texture covers about
    // 0.56 screen px at 1080p (a mild minify) and 1.1 at 4K, and the xBR art already carries its smoothed edges (#312).
    dl->push_back(gsDPPipeSync());
    dl->push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
    dl->push_back(gsDPSetCombineMode(G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM));
    dl->push_back(gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP |
                                       G_TT_NONE | G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE |
                                       G_PM_NPRIMITIVE,
                                   G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));
    dl->push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
    dl->push_back(gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
    const int32_t pieces = 2 * t->sockets + 1;
    auto emit = [&](const char* token, float left, float top, bool mirror) {
        // EmitRect takes the ortho's centred, y-up space. A mirrored piece: its right edge as x and a negative
        // width, which maps texel column 0 to the right; no culling in this state, so the flipped winding draws.
        const float x = left - (float)(SCREEN_WIDTH / 2);
        const int32_t n = RsSprites_EmitRect(gfxCtx, *dl, token, mirror ? x + kTrackPiece : x, top,
                                             mirror ? -(float)kTrackPiece : (float)kTrackPiece, (float)kTrackPiece);
        t->quads += n;
        t->missing += n == 0 ? 1 : 0;
    };
    for (int32_t row = 0; row < t->rows; row++) {
        const int16_t canvasTop = (int16_t)(t->firstY + row * t->rowPitch - kTrackPiece / 2);
        const float top = (float)(SCREEN_HEIGHT / 2 - canvasTop);
        // Piece 1's canvas is centred on the first socket.
        const float piece1 = (float)(t->firstX - kTrackPiece / 2);
        for (int32_t i = 0; i < pieces; i++) {
            emit(TrackPieceToken(i, pieces), piece1 + (float)((i - 1) * kTrackPiece), top, TrackPieceMirrored(i, pieces));
        }
        for (int32_t k = 1; k < t->sockets; k++) { // the first socket is 2853's own
            emit("slider_notch", piece1 + (float)(2 * k * kTrackPiece), top, false);
        }
    }
}

void RsVanilla_DescribeTracks(const RsVanillaTracks& t, int32_t (*slotOf)(int32_t row, int32_t socket),
                              bool (*owned)(int32_t slot), RsMenuPortInfo* info) {
    info->trackRows.clear();
    info->trackQuads = t.quads;
    info->trackMissing = t.missing;
    info->trackDraws = t.draws;
    for (int32_t row = 0; row < t.rows; row++) {
        const int16_t y = (int16_t)(t.firstY + row * t.rowPitch);
        RsMenuTrackRowInfo r;
        r.row = row;
        r.x0 = (int16_t)(t.firstX - kTrackPiece); // the caps' art is the inner half of their canvas
        r.x1 = (int16_t)(t.firstX + (t.sockets - 1) * RsVanilla_kTrackPitch + kTrackPiece);
        r.y0 = (int16_t)(y - kTrackPiece / 2 + 1); // the band: canvas rows 1..14
        r.y1 = (int16_t)(y + kTrackPiece / 2 - 1);
        r.pieces = 2 * t.sockets + 1 + (t.sockets - 1); // the row's segments and the dots on it
        for (int32_t k = 0; k < t.sockets; k++) {
            RsMenuSocketInfo s;
            s.slot = slotOf(row, k);
            s.x = (int16_t)(t.firstX + k * RsVanilla_kTrackPitch);
            s.y = y;
            s.owned = owned(s.slot);
            r.sockets.push_back(s);
        }
        info->trackRows.push_back(r);
    }
}
