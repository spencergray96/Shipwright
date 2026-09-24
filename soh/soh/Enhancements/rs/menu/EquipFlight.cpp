/*
 * EquipFlight.cpp - equipping to a C button or D-pad slot from the scroll, with the icon flying from its
 * slot to the button the way vanilla pause flies it (sturdy-bassoon#125).
 *
 *   start   - KaleidoScope_SetupItemEquip (z_kaleido_item.c:825-881): the button the press names, the
 *             sound, and the flight's starting state. Nothing is written yet.
 *   flight  - KaleidoScope_UpdateItemEquip (:887-1259), line for line, one step per game tick. A magic
 *             arrow first fades in, flies to the Bow, flashes and becomes the loaded bow, then flies on.
 *   landing - the tail of that function (:1187-1253): the rename, the swap with a button already holding
 *             the slot, SoH's equip-dupe fix, the write and Interface_LoadItemIcon1. The save changes
 *             here, as in vanilla, so the button's icon changes the moment the flying one arrives.
 *   draw    - Interface_Draw's "Inventory Equip Effects" (z_parameter.c:5760-5807), from the
 *             OnInterfaceDrawItemButtonsEnd hook at the same point, so the icon passes OVER the HUD's
 *             buttons exactly as vanilla's does. Its own four vertices stand in for kaleido's cursorVtx,
 *             which only exists while kaleido is up.
 *
 * Kaleido's pauseCtx fields (equipTargetItem, equipAnimX/Y/Alpha, ...) and its file statics are the
 * statics below; the menu never touches pauseCtx. The icon's size lives where kaleido keeps it, in
 * WREG(90) (the edge, x10) and WREG(87) (what is left to shrink), so a scroll equip and a vanilla one
 * share them - and share kaleido's quirk: the first equip after boot shrinks from WREG(87) = 80
 * (z_construct.c:484), every later one from WREG(91) = 40.
 *
 * While a flight is up the menu takes no input (RsMenu.cpp): kaleido's sub-state is 3 then, and every
 * input branch - the cursor, L/R, B and START - waits for 0.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-22
 */

#include "VanillaPagesInternal.h"
#include "RsMenu.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/frame_interpolation.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "textures/icon_item_static/icon_item_static.h"
extern PlayState* gPlayState;
// OTRGlobals.h declares these for C translation units only; C++ ones declare what they use, as
// VisualAgony.cpp does.
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);
int16_t OTRGetRectDimensionFromRightEdge(float v);
}

// Interface_Draw's magic-arrow effect colours (z_parameter.c:5101-5103), fire / ice / light.
static const s16 kArrowEffectR[] = { 255, 100, 255 };
static const s16 kArrowEffectG[] = { 0, 100, 255 };
static const s16 kArrowEffectB[] = { 0, 255, 100 };

// The magic arrows' effect items: kaleido renames an arrow to 0xBF + index while its effect plays.
constexpr u16 kArrowEffectBase = 0xBF;

struct EquipFlight {
    bool active = false;
    s16 state = 0;       // sEquipState: 0 fade in, 1 to the Bow, 2 the flash, 3 to the button
    s16 animTimer = 0;   // sEquipAnimTimer
    s16 moveTimer = 10;  // sEquipMoveTimer
    s16 flashTimer = 0;  // D_8082A488
    u16 item = 0;        // pauseCtx->equipTargetItem
    u16 slot = 0;        // pauseCtx->equipTargetSlot
    int32_t target = 0;  // pauseCtx->equipTargetCBtn: 0-2 C-left/down/right, 3-6 D-pad up/down/left/right
    s16 animX = 0;       // pauseCtx->equipAnimX, screen-centred, x10
    s16 animY = 0;
    s16 alpha = 0;       // pauseCtx->equipAnimAlpha
    s16 bowX = 0;        // the Bow slot's top-left, the magic arrow's first stop (kaleido's itemVtx[12])
    s16 bowY = 0;
    int32_t ticks = 0;   // update ticks this flight has taken (the test-only hold counts these)
    s16 linger = 0;      // #133: ticks still to DRAW after landing - see the landing branch
};
static EquipFlight sFlight;
static int32_t sFlights = 0;
static int32_t sLandings = 0;
// TEST-ONLY (menu flight hold <n>): stop advancing once a flight has taken n ticks, so a run can read
// and screenshot one pose of a half-second animation; -1 is off.
static int32_t sHoldAt = -1;
// #133's loop: the flight's opening state, kept so the loop can replay it, and the flag itself.
static EquipFlight sFlightStart;
static s16 sFlightStartW87 = 0; // the size registers as the flight began - see RestartFlight
static s16 sFlightStartW90 = 0;
static bool sHaveFlightStart = false;
static bool sFlightLoop = false;
static bool sFlightProbe = false;

