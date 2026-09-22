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
// kaleido's state machine: this menu REFUSES to open while `pauseCtx.state != 0`, and whenever it
// means to take START (it is up, or `primary` is `custom`) it VETOES vanilla's open through
// VB_OPEN_PAUSE_MENU, which wraps the bare START check inside KaleidoSetup_Update itself
// (z_kaleido_setup.c). Since stage 6 that veto is the whole mechanism - the OnGameStateMainStart
// input filter it replaced lost a hash-bucket race to the agent harness and is gone
// (docs/decisions/2026-09-19-pause-menu-start-veto.md). (VB_CLOSE_PAUSE_MENU is NOT the hook for
// this - it governs CLOSING and would trap the player inside vanilla pause. Research D.1.)

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
// A flat, untextured rectangle - a rule under a title, the line through a ticked step. Same space,
// same validity rule as the text calls. It switches the list to flat colour for the quad and back
// to the text state afterwards, so a page can interleave it with text freely.
void RsMenu_DrawBar(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t r, uint8_t g, uint8_t b);

// STAGE 8 - A TEXTURED QUAD: an item icon, a song note, a digit. Same space, same validity rule as
// the text calls, and Vtx for the same reason (a texture rectangle steps at 20 Hz while the scroll
// glides). The WHOLE texture is mapped onto the box `x, y, w, h`, whatever its size - vanilla's quest
// page squeezes 24x24 stone icons into 20x20 boxes the same way. `texture` is what the game's own
// tables hold (an `__OTR__` path for gItemIcons and friends). One texture load and one quad each, so
// an icon costs what a glyph costs: one draw (stage 7). `grey` draws it the way kaleido draws an item
// the current age cannot use (gSPGrayscale at 109). The list is left in the icon state; the text and
// bar calls switch back by themselves.
enum RsMenuTexFormat {
    RS_MENU_TEX_RGBA32 = 0, // item and quest icons (gItemIcons below the song notes)
    RS_MENU_TEX_IA8,        // song note, heart pieces, equipped outline, ammo digits
    RS_MENU_TEX_I8,         // the counter digits (digitTextures)
};
void RsMenu_DrawIcon(const void* texture, RsMenuTexFormat format, int16_t texW, int16_t texH, int16_t x, int16_t y,
                     int16_t w, int16_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool grey);

// STAGE 8 - LINK'S PORTRAIT: a quad textured from the pause-Link framebuffer, composited onto the
// parchment under the scroll's matrix, and the request that makes the menu render Link into that
// framebuffer this frame (PauseLink.cpp). Same validity rule as the text calls. The render goes into
// WORK_DISP, which the RCP runs before any pool this quad is in, so the framebuffer is always this
// frame's. Only a page that calls this pays for the render.
void RsMenu_DrawPauseLink(int16_t x, int16_t y, int16_t w, int16_t h);

// A page's cursor-node contribution, called once per update tick while that page is visible, from
// between the two hand nodes. A page adds its selectable items with RsMenu_AddCursorNode; the
// hands are added for it. Null means "this page has no items", and then the live graph is exactly
// the two hands (the stage-5 greybox pages were this; since stage 8 every registered page has items).
typedef void (*RsMenuPageNodesFn)(int32_t pageIndex, void* userData);

struct RsMenuCursorNode;

// STAGE 6 - THE SECOND LEVEL. "A enters a detail view, B leaves. Two levels, no third." A page that
// has detail views supplies `select`, called when A lands on one of its own items (never on a
// hand): return true and the menu runs the down-a-level animation and then calls `detailDraw`
// instead of `draw`. What the detail shows is the page's own state - it knows which item it said
// yes to. `detailDraw` has the same contract as `draw` (inside the content node, through the
// RsMenu_DrawText helpers), except that it is called under the UNROTATED matrix while the
// parchment is vertical, so its text stays upright; RsMenu_DetailRect says where the vertical
// parchment has room for it.
typedef bool (*RsMenuPageSelectFn)(int32_t pageIndex, const RsMenuCursorNode* node, void* userData);

