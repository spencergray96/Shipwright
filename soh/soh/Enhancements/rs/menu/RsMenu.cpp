/*
 * RsMenu.cpp - the mod-owned pause interface (sturdy-bassoon#111), stages 1-3.
 *
 * What it is at this stage: a full-screen greybox panel that opens on N64 L, hard-freezes the
 * world, hides the HUD, draws one of N registered pages, cycles on L/R, and closes on B or START -
 * fully drivable and assertable from the console (MenuConsole.h) with nobody at the keyboard.
 *
 * The three mechanisms it rests on, each of which has a trap attached:
 *
 *   - FREEZE: `play->haltAllActors = 1`, which skips ONLY Actor_UpdateAll (z_play.c:1207-1209).
 *     Player has no separate update path, so Link freezes with everything else, and `pauseCtx`
 *     stays 0 - which is why the agent harness keeps running through the pause where a real
 *     `press START` wedges it (research D.3). IREG(72) would also work and freezes far more:
 *     collision, effects, gameplayFrames, the play timers. Two consequences handled below: the
 *     camera keeps updating (Camera_Update is gated on pauseCtx, not on this), and a scene load
 *     with the flag set never emits the harness's `ready` marker, so the menu closes itself on a
 *     pending transition AND on OnSceneInit.
 *   - DRAW: OnPlayDrawEnd (z_play.c:1644), appending to OVERLAY_DISP. The pool matters and was
 *     picked the hard way: POLY_OPA put the panel UNDER every translucent thing the world had
 *     already drawn, because the four pools are CHAINED WORK -> POLY_OPA -> POLY_XLU -> OVERLAY
 *     (graph.c:310-312) rather than submitted separately - so a Door_Warp1 sparkle rendered
 *     straight through the greybox. OVERLAY is last and draws over everything. Its budget is the
 *     smallest, though: 2048 Gfx words (z64.h:107-112) against POLY_OPA's 12224. A panel and two
 *     short lines are nowhere near it, but a text-heavy page would be - a glyph costs 12 words
 *     here - so anything approaching a journal builds into a heap std::vector<Gfx> and submits one
 *     gSPDisplayList, the way kaleido.cpp:352-356 and nametag.cpp:186 do.
 *     The hook does not fire at all while vanilla pause is up, which is fine - the two never
 *     coexist.
 *   - TRIGGER: BTN_L, read as an edge off `play->state.input[0].press.button`. CONTROLLER1() is a
 *     Majora's Mask macro and does not exist here. `press`/`rel` are truncated to u16 in
 *     padmgr.c:291, so a mask above 0xFFFF never produces an edge; BTN_L (0x20) is well under it.
 *
 * The open-ended page ring is the load-bearing structural rule - see RsMenu.h.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-19
 */

#include "RsMenu.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>

#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/OTRGlobals.h"
#include "soh/ShipInit.hpp"
#include "soh/ShipUtils.h"
#include "soh/frame_interpolation.h"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

#define CVAR_RS_MENU_ON CVAR_ENHANCEMENT("RsMenu")
#define CVAR_RS_MENU_PRIMARY CVAR_ENHANCEMENT("RsMenuPrimary")

// File-static rather than an anonymous namespace, and that is not a style choice. OPEN_DISPS
// expands to a BLOCK-SCOPE redeclaration of FrameInterpolation_RecordOpenChild (macros.h:206-222);
// inside an anonymous namespace MSVC gives that redeclaration C++ linkage instead of picking up
// frame_interpolation.h's extern "C" one, and the call comes out mangled and unresolvable at link
// time. Every other C++ file in the tree that draws - nametag.cpp, kaleido.cpp - keeps its drawing
// code out of an anonymous namespace for the same reason, whether or not it says so.

// --- layout ------------------------------------------------------------------------------------
//
// The parchment extent from the settled option-A layout, measured off
// docs/notes/2026-09-17-pause-menu-diagrams/scroll-layout-options.svg in OoT's 320x240 authoring
// space: 288 px wide, hands low at the bottom corners (not drawn until stage 5). Inclusive pixel
// coordinates, the way gDPFillRectangle takes them.
//
// It is inset rather than literally edge-to-edge on purpose: #111's own stage-3 open question asks
// what the live world looks like BEHIND the menu, which an opaque full-screen quad would make
// unanswerable, and the margin is where stages 4+ put the roll ends anyway.
constexpr int16_t kPanelX0 = 16;
constexpr int16_t kPanelY0 = 48;
constexpr int16_t kPanelX1 = 303;
constexpr int16_t kPanelY1 = 191;
constexpr int16_t kPanelBorder = 3;
constexpr int16_t kPanelCentreX = (kPanelX0 + kPanelX1 + 1) / 2;

