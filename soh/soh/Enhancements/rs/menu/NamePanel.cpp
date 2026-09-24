/*
 * NamePanel.cpp - vanilla's info panel, ported under the scroll (sturdy-bassoon#132). A straight port of
 * KaleidoScope_UpdateNamePanel (the named item and its timer) and KaleidoScope_DrawInfoPanel (the stone, the
 * name, the button prompts, the page-arrow labels and the L/R icons), z_kaleido_scope_PAL.c:1885-2523, drawn
 * through the menu's helpers (RsMenu.h) rather than kaleido's vertex buffer. Spencer's calls (2026-09-22 on
 * #132, and 2026-09-24):
 *   - the name alternates with the prompts on vanilla's own timer, WREG(88) and WREG(89), on vanilla's stone;
 *   - under the scroll; the Quest Journal gets no stone (it has no `name` callback);
 *   - on a hand, the "To <page>" label of the page the hand turns to, and nothing toward the Journal;
 *   - the L/R icons on every page, each vertically centred on its hand's palm and clear of the hand by the
 *     stepper's gap. RsMenu.cpp works those positions out; this file draws what it is handed.
 *
 * NOT PORTED, because nothing on the scroll can reach them: the map page's names and its Gold Skulltula icon,
 * the save prompt's "A to Decide" (state 7), and the Skulltula debug toggle.
 *
 * What the panel knows about a page it learns from the page's `name` callback (RsMenu.h, RsMenuNameInfo): the
 * item, kaleido's nameColorSet, whether the timer runs and which prompt the node gets. Nothing here is keyed on
 * a page id - the ring's invariant.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-24
 */

#include "NamePanel.h"
#include "VanillaPagesInternal.h"

#include <cstring>
#include <set>
#include <string>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "overlays/misc/ovl_kaleido_scope/z_kaleido_scope.h"
#include "textures/icon_item_static/icon_item_static.h"
#include "textures/icon_item_nes_static/icon_item_nes_static.h"
#include "textures/icon_item_ger_static/icon_item_ger_static.h"
#include "textures/icon_item_fra_static/icon_item_fra_static.h"
#include "textures/icon_item_jpn_static/icon_item_jpn_static.h"
extern PlayState* gPlayState;
}

// --- geometry: kaleido's info-panel vertices, in its page-local units -----------------------------------------
// The stone is two 72 x 24 halves from x -72 (infoPanelVtx 0-7, :2016-2052); the text row is 4 below the
// stone's top (-80 against -76, :2153) and 16 tall. Kaleido x 0 is the stone's centre, so a kaleido x `kx`
// draws at stoneX + kStoneHalf + kx.
constexpr int16_t kStoneHalf = 72;
constexpr int16_t kStoneH = 24;
constexpr int16_t kTextDown = 4;
constexpr int16_t kTextH = 16;
constexpr int16_t kNameX = -63; // the name, and a hand's label: 128 wide from -63 (:2171-2176, :2270-2275)
constexpr int16_t kNameW = 128;
// The L/R icons: 24 x 32 while the cursor rests on that arrow, else 18 x 26 inset 3 - the same centre
// (infoPanelVtx 8-15, :2054-2094).
constexpr int16_t kArrowW = 24;
constexpr int16_t kArrowH = 32;
constexpr int16_t kArrowInset = 3;

// DrawInfoPanel's per-language widths (:1944-1946): "to Equip" and "to Play Melody", by gSaveContext.language.
static const int16_t kToEquipW[4] = { 56, 88, 80, 56 };
static const int16_t kPlayMelodyW[4] = { 80, 104, 112, 80 };
// The C symbols' texrect x, by language (:2311-2318): ENG and JPN 112, GER 175, FRA 98 - screen x, so kaleido x
// is 160 less. (The vertex path's WREG(49 + lang) says 176 for GER; the texrect is what vanilla draws.)
static int16_t CSymbolsKaleidoX(int32_t language) {
    if (language == LANGUAGE_GER) {
        return 175 - 160;
    }
    if (language == LANGUAGE_FRA) {
        return 98 - 160;
    }
    return 112 - 160;
}

