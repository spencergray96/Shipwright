#ifndef SOH_RS_MENU_H
#define SOH_RS_MENU_H

#include <stdint.h>
#include <string>
#include <vector>

// The mod-owned pause interface (sturdy-bassoon#111) - the RS-style scroll, built BESIDE kaleido
// rather than inside it. It never touches `pauseCtx`: it updates on OnGameFrameUpdate, draws on
// OnPlayDrawEnd, freezes the world with `play->haltAllActors` and hides the HUD with
// Interface_ChangeHudVisibilityMode. Research: docs/notes/2026-09-17-pause-menu-research.md.
//
// THE ONE STRUCTURAL INVARIANT: the page set is open-ended. Pages register into the runtime list
// below, the ring wraps modulo `RsMenu_PageCount()`, and NOTHING anywhere is sized to the number of
// pages - no fixed-width parallel table, no enum a layout switches on, no `[4]`. Vanilla's
// four-wide `sKaleidoSetupKscpPos*` tables (z_kaleido_setup.c:3-9) are the exact corner this exists
// to avoid, so adding a page later must stay "call RsMenu_RegisterPage once, change nothing else".
//
// Coexistence with vanilla pause is two guards over one predicate, and no cross-driving of
// kaleido's state machine: this menu REFUSES to open while `pauseCtx.state != 0`, and while it is
// open an OnGameStateMainStart input filter swallows START before KaleidoSetup_Update can see it.
// (VB_CLOSE_PAUSE_MENU is NOT the hook for that - it governs CLOSING and would trap the player
// inside vanilla pause. Research D.1.)

struct PlayState;

// A page's body draw, called once per frame while that page is the visible one, after the menu has
// drawn the panel behind it; it opens its own OPEN_DISPS block. A page that passes nullptr gets the
// greybox body (its title, centred) - which is what every page is at stage 3.
typedef void (*RsMenuPageDrawFn)(struct PlayState* play, int32_t pageIndex, void* userData);

// One registered page. `id` is the greppable key a console line carries (no spaces); `title` is
// what the page draws and may contain spaces, so every console line that prints it puts it last and
// quotes it.
struct RsMenuPage {
    std::string id;
    std::string title;
    RsMenuPageDrawFn draw;
    void* userData;
};

// Registers a page at the end of the ring and returns its 0-based index, or -1 if `id` is empty or
// already taken. Safe to call from a ShipInit function; callers guard their own re-registration,
// since ShipInit functions re-run on every config and preset load.
int32_t RsMenu_RegisterPage(const char* id, const char* title, RsMenuPageDrawFn draw, void* userData);

int32_t RsMenu_PageCount();
// 0-based. Null when `index` is out of range, and null whenever the count is 0.
const RsMenuPage* RsMenu_PageAt(int32_t index);

// Why an open was refused. Anything but RS_MENU_OPEN_OK is an rc=1 on the console.
enum RsMenuOpenResult {
    RS_MENU_OPEN_OK = 0,
    RS_MENU_OPEN_ALREADY,       // it was already open - not a refusal
    RS_MENU_OPEN_DISABLED,      // the feature CVar is off
    RS_MENU_OPEN_NO_PLAY,       // no PlayState, or not GAMEMODE_NORMAL
    RS_MENU_OPEN_KALEIDO,       // vanilla pause is up; one menu at a time
    RS_MENU_OPEN_NO_PAGES,      // nothing registered, so there is nothing to show
};
const char* RsMenu_OpenResultName(RsMenuOpenResult result);

bool RsMenu_IsEnabled();
bool RsMenu_IsOpen();
// 0-based index into the ring; 0 when nothing is registered.
int32_t RsMenu_CurrentPage();

RsMenuOpenResult RsMenu_Open();
// Returns whether it had been open. Clears haltAllActors and restores the HUD either way.
bool RsMenu_Close();
// 0-based. False when out of range or nothing is registered.
bool RsMenu_SetPage(int32_t index);
// The ring: wraps modulo the page count, in both directions. No-op with nothing registered.
void RsMenu_CyclePage(int32_t delta);

// How the scroll geometry gets a viewport and a projection (stage 4). Research § C.1 named three
// levels and recommended level 2 - an own `View` applied with func_800AAA50(&myView, 127) and
// restored with func_800AAA50(&play->view, 15). Level 2 is NOT what shipped, for three measured
// reasons spelled out in RsMenu.cpp's "THE PROJECTION" block; these modes exist so the rejected
// alternative can be SEEN rather than argued about, and each of those three findings came out of
// running one of them. Diagnostic only: deliberately NOT a CVar, so nothing a run sets here can be
// left behind in the owner's shipofharkinian.json the way `primary` can.
enum RsMenuViewMode {
    // What ships: our own Vp + guOrtho emitted straight into the menu's own pool, the letterbox
    // pattern (z_rcp.c:1632-1706). `play->view` is never touched, so there is nothing to restore.
    RS_MENU_VIEW_OWN_VP = 0,
    // The same, plus the level-2 View bracket wrapped around it. The geometry still draws (our own
    // viewport wins); what this isolates is what the bracket COSTS.
    RS_MENU_VIEW_BRACKET = 1,
    // Level 2 on its own: the View bracket and no viewport of our own, with kaleido's own eye. This
    // is what research § C.1 recommended, applied to the pool the menu actually lives in - and the
    // fact that it draws IDENTICALLY to `ownvp` is the photograph of the bracket being inert here.
    RS_MENU_VIEW_INHERIT = 2,
};
int32_t RsMenu_GetViewMode();
void RsMenu_SetViewMode(int32_t mode);
const char* RsMenu_ViewModeName(int32_t mode);
bool RsMenu_ParseViewMode(const std::string& word, int32_t* mode);