// A page's own buttons, once per update tick while the menu is settled and nothing is animating.
// `level` is 0 on the page itself and 1 in its detail view. `press` is the raw press mask, for
// what the menu does not claim (C-left/C-right paging, say). `navY` is -1/0/+1 from the D-pad or the
// latched stick, and is only ever non-zero at level 1: at level 0 up/down walk the cursor graph, in
// a detail view there is no graph and they are the page's to scroll with.
typedef void (*RsMenuPageInputFn)(int32_t pageIndex, int32_t level, uint16_t press, int32_t navY, void* userData);

// STAGE 8: which D-pad bits are the PAGE's this tick rather than the cursor's, asked once per update
// tick at level 0, BEFORE the cursor moves - the page's input callback itself runs after the move, as
// kaleido moves its cursor first and tests the equip buttons second (z_kaleido_item.c:694). The reason:
// with SoH's DpadEquips on, kaleido gives a D-pad press to equipping rather than to the cursor, and the
// port has to be able to say the same. `held` is the held mask (the C-up modifier). Bits outside the
// D-pad are ignored - the stick, A, B and the shoulders stay the menu's.
typedef uint16_t (*RsMenuPageClaimFn)(int32_t pageIndex, uint16_t held, void* userData);

// One registered page. `id` is the greppable key a console line carries (no spaces); `title` is
// what the page draws and may contain spaces, so every console line that prints it puts it last and
// quotes it. Every callback may be null.
//
// `ownsItemHighlight`: the page draws its own marker on the item the cursor is on (the quest
// list's arrow), so the menu does not draw its yellow box around that item. The box still draws
// around a HAND, which the page does not own.
struct RsMenuPage {
    std::string id;
    std::string title;
    RsMenuPageDrawFn draw = nullptr;
    RsMenuPageNodesFn nodes = nullptr;
    void* userData = nullptr;
    RsMenuPageSelectFn select = nullptr;
    RsMenuPageDrawFn detailDraw = nullptr;
    RsMenuPageInputFn input = nullptr;
    bool ownsItemHighlight = false;
    RsMenuPageClaimFn claim = nullptr;
};

// Registers a page at the end of the ring and returns its 0-based index, or -1 if `id` is empty or
// already taken. Safe to call from a ShipInit function; callers guard their own re-registration,
// since ShipInit functions re-run on every config and preset load. `nodes` may be null.
int32_t RsMenu_RegisterPage(const char* id, const char* title, RsMenuPageDrawFn draw, RsMenuPageNodesFn nodes,
                            void* userData);
// The same, from one struct - which is the invariant's "adding a page is registering one struct"
// read literally. The short form above is this with the stage-6 fields left at their defaults.
int32_t RsMenu_RegisterPageStruct(const RsMenuPage& page);

// Where a page may draw, in the 320x240 game space. RsMenu_PageRect is the horizontal parchment's
// inside, less the strips the two hands cover; RsMenu_DetailRect is the VERTICAL parchment's inside
// at rest, less the bands the hands cover at either end - roughly 128 wide, which is about twenty
// characters of journal text at the detail scale. Both are derived from the geometry constants in
// RsMenu.cpp, so re-tuning the vertical separation moves the detail layout with it.
struct RsMenuRect {
    int16_t x0, y0, x1, y1;
};
RsMenuRect RsMenu_PageRect();
RsMenuRect RsMenu_DetailRect();

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
// hand]. By default it is wired left-to-right in that order - a ROW, which is what the hands-only
// stage-5 greybox pages needed. A page whose items are a list declares a COLUMN instead
// (RsMenu_SetCursorColumn, stage 6), and then the VERTICAL EDGES COME FROM THE PAGE'S ITEM ORDER:
// each item's up/down is the item added before/after it, the first item has no up and the last no
// down (refused, not wrapped), every item's left is the left hand and its right the right hand, and
// both hands lead back into the column at the page's `entry` item. Still nothing derives an edge
// from a coordinate. Outside the page's own ring level (in a detail view) the graph is EMPTY: a
// detail has nothing to select, and up/down belong to the page there (RsMenuPageInputFn).
struct RsMenuCursorNode {
    std::string id;
    int16_t x, y, w, h;               // where the highlight draws, in the same 320x240 game space
    int32_t left, right, up, down;    // indices into the live list; -1 for no neighbour
    int32_t hand;                     // 0 = left hand, 1 = right hand, -1 = an ordinary item
};