constexpr float kTitleScale = 1.5f;
constexpr float kSubScale = 0.8f;
constexpr int16_t kTitleY = 96;
constexpr int16_t kSubY = 130;

// How many greybox pages stage 3 registers. This is a REGISTRATION-SITE count and nothing else -
// no table is sized to it, the ring wraps modulo RsMenu_PageCount(), and deleting this constant
// would cost exactly one loop bound. See RsMenu.h's invariant.
constexpr int32_t kGreyboxPageCount = 4;

// --- state -------------------------------------------------------------------------------------

static std::vector<RsMenuPage>& Pages() {
    // Function-local so page registration from another translation unit's ShipInit function cannot
    // race this one's static initialisation.
    static std::vector<RsMenuPage> pages;
    return pages;
}

static bool sOpen = false;
static int32_t sPage = 0;

static u8 sHaltPrev = 0;
static bool sHudApplied = false;
static u16 sHudPrev = 0;

// Set by the OnGameStateMainStart filter, consumed by the update. The filter has to run there -
// before Play_Update reaches KaleidoSetup_Update - but the decision belongs with the rest of the
// update logic.
static bool sStartEdge = false;

static bool sBootLineWritten = false;
static bool sGreyboxRegistered = false;

static int32_t sOpens = 0;
static int32_t sCloses = 0;
static int32_t sPageChanges = 0;
static int32_t sOpenFrames = 0;
static int32_t sDrawFrames = 0;
static int32_t sInputFrames = 0;
static int32_t sStickFrames = 0;
static int32_t sButtonFrames = 0;
static int32_t sLastStickX = 0;
static int32_t sLastStickY = 0;
static u16 sLastButtons = 0;

// The START filter's own witness. Without these there is no way to tell "the filter swallowed it"
// from "no START ever arrived" - the two produce an identical outcome when the menu simply stays
// as it was. Same discipline as stick_frames above: record what the mechanism was OFFERED.
static int32_t sFilterArmedFrames = 0;
static int32_t sStartSwallowed = 0;
static int32_t sStartConsumed = 0;
// Every press bit the filter has EVER seen, OR-ed together. This is the discriminator for "did the
// filter run before or after whoever set the bit": if a button the harness injected never appears
// here, this hook ran first and cannot swallow anything the harness sends.
static uint32_t sFilterPressSeen = 0;

static bool InNormalPlay() {
    return gPlayState != nullptr && gSaveContext.gameMode == GAMEMODE_NORMAL;
}

// "Vanilla pause is already up" - the ONE shared predicate behind both coexistence guards, which is
// what RsMenu.h means by "two guards over one predicate". `debugState` belongs in it because the
// L + C-Up debug pause (z_kaleido_setup.c:22) is the same state machine on a different field, and a
// scroll drawn over that would be just as wrong.
static bool VanillaPauseIsUp(const PlayState* play) {
    return play->pauseCtx.state != 0 || play->pauseCtx.debugState != 0;
}

// --- text ----------------------------------------------------------------------------------------
//
// A glyph is a 16x16 I4 texture from the game's own font, bound and drawn as one screen-space
// texture rectangle: gDPLoadTextureBlock_4b + gSPTextureRectangle, the same two calls
// Interface_DrawTextCharacter makes. It is NOT Interface_DrawTextLine, which multiplies
// R_TEXT_CHAR_SCALE (XREG(57)) - a register only the message system ever writes, so a menu opened
// before any textbox in the session would scale its text by zero and draw nothing.
//
// Texture rectangles do not frame-interpolate. That is fine for static text and is exactly why
// stage 5's moving geometry has to be Vtx under a Matrix_* op instead (SOH_2D_DRAWING.md).