// The interpolation probe. Off by default; on, it drives a stepped per-tick vertical offset into
// BOTH halves of the menu at once - the scroll geometry through Matrix_Translate, a probe string
// through its texture rectangle's y - and recolours the scroll's centre columns magenta so a pixel
// scan cannot confuse them with the scene. The two halves therefore disagree in a single frame
// exactly when the matrix half interpolates and the texrect half does not, which is the measurement
// stage 4 owes stage 5. See SOH_2D_DRAWING.md § "Verifying smoothness". There is no getter beside
// this one: the probe's whole live state, lattice included, is on RsMenuStatus below, which is the
// single place the console reads menu state from.
void RsMenu_SetProbe(bool on);

// Which menu START opens. Flipped live from the console so either is reachable mid-session.
enum RsMenuPrimary {
    RS_MENU_PRIMARY_VANILLA = 0,
    RS_MENU_PRIMARY_CUSTOM = 1,
};
int32_t RsMenu_GetPrimary();
void RsMenu_SetPrimary(int32_t primary);
const char* RsMenu_PrimaryName(int32_t primary);
// Parses "vanilla"/"custom"; false on anything else.
bool RsMenu_ParsePrimary(const std::string& word, int32_t* primary);

// The trigger's binding state on port 0, read from the control deck.
//
// N64 L is the only bit vanilla never reads during gameplay (research D.2) - and on a GameCube pad
// NOTHING produces it: SoH binds it to SDL `leftshoulder`, which no GC adapter mapping exposes, and
// the GC L trigger is already N64 Z. So an unbound trigger is indistinguishable from a broken menu,
// and the boot line below exists to tell them apart. The agent harness injects the BTN_L mask
// directly and is unaffected by bindings. Never describe the feature to a player as "press L".
struct RsMenuTriggerInfo {
    uint16_t mask;
    int32_t bindings;   // -1 when the control deck could not be reached at all
    bool bound;
};
RsMenuTriggerInfo RsMenu_TriggerInfo();

// Everything `menu dump` reports that is not a page. Kept as a struct so the console renderer holds
// no menu logic and the two cannot drift.
struct RsMenuStatus {
    bool enabled;
    bool open;
    int32_t page;        // 0-based
    int32_t pages;
    int32_t primary;
    int32_t viewMode;
    // The probe's live lattice. `probeStep` is how far one game tick moves both halves, so a
    // measured position that is not a whole number of steps from the park position cannot have come
    // from the 20 Hz animation and is therefore an interpolated frame.
    bool probe;
    int32_t probePhase;
    float probeStep;
    float probeDy;
    // Frame interpolation's camera epoch, and the register that exempts vanilla pause from the
    // jump heuristic that bumps it. Both are here because stage 4's view-mode question turned out
    // to be answerable only by watching them.
    int32_t cameraEpoch;
    int32_t pauseMenuMode;
    // Freeze and HUD, as actually applied to the live PlayState.
    bool halt;           // play->haltAllActors right now
    bool haltPrev;       // what it was when the menu opened, and what close restores
    bool hudHidden;
    int32_t hudPrev;     // gSaveContext.hudVisibilityMode at open; what close restores
    int32_t hudNow;
    // Counters. The stage-2 evidence lives here: `stickFrames` says input REACHED the game while
    // the world was frozen, which is what turns "Link did not move" from an untested negative into
    // a challenged one (a still screenshot proves nothing if the input never arrived).
    int32_t opens;
    int32_t closes;
    int32_t pageChanges;
    int32_t openFrames;
    int32_t drawFrames;
    int32_t inputFrames;
    int32_t stickFrames;
    int32_t buttonFrames;
    int32_t lastStickX;
    int32_t lastStickY;
    uint16_t lastButtons;
    // The START filter's witness, and the same discipline as `stickFrames`: a filter that swallowed
    // nothing and a filter that was never offered anything leave the menu in identical states.
    // `kaleido` is `pauseCtx.state`, which says outright whether vanilla pause got in.
    int32_t kaleido;
    int32_t filterArmedFrames;
    int32_t startSwallowed;
    int32_t startConsumed;
    uint32_t filterPressSeen;
};
RsMenuStatus RsMenu_Status();

#endif // SOH_RS_MENU_H