// Called by a page's RsMenuPageNodesFn, and valid only from inside it. Returns the new node's
// index, or -1 outside a rebuild. Neighbours are wired by the menu afterwards.
int32_t RsMenu_AddCursorNode(const char* id, int16_t x, int16_t y, int16_t w, int16_t h);
// Called from inside the same RsMenuPageNodesFn: wire this page's items as a column (see above).
// `entry` is the index RsMenu_AddCursorNode returned for the item the hands lead to - the quest list
// passes the row the cursor last stood on (or its top visible row, when that one has scrolled away),
// so stepping off a hand lands on something on screen. It is also where the cursor goes when the id
// it was on is not on this page - which is how opening the menu lands on the list rather than a hand.
void RsMenu_SetCursorColumn(int32_t entry);

// STAGE 8 - A GRID WHOSE EDGES THE PAGE WIRES ITSELF. The ported vanilla pages move the cursor by
// kaleido's own rules (skip empty slots, wrap into the next row, leave the page at an edge), and no
// row or column captures that - so the page computes every edge from the live inventory and hands
// them over. Still nothing derives an edge from a coordinate: the page runs vanilla's cursor
// arithmetic, which is about slots, not pixels.
//
// RsMenu_SetCursorGrid switches this rebuild to explicit edges; `entry` is the node a cursor whose id
// is not on this page lands on (opening the menu), or -1 for the left hand. RsMenu_LinkCursorNode
// sets one item's four neighbours, each a node index RsMenu_AddCursorNode returned, a hand below, or
// -1 for none (refused, like every other missing edge). RsMenu_LinkHands sets where the hands lead
// INTO the page: the left hand's right and the right hand's left - vanilla's "step off the page
// arrow" scans. A hand's outward and vertical edges stay empty, as on every other page. All three are
// valid only from inside a nodes callback.
constexpr int32_t RS_MENU_LINK_NONE = -1;
constexpr int32_t RS_MENU_LINK_HAND_LEFT = -2;
constexpr int32_t RS_MENU_LINK_HAND_RIGHT = -3;
void RsMenu_SetCursorGrid(int32_t entry);
void RsMenu_LinkCursorNode(int32_t node, int32_t left, int32_t right, int32_t up, int32_t down);
void RsMenu_LinkHands(int32_t leftHandRight, int32_t rightHandLeft);

int32_t RsMenu_CursorCount();
// 0-based. Null when out of range.
const RsMenuCursorNode* RsMenu_CursorAt(int32_t index);
int32_t RsMenu_CursorIndex();
// The id the cursor is on - the thing that PERSISTS across rebuilds. Valid inside a page's nodes
// callback too, where it is how a list keeps the cursor's row on screen: it is the id the cursor
// was on when the rebuild started, and the menu rebuilds again straight after any move, so a page
// never lags its own cursor by a tick. Empty before the first rebuild.
const std::string& RsMenu_CursorId();
// Moves one step along the graph. False when that direction has no neighbour - refused rather than
// clamped silently, so a run cannot report reaching a node it never reached.
bool RsMenu_MoveCursor(int32_t dx, int32_t dy);
// Jumps to a node by id. False when no node carries it.
bool RsMenu_SetCursorById(const char* id);

// What A did. A hand starts the sweep its side implies, which is the design's "selecting a hand
// does what L/R does"; an item whose page says yes goes down a level into its detail view.
enum RsMenuSelectResult {
    RS_MENU_SELECT_NONE = 0,    // no node under the cursor at all
    RS_MENU_SELECT_HAND_LEFT,   // rolled back a page
    RS_MENU_SELECT_HAND_RIGHT,  // rolled forward a page
    RS_MENU_SELECT_BUSY,        // something is already animating; the press was dropped
    RS_MENU_SELECT_ITEM,        // an item its page has no detail view for - nothing happened
    RS_MENU_SELECT_DESCEND,     // an item with a detail view: the down-a-level animation started
};
RsMenuSelectResult RsMenu_SelectCursor();
const char* RsMenu_SelectResultName(RsMenuSelectResult result);