static const void* const kToEquip[4] = { gPauseToEquipENGTex, gPauseToEquipGERTex, gPauseToEquipFRATex,
                                         gPauseToEquipJPNTex };
static const void* const kPlayMelody[4] = { gPauseToPlayMelodyENGTex, gPauseToPlayMelodyGERTex,
                                            gPauseToPlayMelodyFRATex, gPauseToPlayMelodyJPNTex };
// gSaveContext.language as an index into the four-wide tables, and the same with JPN read as ENG for the WREG
// offsets (DrawInfoPanel's languageOffset, :1972-1977: the registers only have three languages).
static int32_t PanelLanguage() {
    const int32_t l = gSaveContext.language;
    return l >= LANGUAGE_ENG && l <= LANGUAGE_JPN ? l : LANGUAGE_ENG;
}
static int32_t PanelLanguageOffset() {
    const int32_t l = PanelLanguage();
    return l == LANGUAGE_JPN ? LANGUAGE_ENG : l;
}

// Which of kaleido's sub-states (unk_1E4, RsMenuNameInfo::subState) show the name and which the prompts - the
// two sets DrawInfoPanel tests (:2165-2170, :2230). 1 is a page turn, 3 an item's equip flight, 9 a song's
// lead-in; 2 and 4-6 a song playing, 7 the equipment page's equip lockout, 8 a song's preview.
static bool SubStateShowsName(int32_t sub) {
    return sub == 0 || sub == 2 || (sub >= 4 && sub <= 7) || sub == 8;
}
static bool SubStateShowsPrompt(int32_t sub) {
    return sub < 3 || sub == 7 || sub == 8;
}

// --- the named item and its timer: KaleidoScope_UpdateNamePanel (:2437-2523) -------------------------------------

constexpr RsMenuNameInfo kNamesNothing = { -1, false, false, 0, RS_MENU_PROMPT_NONE };
static RsMenuNameInfo sInfo = kNamesNothing;
static int32_t sNamed = -1;        // namedItem; -1 is PAUSE_ITEM_NONE
static int32_t sTimer = 0;         // nameDisplayTimer
static const char* sTex = nullptr; // what nameSegment would hold: a resource path
static bool sCustom = false;
static int32_t sLookups = 0;
static int32_t sCustoms = 0;

// The hook writes a path into pauseCtx->nameSegment, which kaleido allocates only while it is up (:3977) - null
// before the first vanilla pause and a stale block after it. So the lookup lends it this buffer, the size kaleido
// allocates, for the length of the call. A path read back is interned, because the renderer keys its texture
// cache on the pointer, and one buffer holding different paths over time would need invalidating the way
// kaleido invalidates nameSegment (z_kaleido_equipment.c:872).
static char sNameBuffer[0x400 + 0xA00];
static std::set<std::string> sCustomPaths;

static void LookUpName(int32_t item) {
    sLookups++;
    sCustom = false;
    sTex = KaleidoScope_ItemNameTexture((u16)item);
    if (gPlayState == nullptr) {
        return;
    }
    PauseContext* pauseCtx = &gPlayState->pauseCtx;
    u8* const lent = pauseCtx->nameSegment;
    sNameBuffer[0] = '\0';
    pauseCtx->nameSegment = (u8*)sNameBuffer;
    const bool custom = GameInteractor_Should(VB_DRAW_CUSTOM_ITEM_NAME, false, (u16)item);
    pauseCtx->nameSegment = lent;
    sNameBuffer[sizeof(sNameBuffer) - 1] = '\0';
    // Vanilla draws whatever the hook left in nameSegment; a hook that said yes and wrote nothing would show
    // the last name, so this keeps the vanilla one instead.
    if (custom && sNameBuffer[0] != '\0') {
        sTex = sCustomPaths.insert(sNameBuffer).first->c_str();
        sCustom = true;
        sCustoms++;
    }
}