// #133: WHERE THE QUAD IS, as floats, in the same screen space vanilla's integers used. Pulled out of
// the draw so the two channels a frame capture measures - the corner and the side - are one function,
// and so the matrix below and the tick state can never describe different rectangles.
struct FlightQuad {
    float left, top, side;
};

// #133: back to the stored opening state, counted as a fresh flight. One place, because the loop
// restarts from two - the tick that would have landed, and `flight loop` itself.
//
// THE SIZE REGISTERS ARE PART OF THAT STATE, and restoring them rather than calling ResetFlightSize is
// the whole of the difference between a replay and a lookalike. The icon's shrink comes out of
// WREG(87), which is 80 at boot and WREG(91) = 40 once ResetFlightSize has run (z_construct.c:484; the
// file header above). BeginEquip does NOT reset it, so the FIRST flight after boot shrinks 32 -> 24 and
// every later one 32 -> 28. A loop that reset it would replay that first flight on the wrong shrink
// curve - which matters here more than anywhere, because the size is one of the two channels the
// probe exists to measure.
static void RestartFlight() {
    sFlight = sFlightStart;
    WREG(87) = sFlightStartW87;
    WREG(90) = sFlightStartW90;
    sFlights++;
}

// ONE DELIBERATE DEPARTURE FROM VANILLA, and it is the point rather than an oversight: vanilla writes
// `(s16)(animX / 10)`, truncating the drawn corner to a whole screen unit. These are floats, so the
// corner can sit up to a unit from where vanilla drew it (#125 logged t=5 at x=61; this reports 61.5).
// Truncating would re-quantise the very thing option A exists to smooth - the interpolator would tween
// between whole units instead of between the real ones. The TICK STATE is untouched, so every `flight=`
// figure #125 recorded still reads the same; it is the rendered geometry that is finer than vanilla's.
static FlightQuad FlightQuadOf(const EquipFlight& f) {
    FlightQuad q;
    q.left = (float)f.animX / 10.0f;
    q.top = (float)f.animY / 10.0f;
    q.side = (float)WREG(90) / 10.0f;
    if (f.item >= kArrowEffectBase && f.alpha > 0 && f.alpha < 255) {
        // The magic arrow's effect swells as it fades in (:5788-5798). `grow` stays INTEGRAL, unlike
        // the corner above: it is a step function of alpha rather than a position, so smoothing it
        // would change the effect's shape instead of its motion.
        const float grow = (float)((f.alpha / 8) / 2);
        q.left -= grow;
        q.top += grow;
        q.side = grow * 2.0f + 32.0f;
    }
    return q;
}


// The three magic arrows, in kaleido's effect order (fire, ice, light); their ITEM_ ids are not
// consecutive (0x04, 0x0C, 0x12). -1 for anything else.
static int32_t MagicArrowIndex(u16 item) {
    return item == ITEM_ARROW_FIRE ? 0 : item == ITEM_ARROW_ICE ? 1 : item == ITEM_ARROW_LIGHT ? 2 : -1;
}
static const u16 kMagicArrows[] = { ITEM_ARROW_FIRE, ITEM_ARROW_ICE, ITEM_ARROW_LIGHT };

// The icon back to full size for the next flight: kaleido's reset after every landing and at the flash.
static void ResetFlightSize() {
    WREG(90) = 320;
    WREG(87) = WREG(91);
}