static float TextWidth(const char* text, float scale) {
    float width = 0.0f;
    for (const char* c = text; *c != '\0'; c++) {
        width += Ship_GetCharFontWidth((u8)*c) * scale;
    }
    return width;
}

static void DrawMenuText(PlayState* play, const char* text, int16_t x, int16_t y, float scale, u8 r, u8 g, u8 b, u8 a) {
    const int32_t texSize = (int32_t)(FONT_CHAR_TEX_WIDTH * scale);
    const int32_t texScale = (int32_t)(1024.0f / scale);
    float cursor = (float)x;

    OPEN_DISPS(play->state.gfxCtx);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, r, g, b, a);
    for (const char* c = text; *c != '\0'; c++) {
        const u8 ch = (u8)*c;
        // Space advances but is not drawn, the same special case every other renderer in the tree
        // makes (z_message_PAL.c:1330, z_parameter.c:6986).
        if (ch != ' ') {
            const int16_t gx = (int16_t)cursor;
            gDPPipeSync(OVERLAY_DISP++);
            gDPLoadTextureBlock_4b(OVERLAY_DISP++, Ship_GetCharFontTexture(ch), G_IM_FMT_I, FONT_CHAR_TEX_WIDTH,
                                   FONT_CHAR_TEX_HEIGHT, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                                   G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, r, g, b, a);
            gSPTextureRectangle(OVERLAY_DISP++, gx << 2, y << 2, (gx + texSize) << 2, (y + texSize) << 2,
                                G_TX_RENDERTILE, 0, 0, texScale, texScale);
        }
        cursor += Ship_GetCharFontWidth(ch) * scale;
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

static void DrawMenuTextCentred(PlayState* play, const char* text, int16_t centreX, int16_t y, float scale, u8 r, u8 g,
                         u8 b, u8 a) {
    DrawMenuText(play, text, (int16_t)(centreX - TextWidth(text, scale) / 2.0f), y, scale, r, g, b, a);
}

static void FillPanelRect(PlayState* play, int16_t x0, int16_t y0, int16_t x1, int16_t y1, u8 r, u8 g, u8 b) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCycleType(OVERLAY_DISP++, G_CYC_FILL);
    gDPSetRenderMode(OVERLAY_DISP++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(OVERLAY_DISP++, (GPACK_RGBA5551(r, g, b, 1) << 16) | GPACK_RGBA5551(r, g, b, 1));
    gDPFillRectangle(OVERLAY_DISP++, x0, y0, x1, y1);
    gDPPipeSync(OVERLAY_DISP++);
    CLOSE_DISPS(play->state.gfxCtx);
}

// The body every page gets until it supplies one of its own: the page title, large and centred, and
// its position in the ring underneath. The title is what a screenshot is asserted against and what
// `menu dump` prints, so the two agree by construction.
static void DrawGreyboxBody(PlayState* play, int32_t pageIndex, const RsMenuPage& page) {
    DrawMenuTextCentred(play, page.title.c_str(), kPanelCentreX, kTitleY, kTitleScale, 255, 255, 255, 255);

    char ring[64];
    std::snprintf(ring, sizeof(ring), "%d of %d", pageIndex + 1, RsMenu_PageCount());
    DrawMenuTextCentred(play, ring, kPanelCentreX, kSubY, kSubScale, 190, 190, 200, 255);
}

// --- the trigger's binding -----------------------------------------------------------------------

static void WriteBootLineOnce() {
    if (sBootLineWritten) {
        return;
    }
    const RsMenuTriggerInfo trigger = RsMenu_TriggerInfo();
    if (trigger.bindings < 0) {
        return; // control deck not up yet; try again next frame
    }
    sBootLineWritten = true;

    char line[256];
    if (trigger.bound) {
        std::snprintf(line, sizeof(line), "rs_menu boot button=N64_L mask=0x%04X bindings=%d bound=1", trigger.mask,
                      trigger.bindings);
    } else {
        // The whole reason this line exists. On a GameCube pad nothing produces N64 L by default -
        // SoH binds it to SDL leftshoulder, which no GC adapter mapping exposes, and the GC L
        // trigger is already N64 Z (research D.2). Without this line an unbound trigger and a
        // broken menu look identical from the log.
        std::snprintf(line, sizeof(line),
                      "rs_menu boot button=N64_L mask=0x%04X bindings=0 bound=0 "
                      "hint=rebind_in_settings_configure_controller", trigger.mask);
    }
    AgentTest_WriteMarker(line);
}

// --- hooks ---------------------------------------------------------------------------------------

// Fires at game.c:268, immediately before gameState->main -> Play_Update -> KaleidoSetup_Update
// (z_play.c:1143). That ordering is the whole point: KaleidoSetup_Update reads a bare
// CHECK_BTN_ALL(input->press.button, BTN_START) at z_kaleido_setup.c:26 and has no VB_ hook, so an
// input filter here is the only non-invasive way to stop vanilla pause opening.
//
// Gated on `pauseCtx.state == 0`, without which this also breaks the save prompt's START
// (z_kaleido_scope_PAL.c:4437) and the continue prompt's (:4707).
//
// ⚠️ KNOWN LIMITATION, observed rather than reasoned (2026-09-19): this filter does NOT beat the
// agent harness's injected START. AgentTest ORs its buttons into press.button from its own
// OnGameStateMainStart hook (AgentTest.cpp:948), and GameInteractor runs hooks by iterating an
// `unordered_map<HOOK_ID, fn>` (GameInteractor.h:226, executed :279) - so the order is HASH-BUCKET
// order, not registration order, and nothing a mod does decides it. On this build AgentTest's hook
// runs second and puts START back, so with `menu primary custom` an `agenttest press START` opens
// vanilla pause and wedges the command channel exactly as a bare `press START` always has.
//
// It is not a regression and it does not affect a player: a REAL pad press is already in the input
// struct before any hook runs, so it is always filtered. The only loser is a harness-injected
// START, which had no working behaviour to lose. The order-independent fix, if a later stage needs
// one, is a VB_ hook at the bare CHECK_BTN_ALL in z_kaleido_setup.c:26 - one line of engine, and
// deliberately not taken here because the input filter is what #111 settled on.
static void RsMenu_OnGameStateMainStart() {
    if (!InNormalPlay()) {
        return;
    }
    PlayState* play = gPlayState;
    if (VanillaPauseIsUp(play)) {
        return;
    }
    const bool swallow = sOpen || (RsMenu_IsEnabled() && RsMenu_GetPrimary() == RS_MENU_PRIMARY_CUSTOM);
    if (!swallow) {
        return;
    }
    sFilterArmedFrames++;
    Input* input = &play->state.input[0];
    sFilterPressSeen |= (uint32_t)input->press.button;
    if (CHECK_BTN_ALL(input->press.button, BTN_START)) {
        input->press.button &= ~BTN_START;
        input->cur.button &= ~BTN_START;
        sStartEdge = true;
        sStartSwallowed++;
    }
}

// What the menu was OFFERED, recorded whether or not anything came of it. A frozen world plus a
// still screenshot proves nothing if the input never arrived; `stick_frames` on `menu dump` is what
// makes "Link did not move" a challenged negative rather than an untested one.
static void RecordOfferedInput(const Input* input) {
    const bool stick = input->cur.stick_x != 0 || input->cur.stick_y != 0;
    const bool buttons = input->cur.button != 0;
    if (stick) {
        sStickFrames++;
        sLastStickX = input->cur.stick_x;
        sLastStickY = input->cur.stick_y;
    }
    if (buttons) {
        sButtonFrames++;
        sLastButtons = input->cur.button;
    }
    if (stick || buttons) {
        sInputFrames++;
    }
}

static void RsMenu_OnGameFrameUpdate() {
    WriteBootLineOnce();

    if (!InNormalPlay()) {
        if (sOpen) {
            // No PlayState to restore anything on. Drop the state rather than reaching through a
            // null pointer; the HUD belongs to the save context and the next scene re-establishes it.
            sOpen = false;
            sCloses++;
            sHudApplied = false;
        }
        sStartEdge = false;
        return;
    }

    PlayState* play = gPlayState;
    const bool startEdge = sStartEdge;
    sStartEdge = false;

    if (!RsMenu_IsEnabled()) {
        if (sOpen) {
            RsMenu_Close();
        }
        return;
    }

    Input* input = &play->state.input[0];

    if (sOpen) {
        // A pending transition would carry the freeze into Play_Init, where it stops
        // GameInteractor_ExecuteOnPlayerUpdate firing and so stops the agent harness ever emitting
        // `ready` again - which kills perf markers, event draining AND command consumption
        // outright. Close before the load rather than at it. (OnSceneInit closes too, as a second
        // net for a transition this never sees.)
        if (play->transitionTrigger != TRANS_TRIGGER_OFF) {
            RsMenu_Close();
            return;
        }
        // Never fight kaleido's state machine: if vanilla pause is somehow up, stand down.
        if (VanillaPauseIsUp(play)) {
            RsMenu_Close();
            return;
        }

        // Re-asserted every frame rather than set once: the flag is cleared per scene
        // (z_play.c:538) and vanilla writes it from cutscenes, Sun's Song and the void-out.
        play->haltAllActors = 1;
        sOpenFrames++;
        RecordOfferedInput(input);

        if (startEdge || CHECK_BTN_ALL(input->press.button, BTN_B)) {
            if (startEdge) {
                sStartConsumed++;
            }
            RsMenu_Close();
        } else if (CHECK_BTN_ALL(input->press.button, BTN_L)) {
            RsMenu_CyclePage(-1);
        } else if (CHECK_BTN_ALL(input->press.button, BTN_R)) {
            RsMenu_CyclePage(1);
        }
        return;
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_L) || startEdge) {
        if (startEdge) {
            sStartConsumed++;
        }
        // A refusal here is silent on purpose - a player mashing the trigger inside vanilla pause
        // does not want a log line per frame. The console names refusals; that is what it is for.
        RsMenu_Open();
    }
}