void RsNamePanel_Tick(const RsNamePanelFrame& f) {
    RsMenuNameInfo info = kNamesNothing;
    if (f.page != nullptr && f.page->name != nullptr && f.node != nullptr && f.node->hand < 0) {
        f.page->name(f.pageIndex, f.node, &info, f.page->userData);
    }
    sInfo = info;
    // Kaleido runs this in state 6 only (:3864, :4255). A hand names nothing here, as the item page's arrows do
    // (z_kaleido_item.c:724), so stepping back off a hand restarts the timer - which every kaleido page does
    // too, by zeroing it as the cursor leaves an arrow (z_kaleido_item.c:554, z_kaleido_equipment.c:364).
    if (!f.settled) {
        return;
    }
    // Nothing named and still nothing: the timer stands still. Kaleido's item and equipment pages re-enter the
    // change branch on every such tick, because their raw cursorItem is never PAUSE_ITEM_NONE (:2444-2458);
    // its quest page zeroes the timer instead. Either way nothing is named, so nothing shows the difference.
    if (info.item < 0 && sNamed < 0) {
        return;
    }
    if (info.item != sNamed) {
        sNamed = info.item;
        // Changing to "none" loads nothing and leaves the timer, as kaleido does (:2460-2505).
        if (sNamed >= 0) {
            LookUpName(sNamed);
            sTimer = 0;
        }
    } else if (!info.grey) {
        if (info.alternates) {
            if (sNamed != ITEM_SOLD_OUT) {
                sTimer++;
                if (sTimer > WREG(88)) {
                    sTimer = 0;
                }
            }
        } else {
            sTimer = 0;
        }
    } else {
        sTimer = 0;
    }
}

// --- drawing: KaleidoScope_DrawInfoPanel (:1885-2435) ----------------------------------------------------------

// The button colours DrawInfoPanel starts with (:1886-1912), SoH's cosmetics included.
static Color_RGB8 AButtonColour() {
    Color_RGB8 c = { 0, 100, 255 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("HUD.AButton.Value"), c);
    } else if (CVarGetInteger(CVAR_COSMETIC("DefaultColorScheme"), COLORSCHEME_N64) == COLORSCHEME_GAMECUBE) {
        c = { 0, 255, 100 };
    }
    return c;
}
static Color_RGB8 CButtonColour(const char* changed, const char* value) {
    Color_RGB8 c = { 255, 160, 0 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CButtons.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("HUD.CButtons.Value"), c);
    }
    if (CVarGetInteger(changed, 0)) {
        c = CVarGetColor24(value, c);
    }
    return c;
}

// The page arrow's pulse (:1981-2014): four channels easing toward one of two colours, swapping target every
// ZREG(28) draws. Stepped once per draw, as kaleido steps it.
static const s16 kPulse[2][4] = { { 180, 210, 255, 220 }, { 100, 100, 150, 220 } };
static s16 sPulse[4] = { 0, 0, 0, 0 };
static s16 sPulseTimer = 20;
static s16 sPulseTarget = 0;

static void StepPulse() {
    s16 step[4];
    for (int32_t i = 0; i < 4; i++) {
        step[i] = ABS(sPulse[i] - kPulse[sPulseTarget][i]) / sPulseTimer;
    }
    for (int32_t i = 0; i < 4; i++) {
        if (sPulse[i] >= kPulse[sPulseTarget][i]) {
            sPulse[i] -= step[i];
        } else {
            sPulse[i] += step[i];
        }
    }
    sPulseTimer--;
    if (sPulseTimer == 0) {
        for (int32_t i = 0; i < 4; i++) {
            sPulse[i] = kPulse[sPulseTarget][i];
        }
        sPulseTimer = ZREG(28);
        sPulseTarget ^= 1;
    }
}

static RsNamePanelState sState;

static void Rect(float* out, float x0, float y0, float x1, float y1) {
    out[0] = x0;
    out[1] = y0;
    out[2] = x1;
    out[3] = y1;
}