// The button a press names: KaleidoScope_SetupItemEquip's choice (z_kaleido_item.c:830-846).
static int32_t TargetOf(uint16_t press) {
    if (CHECK_BTN_ALL(press, BTN_CLEFT)) {
        return 0;
    }
    if (CHECK_BTN_ALL(press, BTN_CDOWN)) {
        return 1;
    }
    if (CHECK_BTN_ALL(press, BTN_CRIGHT)) {
        return 2;
    }
    if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0)) {
        if (CHECK_BTN_ALL(press, BTN_DUP)) {
            return 3;
        }
        if (CHECK_BTN_ALL(press, BTN_DDOWN)) {
            return 4;
        }
        if (CHECK_BTN_ALL(press, BTN_DLEFT)) {
            return 5;
        }
        if (CHECK_BTN_ALL(press, BTN_DRIGHT)) {
            return 6;
        }
    }
    return -1;
}

// The C original assigns these floats to s16 implicitly (truncating toward zero); C++ under /WX wants
// the conversions spelled out, which is all these do.
static s16 FromLeftEdge(int32_t v) {
    return (s16)OTRGetDimensionFromLeftEdge((float)v);
}
static s16 FromRightEdge(int32_t v) {
    return (s16)OTRGetDimensionFromRightEdge((float)v);
}
static s16 RectFromRightEdge(int32_t v) {
    return (s16)OTRGetRectDimensionFromRightEdge((float)v);
}

