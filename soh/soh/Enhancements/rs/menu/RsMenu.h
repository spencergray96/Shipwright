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

// A page's body draw, called once per frame while that page is the visible one, from INSIDE the
// menu's own interpolation node and UNDER the sweep matrix, so whatever it draws rides the scroll.
// It does NOT open an OPEN_DISPS block of its own: it appends to the menu's shared heap display
// list through the RsMenu_DrawText* helpers below, which is what keeps a whole page of glyphs
// inside the 2048-word OVERLAY_DISP budget and inside one interpolation node. A page that passes
// nullptr gets the greybox body (its title, centred), which is what every page is up to stage 5.
typedef void (*RsMenuPageDrawFn)(struct PlayState* play, int32_t pageIndex, void* userData);

// Text, in the menu's own 320x240 game space, y down from the top-left. Valid ONLY from inside a
// page's RsMenuPageDrawFn: each glyph is four vertices and a quad appended to the shared list, so
// calling one of these outside a draw has nowhere to put them and does nothing.
//
// Glyphs are Vtx rather than texture rectangles on purpose and it is the substance of stage 5 -
// a texrect's coordinates are baked into Gfx words, replay identically on every rendered frame,
// and so step at 20 Hz while the scroll around them glides (SOH_2D_DRAWING.md). `x`/`y` is the
// top-left of the first glyph cell, the same anchor the texrect renderer used.
void RsMenu_DrawText(const char* text, int16_t x, int16_t y, float scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void RsMenu_DrawTextCentred(const char* text, int16_t centreX, int16_t y, float scale, uint8_t r, uint8_t g, uint8_t b,
                            uint8_t a);
float RsMenu_TextWidth(const char* text, float scale);

// A page's cursor-node contribution, called once per update tick while that page is visible, from
// between the two hand nodes. A page adds its selectable items with RsMenu_AddCursorNode; the
// hands are added for it. Null means "this page has no items", which is every page at stage 5 -
// the four greybox pages have nothing to select, so the live graph is exactly the two hands.
typedef void (*RsMenuPageNodesFn)(int32_t pageIndex, void* userData);

// One registered page. `id` is the greppable key a console line carries (no spaces); `title` is
// what the page draws and may contain spaces, so every console line that prints it puts it last and
// quotes it.
struct RsMenuPage {
    std::string id;
    std::string title;
    RsMenuPageDrawFn draw;
    RsMenuPageNodesFn nodes;
    void* userData;
};

// Registers a page at the end of the ring and returns its 0-based index, or -1 if `id` is empty or
// already taken. Safe to call from a ShipInit function; callers guard their own re-registration,
// since ShipInit functions re-run on every config and preset load. `nodes` may be null.
int32_t RsMenu_RegisterPage(const char* id, const char* title, RsMenuPageDrawFn draw, RsMenuPageNodesFn nodes,
                            void* userData);

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
// True from the moment an open is accepted until the closing slide has finished - i.e. whenever the
// menu is on screen in any form, which is also whenever the world is frozen.
bool RsMenu_IsOpen();

// --- the arrival -----------------------------------------------------------------------------
//
// The menu is not switched on and off, it ARRIVES: the whole assembly rises from below the bottom
// edge and drops back down, and the world dims behind it as it comes. The rise is one more term in
// the same Matrix_Translate the probe uses, so it interpolates; the dim is a prim-colour alpha and
// therefore steps in kEntryTicks levels, which is acceptable for a fade and would not be for a
// motion (SOH_2D_DRAWING.md).
//
// PHASE IS NOT THE SAME QUESTION AS OPEN. `RsMenu_IsOpen` answers "is the menu on screen at all",
// which is what the freeze and the HUD follow. The phase answers "where is it in its arrival",
// which is what a screenshot is checked against. A run that closes the menu and asserts `open=0`
// should not have to wait out the slide, so the console prints both.
enum RsMenuPhaseId {
    RS_MENU_PHASE_ID_CLOSED = 0,
    RS_MENU_PHASE_ID_OPENING,
    RS_MENU_PHASE_ID_OPEN,
    RS_MENU_PHASE_ID_CLOSING,
};
int32_t RsMenu_Phase();
const char* RsMenu_PhaseName(int32_t phase);
// 0-based index into the ring; 0 when nothing is registered. During a sweep this is the OUTGOING
// page until the midpoint tick and the INCOMING one after it - the swap is the animation's
// midpoint, not its start or its end.
int32_t RsMenu_CurrentPage();

RsMenuOpenResult RsMenu_Open();

// Starts the closing slide - what B, START and the console's `close` do. Returns false only when
// the menu was not up. The world stays frozen until the slide finishes, because un-freezing halfway
// down would show the world moving under a menu that is still on screen. Reversing out of a
// half-finished arrival picks up where it got to rather than snapping to the top first.
bool RsMenu_BeginClose();

// Closes INSTANTLY, with no slide: restores haltAllActors and the HUD on the spot. Returns whether
// it had been up. This is the path a scene load and the CVar-off guard must take - a slide cannot
// outlive a Play_Init, and the freeze has to be gone before the transition, or the agent harness
// never emits `ready` again. It is also `menu close now` on the console.
bool RsMenu_Close();
// 0-based. False when out of range or nothing is registered. Instant - no sweep; this is the
// console's page selector, and a run that wants the animation asks for a sweep instead. It also
// abandons a sweep in flight, rather than letting it swap again on a tick the caller cannot see.
bool RsMenu_SetPage(int32_t index);

// --- the sweep (stage 5) -------------------------------------------------------------------------
//
// L/R do not cut between pages, they SHUT AND RE-OPEN the scroll. An integer per-tick counter
// drives one side - its roll end and the hand holding it - horizontally across to meet the other
// side, which does not move; the parchment between them shrinks to nothing under a Matrix_Scale, so
// the scroll is genuinely closed at the midpoint; then the same side travels straight back out. The
// page content changes while it is shut. `delta` is +1 (R, the right side travels) or -1 (L, the
// left side). Refused while a sweep is already running, and while the ring has fewer than two
// pages - there is nothing to roll to.
bool RsMenu_StartSweep(int32_t delta);

// Keep sweeping, alternating direction, until told to stop - THE INSTRUMENT FOR THIS STAGE, and it
// exists because of the agent loop rather than because of the game. One sweep is well under a
// second; a command round trip through agent-commands.txt is seconds, so a run can never
// photograph a chosen tick of a single sweep. Under a loop every capture lands on SOME tick of a
// live animation, and a burst of them samples the whole excursion - which is what turns "it looks
// smooth" into a distribution. `RsMenu_StopSweepLoop` also abandons the sweep in flight.
bool RsMenu_StartSweepLoop();
void RsMenu_StopSweepLoop();

// Parks the animation AT one tick and leaves it there - the other half of the same agent-loop
// problem the sweep loop solves, from the other end. The loop makes every capture land somewhere in
// a live excursion, which is what a DISTRIBUTION needs; a hold makes a capture land on a chosen
// tick, which is what a LOOK needs - "is the paper really gone at the midpoint", "which side is
// travelling when R is pressed". Without it those are a race against the whole gesture.
//
// `tick` is 0..the sweep length, and the page swap is applied as if the sweep had been played to
// that tick, so a held frame is a frame the animation really produces rather than a pose only the
// console can reach. False when the ring has fewer than two pages or `tick` is out of range;
// RsMenu_StopSweepLoop releases it.
bool RsMenu_HoldSweep(int32_t delta, int32_t tick);

// Everything a console line or an acceptance script needs to say WHERE in the sweep a screenshot
// was taken, which is what makes a mid-sweep capture self-describing.
struct RsMenuSweepState {
    bool active;
    int32_t tick;       // 0 at rest; 1..ticks while sweeping
    int32_t ticks;      // the whole excursion, in game ticks
    int32_t dir;        // +1 (R) or -1 (L); retained after the sweep ends
    int32_t fromPage;   // 0-based, the page the sweep left
    int32_t toPage;     // 0-based, the page it is going to
    int32_t movingHand; // 0 = left, 1 = right; which hand the shoulder pressed moves
    float env;          // the gesture's envelope, 0 wide open, 1 fully shut
    float dx;           // game units the moving side is displaced this tick
    float width;        // how much parchment is still showing: 1 wide open, 0 shut
    int32_t sweeps;     // how many have run this session
    bool loop;          // sweeping on repeat, alternating direction
    bool hold;          // parked at `tick` rather than advancing
};
RsMenuSweepState RsMenu_SweepState();

// --- the cursor graph (stage 5) ------------------------------------------------------------------
//
// ADJACENCY IS THE GRAPH'S, NOT THE PIXELS'. That is the settled design's load-bearing claim and
// the reason option A's full-width parchment costs nothing: pressing left at the leftmost item
// reaches the left hand wherever the hand happens to be drawn, and the highlight follows onto it.
// Nothing here derives a neighbour from a coordinate; `x/y/w/h` exist only so the highlight knows
// where to draw.
//
// The node list is rebuilt every update tick as [left hand] + [the page's own items] + [right
// hand] and wired left-to-right in that order. At stage 5 no page contributes items, so the graph
// is exactly the two hands and the "empty middle" is literal. A page that needs 2-D adjacency is
// stage 6's problem and will supply it; the linear wiring is the default, not a limit.
struct RsMenuCursorNode {
    std::string id;
    int16_t x, y, w, h;               // where the highlight draws, in the same 320x240 game space
    int32_t left, right, up, down;    // indices into the live list; -1 for no neighbour
    int32_t hand;                     // 0 = left hand, 1 = right hand, -1 = an ordinary item
};

// Called by a page's RsMenuPageNodesFn, and valid only from inside it. Returns the new node's
// index, or -1 outside a rebuild. Neighbours are wired by the menu afterwards.
int32_t RsMenu_AddCursorNode(const char* id, int16_t x, int16_t y, int16_t w, int16_t h);

int32_t RsMenu_CursorCount();
// 0-based. Null when out of range.
const RsMenuCursorNode* RsMenu_CursorAt(int32_t index);
int32_t RsMenu_CursorIndex();
// Moves one step along the graph. False when that direction has no neighbour - refused rather than
// clamped silently, so a run cannot report reaching a node it never reached.
bool RsMenu_MoveCursor(int32_t dx, int32_t dy);
// Jumps to a node by id. False when no node carries it.
bool RsMenu_SetCursorById(const char* id);

// What A did. A hand starts the sweep its side implies, which is the design's "selecting a hand
// does what L/R does"; an ordinary item has nothing to enter until stage 6's detail views exist.
enum RsMenuSelectResult {
    RS_MENU_SELECT_NONE = 0,    // no node under the cursor at all
    RS_MENU_SELECT_HAND_LEFT,   // rolled back a page
    RS_MENU_SELECT_HAND_RIGHT,  // rolled forward a page
    RS_MENU_SELECT_BUSY,        // a sweep is already running; the press was dropped
    RS_MENU_SELECT_ITEM,        // an ordinary item - nothing to do until stage 6
};
RsMenuSelectResult RsMenu_SelectCursor();
const char* RsMenu_SelectResultName(RsMenuSelectResult result);

// --- the interpolation probe ---------------------------------------------------------------------
//
// Stage 4's two-channel instrument, kept and repointed. Off by default; on, it drives a stepped
// per-tick vertical offset into BOTH halves of the menu at once - the whole scroll through the same
// Matrix_Translate chain the sweep uses, and a probe string through a texture rectangle's baked y -
// and recolours the scroll's centre columns magenta so a pixel scan cannot confuse them with the
// scene. The two therefore disagree in a single frame exactly when the matrix half interpolates and
// the texrect half does not.
//
// ⚠ The probe string is the LAST texture rectangle in this menu, and it is one deliberately. Stage
// 5 converted the parchment and every glyph to Vtx, which is the whole point of the stage - but
// that would have left the probe with two interpolating channels and nothing to compare them
// against. RsMenu.cpp keeps a texrect glyph renderer alive for this string alone, labelled as the
// reference channel. See SOH_2D_DRAWING.md § "Verifying smoothness".
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

// Everything `menu dump` reports that is not a page or a cursor node. Kept as a struct so the
// console renderer holds no menu logic and the two cannot drift.
struct RsMenuStatus {
    bool enabled;
    bool open;
    int32_t page;        // 0-based
    int32_t pages;
    int32_t primary;
    // The probe's live lattice. `probeStep` is how far one game tick moves both halves, so a
    // measured position that is not a whole number of steps from the park position cannot have come
    // from the 20 Hz animation and is therefore an interpolated frame.
    bool probe;
    int32_t probePhase;
    float probeStep;
    float probeDy;
    // Frame interpolation's camera epoch. Kept from stage 4, where it was the number that settled
    // the projection question: anything that bumps it once per game tick has quietly switched
    // interpolation off for everything under the camera node. It must stand still while the menu is
    // open, and a stage-5 regression here would mean the sweep cannot interpolate either.
    int32_t cameraEpoch;
    int32_t pauseMenuMode;
    // The arrival. `phase` is an RsMenuPhaseId; `entryProgress` is 0 fully below the screen and 1
    // settled in place, and `dimAlpha` is what the world is being darkened by right now.
    int32_t phase;
    int32_t entryTick;
    int32_t entryTicks;
    float entryProgress;
    int32_t dimAlpha;
    // Freeze and HUD, as actually applied to the live PlayState.
    bool halt;           // play->haltAllActors right now
    bool haltPrev;       // what it was when the menu opened, and what close restores
    bool hudHidden;
    int32_t hudPrev;     // gSaveContext.hudVisibilityMode at open; what close restores
    int32_t hudNow;
    // What the last drawn frame cost, in the units the OVERLAY_DISP budget is denominated in.
    // `dlWords` is the heap display list's length - OVERLAY_DISP itself holds only 2048 Gfx words
    // (z64.h:107-112), which the menu no longer spends because it submits ONE gSPDisplayList, but
    // the number is the measurement stage 7 needs and it is free to keep here.
    int32_t drawGlyphs;
    int32_t drawQuads;
    int32_t dlWords;
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