void RsNamePanel_Hidden() {
    sState.shown = false;
    sState.stone = false;
    sState.lr = false;
    sState.lrBig = 0;
    sState.text = "none";
    sState.tex = "-";
}

// One L/R icon (:2054-2143). Colour, with SoH's FixMenuLR off, is vanilla's own slip: the left icon draws in the
// panel's prim colour (90, 100, 130) because the reset to (180, 210, 255) came after it; FixMenuLR puts the
// reset first. On the arrow, both pulse.
static void DrawArrow(const RsNamePanelFrame& f, int32_t side, bool onArrow) {
    int16_t x0 = side == 0 ? (int16_t)(f.lrInner[0] - kArrowW) : f.lrInner[1];
    int16_t y0 = (int16_t)(f.lrY - kArrowH / 2);
    int16_t w = kArrowW;
    int16_t h = kArrowH;
    if (!onArrow) {
        x0 += kArrowInset;
        y0 += kArrowInset;
        w -= 2 * kArrowInset;
        h -= 2 * kArrowInset;
    }
    u8 r = 180, g = 210, b = 255, a = 255;
    if (onArrow) {
        r = (u8)sPulse[0];
        g = (u8)sPulse[1];
        b = (u8)sPulse[2];
        a = (u8)sPulse[3];
    } else if (side == 0 && CVarGetInteger(CVAR_ENHANCEMENT("FixMenuLR"), 0) == 0) {
        r = 90;
        g = 100;
        b = 130;
    }
    RsMenu_DrawIcon(side == 0 ? gLButtonTex : gRButtonTex, RS_MENU_TEX_IA8, kArrowW, kArrowH, x0, y0, w, h, r, g, b,
                    a, false);
    Rect(side == 0 ? sState.lRect : sState.rRect, (float)x0, y0 + f.screenDy, (float)(x0 + w),
         y0 + h + f.screenDy);
}

// A button symbol and its words (:2240-2267, :2356-2430): the symbol at `symbolX`, 24 wide, in A's colour; the
// words at `wordsX`, `wordsW` wide, white.
static void DrawAPrompt(int16_t textY, int16_t symbolX, int16_t wordsX, const void* words, int16_t wordsW) {
    const Color_RGB8 a = AButtonColour();
    RsMenu_DrawPanelTexture(gABtnSymbolTex, RS_MENU_TEX_IA8, 24, 16, 0, 24, symbolX, textY, a.r, a.g, a.b, 255);
    RsMenu_DrawPanelTexture(words, RS_MENU_TEX_IA8, wordsW, 16, 0, wordsW, wordsX, textY, 255, 255, 255, 255);
}