static void RsMenu_OnPlayDrawEnd() {
    if (!sOpen) {
        return;
    }
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    const RsMenuPage* page = RsMenu_PageAt(sPage);
    if (page == nullptr) {
        return;
    }
    sDrawFrames++;

    // The panel: a light frame with a dark body, so the greybox reads as a panel rather than as a
    // rendering fault. Two fills, FILL cycle, no z - cheap, static, and nothing here animates, so
    // the interpolation rules in SOH_2D_DRAWING.md do not bind yet. They will at stage 5.
    FillPanelRect(play, kPanelX0, kPanelY0, kPanelX1, kPanelY1, 150, 140, 110);
    FillPanelRect(play, kPanelX0 + kPanelBorder, kPanelY0 + kPanelBorder, kPanelX1 - kPanelBorder,
             kPanelY1 - kPanelBorder, 46, 42, 38);

    // SETUPDL_39 is the HUD/text preset - MODULATEIA_PRIM over XLU_SURF - and it also puts the RDP
    // back into 1-cycle after the fills above.
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_39Overlay(play->state.gfxCtx);
    CLOSE_DISPS(play->state.gfxCtx);

    if (page->draw != nullptr) {
        page->draw(play, sPage, page->userData);
    } else {
        DrawGreyboxBody(play, sPage, *page);
    }
}