// Where each button's icon sits, screen-centred (x - 160, 120 - y): kaleido's sCButtonPosX/Y as
// KaleidoScope_UpdateItemEquip recomputes them every tick from SoH's HUD cosmetics
// (z_kaleido_item.c:884-1098), verbatim but for writing into the caller's arrays.
static void ButtonPositions(s16 posX[7], s16 posY[7]) {
    const s16 Top_HUD_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.T"), 0);
    const s16 Left_HUD_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.L"), 0);
    const s16 Right_HUD_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.R"), 0);

    s16 X_Margins_CL = 0;
    s16 X_Margins_CR = 0;
    s16 X_Margins_CD = 0;
    s16 Y_Margins_CL = 0;
    s16 Y_Margins_CR = 0;
    s16 Y_Margins_CD = 0;
    s16 X_Margins_DPad_Items = 0;
    s16 Y_Margins_DPad_Items = 0;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_CL = Right_HUD_Margin;
        }
        Y_Margins_CL = (Top_HUD_Margin * -1);
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_CR = Right_HUD_Margin;
        }
        Y_Margins_CR = (Top_HUD_Margin * -1);
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_CD = Right_HUD_Margin;
        }
        Y_Margins_CD = (Top_HUD_Margin * -1);
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_DPad_Items = Right_HUD_Margin;
        }
        Y_Margins_DPad_Items = (Top_HUD_Margin * -1);
    }
    const s16 ItemIconPos_ori[7][2] = { { C_LEFT_BUTTON_X + X_Margins_CL, C_LEFT_BUTTON_Y + Y_Margins_CL },
                                        { C_DOWN_BUTTON_X + X_Margins_CD, C_DOWN_BUTTON_Y + Y_Margins_CD },
                                        { C_RIGHT_BUTTON_X + X_Margins_CR, C_RIGHT_BUTTON_Y + Y_Margins_CR },
                                        { DPAD_UP_X + X_Margins_DPad_Items, DPAD_UP_Y + Y_Margins_DPad_Items },
                                        { DPAD_DOWN_X + X_Margins_DPad_Items, DPAD_DOWN_Y + Y_Margins_DPad_Items },
                                        { DPAD_LEFT_X + X_Margins_DPad_Items, DPAD_LEFT_Y + Y_Margins_DPad_Items },
                                        { DPAD_RIGHT_X + X_Margins_DPad_Items,
                                          DPAD_RIGHT_Y + Y_Margins_DPad_Items } };
    const s16 DPad_ItemsOffset[4][2] = { { 7, -8 }, { 7, 24 }, { -9, 8 }, { 23, 8 } }; // up, down, left, right

    // The D-pad slots.
    const int32_t dpadType = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0);
    if (dpadType != ORIGINAL_LOCATION) {
        const s16 dpadX = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0);
        const s16 dpadY = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 0);
        for (int32_t i = 0; i < 4; i++) {
            posY[3 + i] = dpadY + Y_Margins_DPad_Items + DPad_ItemsOffset[i][1];
        }
        if (dpadType == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
                X_Margins_DPad_Items = Left_HUD_Margin;
            }
            for (int32_t i = 0; i < 4; i++) {
                posX[3 + i] = FromLeftEdge(dpadX + X_Margins_DPad_Items + DPad_ItemsOffset[i][0]);
            }
        } else if (dpadType == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
                X_Margins_DPad_Items = Right_HUD_Margin;
            }
            for (int32_t i = 0; i < 4; i++) {
                posX[3 + i] = FromRightEdge(dpadX + X_Margins_DPad_Items + DPad_ItemsOffset[i][0]);
            }
        } else if (dpadType == ANCHOR_NONE) {
            for (int32_t i = 0; i < 4; i++) {
                posX[3 + i] = dpadX + DPad_ItemsOffset[i][0];
            }
        }
    } else {
        for (int32_t i = 3; i < 7; i++) {
            posX[i] = FromRightEdge(ItemIconPos_ori[i][0]);
            posY[i] = ItemIconPos_ori[i][1];
        }
    }

    // The three C buttons, each the same shape (:1018-1083).
    struct CButton {
        const char* posType;
        const char* posX;
        const char* posY;
        const char* useMargins;
        s16 xMargin;
        s16 yMargin;
    };
    const CButton cButtons[3] = {
        { CVAR_COSMETIC("HUD.CLeftButton.PosType"), CVAR_COSMETIC("HUD.CLeftButton.PosX"),
          CVAR_COSMETIC("HUD.CLeftButton.PosY"), CVAR_COSMETIC("HUD.CLeftButton.UseMargins"), X_Margins_CL,
          Y_Margins_CL },
        { CVAR_COSMETIC("HUD.CDownButton.PosType"), CVAR_COSMETIC("HUD.CDownButton.PosX"),
          CVAR_COSMETIC("HUD.CDownButton.PosY"), CVAR_COSMETIC("HUD.CDownButton.UseMargins"), X_Margins_CD,
          Y_Margins_CD },
        { CVAR_COSMETIC("HUD.CRightButton.PosType"), CVAR_COSMETIC("HUD.CRightButton.PosX"),
          CVAR_COSMETIC("HUD.CRightButton.PosY"), CVAR_COSMETIC("HUD.CRightButton.UseMargins"), X_Margins_CR,
          Y_Margins_CR },
    };
    for (int32_t i = 0; i < 3; i++) {
        const CButton& b = cButtons[i];
        const int32_t type = CVarGetInteger(b.posType, 0);
        if (type != ORIGINAL_LOCATION) {
            posY[i] = CVarGetInteger(b.posY, 0) + b.yMargin;
            if (type == ANCHOR_LEFT) {
                const s16 margin = CVarGetInteger(b.useMargins, 0) != 0 ? Left_HUD_Margin : b.xMargin;
                posX[i] = FromLeftEdge(CVarGetInteger(b.posX, 0) + margin);
            } else if (type == ANCHOR_RIGHT) {
                const s16 margin = CVarGetInteger(b.useMargins, 0) != 0 ? Right_HUD_Margin : b.xMargin;
                posX[i] = FromRightEdge(CVarGetInteger(b.posX, 0) + margin);
            } else if (type == ANCHOR_NONE) {
                posX[i] = CVarGetInteger(b.posX, 0);
            }
        } else {
            posX[i] = RectFromRightEdge(ItemIconPos_ori[i][0]);
            posY[i] = ItemIconPos_ori[i][1];
        }
    }

    for (int32_t i = 0; i < 7; i++) {
        posX[i] = posX[i] - 160;
        posY[i] = 120 - posY[i];
    }
}