void RsNamePanel_Draw(const RsNamePanelFrame& f) {
    const int32_t language = PanelLanguage();
    const int32_t lo = PanelLanguageOffset();
    const int16_t cx = (int16_t)(f.stoneX + kStoneHalf);
    const int16_t textY = (int16_t)(f.stoneY + kTextDown);
    const bool onHand = f.node != nullptr && f.node->hand >= 0;
    const int32_t sub = sInfo.subState;

    sState.shown = true;
    sState.stone = false;
    sState.text = "none";
    sState.tex = "-";
    StepPulse();

    // The stone (:2102-2115): kaleido's two halves in the cosmetics' name-panel colour.
    if (f.page != nullptr && f.page->name != nullptr) {
        const Color_RGBA8 stone =
            CVarGetColor(CVAR_COSMETIC("Kaleido.NamePanel.Value"), Color_RGBA8{ 90, 100, 130, 255 });
        RsMenu_DrawIcon(gNamePanelLeftTex, RS_MENU_TEX_IA8, kStoneHalf, kStoneH, f.stoneX, f.stoneY, kStoneHalf,
                        kStoneH, stone.r, stone.g, stone.b, stone.a, false);
        RsMenu_DrawIcon(gNamePanelRightTex, RS_MENU_TEX_IA8, kStoneHalf, kStoneH, cx, f.stoneY, kStoneHalf, kStoneH,
                        stone.r, stone.g, stone.b, stone.a, false);
        sState.stone = true;
        Rect(sState.stoneRect, (float)f.stoneX, f.stoneY + f.screenDy,
             (float)(f.stoneX + 2 * kStoneHalf), f.stoneY + kStoneH + f.screenDy);
    }

    // The arrows, full size and pulsing while the cursor is on one with nothing moving - kaleido's unk_1E4 == 0,
    // in any state (:2054, :2074). That is not while the menu opens, since kaleido opens with unk_1E4 1
    // (z_kaleido_setup.c:40), but it is while it closes, from 0.
    const bool restingOnHand = onHand && !f.opening && sub == 0;
    sState.lrBig = restingOnHand ? f.node->hand + 1 : 0;
    for (int32_t side = 0; side < 2; side++) {
        DrawArrow(f, side, restingOnHand && f.node->hand == side);
    }
    sState.lr = true;

    if (!sState.stone) {
        return;
    }

    // The name (:2165-2187): settled, something named, the timer in its first WREG(89) ticks, and a sub-state
    // that shows names.
    if (f.settled && sNamed >= 0 && sTimer < WREG(89) && SubStateShowsName(sub) && !onHand && sTex != nullptr) {
        const u8 v = sInfo.grey ? 70 : 255;
        RsMenu_DrawPanelTexture(sTex, RS_MENU_TEX_IA4, kNameW, kTextH, 0, kNameW, (int16_t)(cx + kNameX), textY, v, v,
                                v, 255);
        sState.text = "name";
        sState.tex = sTex;
        return;
    }
    // Otherwise the prompts (:2230-2432), in a sub-state that shows them.
    if (!SubStateShowsPrompt(sub)) {
        return;
    }
    if (onHand) {
        // A page arrow's label, yellow, once settled with nothing moving - the label of the page it turns to.
        if (f.settled && sub == 0 && f.handTo != nullptr && f.handTo->toLabel != nullptr) {
            const void* label = f.handTo->toLabel[language];
            RsMenu_DrawPanelTexture(label, RS_MENU_TEX_IA8, kNameW, kTextH, 0, kNameW, (int16_t)(cx + kNameX), textY,
                                    255, 200, 0, 255);
            sState.text = "to";
            sState.tex = (const char*)label;
        }
        return;
    }
    switch (sInfo.prompt) {
        case RS_MENU_PROMPT_C_EQUIP: {
            // The C symbols (:2311-2351): one texture drawn three times from its left edge, each crop narrower
            // and in its own button's colour, so C-left, C-down and C-right each keep their own third. Loaded as
            // 46 wide, as kaleido loads it (icon_w, :2320), though the texture is 48: the renderer squeezes the 48
            // into the 46, and that squeeze is what vanilla shows - drawn at 48 the symbols came out about 4%
            // wider than vanilla's (the #132 run's n1 differential).
            constexpr int16_t kSymbolsW = 46;
            const int16_t x = (int16_t)(cx + CSymbolsKaleidoX(language));
            const Color_RGB8 right = CButtonColour(CVAR_COSMETIC("HUD.CRightButton.Changed"),
                                                   CVAR_COSMETIC("HUD.CRightButton.Value"));
            const Color_RGB8 down = CButtonColour(CVAR_COSMETIC("HUD.CDownButton.Changed"),
                                                  CVAR_COSMETIC("HUD.CDownButton.Value"));
            const Color_RGB8 left = CButtonColour(CVAR_COSMETIC("HUD.CLeftButton.Changed"),
                                                  CVAR_COSMETIC("HUD.CLeftButton.Value"));
            constexpr int16_t kCrop = 17;
            RsMenu_DrawPanelTexture(gCBtnSymbolsTex, RS_MENU_TEX_IA8, kSymbolsW, 16, 0, kCrop * 3 - 3, x, textY,
                                    right.r, right.g, right.b, 255);
            RsMenu_DrawPanelTexture(gCBtnSymbolsTex, RS_MENU_TEX_IA8, kSymbolsW, 16, 0, kCrop * 2 - 3, x, textY,
                                    down.r, down.g, down.b, 255);
            RsMenu_DrawPanelTexture(gCBtnSymbolsTex, RS_MENU_TEX_IA8, kSymbolsW, 16, 0, kCrop, x, textY, left.r,
                                    left.g, left.b, 255);
            const int16_t wordsX = (int16_t)(cx + WREG(49 + lo) + WREG(58 + lo));
            RsMenu_DrawPanelTexture(kToEquip[language], RS_MENU_TEX_IA8, kToEquipW[language], 16, 0,
                                    kToEquipW[language], wordsX, textY, 255, 255, 255, 255);
            break;
        }
        case RS_MENU_PROMPT_A_EQUIP: {
            const int16_t symbolX = (int16_t)(cx + WREG(64 + lo));
            DrawAPrompt(textY, symbolX, (int16_t)(symbolX + WREG(52 + lo)), kToEquip[language], kToEquipW[language]);
            break;
        }
        case RS_MENU_PROMPT_A_PLAY_MELODY: {
            const int16_t symbolX = (int16_t)(cx + WREG(55 + lo));
            // German's words go 99 LEFT of the A (:2369-2372).
            const int16_t wordsX =
                (int16_t)(language == LANGUAGE_GER ? symbolX - 99 : symbolX + WREG(52 + lo));
            DrawAPrompt(textY, symbolX, wordsX, kPlayMelody[language], kPlayMelodyW[language]);
            break;
        }
        default:
            return;
    }
    sState.text = "prompt";
}