static void RsMenu_OnSceneInit(int16_t sceneNum) {
    (void)sceneNum;
    RsMenu_Close();
}

static void RegisterGreyboxPages() {
    if (sGreyboxRegistered) {
        return;
    }
    sGreyboxRegistered = true;
    for (int32_t i = 0; i < kGreyboxPageCount; i++) {
        char id[32];
        char title[32];
        std::snprintf(id, sizeof(id), "greybox-%d", i + 1);
        std::snprintf(title, sizeof(title), "page %d", i + 1);
        RsMenu_RegisterPage(id, title, nullptr, nullptr);
    }
}

static void RegisterRsMenu() {
    RegisterGreyboxPages();
    // COND_HOOK unregisters its previous hook before registering, so a ShipInit re-run (every
    // config and preset load) leaves exactly one of each. The hooks are registered unconditionally
    // and the CVar is read inside them: SoH has no console `set`, so a CVar the console writes
    // would never take effect if it were baked into a COND_HOOK condition at ShipInit time.
    COND_HOOK(OnGameFrameUpdate, true, RsMenu_OnGameFrameUpdate);
    COND_HOOK(OnGameStateMainStart, true, RsMenu_OnGameStateMainStart);
    COND_HOOK(OnPlayDrawEnd, true, RsMenu_OnPlayDrawEnd);
    COND_HOOK(OnSceneInit, true, RsMenu_OnSceneInit);
}