// --- levels: going down into a detail view and back (stage 6) ------------------------------------
//
// GOING DOWN A LEVEL IS CLOSE -> TURN -> OPEN (#111 comment 10, Spencer's spec):
//
//   1. CLOSE TO THE CENTRE. Both sides - roll end and hand together, exactly as L/R moves one -
//      travel inward and meet in the middle, each covering half the one-hand L/R travel. The
//      parchment scales about its CENTRE, so its edges stay on the roll centres and nothing is
//      left in the rolls' trail.
//   2. TURN. The closed bundle rotates COUNTER-CLOCKWISE to vertical about the rolls' midpoint:
//      the right roll ends on top, the left on the bottom. The right hand is rigid and ends on top
//      reaching in from the right; the LEFT hand swivels back on its own grip as the scroll turns,
//      so it ends on the bottom roll reaching in from the LEFT (Spencer's correction after stage 6 -
//      rigidly rotated, both hands came in from the right).
//   3. OPEN VERTICALLY - but only to a separation that fits the screen, which is a named constant
//      in RsMenu.cpp (kVerticalSpan) and NOT the horizontal 288: the vertical rest pose is its own
//      pose, not the horizontal one rotated.
//
// Going back up (B) plays exactly the same path in reverse. The level itself changes at the middle
// of the turn, where the scroll is shut; the content blinks out when the animation starts and the
// other level's content blinks in when it ends - Spencer's 1.0, the same as a page roll.
//
// One MATRIX CHAIN carries all of it, every frame, whatever is animating: base * rotate(theta about
// the pivot) * the side's own local translate * geometry, with theta = 0 whenever the scroll is
// horizontal. So the L/R chains carry the rotation ops too, at zero - ops are matched positionally
// inside an interpolation node, and a chain that only sometimes has a rotation would break it.
int32_t RsMenu_Level(); // 0 = the page, 1 = its detail view

// A at level 0, through the page's `select`. False when a detail view cannot be entered from here:
// not settled, something already animating, not at level 0, or the page said no.
bool RsMenu_Descend();
// B at level 1. False when not at level 1 or something is already animating.
bool RsMenu_Ascend();

// The same two agent-loop instruments `sweep` has, for the same reason: the whole gesture is 1.2 s
// and a command round trip is seconds. The LOOP runs down, up, down, up... until stopped, so a
// burst of captures samples every phase; a HOLD parks the animation at one tick (0..ticks) of the
// way down (`dir` +1) or the way up (-1), the level swap applied as if it had been played there.
// Both refuse on a page with no detail view. RsMenu_StopLevelLoop releases either, and lands on
// whichever level the animation had reached.
bool RsMenu_StartLevelLoop();
bool RsMenu_HoldLevel(int32_t dir, int32_t tick);
void RsMenu_StopLevelLoop();

struct RsMenuLevelState {
    int32_t level;
    bool active;
    int32_t tick;    // 0..ticks along the direction of travel
    int32_t ticks;   // the whole gesture
    int32_t dir;     // +1 down, -1 up; retained after the animation ends
    int32_t pose;    // 0..ticks along the DOWNWARD path, i.e. where the scroll is: 0 flat, ticks vertical
    const char* phase; // "rest", "close", "turn" or "open" - the phase the pose is in
    float separation; // game units between the two roll centres, in the scroll's own frame
    float angle;      // degrees counter-clockwise, 0 horizontal, 90 vertical
    int32_t swaps;    // how many level changes have happened this session
    bool loop;
    bool hold;
};
RsMenuLevelState RsMenu_LevelState();

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