void RsNamePanel_Reset() {
    // KaleidoSetup_Update's own reset (z_kaleido_setup.c:119-120: nameDisplayTimer and nameColorSet 0), and
    // namedItem none as well, which kaleido does not do - so the first settled tick looks the name up afresh
    // rather than trusting a lookup from the last time the menu was up.
    sNamed = -1;
    sTimer = 0;
    sTex = nullptr;
    sCustom = false;
    sInfo = kNamesNothing;
    RsNamePanel_Hidden();
    RsNamePanel_SetTestCustom(-1);
}

RsNamePanelState RsNamePanel_State() {
    RsNamePanelState s = sState;
    if (s.text == nullptr) { // before the first frame the menu ever drew
        s.text = "none";
        s.tex = "-";
    }
    s.item = sNamed;
    s.itemName = sNamed >= 0 ? RsVanilla_ItemToken(sNamed) : "-";
    s.grey = sInfo.grey;
    s.timer = sTimer;
    s.alternates = sInfo.alternates;
    s.subState = sInfo.subState;
    s.prompt = sInfo.prompt;
    s.custom = sCustom;
    s.lookups = sLookups;
    s.customs = sCustoms;
    return s;
}

// --- the test-only custom name ----------------------------------------------------------------------------------

static int32_t sTestCustomItem = -1;
static HOOK_ID sTestCustomHook = 0;

int32_t RsNamePanel_SetTestCustom(int32_t item) {
    if (sTestCustomHook != 0) {
        GameInteractor::Instance->UnregisterGameHookForID<GameInteractor::OnVanillaBehavior>(sTestCustomHook);
        sTestCustomHook = 0;
    }
    sTestCustomItem = item;
    // Named afresh on the next settled tick, so the hook is asked about the item under the cursor now rather
    // than at the next change of item.
    sNamed = -1;
    if (item < 0) {
        return -1;
    }
    // The body is RocsFeather.cpp's, with the Ocarina of Time's name standing in for the feather's.
    sTestCustomHook = REGISTER_VB_SHOULD(VB_DRAW_CUSTOM_ITEM_NAME, {
        u32 namedItem = va_arg(args, u32);
        if ((int32_t)namedItem == sTestCustomItem && gPlayState != nullptr) {
            *should = true;
            const char* textureName = KaleidoScope_ItemNameTexture(ITEM_OCARINA_TIME);
            memcpy(gPlayState->pauseCtx.nameSegment, textureName, strlen(textureName) + 1);
        }
    });
    return item;
}