static RegisterShipInitFunc rsMenuInitFunc(RegisterRsMenu);

// --- public API ----------------------------------------------------------------------------------

int32_t RsMenu_RegisterPage(const char* id, const char* title, RsMenuPageDrawFn draw, void* userData) {
    if (id == nullptr || *id == '\0' || title == nullptr) {
        return -1;
    }
    std::vector<RsMenuPage>& pages = Pages();
    for (const RsMenuPage& page : pages) {
        if (page.id == id) {
            return -1;
        }
    }
    RsMenuPage page;
    page.id = id;
    page.title = title;
    page.draw = draw;
    page.userData = userData;
    pages.push_back(page);
    return (int32_t)pages.size() - 1;
}

int32_t RsMenu_PageCount() {
    return (int32_t)Pages().size();
}

const RsMenuPage* RsMenu_PageAt(int32_t index) {
    const std::vector<RsMenuPage>& pages = Pages();
    if (index < 0 || index >= (int32_t)pages.size()) {
        return nullptr;
    }
    return &pages[(size_t)index];
}

const char* RsMenu_OpenResultName(RsMenuOpenResult result) {
    switch (result) {
        case RS_MENU_OPEN_OK:
            return "ok";
        case RS_MENU_OPEN_ALREADY:
            return "already_open";
        case RS_MENU_OPEN_DISABLED:
            return "disabled";
        case RS_MENU_OPEN_NO_PLAY:
            return "no_play";
        case RS_MENU_OPEN_KALEIDO:
            return "kaleido_open";
        case RS_MENU_OPEN_NO_PAGES:
            return "no_pages";
    }
    return "unknown";
}

bool RsMenu_IsEnabled() {
    return CVarGetInteger(CVAR_RS_MENU_ON, 1) != 0;
}

bool RsMenu_IsOpen() {
    return sOpen;
}

int32_t RsMenu_CurrentPage() {
    return sPage;
}

RsMenuOpenResult RsMenu_Open() {
    if (!RsMenu_IsEnabled()) {
        return RS_MENU_OPEN_DISABLED;
    }
    if (sOpen) {
        return RS_MENU_OPEN_ALREADY;
    }
    if (!InNormalPlay()) {
        return RS_MENU_OPEN_NO_PLAY;
    }
    PlayState* play = gPlayState;
    // One menu at a time, and the scroll is the one that yields. Nothing here drives kaleido's
    // state machine - it just declines.
    if (VanillaPauseIsUp(play)) {
        return RS_MENU_OPEN_KALEIDO;
    }
    if (RsMenu_PageCount() == 0) {
        return RS_MENU_OPEN_NO_PAGES;
    }
    if (sPage >= RsMenu_PageCount()) {
        sPage = 0;
    }

    sHaltPrev = play->haltAllActors;
    play->haltAllActors = 1;

    sHudPrev = gSaveContext.hudVisibilityMode;
    Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_NOTHING_INSTANT);
    sHudApplied = true;

    sOpen = true;
    sOpens++;
    return RS_MENU_OPEN_OK;
}

bool RsMenu_Close() {
    const bool wasOpen = sOpen;
    sOpen = false;
    if (!wasOpen) {
        // Closing a closed menu is a genuine no-op, and it has to be: this runs from OnSceneInit on
        // EVERY scene load and from the CVar-off path, and `haltAllActors` is the ENGINE's flag -
        // vanilla sets it for cutscenes (z_demo.c:402/405), Sun's Song (z_parameter.c:6900/6905)
        // and the void-out (z_player.c:4766). Writing a stale sHaltPrev over one of those would
        // un-freeze a freeze that was never ours to hold.
        return false;
    }
    sCloses++;
    if (gPlayState != nullptr) {
        gPlayState->haltAllActors = sHaltPrev;
    }
    if (sHudApplied) {
        // Restore what was there, with ONE rewrite: HUD_VISIBILITY_NO_CHANGE is 0, so restoring it
        // verbatim does nothing at all and would leave the HUD hidden for good. The NOTHING modes
        // restore as-is on purpose - the menu opened over an already-hidden HUD (a cutscene, say),
        // and revealing it on close would be the menu breaking something it did not set.
        u16 restore = sHudPrev;
        if (restore == HUD_VISIBILITY_NO_CHANGE) {
            restore = HUD_VISIBILITY_ALL;
        }
        Interface_ChangeHudVisibilityMode(restore);
        sHudApplied = false;
    }
    return true;
}