// The landing: the tail of KaleidoScope_UpdateItemEquip (z_kaleido_item.c:1187-1247). A magic arrow
// goes on the button as the bow loaded with it and - unless SeparateArrows is on - takes the bow's slot;
// the flash already did that when the arrow animation ran, so here it covers SkipArrowAnimation.
static void Land(PlayState* play, int32_t target, u16 item, u16 slot) {
    if (MagicArrowIndex(item) >= 0) {
        item = (u16)(ITEM_BOW_ARROW_FIRE + MagicArrowIndex(item)); // the loaded bows ARE consecutive
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
    sLandings++;
}

bool RsVanilla_BeginEquip(uint16_t press, uint16_t item, uint16_t slot, int16_t slotX, int16_t slotY, int16_t bowX,
                          int16_t bowY) {
    const int32_t target = TargetOf(press);
    if (target < 0 || sFlight.active) {
        return false;
    }
    // KaleidoScope_SetupItemEquip (:848-880), with the slot's top-left made screen-centred and x10. The
    // slot is in the page's game space and drawn under the scroll's matrix, which puts it RsMenu_ScreenDy
    // lower on screen (#130's drop); the flight is drawn by the interface, so it starts from there.
    const float dy = RsMenu_ScreenDy();
    slotY = (int16_t)std::lround((float)slotY + dy);
    bowY = (int16_t)std::lround((float)bowY + dy);
    sFlight = EquipFlight();
    sFlight.active = true;
    sFlight.target = target;
    sFlight.item = item;
    sFlight.slot = slot;
    sFlight.animX = (s16)((slotX - 160) * 10);
    sFlight.animY = (s16)((120 - slotY) * 10);
    sFlight.bowX = (s16)((bowX - 160) * 10);
    sFlight.bowY = (s16)((120 - bowY) * 10);
    sFlight.alpha = 255;
    sFlight.animTimer = 0;
    sFlight.state = 3;
    sFlight.moveTimer = 10;
    if (MagicArrowIndex(item) >= 0) {
        if (CVarGetInteger(CVAR_ENHANCEMENT("SkipArrowAnimation"), 0)) {
            RsMenu_PlaySfxId(NA_SE_SY_DECIDE);
        } else {
            const u16 index = (u16)MagicArrowIndex(item);
            RsMenu_PlaySfxId(NA_SE_SY_SET_FIRE_ARROW + index);
            sFlight.item = (u16)(kArrowEffectBase + index);
            sFlight.state = 0;
            sFlight.alpha = 0;
            sFlight.moveTimer = 6;
        }
    } else {
        RsMenu_PlaySfxId(NA_SE_SY_DECIDE);
    }
    sFlights++;
    // #133: kept for the loop, which replays this exact opening state rather than equipping again -
    // the size registers included, because BeginEquip deliberately does not reset them.
    sFlightStart = sFlight;
    sFlightStartW87 = WREG(87);
    sFlightStartW90 = WREG(90);
    sHaveFlightStart = true;
    return true;
}

// Moves `from` a step of `offset` towards `to`, the way kaleido does - `>=` goes down, so it never
// overshoots from above and lands exactly when the move timer runs out.
static s16 Step(s16 from, s16 to, int32_t offset) {
    return (s16)(from >= to ? from - offset : from + offset);
}

void RsVanilla_UpdateEquipFlight(PlayState* play) {
    if (!sFlight.active || play == nullptr) {
        return;
    }
    EquipFlight& f = sFlight;
    if (sHoldAt >= 0 && f.ticks >= sHoldAt) {
        return;
    }
    // #133: the tick after the landing. The pose was drawn on the tick that landed; now it goes away.
    // Before `f.ticks++`, so the linger is not a tick of the flight and `flight hold n` still parks at
    // the pose it always did.
    if (f.linger > 0) {
        if (--f.linger == 0) {
            f.active = false;
            f.moveTimer = 10;
            ResetFlightSize();
        }
        return;
    }
    f.ticks++;
    s16 posX[7];
    s16 posY[7];
    ButtonPositions(posX, posY);

    if (f.state == 0) {
        f.alpha += 14;
        if (f.alpha > 255) {
            f.alpha = 254;
            f.state++;
        }
        f.animTimer = 5;
        return;
    }
    if (f.state == 2) {
        f.flashTimer--;
        if (f.flashTimer == 0) {
            f.item -= kArrowEffectBase - ITEM_BOW_ARROW_FIRE;
            if (!CVarGetInteger(CVAR_ENHANCEMENT("SeparateArrows"), 0)) {
                f.slot = SLOT_BOW;
            }
            f.moveTimer = 6;
            ResetFlightSize();
            f.state++;
            RsMenu_PlaySfxId(NA_SE_SY_SYNTH_MAGIC_ARROW);
        }
        return;
    }

    const s16 toX = f.state == 1 ? f.bowX : (s16)(posX[f.target] * 10);
    const s16 toY = f.state == 1 ? f.bowY : (s16)(posY[f.target] * 10);
    // u16 in kaleido (:891-892); the distances never go negative, so an int holds the same value.
    const int32_t offsetX = std::abs(f.animX - toX) / f.moveTimer;
    const int32_t offsetY = std::abs(f.animY - toY) / f.moveTimer;

    if (f.item >= kArrowEffectBase && f.alpha < 254) {
        f.alpha += 14;
        if (f.alpha > 255) {
            f.alpha = 254;
        }
        f.animTimer = 5;
        return;
    }

    if (f.animTimer == 0) {
        WREG(90) -= WREG(87) / f.moveTimer;
        WREG(87) -= WREG(87) / f.moveTimer;
        f.animX = Step(f.animX, toX, offsetX);
        f.animY = Step(f.animY, toY, offsetY);
        f.moveTimer--;
        if (f.moveTimer == 0) {
            if (f.state == 1) {
                f.state++;
                f.flashTimer = 4;
                return;
            }
            // #133, TEST-ONLY: the loop replays the flight and NEVER LANDS IT. Land() is what writes
            // the save, so looping past it would re-equip - and swap, and re-equip - for as long as
            // the burst ran. The animation is identical either way; only the ending differs.
            if (sFlightLoop && sHaveFlightStart) {
                RestartFlight();
                return;
            }
            Land(play, f.target, f.item, f.slot);
            // #133: ONE MORE TICK OF DRAWING, and this is the one place the flight leaves vanilla.
            //
            // The step above put the icon exactly ON the button - the offset is the whole remaining
            // distance once moveTimer is 1. Vanilla computes that final pose and never draws it:
            // KaleidoScope_UpdateItemEquip writes the save, loads the button's icon and sets
            // `unk_1E4 = 0` in this same branch (z_kaleido_item.c:1245-1253), and `unk_1E4 == 3` is
            // exactly what the draw gate tests (z_parameter.c:5760). So vanilla's last DRAWN pose is
            // one full step short - a tenth of the journey, about fourteen game units - and the icon
            // vanishes there while the button's own icon appears.
            //
            // At 20 Hz that reads as the animation's cadence, because all ten steps are equal jumps.
            // Once option A made the other nine glide, the last one was the only discontinuity left
            // and read as a snap (Spencer, in game). Drawing the pose that was already computed costs
            // one tick and closes it: the interpolator carries the icon all the way in, and it
            // disappears at the moment it is indistinguishable from the button icon underneath.
            //
            // The save is written HERE, on vanilla's own tick - the linger changes what is drawn, not
            // when the equip happens. `active` stays true through it, so the menu's input lock holds
            // one tick longer, which is what vanilla does for the whole flight anyway. The size
            // registers are NOT reset yet: resetting them here would pop the icon back to full size
            // for the very frame this exists to show.
            f.linger = 1;
            return;
        }
    } else {
        f.animTimer--;
        if (f.animTimer == 0) {
            f.alpha = 255;
        }
    }
}

void RsVanilla_SetEquipFlightHold(int32_t tick) {
    sHoldAt = tick < 0 ? -1 : tick;
}

bool RsVanilla_StartEquipFlightLoop() {
    if (!sHaveFlightStart) {
        return false; // nothing to replay: the run equips once first, then loops that
    }
    // A flight already in the air is ABANDONED, not landed - the same rule RsMenu_SetPage applies to a
    // sweep, and for the same reason: waiting for it would let a run assert a pose the loop never
    // produced. Nothing is equipped by it either, because only Land() writes the save.
    sFlightLoop = true;
    // THE HOLD IS LEFT ALONE, and the two together are the instrument that enumerates the lattice.
    // A loop restarts the flight at tick 0; a hold parks it once it has taken n ticks. Both on, the
    // flight sits at tick n for as long as you like, and `flight hold n+1` walks it forward exactly
    // one tick - so `flight_quad=` can be read at every pose the 20 Hz animation can draw, one
    // console command per pose, without a fresh equip each time. `flight release` then lets the loop
    // run free for the capture burst. `flight_hold=` is on the same line, so a run can always see
    // which of the two it is in.
    RestartFlight();
    return true;
}

void RsVanilla_StopEquipFlightLoop() {
    sFlightLoop = false;
    // AND THE HOLD, or the sentence below is false: a held flight returns from the update before it
    // ever reaches moveTimer == 0, so nothing would land and the run would equip zero times. `stop`
    // ends the instrument, both halves of it.
    sHoldAt = -1;
    // Left in flight rather than landed: the next tick runs the ordinary ending, which DOES land it,
    // so stopping a loop equips exactly once - the equip the run asked for in the first place.
}

void RsVanilla_SetEquipFlightProbe(bool on) {
    sFlightProbe = on;
}

bool RsVanilla_EquipFlightActive() {
    return sFlight.active;
}

void RsVanilla_FinishEquipFlight(PlayState* play) {
    // A close mid-flight (a scene change, the CVar switched off) lands the equip at once rather than
    // dropping it: the press was accepted and the sound played, so the save gets the equip.
    // #133: `linger` means it has ALREADY landed and is only still being drawn - landing it again
    // would equip twice, which a scene load during that one tick would otherwise do.
    if (sFlight.active && sFlight.linger == 0 && play != nullptr) {
        // Mid-effect the item is still kaleido's 0xBF + index; the arrow ids themselves are not
        // consecutive (0x04, 0x0C, 0x12), so they come back through a table.
        const u16 item =
            sFlight.item >= kArrowEffectBase ? kMagicArrows[sFlight.item - kArrowEffectBase] : sFlight.item;
        Land(play, sFlight.target, item, sFlight.slot);
        ResetFlightSize();
    }
    sFlight.active = false;
    sFlight.linger = 0;
    // #133: AND THE TEST-ONLY STATE DIES WITH THE MENU. This runs from the close and from every scene
    // load. A loop left on would otherwise outlive the session that set it, and then every later REAL
    // equip would fly forever and never write the save - BeginEquip refuses while a flight is active -
    // which is a wedged game, not a wedged test. The same for the hold, which had the hole first, and
    // for the probe, which would leave a magenta square on a player's screen.
    sFlightLoop = false;
    sHoldAt = -1;
    sFlightProbe = false;
}

RsMenuEquipFlightState RsVanilla_EquipFlightState() {
    RsMenuEquipFlightState s = {};
    s.active = sFlight.active;
    s.state = sFlight.state;
    s.item = sFlight.item;
    s.target = sFlight.target;
    s.x = sFlight.animX / 10;
    s.y = sFlight.animY / 10;
    s.alpha = sFlight.alpha;
    s.size = WREG(90) / 10;
    s.moveTimer = sFlight.moveTimer;
    s.flights = sFlights;
    s.landings = sLandings;
    s.ticks = sFlight.ticks;
    s.holdAt = sHoldAt;
    const FlightQuad q = FlightQuadOf(sFlight);
    s.qLeft = q.left;
    s.qTop = q.top;
    s.qSide = q.side;
    s.loop = sFlightLoop;
    s.probe = sFlightProbe;
    return s;
}

// Interface_Draw's "Inventory Equip Effects" (z_parameter.c:5760-5807), called from inside
// Interface_Draw by OnInterfaceDrawItemButtonsEnd. OPEN_DISPS from a file-static function, never from
// an anonymous namespace (PauseLink.cpp has why).
// #133 OPTION A: THE QUAD IS A UNIT SQUARE UNDER A MATRIX, not four screen-space vertices.
//
// Vanilla writes the icon's corners straight into Vtx from the tick state, and so did this up to
// #133. Vertices written by hand replay identically on every rendered frame, so the flight STEPPED at
// the 20 Hz game tick while the scroll and the hands beside it glided - they are drawn through
// matrices inside interpolation nodes and SoH tweens those to the display rate
// (SOH_2D_DRAWING.md; the same argument stage 5 rests on). One unit quad plus
// Matrix_Translate + Matrix_Scale from the same tick state puts the flight on that channel too, and
// costs nothing else: the path, the timing and the WREG values are still vanilla's.
//
// THE NODE'S KEY is the flight number and one bit. A key that no longer matches last tick's tree makes
// the node render at its exact tick position instead of lerping into it
// (frame_interpolation.cpp:300-307), which is wanted at exactly two discontinuities and nowhere else:
// a NEW flight must not slide in from where the last one ended, and the magic arrow's state 2 -> 3
// resets the icon's size through ResetFlightSize, which is a jump rather than a motion.
//
// So the bit is `state >= 3`, NOT the state itself. Keying on the state would also snap at 0 -> 1 and
// 1 -> 2, and neither is a discontinuity: the fade-in and the flash both hold the icon still, and
// state 1 flies to the Bow from exactly where the fade-in left it. Snapping there costs a tick of
// interpolation for nothing.
static int32_t FlightNodeKey(const EquipFlight& f) {
    return sFlights * 2 + (f.state >= 3 ? 1 : 0);
}
static const char sFlightNodeKey = 0;
static const void* const sFlightNode = &sFlightNodeKey;

static void DrawEquipFlight(PlayState* play) {
    const EquipFlight& f = sFlight;
    const FlightQuad q = FlightQuadOf(sFlight);
    Vtx* vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Vtx));
    // The unit square, y DOWN from its top-left corner - the shape vanilla's x0/x1, y0/y1 described.
    // Constant every frame, which is the point: everything that moves is now in the matrix.
    const s16 xs[4] = { 0, 1, 0, 1 };
    const s16 ys[4] = { 0, 0, -1, -1 };
    const s16 ss[4] = { 0, 32 << 5, 0, 32 << 5 };
    const s16 ts[4] = { 0, 0, 32 << 5, 32 << 5 };
    for (int32_t i = 0; i < 4; i++) {
        vtx[i].v.ob[0] = xs[i];
        vtx[i].v.ob[1] = ys[i];
        vtx[i].v.ob[2] = 0;
        vtx[i].v.flag = 0;
        vtx[i].v.tc[0] = ss[i];
        vtx[i].v.tc[1] = ts[i];
        vtx[i].v.cn[0] = vtx[i].v.cn[1] = vtx[i].v.cn[2] = vtx[i].v.cn[3] = 255;
    }

    OPEN_DISPS(play->state.gfxCtx);
    FrameInterpolation_RecordOpenChild(sFlightNode, FlightNodeKey(f));
    Gfx_SetupDL_42Overlay(play->state.gfxCtx);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    Matrix_Push();
    Matrix_Translate(q.left, q.top, 0.0f, MTXMODE_NEW);
    Matrix_Scale(q.side, q.side, 1.0f, MTXMODE_APPLY);
    gSPMatrix(OVERLAY_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    Matrix_Pop();
    if (sFlightProbe) {
        // #133, TEST-ONLY: a solid magenta rectangle instead of the icon - no texture, and opaque
        // whatever the fade is doing. A pixel scan then recovers the quad's corner AND its side from
        // one frame, which are exactly the two channels the smoothness claim is about; an item icon's
        // transparent edges and per-item colours give neither reliably. The combiner is prim-only, so
        // this is the same quad under the same matrix with the texture stage taken out.
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 0, 255, 255);
        gSPVertex(OVERLAY_DISP++, (uintptr_t)vtx, 4, 0);
    } else if (f.item < kArrowEffectBase) {
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, f.alpha);
        gSPVertex(OVERLAY_DISP++, (uintptr_t)vtx, 4, 0);
        gDPLoadTextureBlock(OVERLAY_DISP++, gItemIcons[f.item], G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
    } else {
        const int32_t index = f.item - kArrowEffectBase;
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, kArrowEffectR[index], kArrowEffectG[index], kArrowEffectB[index],
                        f.alpha);
        gSPVertex(OVERLAY_DISP++, (uintptr_t)vtx, 4, 0);
        gDPLoadTextureBlock(OVERLAY_DISP++, gMagicArrowEquipEffectTex, G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
    }
    gSP1Quadrangle(OVERLAY_DISP++, 0, 2, 3, 1, 0);
    // The modelview back to identity, which is what vanilla left loaded (it never replaced gMtxClear).
    // Nothing later in Interface_Draw submits vertices today, so this is housekeeping rather than a fix
    // - but a scale left on the stack is exactly the kind of inherited state that costs a session to
    // find. A constant op in a constant position, so the node's index-matched interpolation is unmoved.
    gSPMatrix(OVERLAY_DISP++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    FrameInterpolation_RecordCloseChild();
    CLOSE_DISPS(play->state.gfxCtx);
}

void RsVanilla_DrawEquipFlight(void* playArg) {
    PlayState* play = (PlayState*)playArg;
    if (play == nullptr || !sFlight.active) {
        return;
    }
    DrawEquipFlight(play);
}