// --- stage 7: what the last frame's view drew, and a dense horizontal page to measure -----------
//
// `section=draw` counts the whole frame; this counts only the VIEW - whatever drew inside the
// content node this frame: a page's `draw`, its `detailDraw`, the greybox body, or the stress body
// below. `rows` is how many distinct text lines it put down (distinct y among the text calls that
// drew at least one glyph, so a row's arrow and its title are one row), `glyphs` how many glyphs,
// and `words` how many Gfx words it appended to the heap list. `view` is "page", "detail", "stress"
// or "none" - none when the last frame drew no content at all, which is every frame of a roll or a
// level change (the 1.0 blink). `frame` is the draw frame it belongs to (RsMenuStatus::drawFrames).
struct RsMenuViewStats {
    const char* view;
    int32_t frame;
    int32_t rows;
    int32_t glyphs;
    int32_t words;
    int32_t icons; // stage 8: RsMenu_DrawIcon / RsMenu_DrawPauseLink quads the view drew
};
RsMenuViewStats RsMenu_ViewStats();

// THE STRESS BODY - TEST-ONLY, and the reason it is not a page: a registered page would change the
// ring every other run walks (`pages=`, which page L lands on), and the ring has no unregister. So
// it is a MODE: while it is on, the visible page's level-0 body is replaced by `glyphs` glyphs laid
// out row by row across RsMenu_PageRect() at the journal scale, on any page. Detail views are left
// alone (#111 comment 12 scopes stage 7 to the horizontal page). ONLY THE DRAW is replaced: the page's
// cursor graph, input and `select` stay live underneath, so A on the quest page still goes into the
// quest the cursor is on. That is fine for a measurement and is the point of it being test-only.
// `drawn_rows` counts exact y, so text that should read as one row must share its y (the quest list's
// arrow and title do). Reachable only from the console -
// no CVar, nothing persists past the session, off at boot - so it cannot ship visible to a player.
//
// `glyphs` 0 is a real setting, not off: the menu open with an EMPTY page, which is the bracket that
// separates what the chrome costs from what the text costs. `same` draws one character repeated
// instead of cycling through letters and digits - it asks whether a run of identical glyphs is any
// cheaper (Fast3D's GPU cache is keyed by pointer, but every load still marks the texture changed).
// False when `glyphs` is negative or more than the page can hold; RsMenu_StressCapacity says how many.
bool RsMenu_SetStress(int32_t glyphs, bool same);
void RsMenu_StopStress();
int32_t RsMenu_StressCapacity(bool same);

struct RsMenuStressState {
    bool on;
    int32_t glyphs;
    bool same;
    int32_t capacity;     // for the mode currently chosen
    float scale;
    int16_t pitch;
};
RsMenuStressState RsMenu_StressState();

// Which menu START opens. Flipped live from the console so either is reachable mid-session. The
// default is `custom` since stage 6; a value already saved in the config wins over it.
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
    int32_t hudReasserts; // stage 8: times the hidden HUD was put back while the menu was up
    // What the last drawn frame cost, in the units the OVERLAY_DISP budget is denominated in - and
    // zero while the menu is closed, because then the last frame drew nothing.
    // `dlWords` is the heap display list's length - OVERLAY_DISP itself holds only 2048 Gfx words
    // (z64.h:107-112), which the menu no longer spends because it submits ONE gSPDisplayList, but
    // the number is the measurement stage 7 needs and it is free to keep here.
    int32_t drawGlyphs;
    int32_t drawQuads;
    int32_t dlWords;
    int32_t drawIcons; // stage 8: textured quads (icons and the pause-Link composite)
    bool cursorDrawn;  // #124: the cursor box was drawn (never during a roll or a level change)
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
    // The START veto's witness, and the same discipline as `stickFrames`: a veto that took nothing
    // and a veto that was never offered anything leave the menu in identical states. The field
    // names are the stage-1 filter's, kept on purpose (the ADR says so): `filterArmedFrames` counts
    // VB_OPEN_PAUSE_MENU calls on which the menu meant to take START, `startSwallowed` the START
    // edges it vetoed, `filterPressSeen` every press bit the veto has seen - and because the veto
    // runs inside KaleidoSetup_Update, after every hook, a harness-injected press now shows up in
    // it. `kaleido` is `pauseCtx.state`, which says outright whether vanilla pause got in.
    int32_t kaleido;
    int32_t filterArmedFrames;
    int32_t startSwallowed;
    int32_t startConsumed;
    uint32_t filterPressSeen;
};
RsMenuStatus RsMenu_Status();

#endif // SOH_RS_MENU_H