bool RsMenu_SetPage(int32_t index) {
    if (index < 0 || index >= RsMenu_PageCount()) {
        return false;
    }
    if (index != sPage) {
        sPageChanges++;
    }
    sPage = index;
    return true;
}

void RsMenu_CyclePage(int32_t delta) {
    const int32_t count = RsMenu_PageCount();
    if (count <= 0) {
        return;
    }
    // The ring. Modulo the live count in both directions, so a page registered later costs nothing.
    int32_t next = (sPage + delta) % count;
    if (next < 0) {
        next += count;
    }
    if (next != sPage) {
        sPageChanges++;
    }
    sPage = next;
}

int32_t RsMenu_GetPrimary() {
    return CVarGetInteger(CVAR_RS_MENU_PRIMARY, RS_MENU_PRIMARY_VANILLA) == RS_MENU_PRIMARY_CUSTOM
               ? RS_MENU_PRIMARY_CUSTOM
               : RS_MENU_PRIMARY_VANILLA;
}

void RsMenu_SetPrimary(int32_t primary) {
    const int32_t value = primary == RS_MENU_PRIMARY_CUSTOM ? RS_MENU_PRIMARY_CUSTOM : RS_MENU_PRIMARY_VANILLA;
    CVarSetInteger(CVAR_RS_MENU_PRIMARY, value);
    CVarSave();
}

const char* RsMenu_PrimaryName(int32_t primary) {
    return primary == RS_MENU_PRIMARY_CUSTOM ? "custom" : "vanilla";
}

bool RsMenu_ParsePrimary(const std::string& word, int32_t* primary) {
    if (word == "custom") {
        *primary = RS_MENU_PRIMARY_CUSTOM;
        return true;
    }
    if (word == "vanilla") {
        *primary = RS_MENU_PRIMARY_VANILLA;
        return true;
    }
    return false;
}

RsMenuTriggerInfo RsMenu_TriggerInfo() {
    RsMenuTriggerInfo info;
    info.mask = BTN_L;
    info.bindings = -1;
    info.bound = false;

    auto context = Ship::Context::GetRawInstance();
    if (context == nullptr) {
        return info;
    }
    auto controlDeck = context->GetControlDeck();
    if (controlDeck == nullptr) {
        return info;
    }
    auto controller = controlDeck->GetControllerByPort(0);
    if (controller == nullptr) {
        return info;
    }
    auto button = controller->GetButton(BTN_L);
    if (button == nullptr) {
        return info;
    }
    info.bindings = (int32_t)button->GetAllButtonMappings().size();
    info.bound = info.bindings > 0;
    return info;
}

RsMenuStatus RsMenu_Status() {
    RsMenuStatus status;
    status.enabled = RsMenu_IsEnabled();
    status.open = sOpen;
    status.page = sPage;
    status.pages = RsMenu_PageCount();
    status.primary = RsMenu_GetPrimary();
    status.halt = gPlayState != nullptr && gPlayState->haltAllActors != 0;
    status.haltPrev = sHaltPrev != 0;
    status.hudHidden = sHudApplied;
    status.hudPrev = (int32_t)sHudPrev;
    status.hudNow = (int32_t)gSaveContext.hudVisibilityMode;
    status.opens = sOpens;
    status.closes = sCloses;
    status.pageChanges = sPageChanges;
    status.openFrames = sOpenFrames;
    status.drawFrames = sDrawFrames;
    status.inputFrames = sInputFrames;
    status.stickFrames = sStickFrames;
    status.buttonFrames = sButtonFrames;
    status.lastStickX = sLastStickX;
    status.lastStickY = sLastStickY;
    status.lastButtons = sLastButtons;
    status.kaleido = gPlayState != nullptr ? (int32_t)gPlayState->pauseCtx.state : -1;
    status.filterArmedFrames = sFilterArmedFrames;
    status.startSwallowed = sStartSwallowed;
    status.startConsumed = sStartConsumed;
    status.filterPressSeen = sFilterPressSeen;
    return status;
}
