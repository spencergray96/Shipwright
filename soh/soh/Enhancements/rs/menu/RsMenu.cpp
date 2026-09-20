/*
 * RsMenu.cpp - the mod-owned pause interface (sturdy-bassoon#111), stages 1-5.
 *
 * What it is at this stage: an RS-style scroll - parchment, two roll ends and two blocky hands, all
 * real vertex-coloured geometry - that opens on N64 L, hard-freezes the world, hides the HUD, and
 * SHUTS AND RE-OPENS when a shoulder is pressed. The side matching the shoulder - roll end and hand
 * together - travels horizontally across until its hand meets the other hand's edge; the parchment
 * between them shrinks with it, down to a fifth of its open width, so the scroll is a closed
 * bundle at the midpoint, where the page changes; then the same side travels straight back out.
 * Nobody can navigate a real scroll this way, which is the point: it is one open-and-shut gesture
 * played in whichever direction the ring turns.
 * A cursor graph sits over it whose end nodes are the two hands. All of it is drivable and
 * assertable from the console (MenuConsole.h) with nobody at the keyboard.
 *
 * Stage 4's answer to "how does the geometry get a projection" is in the "THE PROJECTION" block
 * further down and is not re-opened here: its own Vp and guOrtho in the pool (level 3), NOT the
 * own-`View` level 2 research recommended.
 *
 * STAGE 5'S OWN ANSWER, and the thing that shapes this whole file: EVERYTHING THAT MOVES IS Vtx.
 * Stage 4 measured the split and photographed it - geometry under a Matrix_* op frame-interpolates
 * (40 of 60 sampled frames a third of a step off the 20 Hz lattice, the 60-against-20 signature),
 * texture rectangles do not at all (60 of 60 exactly on it). So a scroll whose rolls are Vtx and
 * whose parchment is a gDPFillRectangle and whose text is a gSPTextureRectangle sweeps its rolls
 * away from the paper they are supposed to be holding. The parchment is four vertices and the text
 * is four per glyph, and after this stage the ONLY texture rectangle left in the menu is the
 * probe's reference string, which is a texrect deliberately - see kProbe below.
 *
 * Three consequences of that, each of which shapes the code more than it looks:
 *
 *   - THE DISPLAY LIST IS A HEAP std::vector<Gfx>, submitted as one gSPDisplayList. OVERLAY_DISP
 *     holds 2048 Gfx words (z64.h:107-112) against POLY_OPA's 12224, and a glyph costs about a
 *     dozen; a page of text plus a sweep does not fit. kaleido.cpp:352-356 and nametag.cpp:186 both
 *     do exactly this. The vector is file-static and cleared at the top of each draw, because the
 *     interpreter consumes it after the hook has returned - a local would be gone by then. A
 *     matrix still records from inside one: Matrix_NewMtx is instrumented (sys_matrix.c:563), so
 *     gsSPMatrix(Matrix_NewMtx(...)) pushed into the vector interpolates like any other.
 *   - gSPVertex CAPS AT 64 VERTICES, so glyphs load in groups of 16 (kaleido.cpp:117-122).
 *   - THE Matrix_* OPS ARE EMITTED UNCONDITIONALLY, every frame, in the same order, whatever the
 *     menu is doing. Ops are matched POSITIONALLY inside an interpolation node, so a branch around
 *     a Matrix_* call misaligns everything after it. At rest the sweep's arguments are all zero and
 *     the chain is the identity, which costs three ops and no correctness. (The OTHER warning the
 *     issue carried - that the glyph loop's variable op count would misalign matching - is FALSE
 *     and was corrected at stage 4: the recorder records only Matrix_* calls and child open/close,
 *     so glyph count is invisible to it.)
 *
 * WHAT THE INTERPOLATION NODES ARE KEYED ON, which vanilla does not have an answer for.
 * KaleidoScope_DrawPages wraps its whole page draw in one explicit node keyed on
 * `pauseCtx->state + pauseCtx->pageIndex * 100` (z_kaleido_scope_PAL.c:1456/:1882) - right for a
 * page that changes BETWEEN frames. A scroll's content changes MID-SWEEP, so one key cannot be
 * right for both halves of what is on screen, and this file uses three nodes instead:
 *
 *   - the scroll chrome (parchment + roll ends) on a CONSTANT key, so it interpolates smoothly
 *     through a whole sweep - it is the same geometry before and after the swap;
 *   - the page content on a key carrying the page index, so the tick the content changes finds no
 *     matching node in last tick's tree and renders at its exact tick position instead of lerping
 *     between two different pages' glyphs (frame_interpolation.cpp:300-307 interpolates an
 *     unmatched child against itself, which is exactly "snap, do not smear");
 *   - the hands on a constant key, for the same reason as the chrome.
 *
 * That is why the swap is at the MIDPOINT and why the envelope is a sine: at the midpoint the
 * scroll is a closed bundle and the page is not drawn at all, so there is nothing on screen to
 * smear, and the per-tick motion is smallest
 * there (sin is flat at its peak), so the two ticks the content spends un-interpolated move about a
 * game unit between them instead of a whole step.
 *
 * AT 1.0 THE CONTENT SIMPLY IS NOT DRAWN while the scroll is moving - it blinks out when the gesture
 * starts and the new page blinks in when the paper is open again. 2.0 replaces that with a scissor
 * so the content is revealed and hidden as a horizontal wipe under the moving roll; the note beside
 * the branch in RsMenu_OnPlayDrawEnd says why the scissor has to carve at the sweep's TRAILING edge.
 *
 * The three mechanisms the menu rests on, each of which has a trap attached:
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
 *     straight through the greybox. OVERLAY is last and draws over everything.
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

#include <algorithm>
#include <cmath>
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
// OTRGlobals.h declares this only for C (#ifndef __cplusplus); the definition is extern "C". Same
// forward declaration AgentTest.cpp carries, for the same reason. The dim quad needs it because it
// is the one piece of this menu that spans the whole window rather than the 4:3 band.
float OTRGetAspectRatio(void);
}

#define CVAR_RS_MENU_ON CVAR_ENHANCEMENT("RsMenu")
#define CVAR_RS_MENU_PRIMARY CVAR_ENHANCEMENT("RsMenuPrimary")

// File-static rather than an anonymous namespace, and that is not a style choice. OPEN_DISPS
// expands to a BLOCK-SCOPE redeclaration of FrameInterpolation_RecordOpenChild (macros.h:206-222);
// inside an anonymous namespace MSVC gives that redeclaration C++ linkage instead of picking up
// frame_interpolation.h's extern "C" one, and the call comes out mangled and unresolvable at link
// time. Checked rather than assumed: no file under soh/soh/ uses OPEN_DISPS from inside an
// anonymous namespace except frame_interpolation.cpp, which defines the function and so links
// either way. nametag.cpp and kaleido.cpp have no anonymous namespace at all - whether that was
// this trap or just habit, neither says.

// --- layout ------------------------------------------------------------------------------------
//
// Every number below was measured off docs/notes/2026-09-17-pause-menu-diagrams/scroll-layout-
// options.svg, option A (the settled layout), in OoT's 320x240 authoring space. The SVG's screen
// frame is x=40 y=70 w=280 h=210 for the whole 320x240, so the conversion is
// game = (svg - (40, 70)) / 0.875.
//
// The parchment is inset rather than edge-to-edge on purpose: #111's stage-3 open question asks
// what the live world looks like BEHIND the menu, which an opaque full-screen quad would make
// unanswerable, and the margin is where the roll ends live anyway.
constexpr int16_t kPanelX0 = 16;
constexpr int16_t kPanelY0 = 48;
constexpr int16_t kPanelX1 = 304; // exclusive now that this is a quad, not an inclusive fill rect
constexpr int16_t kPanelY1 = 192;
constexpr int16_t kPanelBorder = 3;
constexpr int16_t kPanelCentreX = (kPanelX0 + kPanelX1) / 2;

constexpr float kTitleScale = 1.5f;
constexpr float kSubScale = 0.8f;
constexpr int16_t kTitleY = 96;
constexpr int16_t kSubY = 130;

// The two roll ends: SVG 45,108,18,134 and 297,108,18,134. Each roll is three COLUMNS wide rather
// than two, so the middle one can carry a lighter vertex colour and the thing reads as a cylinder
// instead of a stripe.
constexpr int32_t kRollCount = 2;
constexpr int32_t kRollColumns = 3; // left edge, lit centre, right edge
constexpr int32_t kRollRows = 2;    // top, bottom
constexpr int32_t kVtxPerRoll = kRollColumns * kRollRows;
constexpr int16_t kRollTopY = 43;
constexpr int16_t kRollBottomY = 196;
constexpr int16_t kRollX[kRollCount][kRollColumns] = { { 6, 16, 26 }, { 294, 304, 314 } };

// The hands: SVG 40,201,40,79 and 280,201,40,79, i.e. game x 0-46 / 274-320, y 150-240. They come
// in from the bottom edge and close over the roll ends, which is option A's "hands low, POV from
// the bottom edge". They are RIGID DISPLAY LISTS UNDER AN ANIMATED MATRIX, never rigged skeletons -
// and each is reached through kHand below rather than inlined, so the equipment-reactive swap
// stages 8-9 want stays a one-line change.
//
// Authored ONCE, for the left hand; the right is this mirrored about x = 160, which is why every
// box is written left-to-right and the mirror is a subtraction rather than a second table.
constexpr int32_t kHandTonePalette = 4;
constexpr u8 kHandTones[kHandTonePalette][3] = {
    { 214, 172, 134 }, // 0 skin
    { 176, 136, 102 }, // 1 skin, shaded (the parts turned away)
    { 236, 198, 164 }, // 2 skin, lit (the fingers catching the light over the roll)
    { 96, 74, 52 },    // 3 the RS-style wrist cuff
};

struct RsHandBox {
    int16_t x0, y0, x1, y1;
    u8 tone;
};

// Five boxes, 20 vertices, 10 triangles per hand. Deliberately blocky - stage 5 is the mechanism,
// not the art.
constexpr RsHandBox kHandLeft[] = {
    { 2, 210, 38, 240, 3 },  // wrist cuff, running off the bottom edge
    { 6, 184, 36, 212, 1 },  // forearm / back of the hand
    { 8, 162, 40, 188, 0 },  // palm
    { 0, 170, 10, 196, 1 },  // thumb, outboard
    { 10, 148, 46, 166, 2 }, // fingers, closed over the roll end
};
constexpr int32_t kHandBoxCount = (int32_t)(sizeof(kHandLeft) / sizeof(kHandLeft[0]));
constexpr int32_t kHandVtxCount = kHandBoxCount * 4;
// The hand's selectable extent, which is what the cursor highlight is drawn around. It is the union
// of the boxes above, held as four numbers rather than recomputed, because it is also the number a
// console line prints and a run asserts on.
constexpr int16_t kHandBoxX = 0;
constexpr int16_t kHandBoxY = 148;
constexpr int16_t kHandBoxW = 46;
constexpr int16_t kHandBoxH = 92;

// The roll's three columns, dark - light - dark, which is what makes it read as a cylinder rather
// than a stripe. The centre column is the only thing the probe recolours: magenta is a colour no
// OoT scene produces, so a pixel scan looking for the roll cannot pick up the world behind it.
constexpr u8 kRollEdgeColour[3] = { 92, 62, 30 };
constexpr u8 kRollLightColour[3] = { 214, 178, 120 };
constexpr u8 kProbeLightColour[3] = { 255, 0, 255 };
// Two more probe channels, for the two things stage 5 exists to stop stepping. The parchment frame
// goes cyan so its edge can be located to the pixel independently of the rolls, and each page's
// glyphs take a colour of their own so a single frame says outright WHICH page is drawn. Both are
// saturated colours no OoT scene produces, the same reason the rolls go magenta - and neither is
// the probe string's green, or a pixel scan could not tell the interpolating channels from the
// reference one. The page colours are indexed modulo their own count, so they do not breach the
// "nothing is sized to the number of pages" invariant.
constexpr u8 kProbeFrameColour[3] = { 0, 255, 255 };
constexpr int32_t kProbePageColourCount = 4;
constexpr u8 kProbePageColours[kProbePageColourCount][3] = {
    { 255, 0, 0 },   // red
    { 255, 128, 0 }, // orange
    { 0, 128, 255 }, // azure
    { 255, 255, 0 }, // yellow
};
constexpr u8 kPanelFrameColour[3] = { 150, 140, 110 };
constexpr u8 kPanelBodyColour[3] = { 46, 42, 38 };
constexpr u8 kCursorColour[3] = { 255, 226, 88 };
constexpr int16_t kCursorOutline = 2; // how thick the highlight's four bars are, in game units

// --- the sweep -----------------------------------------------------------------------------------
//
// Ten game ticks, half a second at 20 Hz. The counter is an INTEGER per-tick one, so every position
// the animation itself can draw is a member of a known eleven-element set and anything else on a
// screenshot came from the renderer - the same argument the stage-4 probe rests on, applied to the
// real animation.
//
// The envelope is sin(pi * t) rather than a triangle, and that is load-bearing rather than
// decorative. The content swap is at the midpoint, and the two ticks straddling it are the ones
// whose interpolation node key changes and which therefore render at their exact tick positions.
// Under a sine the scroll moves ~1 game unit across those two ticks (sin is flat at its peak);
// under a triangle it would move a whole step and the swap would read as a jolt. The midpoint is
// also where the scroll is SHUT, so the swap happens behind closed paper either way.
constexpr int32_t kSweepTicks = 16;
constexpr int32_t kSweepSwapTick = kSweepTicks / 2;

// THE SCROLL SHUTS AND RE-OPENS. It does not swing, tip or pivot - an earlier build did, and it
// read as the whole thing sliding rather than as a scroll being rolled. What happens instead:
//
//   - the side matching the shoulder pressed - roll end AND hand together - travels HORIZONTALLY
//     across to meet the other side, which does not move at all;
//   - the parchment between them shrinks to zero width as it goes, so the scroll is genuinely shut
//     at the midpoint;
//   - then the same side travels straight back out and the parchment re-opens behind it.
//
// Nobody can navigate a real scroll this way, which is the point: it is one open-and-shut gesture
// played in whichever direction the ring is turning, and the page changes while it is shut.
//
// Two consequences that decide how this is drawn:
//
//   - THE PARCHMENT IS NOT RIGID ANY MORE, so it cannot ride a translate. It shrinks under a
//     Matrix_Scale on x about the stationary side's edge, which IS a recorded op
//     (frame_interpolation.cpp Op::MatrixScale) and so still interpolates. Rebuilding its vertices
//     per tick would work and would step at 20 Hz, which is the exact defect stage 5 existed to
//     remove.
//     WARNING for stage 10+: an x-scale STRETCHES a texture rather than rolling it away. While the
//     parchment is a flat colour this is invisible; the moment it has a real texture it will need
//     UV compensation or a different mechanism.
//   - THE REGION THE MOVING SIDE HAS CROSSED HAS NO PARCHMENT. That falls out of scaling about the
//     stationary edge for free: the parchment's moving edge lands exactly on the moving roll's
//     centre at every value of the envelope, so the world shows through behind it.
constexpr float kGripY = 157.0f;                // the middle of the fingers box below
constexpr float kGripX[2] = { 16.0f, 304.0f };  // left roll centre, right roll centre
// How far the moving side travels: until the two hands' inner edges MEET, never past it. The hands
// are wider than the rolls they hold and their fingers overhang inward, so they touch before the
// rolls do - which means the scroll closes to a BUNDLE rather than to a zero-width line, and that
// is correct rather than a compromise. A real rolled scroll is exactly this: two rolls held side by
// side with the remaining paper gathered between them.
//
// What it leaves at full close, worked from the numbers below rather than eyeballed: the hands sit
// at x 0-46 and 46-92, touching; the rolls at x 6-26 and 66-86, forty units apart; and the
// parchment is 60 units wide, 21% of open. That forty-unit strip of paper stays visible between the
// rolls above the hands (game y 48-148) and is the bundle.
//
// Derived from the hand box rather than typed, so re-authoring the hand cannot silently put the two
// hands back on top of each other. Three other travels were on the table and are recorded because
// the choice is a look, not a fact: 268 makes the ROLL edges meet and overlaps the hands by 40, and
// 288 puts the roll centres together and overlaps the hands by 60 - which is what the first build
// did, and it read as one hand rather than two.
constexpr float kSweepTravel = (float)(SCREEN_WIDTH - kHandBoxW) - (float)(kHandBoxX + kHandBoxW);

// The parchment's full span. The scale factor is derived from it so the parchment's moving edge
// lands exactly on the moving roll's centre at every value of the envelope - one identity instead
// of two numbers to keep in step, and the reason the paper never detaches from the roll carrying it.
constexpr float kPanelSpan = (float)(kPanelX1 - kPanelX0);
static_assert(kGripX[0] == (float)kPanelX0 && kGripX[1] == (float)kPanelX1,
              "the roll centres and the parchment edges must coincide, or the paper will detach "
              "from the roll that is supposed to be rolling it up");
// Spelled out rather than reached for: M_PI lives in soh/include/libc/math.h, which this file has
// no direct include of and only reaches transitively.
constexpr float kPi = 3.14159265f;

// --- the entry ------------------------------------------------------------------------------------
//
// The menu is not switched on and off, it ARRIVES: the whole assembly rises from below the bottom
// edge when it opens and drops back down when it closes, and the world dims behind it as it comes.
//
// The rise is one more term in the SAME Matrix_Translate the probe already uses, so it costs no
// extra recorded op and interpolates for free. The dim cannot: alpha is an immediate value in a
// gDPSetPrimColor word and replays identically on every rendered frame, so it steps in
// kEntryTicks discrete levels while the geometry glides. That is the documented split
// (SOH_2D_DRAWING.md) and it is acceptable here only because banding in a fade is far less
// legible than stepping in a motion - it would not be acceptable for the slide itself.
constexpr int32_t kEntryTicks = 8;     // 0.4 s at 20 Hz, each way
constexpr float kEntryDrop = 210.0f;   // enough to put the roll tops (game y 43) below the screen
constexpr u8 kDimAlpha = 140;          // how far the world goes down behind the menu, 0-255
// A few units of overshoot on the dim quad. It spans the whole WINDOW rather than the 4:3 band, and
// overshoot clips for free, so this only has to beat rounding.
constexpr float kDimMargin = 8.0f;

// The interpolation probe's lattice. One game tick moves the whole scroll by kProbeStep, up for ten
// ticks and back down for ten, so every position the 20 Hz animation can produce is a whole
// multiple of kProbeStep from the park position. 4 units is ~17 px on a 1039-high window, far
// enough apart that a pixel scan cannot mistake one lattice point for its neighbour.
constexpr float kProbeStep = 4.0f;
constexpr int32_t kProbeHalfPeriod = 10;
constexpr int32_t kProbePeriod = kProbeHalfPeriod * 2;
constexpr int16_t kProbeTextX = kPanelX0 + 24;
constexpr int16_t kProbeTextY = 134;

// The stick deflection that counts as a cursor press, and the one it has to fall back under before
// another will register. Two thresholds rather than one so a stick resting just past the line does
// not machine-gun the cursor across the graph - the same shape as kaleido's own stickRelX gate.
constexpr int32_t kStickPress = 40;
constexpr int32_t kStickRelease = 18;

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

static std::vector<RsMenuCursorNode>& CursorNodes() {
    static std::vector<RsMenuCursorNode> nodes;
    return nodes;
}

// The menu's whole frame, built on the heap and submitted as one gSPDisplayList. File-static
// because the interpreter consumes it long after OnPlayDrawEnd has returned; cleared at the top of
// every draw, which is what kaleido.cpp and nametag.cpp both do with theirs.
static std::vector<Gfx>& MenuDl() {
    static std::vector<Gfx> dl;
    return dl;
}

// Where the menu is in its arrival, which is NOT the same question as whether it is logically open.
// A run asserting `open=0` after `menu close` must not have to wait out the slide, so the console
// reports both: `open=` is the logical answer and `phase=` is the presentational one.
enum RsMenuPhase {
    RS_MENU_PHASE_CLOSED = 0,
    RS_MENU_PHASE_OPENING,
    RS_MENU_PHASE_OPEN,
    RS_MENU_PHASE_CLOSING,
};
static int32_t sPhase = RS_MENU_PHASE_CLOSED;
static int32_t sEntryTick = 0;
static int32_t sPage = 0;

// "The menu is on screen in some form." Everything that used to test sOpen tests this: the world
// stays frozen and the HUD stays hidden for the whole of both slides, because un-freezing halfway
// down would show the world moving under a menu that is still there.
static bool MenuIsUp() {
    return sPhase != RS_MENU_PHASE_CLOSED;
}

// "The menu is settled and will accept input." Distinct from MenuIsUp on purpose - a shoulder press
// landing mid-slide would start a roll on a scroll that is still arriving.
static bool MenuIsSettled() {
    return sPhase == RS_MENU_PHASE_OPEN;
}

static u8 sHaltPrev = 0;
static bool sHudApplied = false;
static u16 sHudPrev = 0;

// Set by the OnGameStateMainStart filter, consumed by the update. The filter has to run there -
// before Play_Update reaches KaleidoSetup_Update - but the decision belongs with the rest of the
// update logic.
static bool sStartEdge = false;

static bool sBootLineWritten = false;
static bool sGreyboxRegistered = false;

// Diagnostic state, deliberately NOT CVar-backed: `primary` is saved to the owner's
// shipofharkinian.json and a wedged session can leave it there, which has already cost one cleanup
// pass. These reset to the shipping values every launch.
static bool sProbe = false;
static int32_t sProbePhase = 0;

// The sweep. `sSweepDir` survives the sweep that set it so a console line can still say which way
// the last roll went; `sSweepActive` is the one that gates anything.
static bool sSweepActive = false;
static int32_t sSweepTick = 0;
static int32_t sSweepDir = 1;
static int32_t sSweepFrom = 0;
static int32_t sSweepTo = 0;
static int32_t sSweeps = 0;
static bool sSweepLoop = false;
static bool sSweepHold = false;

// The cursor. `sCursorId` rather than the index is the thing that persists: the graph is rebuilt
// every tick, and a page whose item list changes must not silently move the cursor onto a different
// thing that happens to sit at the same index.
static std::string sCursorId = "hand_left";
static int32_t sCursorIndex = 0;
static bool sCursorRebuilding = false;
static int32_t sCursorMoves = 0;
static int32_t sCursorSelects = 0;
static bool sStickLatchX = false;
static bool sStickLatchY = false;

// Set for the duration of one draw. RsMenu_DrawText is a public entry point a page's draw callback
// calls, and this is how it knows there is a frame to append to - null means "not inside a draw",
// which is a no-op rather than a crash.
static GraphicsContext* sDrawGfxCtx = nullptr;
static int32_t sDrawGlyphs = 0;
static int32_t sDrawQuads = 0;
static int32_t sDlWords = 0;

// The three explicit interpolation-node keys. FrameInterpolation's label is {const void*, int}, so
// taking the address of a file-static gives a key that cannot collide with vanilla's (which pass
// NULL) or with OPEN_DISPS's (which passes __FILE__). The int carries the page index on the content
// node and nothing on the other two.
static const char sNodeKeys[4] = { 0, 1, 2, 3 };
static const void* const sNodeScroll = &sNodeKeys[0];
static const void* const sNodeContent = &sNodeKeys[1];
static const void* const sNodeHands = &sNodeKeys[2];
static const void* const sNodeDim = &sNodeKeys[3];

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

// --- the sweep's live numbers --------------------------------------------------------------------

// The excursion envelope: 0 at rest, 1 at the midpoint. See the constants block for why it is a
// sine and not a triangle.
static float SweepEnv() {
    if (!sSweepActive) {
        return 0.0f;
    }
    return std::sin(kPi * (float)sSweepTick / (float)kSweepTicks);
}

// How far the menu has arrived: 0 fully below the screen, 1 settled in place. Eased with a quarter
// sine so it decelerates into position on the way up and accelerates on the way down, which is the
// same curve read forwards and backwards.
static float EntryProgress() {
    switch (sPhase) {
        case RS_MENU_PHASE_CLOSED:
            return 0.0f;
        case RS_MENU_PHASE_OPENING:
            return std::sin(kPi * 0.5f * (float)sEntryTick / (float)kEntryTicks);
        case RS_MENU_PHASE_CLOSING:
            return std::sin(kPi * 0.5f * (1.0f - (float)sEntryTick / (float)kEntryTicks));
        default:
            return 1.0f;
    }
}

// This tick's downward offset, in game units. Folded into the same Matrix_Translate as the probe's,
// so the arrival costs no extra recorded op and interpolates like everything else.
static float EntryDy() {
    return kEntryDrop * (1.0f - EntryProgress());
}

// The dim behind the menu, ramped with the arrival. u8 because it is a prim-colour alpha, which is
// exactly why it steps - see the constants block.
static u8 DimAlpha() {
    return (u8)((float)kDimAlpha * EntryProgress());
}

// Which side of the scroll travels: R (+1) closes with the right hand, L (-1) with the left. A
// "side" is a roll end and the hand holding it, moved as one - which is what makes it impossible
// for a hand to come apart from its roll.
static int32_t SweepMovingHand() {
    return sSweepDir > 0 ? 1 : 0;
}

// The side that does not move. The parchment scales about ITS edge, so it is also the anchor of the
// whole gesture.
static int32_t SweepAnchorSide() {
    return sSweepDir > 0 ? 0 : 1;
}

// How far this side is displaced this tick. Zero for the anchor side on every frame of every sweep;
// for the moving side, the full travel scaled by the envelope, signed toward the anchor.
static float SideDx(int32_t side) {
    if (!sSweepActive || side != SweepMovingHand()) {
        return 0.0f;
    }
    return (side == 0 ? 1.0f : -1.0f) * kSweepTravel * SweepEnv();
}

// How much of the parchment is still showing, as a fraction of its open width: 1 wide open, about
// 0.21 at full close. It bottoms out above zero on purpose - see kSweepTravel. Scaled by the travel
// rather than by the envelope alone, which is what keeps the parchment's moving edge welded to the
// moving roll's centre. Also what the console reports, so a screenshot can be checked against a
// number rather than against an impression.
static float SweepWidth() {
    return 1.0f - SweepEnv() * (kSweepTravel / kPanelSpan);
}

// This tick's probe offset: a triangle wave, 0 up to kProbeHalfPeriod * kProbeStep and back down.
// `sProbePhase` advances only while the menu is open, and its one write site already holds it inside
// [0, kProbePeriod), so no reader reduces it again.
static float ProbeDy() {
    if (!sProbe) {
        return 0.0f;
    }
    const int32_t tri = sProbePhase <= kProbeHalfPeriod ? sProbePhase : kProbePeriod - sProbePhase;
    return (float)tri * kProbeStep;
}

// --- text ----------------------------------------------------------------------------------------
//
// A glyph is a 16x16 I4 texture from the game's own font. Since stage 5 it is FOUR VERTICES AND A
// QUAD under the scroll's matrix rather than a gSPTextureRectangle, because a texrect's coordinates
// are baked into Gfx words and replay identically on every rendered frame - the text would step at
// 20 Hz while the parchment holding it glided. The quad covers the whole 16x16 cell at `size` and
// the pen advances by Ship_GetCharFontWidth, which is exactly what the texrect renderer did, so the
// conversion changes the interpolation behaviour and nothing about the pixels.
//
// It is NOT Interface_DrawTextLine, which multiplies R_TEXT_CHAR_SCALE (XREG(57)) - a register only
// the message system ever writes, so a menu opened before any textbox in the session would scale
// its text by zero and draw nothing.

float RsMenu_TextWidth(const char* text, float scale) {
    float width = 0.0f;
    if (text == nullptr) {
        return 0.0f;
    }
    for (const char* c = text; *c != '\0'; c++) {
        width += Ship_GetCharFontWidth((u8)*c) * scale;
    }
    return width;
}

// One glyph cell, converted from the menu's 320x240 game space (y down from the top-left) into the
// ortho's centred y-up space on the way in, in TL/TR/BL/BR order. Texture coordinates span the
// whole 16x16 cell in 10.5 fixed point.
static void SetGlyphQuad(Vtx* v, int16_t gameX, int16_t gameY, int16_t size) {
    const int16_t ox[4] = { gameX, (int16_t)(gameX + size), gameX, (int16_t)(gameX + size) };
    const int16_t oy[4] = { gameY, gameY, (int16_t)(gameY + size), (int16_t)(gameY + size) };
    const int16_t s[4] = { 0, FONT_CHAR_TEX_WIDTH << 5, 0, FONT_CHAR_TEX_WIDTH << 5 };
    const int16_t t[4] = { 0, 0, FONT_CHAR_TEX_HEIGHT << 5, FONT_CHAR_TEX_HEIGHT << 5 };
    for (int32_t i = 0; i < 4; i++) {
        v[i].v.ob[0] = (int16_t)(ox[i] - SCREEN_WIDTH / 2);
        // Ortho is y-up and centred, screen space is y-down from the top - the same flip View uses.
        v[i].v.ob[1] = (int16_t)(SCREEN_HEIGHT / 2 - oy[i]);
        v[i].v.ob[2] = 0;
        v[i].v.flag = 0;
        v[i].v.tc[0] = s[i];
        v[i].v.tc[1] = t[i];
        v[i].v.cn[0] = 255;
        v[i].v.cn[1] = 255;
        v[i].v.cn[2] = 255;
        v[i].v.cn[3] = 255;
    }
}

void RsMenu_DrawText(const char* text, int16_t x, int16_t y, float scale, uint8_t r, uint8_t g, uint8_t b,
                     uint8_t a) {
    if (sDrawGfxCtx == nullptr || text == nullptr) {
        return;
    }
    // Spaces advance the pen but are not drawn, the same special case every other renderer in the
    // tree makes (z_message_PAL.c:1330, z_parameter.c:6986) - so they cost no vertices either.
    int32_t drawn = 0;
    for (const char* c = text; *c != '\0'; c++) {
        if (*c != ' ') {
            drawn++;
        }
    }
    if (drawn == 0) {
        return;
    }

    std::vector<Gfx>& dl = MenuDl();
    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, (size_t)drawn * 4 * sizeof(Vtx));
    const int16_t size = (int16_t)(FONT_CHAR_TEX_WIDTH * scale);
    float pen = (float)x;
    int32_t i = 0;
    for (const char* c = text; *c != '\0'; c++) {
        const u8 ch = (u8)*c;
        if (ch != ' ') {
            SetGlyphQuad(&vtx[i * 4], (int16_t)pen, y, size);
            i++;
        }
        pen += Ship_GetCharFontWidth(ch) * scale;
    }

    dl.push_back(gsDPSetPrimColor(0, 0, r, g, b, a));
    i = 0;
    for (const char* c = text; *c != '\0'; c++) {
        const u8 ch = (u8)*c;
        if (ch == ' ') {
            continue;
        }
        // gSPVertex caps at 64 vertices, so glyphs load sixteen at a time (kaleido.cpp:117-122).
        if ((i % 16) == 0) {
            const int32_t group = std::min(drawn - i, 16);
            dl.push_back(gsSPVertex(&vtx[i * 4], group * 4, 0));
        }
        const int16_t base = (int16_t)(4 * (i % 16));
        const Gfx charTexture[] = { gsDPLoadTextureBlock_4b(reinterpret_cast<uintptr_t>(Ship_GetCharFontTexture(ch)),
                                                            G_IM_FMT_I, FONT_CHAR_TEX_WIDTH, FONT_CHAR_TEX_HEIGHT, 0,
                                                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                                                            G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD) };
        dl.insert(dl.end(), std::begin(charTexture), std::end(charTexture));
        dl.push_back(gsSP1Quadrangle(base, (int16_t)(base + 2), (int16_t)(base + 3), (int16_t)(base + 1), 0));
        i++;
        sDrawGlyphs++;
    }
}

void RsMenu_DrawTextCentred(const char* text, int16_t centreX, int16_t y, float scale, uint8_t r, uint8_t g, uint8_t b,
                            uint8_t a) {
    RsMenu_DrawText(text, (int16_t)(centreX - RsMenu_TextWidth(text, scale) / 2.0f), y, scale, r, g, b, a);
}

// THE LAST TEXTURE RECTANGLE IN THE MENU, and it is one on purpose. The probe's whole value is that
// it drives one stepped per-tick offset into two channels at once and they disagree by exactly the
// fraction of a tick the renderer is interpolating across. Converting the real text to Vtx (which
// is the substance of stage 5) would have left the probe with two interpolating channels and
// nothing to compare them against, so this renderer stays, drawing the probe string and nothing
// else. It writes straight into OVERLAY_DISP rather than into the heap list, after the list is
// submitted, so it is unambiguously outside every interpolation node the menu opens.
static void DrawProbeTextRect(PlayState* play, const char* text, int16_t x, int16_t y, float scale, u8 r, u8 g, u8 b,
                              u8 a) {
    const int32_t texSize = (int32_t)(FONT_CHAR_TEX_WIDTH * scale);
    const int32_t texScale = (int32_t)(1024.0f / scale);
    float cursor = (float)x;

    OPEN_DISPS(play->state.gfxCtx);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, r, g, b, a);
    for (const char* c = text; *c != '\0'; c++) {
        const u8 ch = (u8)*c;
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

// The body every page gets until it supplies one of its own: the page title, large and centred, and
// its position in the ring underneath. The title is what a screenshot is asserted against and what
// `menu dump` prints, so the two agree by construction.
static void DrawGreyboxBody(int32_t pageIndex, const RsMenuPage& page) {
    // While the probe is on, each page's glyphs carry a colour of their own - so one frame says
    // outright which page is drawn, and a frame carrying TWO of them is a content smear caught in
    // the act rather than inferred.
    const u8* colour = sProbe ? kProbePageColours[pageIndex % kProbePageColourCount] : nullptr;
    const u8 r = colour != nullptr ? colour[0] : 255;
    const u8 g = colour != nullptr ? colour[1] : 255;
    const u8 b = colour != nullptr ? colour[2] : 255;
    RsMenu_DrawTextCentred(page.title.c_str(), kPanelCentreX, kTitleY, kTitleScale, r, g, b, 255);

    char ring[64];
    std::snprintf(ring, sizeof(ring), "%d of %d", pageIndex + 1, RsMenu_PageCount());
    RsMenu_DrawTextCentred(ring, kPanelCentreX, kSubY, kSubScale, r, g, b, 255);
}

// --- the scroll ----------------------------------------------------------------------------------
//
// THE PROJECTION, and why this is not what research § C.1 recommended.
//
// § C.1 names three levels for drawing real 3D over a 2D screen and recommends LEVEL 2: give the
// menu its own `View`, apply it with func_800AAA50(&myView, 127), restore with
// func_800AAA50(&play->view, 15) - the KaleidoScope_Draw pattern
// (z_kaleido_scope_PAL.c:3499-3545). What ships here is LEVEL 3 instead: our own `Vp` and `guOrtho`
// emitted straight into the pool, the pattern this repo already owns in the letterbox code
// (z_rcp.c:1632-1706). Three facts decided it, and all three were MEASURED (2026-09-19 stage-4 run,
// via a `menu view` diagnostic that existed to run the rejected alternative and was deleted at
// stage 5 once it had) rather than only read:
//
//   1. **Level 2 never reaches OVERLAY_DISP.** View_Apply's perspective path (func_800AAA9C,
//      z_view.c:294-470) emits its gSPViewport and its projection gSPMatrix into POLY_OPA_DISP and
//      POLY_XLU_DISP and nowhere else (z_view.c:313-314, :452-455). This menu draws into
//      OVERLAY_DISP, because stage 3 found the panel rendering UNDER the world's translucent
//      geometry in any earlier pool. Applying the bracket with kaleido's own eye (0, 0, 64) - under
//      which 240-unit-tall geometry would draw more than three times too big - produced a frame
//      PIXEL-IDENTICAL to no bracket at all. It is inert in this pool.
//   2. **OVERLAY_DISP already carries a 320x240 ortho, and it is not ours to overwrite.** The
//      reason the bracket case still drew correctly is that Gfx_SetupFrame's letterbox block
//      (z_rcp.c:1680-1690) has already put a gSPViewport and a guOrtho(+-160, +-120) into this pool
//      earlier in the same frame - and that pair is the ONLY projection setup written into
//      OVERLAY_DISP anywhere in the tree. The vanilla HUD's own OVERLAY quads depend on it and set
//      no projection themselves: z_lifemeter.c:578-589 places a heart at (-130 + x, -(-94 + y)),
//      and z_parameter.c:3729 / :4969 / :5777 draw the enemy health bar, the action icon and
//      kaleido's cursor the same way. So this code sets that same ortho EXPLICITLY (the letterbox
//      block is gated on `R_PAUSE_MENU_MODE < 2 && gTrnsnUnkState < 2`, so inheriting it would be a
//      silent dependency on an unrelated feature) and then leaves it - it must NOT hand a
//      screen-space pool the world's perspective on the way out, which an earlier draft did and
//      which would have broken every HUD element drawn after this hook.
//   3. **The level-2 bracket churns the frame-interpolation camera epoch, once per game tick.**
//      func_800AAA9C runs a jump heuristic over a file-static `old_view` (z_view.c:342-405) and
//      calls FrameInterpolation_DontInterpolateCamera() when the eye moves further than its
//      thresholds; a menu View sits hundreds of units from the world camera, so it trips on every
//      call. That sets camera_epoch = previous_camera_epoch + 1
//      (frame_interpolation.cpp:486-488), and the epoch is the KEY of the child node opened at
//      :408, so a key that changes every tick never matches last tick's tree. Measured: `epoch=` on
//      `menu dump` stood still without the bracket and climbed by one per game tick with it, with
//      `pause_mode=0` confirming that the gate which exempts vanilla kaleido (z_view.c:404) is not
//      available to a menu that must never set R_PAUSE_MENU_MODE (z_play.c:1602 turns it into the
//      pause prerender capture). `epoch=` is still reported, because a stage-5 regression there
//      would silently stop the sweep interpolating.
//
// Level 3 touches no View, no camera epoch and no global register. The vertex space is the same
// 320x240 the fill rectangles and glyphs used: guOrtho at +-160 / +-120 with vscale 640/480 is
// what View_Init's own 320x240 viewport produces (z_view.c:12-23, :43-46), and the interpreter puts
// a rect through the identical AdjXForAspectRatio squeeze it puts a vertex through
// (interpreter.cpp:1635, :2896). Geometry that wanted to span the whole WINDOW would still need the
// letterbox's pre-widening; this does not, because the scroll is inside the 4:3 band by
// construction.

static void SetFlatVtx(Vtx* v, int16_t gameX, int16_t gameY, const u8* colour) {
    v->v.ob[0] = (int16_t)(gameX - SCREEN_WIDTH / 2);
    v->v.ob[1] = (int16_t)(SCREEN_HEIGHT / 2 - gameY);
    v->v.ob[2] = 0;
    v->v.flag = 0;
    v->v.tc[0] = v->v.tc[1] = 0;
    v->v.cn[0] = colour[0];
    v->v.cn[1] = colour[1];
    v->v.cn[2] = colour[2];
    v->v.cn[3] = 255;
}

// TL, TR, BL, BR, matching Ship_CreateQuadVertexGroup's order so the quad indices below read the
// same as every other quad in the tree. Not that helper itself: it authors y DOWN and writes
// texture coordinates from the box's own extent, neither of which is what an untextured quad in a
// y-up ortho wants.
static void SetFlatQuad(Vtx* v, int16_t x0, int16_t y0, int16_t x1, int16_t y1, const u8* colour) {
    SetFlatVtx(&v[0], x0, y0, colour);
    SetFlatVtx(&v[1], x1, y0, colour);
    SetFlatVtx(&v[2], x0, y1, colour);
    SetFlatVtx(&v[3], x1, y1, colour);
}

// The dim's own state: a flat primitive colour over XLU, which is the only translucent thing the
// menu draws. Everything else is opaque.
static void PushDimState() {
    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsDPSetCycleType(G_CYC_1CYCLE));
    dl.push_back(gsDPSetRenderMode(G_RM_XLU_SURF, G_RM_XLU_SURF2));
    dl.push_back(gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE));
    dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF));
    dl.push_back(gsSPClearGeometryMode(G_ZBUFFER | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN |
                                       G_TEXTURE_GEN_LINEAR));
    dl.push_back(gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH));
}

// One quad over the whole WINDOW, darkening the frozen world so the menu reads as foreground.
//
// ⚠ It is authored in ORTHO units directly rather than through SetFlatQuad's game-space conversion,
// because it is the one piece of this menu that must span the whole window rather than the 4:3
// band. The renderer multiplies every vertex's post-projection x by (4:3 / window aspect)
// (AdjXForAspectRatio), so a quad authored to +-160 leaves the pillarbox bright on a wide window;
// pre-widening x by the inverse is the documented fix, and overshoot clips for free
// (SOH_2D_DRAWING.md "The widescreen squeeze"). Everything inside the parchment is inside the 4:3
// band by construction and needs none of this - this quad is the exception, not the pattern.
static void DrawDim() {
    const float halfW = (float)(SCREEN_HEIGHT / 2) * OTRGetAspectRatio() + kDimMargin;
    const float halfH = (float)(SCREEN_HEIGHT / 2) + kDimMargin;
    const float ox[4] = { -halfW, halfW, -halfW, halfW };
    const float oy[4] = { halfH, halfH, -halfH, -halfH };

    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, 4 * sizeof(Vtx));
    for (int32_t i = 0; i < 4; i++) {
        vtx[i].v.ob[0] = (int16_t)ox[i];
        vtx[i].v.ob[1] = (int16_t)oy[i];
        vtx[i].v.ob[2] = 0;
        vtx[i].v.flag = 0;
        vtx[i].v.tc[0] = vtx[i].v.tc[1] = 0;
        vtx[i].v.cn[0] = vtx[i].v.cn[1] = vtx[i].v.cn[2] = 0;
        vtx[i].v.cn[3] = 255;
    }

    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsDPSetPrimColor(0, 0, 0, 0, 0, DimAlpha()));
    dl.push_back(gsSPVertex(vtx, 4, 0));
    dl.push_back(gsSP1Quadrangle(0, 2, 3, 1, 0));
    sDrawQuads++;
}

// Flat vertex colour: shade in, shade out. No texture, no lighting, and no Z - the world has
// already written a depth buffer and the menu must never be tested against it.
static void PushFlatState() {
    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsDPSetCycleType(G_CYC_1CYCLE));
    dl.push_back(gsDPSetRenderMode(G_RM_OPA_SURF, G_RM_OPA_SURF2));
    dl.push_back(gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE));
    dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF));
    dl.push_back(gsSPClearGeometryMode(G_ZBUFFER | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN |
                                       G_TEXTURE_GEN_LINEAR));
    dl.push_back(gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH));
}

// SETUPDL_39 (z_rcp.c) verbatim, which is the HUD/text preset: MODULATEIA_PRIM over XLU_SURF, with
// a LoadGeometryMode that clears Z and culling for us. Pushed into the list rather than called as
// Gfx_SetupDL_39Overlay, because the glyphs live in the list and the state has to arrive with them.
static void PushTextState() {
    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
    dl.push_back(gsDPSetCombineMode(G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM));
    dl.push_back(gsDPSetOtherMode(G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE |
                                      G_TL_TILE | G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  G_AC_THRESHOLD | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));
    dl.push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
}

// THE MATRIX CHAIN, and the rule it exists to obey: the SAME three Matrix_* ops are emitted every
// frame, in the same order, whatever the menu is doing. Ops are matched positionally inside an
// interpolation node, so a branch around one misaligns everything after it and kills interpolation
// for the rest of the node (SOH_2D_DRAWING.md). At rest every argument is zero, the pivot cancels
// against itself because the rotation is the identity, and the chain costs three ops and nothing
// else.
//
// Everything is in the ortho's centred, y-up space, so a game-space pivot comes in already
// converted. `dy` is the probe's offset and is folded into the first translate rather than added as
// a fourth op, for the same reason.
static void ApplyBaseMatrix() {
    // The arrival offset and the probe's, in one translate. Emitted first in every chain, so the
    // whole assembly rises and falls as one piece and the probe's four-channel measurement still
    // means what it meant at stage 5.
    Matrix_Translate(0.0f, -(ProbeDy() + EntryDy()), 0.0f, MTXMODE_NEW);
}

// The parchment shrinks toward the side that is NOT moving. Scaling about that edge is what puts
// the parchment's moving edge exactly on the moving roll's centre at every value of the envelope -
// no separate number to keep in step, and no paper left behind the roll.
static void ApplyParchmentMatrix() {
    const float ax = kGripX[SweepAnchorSide()] - (float)(SCREEN_WIDTH / 2);
    ApplyBaseMatrix();
    Matrix_Translate(ax, 0.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(SweepWidth(), 1.0f, 1.0f, MTXMODE_APPLY);
    Matrix_Translate(-ax, 0.0f, 0.0f, MTXMODE_APPLY);
}

// One side of the scroll - its roll end and its hand, welded by sharing this one matrix. Emitted
// for BOTH sides every frame with a zero displacement on the anchor: ops are matched positionally
// inside a node, so the two sides must record the same chain whichever of them is travelling.
static void ApplySideMatrix(int32_t side) {
    ApplyBaseMatrix();
    Matrix_Translate(SideDx(side), 0.0f, 0.0f, MTXMODE_APPLY);
}

// Pushes the matrix the Matrix_* chain above just built. Separate from the chain so the two nodes
// that need the SAME transform (the chrome and the page content) can each emit their own copy -
// they are different interpolation nodes and a node interpolates only what it recorded itself.
static void PushCurrentMatrix() {
    MenuDl().push_back(gsSPMatrix(Matrix_NewMtx(sDrawGfxCtx, (char*)__FILE__, __LINE__),
                                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
}

// The parchment: two quads, a light frame with a dark body. Up to stage 4 this was two
// gDPFillRectangles, which cannot interpolate - so the moment the rolls started sweeping the paper
// they are supposed to be holding would have stepped behind them. Four more vertices, and the
// stage-4 probe photograph of that split stops being reproducible.
static void DrawPanel() {
    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, 8 * sizeof(Vtx));
    SetFlatQuad(&vtx[0], kPanelX0, kPanelY0, kPanelX1, kPanelY1, sProbe ? kProbeFrameColour : kPanelFrameColour);
    SetFlatQuad(&vtx[4], (int16_t)(kPanelX0 + kPanelBorder), (int16_t)(kPanelY0 + kPanelBorder),
                (int16_t)(kPanelX1 - kPanelBorder), (int16_t)(kPanelY1 - kPanelBorder), kPanelBodyColour);

    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsSPVertex(vtx, 8, 0));
    dl.push_back(gsSP1Quadrangle(0, 2, 3, 1, 0));
    dl.push_back(gsSP1Quadrangle(4, 6, 7, 5, 0));
    sDrawQuads += 2;
}

// One roll end: 6 vertices, 4 triangles, a lighter centre column between two darker edges so it
// reads as a cylinder rather than a stripe. Drawn per SIDE rather than both at once, because the
// two sides no longer share a transform - one of them travels and the other does not.
static void DrawRoll(int32_t side) {
    const u8* light = sProbe ? kProbeLightColour : kRollLightColour;
    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, kVtxPerRoll * sizeof(Vtx));
    for (int32_t row = 0; row < kRollRows; row++) {
        for (int32_t col = 0; col < kRollColumns; col++) {
            SetFlatVtx(&vtx[row * kRollColumns + col], kRollX[side][col], row == 0 ? kRollTopY : kRollBottomY,
                       col == 1 ? light : kRollEdgeColour);
        }
    }

    std::vector<Gfx>& dl = MenuDl();
    // Top row is 0..2 left to right, bottom row 3..5: two quads sharing the lit centre column.
    // Winding does not matter here - G_CULL_BOTH is cleared above.
    dl.push_back(gsSPVertex(vtx, kVtxPerRoll, 0));
    dl.push_back(gsSP2Triangles(0, 3, 4, 0, 0, 4, 1, 0));
    dl.push_back(gsSP2Triangles(1, 4, 5, 0, 1, 5, 2, 0));
    sDrawQuads += 2;
}

// One hand, as a rigid display list: five boxes, 20 vertices, 10 triangles. `hand` is 0 for the
// left and 1 for the right, and the right is the left mirrored about x = 160 rather than a second
// table - so re-authoring the hand, or swapping in an equipment-reactive one at stage 9, is one
// table and no second edit.
static void DrawHand(int32_t hand) {
    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, kHandVtxCount * sizeof(Vtx));
    for (int32_t i = 0; i < kHandBoxCount; i++) {
        const RsHandBox& box = kHandLeft[i];
        const int16_t x0 = hand == 0 ? box.x0 : (int16_t)(SCREEN_WIDTH - box.x1);
        const int16_t x1 = hand == 0 ? box.x1 : (int16_t)(SCREEN_WIDTH - box.x0);
        SetFlatQuad(&vtx[i * 4], x0, box.y0, x1, box.y1, kHandTones[box.tone]);
    }

    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsSPVertex(vtx, kHandVtxCount, 0));
    for (int32_t i = 0; i < kHandBoxCount; i++) {
        const u8 b = (u8)(i * 4);
        dl.push_back(gsSP1Quadrangle(b + 0, b + 2, b + 3, b + 1, 0));
        sDrawQuads++;
    }
}

// The cursor highlight: four thin bars around the node's box, drawn under whatever matrix that
// node's geometry is under, so a highlight on a moving hand rides the hand. An outline rather than
// a fill because the hand has to stay visible under it.
static void DrawCursorOutline(const RsMenuCursorNode& node) {
    const int16_t t = kCursorOutline;
    // The node's box is already where it is drawn - hand nodes are mirrored when the node is built,
    // not here, so there is one mirror in the file and not two to drift apart.
    const int16_t x0 = node.x;
    const int16_t x1 = (int16_t)(x0 + node.w);
    const int16_t y0 = node.y;
    const int16_t y1 = (int16_t)(node.y + node.h);

    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, 16 * sizeof(Vtx));
    SetFlatQuad(&vtx[0], x0, y0, x1, (int16_t)(y0 + t), kCursorColour);              // top
    SetFlatQuad(&vtx[4], x0, (int16_t)(y1 - t), x1, y1, kCursorColour);              // bottom
    SetFlatQuad(&vtx[8], x0, y0, (int16_t)(x0 + t), y1, kCursorColour);              // left
    SetFlatQuad(&vtx[12], (int16_t)(x1 - t), y0, x1, y1, kCursorColour);             // right

    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsSPVertex(vtx, 16, 0));
    for (int32_t i = 0; i < 4; i++) {
        const u8 b = (u8)(i * 4);
        dl.push_back(gsSP1Quadrangle(b + 0, b + 2, b + 3, b + 1, 0));
        sDrawQuads++;
    }
}

// The menu's own viewport and projection, pushed into the list ahead of everything else. Level 3,
// per the block above: SCREEN_WIDTH / SCREEN_HEIGHT rather than gScreenWidth / gScreenHeight, and
// the difference is deliberate even though the letterbox block this copies uses the globals. These
// vertices are authored in a FIXED 320x240 space; the two are equal here (main.c:14 and :85
// initialise the globals to these constants and only z_vimode.c:236 ever moves them), so naming the
// authoring constants says which space the vertices are in rather than changing the numbers.
static void PushViewportAndOrtho() {
    Mtx* ortho = (Mtx*)Graph_Alloc(sDrawGfxCtx, sizeof(Mtx));
    Vp* vp = (Vp*)Graph_Alloc(sDrawGfxCtx, sizeof(Vp));
    vp->vp.vscale[0] = SCREEN_WIDTH * 2;
    vp->vp.vscale[1] = SCREEN_HEIGHT * 2;
    vp->vp.vscale[2] = G_MAXZ / 2;
    vp->vp.vscale[3] = 0;
    vp->vp.vtrans[0] = SCREEN_WIDTH * 2;
    vp->vp.vtrans[1] = SCREEN_HEIGHT * 2;
    vp->vp.vtrans[2] = G_MAXZ / 2;
    vp->vp.vtrans[3] = 0;
    guOrtho(ortho, -(f32)(SCREEN_WIDTH / 2), (f32)(SCREEN_WIDTH / 2), -(f32)(SCREEN_HEIGHT / 2),
            (f32)(SCREEN_HEIGHT / 2), -1.0f, 1.0f, 1.0f);

    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsSPViewport(vp));
    dl.push_back(gsSPMatrix(ortho, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
}

// --- the cursor graph ----------------------------------------------------------------------------

int32_t RsMenu_AddCursorNode(const char* id, int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!sCursorRebuilding || id == nullptr || *id == '\0') {
        return -1;
    }
    RsMenuCursorNode node;
    node.id = id;
    node.x = x;
    node.y = y;
    node.w = w;
    node.h = h;
    node.left = node.right = node.up = node.down = -1;
    node.hand = -1;
    CursorNodes().push_back(node);
    return (int32_t)CursorNodes().size() - 1;
}

static void AddHandNode(int32_t hand) {
    RsMenuCursorNode node;
    node.id = hand == 0 ? "hand_left" : "hand_right";
    // Mirrored HERE rather than at draw time, so `box=` on a console line is where the highlight
    // actually is. The first build stored the left hand's authored box on both nodes and a run
    // asserting the right hand's position would have read the left one's.
    node.x = hand == 0 ? kHandBoxX : (int16_t)(SCREEN_WIDTH - (kHandBoxX + kHandBoxW));
    node.y = kHandBoxY;
    node.w = kHandBoxW;
    node.h = kHandBoxH;
    node.left = node.right = node.up = node.down = -1;
    node.hand = hand;
    CursorNodes().push_back(node);
}

// Rebuilt every update tick, cheaply, because the alternative is an invalidation rule and there is
// nothing here expensive enough to earn one. [left hand] + [the visible page's items] + [right
// hand], wired left-to-right in that order.
//
// ADJACENCY IS NOT DERIVED FROM POSITION. Nothing in this function looks at x or y. That is the
// settled design's claim and the reason option A's full-width parchment costs nothing: the leftmost
// item's left neighbour IS the left hand, wherever either is drawn.
static void RebuildCursorGraph() {
    std::vector<RsMenuCursorNode>& nodes = CursorNodes();
    nodes.clear();

    sCursorRebuilding = true;
    AddHandNode(0);
    const RsMenuPage* page = RsMenu_PageAt(sPage);
    if (page != nullptr && page->nodes != nullptr) {
        page->nodes(sPage, page->userData);
    }
    AddHandNode(1);
    sCursorRebuilding = false;

    const int32_t count = (int32_t)nodes.size();
    for (int32_t i = 0; i < count; i++) {
        nodes[(size_t)i].left = i > 0 ? i - 1 : -1;
        nodes[(size_t)i].right = i + 1 < count ? i + 1 : -1;
        nodes[(size_t)i].up = -1;
        nodes[(size_t)i].down = -1;
    }

    // The cursor follows its ID, not its index. A page change rebuilds the whole list, and landing
    // on "whatever is at index 3 now" would be a different thing every page.
    int32_t found = -1;
    for (int32_t i = 0; i < count; i++) {
        if (nodes[(size_t)i].id == sCursorId) {
            found = i;
            break;
        }
    }
    if (found >= 0) {
        sCursorIndex = found;
    } else if (sCursorIndex >= count) {
        sCursorIndex = count - 1;
    }
    if (sCursorIndex < 0) {
        sCursorIndex = 0;
    }
    if (count > 0) {
        sCursorId = nodes[(size_t)sCursorIndex].id;
    }
}

int32_t RsMenu_CursorCount() {
    return (int32_t)CursorNodes().size();
}

const RsMenuCursorNode* RsMenu_CursorAt(int32_t index) {
    const std::vector<RsMenuCursorNode>& nodes = CursorNodes();
    if (index < 0 || index >= (int32_t)nodes.size()) {
        return nullptr;
    }
    return &nodes[(size_t)index];
}

int32_t RsMenu_CursorIndex() {
    return sCursorIndex;
}

bool RsMenu_MoveCursor(int32_t dx, int32_t dy) {
    const RsMenuCursorNode* node = RsMenu_CursorAt(sCursorIndex);
    if (node == nullptr) {
        return false;
    }
    int32_t next = -1;
    if (dx < 0) {
        next = node->left;
    } else if (dx > 0) {
        next = node->right;
    } else if (dy < 0) {
        next = node->up;
    } else if (dy > 0) {
        next = node->down;
    }
    if (next < 0 || next >= RsMenu_CursorCount()) {
        return false;
    }
    sCursorIndex = next;
    sCursorId = CursorNodes()[(size_t)next].id;
    sCursorMoves++;
    return true;
}

bool RsMenu_SetCursorById(const char* id) {
    if (id == nullptr) {
        return false;
    }
    const std::vector<RsMenuCursorNode>& nodes = CursorNodes();
    for (int32_t i = 0; i < (int32_t)nodes.size(); i++) {
        if (nodes[(size_t)i].id == id) {
            sCursorIndex = i;
            sCursorId = nodes[(size_t)i].id;
            sCursorMoves++;
            return true;
        }
    }
    return false;
}

const char* RsMenu_SelectResultName(RsMenuSelectResult result) {
    switch (result) {
        case RS_MENU_SELECT_NONE:
            return "none";
        case RS_MENU_SELECT_HAND_LEFT:
            return "hand_left";
        case RS_MENU_SELECT_HAND_RIGHT:
            return "hand_right";
        case RS_MENU_SELECT_BUSY:
            return "sweeping";
        case RS_MENU_SELECT_ITEM:
            return "item";
    }
    return "unknown";
}

RsMenuSelectResult RsMenu_SelectCursor() {
    const RsMenuCursorNode* node = RsMenu_CursorAt(sCursorIndex);
    if (node == nullptr) {
        return RS_MENU_SELECT_NONE;
    }
    if (node->hand < 0) {
        // Stage 6's detail views are what an item leads to; there is nothing behind one yet.
        return RS_MENU_SELECT_ITEM;
    }
    sCursorSelects++;
    // "Selecting a hand does what L/R does" - the settled design, verbatim. The left hand rolls
    // back, the right rolls forward, which is the same mapping the shoulders have.
    if (!RsMenu_StartSweep(node->hand == 0 ? -1 : 1)) {
        return RS_MENU_SELECT_BUSY;
    }
    return node->hand == 0 ? RS_MENU_SELECT_HAND_LEFT : RS_MENU_SELECT_HAND_RIGHT;
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
// struct before any hook runs, so it is always filtered. Spencer's call (2026-09-19,
// docs/decisions/2026-09-19-pause-menu-start-veto.md) is that the order-independent replacement -
// a VB_OPEN_PAUSE_MENU wrapped around the bare CHECK_BTN_ALL in z_kaleido_setup.c:26 - lands at
// stage 6, when `primary` flips its default to `custom`. Until then this filter is inert, because
// `primary` is `vanilla`.
static void RsMenu_OnGameStateMainStart() {
    if (!InNormalPlay()) {
        return;
    }
    PlayState* play = gPlayState;
    if (VanillaPauseIsUp(play)) {
        return;
    }
    const bool swallow = MenuIsUp() || (RsMenu_IsEnabled() && RsMenu_GetPrimary() == RS_MENU_PRIMARY_CUSTOM);
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

// One game tick of the roll. The content swap is at the MIDPOINT, where the excursion peaks: that
// is where the scroll is furthest from rest, where the sine's per-tick motion is smallest, and -
// because the content node's interpolation key carries the page index - where the two ticks that
// cannot interpolate cost about one game unit of motion instead of a whole step.
static void AdvanceSweep() {
    if (!sSweepActive || sSweepHold) {
        return;
    }
    sSweepTick++;
    if (sSweepTick == kSweepSwapTick) {
        sPage = sSweepTo;
        sPageChanges++;
    }
    if (sSweepTick >= kSweepTicks) {
        sSweepActive = false;
        sSweepTick = 0;
        if (sSweepLoop) {
            // Alternating rather than always forward, so the loop also exercises both pivots and
            // both hands - and so the content swaps between the SAME two pages every time, which is
            // what makes "a frame showing two pages' glyphs" a detectable event rather than a
            // needle in a four-page haystack.
            RsMenu_StartSweep(-sSweepDir);
        }
    }
}

// The cursor's own input, kept apart from the page ring's because they answer different buttons:
// the shoulders roll the scroll directly, the D-pad and the stick walk the graph, and A selects.
// The stick needs an edge of its own - a held stick would otherwise walk the cursor once per tick.
static void UpdateCursorInput(const Input* input) {
    const u16 press = input->press.button;
    if (CHECK_BTN_ALL(press, BTN_DLEFT)) {
        RsMenu_MoveCursor(-1, 0);
    } else if (CHECK_BTN_ALL(press, BTN_DRIGHT)) {
        RsMenu_MoveCursor(1, 0);
    } else if (CHECK_BTN_ALL(press, BTN_DUP)) {
        RsMenu_MoveCursor(0, -1);
    } else if (CHECK_BTN_ALL(press, BTN_DDOWN)) {
        RsMenu_MoveCursor(0, 1);
    }

    const int32_t sx = input->cur.stick_x;
    const int32_t sy = input->cur.stick_y;
    if (!sStickLatchX && (sx > kStickPress || sx < -kStickPress)) {
        RsMenu_MoveCursor(sx > 0 ? 1 : -1, 0);
        sStickLatchX = true;
    } else if (sStickLatchX && sx < kStickRelease && sx > -kStickRelease) {
        sStickLatchX = false;
    }
    // The stick is y-UP on this pad (forward is positive), and the graph's `up` is the screen's, so
    // the sign flips here rather than at every call site.
    if (!sStickLatchY && (sy > kStickPress || sy < -kStickPress)) {
        RsMenu_MoveCursor(0, sy > 0 ? -1 : 1);
        sStickLatchY = true;
    } else if (sStickLatchY && sy < kStickRelease && sy > -kStickRelease) {
        sStickLatchY = false;
    }

    if (CHECK_BTN_ALL(press, BTN_A)) {
        RsMenu_SelectCursor();
    }
}

static void RsMenu_OnGameFrameUpdate() {
    WriteBootLineOnce();

    if (!InNormalPlay()) {
        if (MenuIsUp()) {
            // No PlayState to restore anything on. Drop the state rather than reaching through a
            // null pointer; the HUD belongs to the save context and the next scene re-establishes it.
            sPhase = RS_MENU_PHASE_CLOSED;
            sEntryTick = 0;
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
        if (MenuIsUp()) {
            RsMenu_Close();
        }
        return;
    }

    Input* input = &play->state.input[0];

    if (MenuIsUp()) {
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
        // The probe's clock is the GAME tick, which is the whole point: it is the 20 Hz lattice the
        // rendered frames are measured against. Advanced whether or not the probe is on, so
        // switching it on does not start from a stale phase.
        sProbePhase = (sProbePhase + 1) % kProbePeriod;
        RebuildCursorGraph();
        RecordOfferedInput(input);

        // The arrival, driven off the same game tick as everything else. The world stays frozen and
        // the HUD stays hidden for the whole of both slides - un-freezing halfway down would show
        // the world moving under a menu that is still on screen.
        if (sPhase == RS_MENU_PHASE_OPENING) {
            if (++sEntryTick >= kEntryTicks) {
                sPhase = RS_MENU_PHASE_OPEN;
                sEntryTick = 0;
            }
            return;
        }
        if (sPhase == RS_MENU_PHASE_CLOSING) {
            if (++sEntryTick >= kEntryTicks) {
                // The instant close does the restoring; the slide only decides WHEN.
                RsMenu_Close();
            }
            return;
        }

        AdvanceSweep();

        if (startEdge || CHECK_BTN_ALL(input->press.button, BTN_B)) {
            if (startEdge) {
                sStartConsumed++;
            }
            RsMenu_BeginClose();
            return;
        }
        // The shoulders roll the scroll. A press arriving mid-sweep is dropped rather than queued:
        // a queue would let a run assert a page the animation never actually rolled to.
        if (CHECK_BTN_ALL(input->press.button, BTN_L)) {
            RsMenu_StartSweep(-1);
        } else if (CHECK_BTN_ALL(input->press.button, BTN_R)) {
            RsMenu_StartSweep(1);
        }
        UpdateCursorInput(input);
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

// The whole menu, as ONE heap display list submitted with one gSPDisplayList, plus the probe's
// reference texrect afterwards. Three explicit interpolation nodes, and which is which is the one
// design decision in this function - see the file header.
static void RsMenu_OnPlayDrawEnd() {
    if (!MenuIsUp()) {
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

    GraphicsContext* gfxCtx = play->state.gfxCtx;
    sDrawGfxCtx = gfxCtx;
    sDrawGlyphs = 0;
    sDrawQuads = 0;

    std::vector<Gfx>& dl = MenuDl();
    dl.clear();

    OPEN_DISPS(gfxCtx);

    PushViewportAndOrtho();

    // --- the dim, on a constant key, first so everything else sits on top of it -------------------
    // It does NOT ride the arrival matrix: the menu slides up over a world that fades down, rather
    // than a dark rectangle sliding up with it. Its one Matrix_ op is an identity, emitted so the
    // node records the same single op every frame and so nothing inherits a stale modelview.
    FrameInterpolation_RecordOpenChild(sNodeDim, 0);
    PushDimState();
    Matrix_Push();
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    PushCurrentMatrix();
    DrawDim();
    Matrix_Pop();
    FrameInterpolation_RecordCloseChild();

    // --- the parchment, on a CONSTANT key so it interpolates through a whole sweep ---------------
    // Drawn FIRST and alone: it is the backdrop the content sits on and the moving side passes over,
    // and it is the only piece whose transform is a scale rather than a translate.
    FrameInterpolation_RecordOpenChild(sNodeScroll, 0);
    PushFlatState();
    Matrix_Push();
    ApplyParchmentMatrix();
    PushCurrentMatrix();
    DrawPanel();
    Matrix_Pop();
    FrameInterpolation_RecordCloseChild();

    // --- the page content, on a key CARRYING THE PAGE INDEX --------------------------------------
    // The tick the content changes, this key no longer matches last tick's tree, so the node
    // interpolates against itself and renders at its exact tick position instead of lerping between
    // two different pages' glyphs (frame_interpolation.cpp:300-307). That is the whole anti-smear
    // mechanism, and it is the case vanilla's single `state + pageIndex * 100` node does not have -
    // vanilla's content changes BETWEEN frames, this one changes MID-SWEEP.
    FrameInterpolation_RecordOpenChild(sNodeContent, sPage);
    PushTextState();
    Matrix_Push();
    ApplyBaseMatrix();
    PushCurrentMatrix();
    // 1.0 OF THE CLOSE: the content is simply absent while the scroll is moving. It blinks out the
    // tick the gesture starts and the new page blinks in when the paper is open again, because
    // there is no paper under it in between.
    //
    // ⚠ This is a branch around GEOMETRY, never around a Matrix_* op - the chain above is emitted
    // on every frame whatever this decides. Glyph count is invisible to the interpolation recorder,
    // which records only Matrix_* calls and child open/close (SOH_2D_DRAWING.md, measured at stage
    // 4 after the opposite was written down first).
    //
    // 2.0 replaces this with a scissor: reveal the content up to the moving roll's trailing edge,
    // so it wipes rather than blinks. The trailing edge matters - a scissor is an immediate value
    // and steps at 20 Hz while the roll glides, so carving at the smaller of last tick's and this
    // tick's edge keeps the roll overdrawing the seam instead of uncovering it. Same problem and
    // same answer as the letterbox's ShrinkWindow_GetSafeVal.
    if (SweepWidth() > 0.999f) {
        if (page->draw != nullptr) {
            page->draw(play, sPage, page->userData);
        } else {
            DrawGreyboxBody(sPage, *page);
        }
    }
    {
        const RsMenuCursorNode* node = RsMenu_CursorAt(sCursorIndex);
        if (node != nullptr && node->hand < 0) {
            PushFlatState();
            DrawCursorOutline(*node);
        }
    }
    Matrix_Pop();
    FrameInterpolation_RecordCloseChild();

    // --- the two sides, on a constant key --------------------------------------------------------
    // A side is a roll end and the hand holding it, under ONE matrix, which is what makes it
    // impossible for a hand to come apart from its roll. Both sides always emit the same chain;
    // only the displacement differs, which is how "the side matching the shoulder travels" stays a
    // change of numbers rather than a branch around a Matrix_* op. Drawn LAST, so the moving side
    // passes in front of the parchment and the content the way a rolled-up edge would.
    FrameInterpolation_RecordOpenChild(sNodeHands, 0);
    PushFlatState();
    for (int32_t side = 0; side < 2; side++) {
        Matrix_Push();
        ApplySideMatrix(side);
        PushCurrentMatrix();
        DrawRoll(side);
        DrawHand(side);
        const RsMenuCursorNode* node = RsMenu_CursorAt(sCursorIndex);
        if (node != nullptr && node->hand == side) {
            DrawCursorOutline(*node);
        }
        Matrix_Pop();
    }
    FrameInterpolation_RecordCloseChild();

    dl.push_back(gsSPEndDisplayList());
    sDlWords = (int32_t)dl.size();
    gSPDisplayList(OVERLAY_DISP++, dl.data());
    gDPPipeSync(OVERLAY_DISP++);
    // No viewport/projection restore into OVERLAY_DISP, deliberately. Point 2 of THE PROJECTION:
    // this pool is screen space from end to end, the ortho set here is numerically the one the
    // letterbox left, and the HUD elements Play_DrawOverlayElements draws after this hook
    // (z_play.c:1648) set no projection of their own. Handing them the world's perspective is
    // exactly how to break them.

    CLOSE_DISPS(gfxCtx);

    if (sProbe) {
        // The reference channel: the same per-tick dy as the geometry above, carried in a texture
        // rectangle's baked y instead of in a matrix. In one frame the two therefore read the split
        // directly - they part company by exactly the fraction of a tick the renderer is
        // interpolating across. Drawn after the list is submitted, so it is outside every
        // interpolation node the menu opens.
        Gfx_SetupDL_39Overlay(gfxCtx);
        char probe[48];
        // Both lattices in the string, so a capture is self-describing on both axes without
        // correlating timestamps: `phase` is the vertical bob's 20-tick counter and `S<n>` the
        // sweep's own tick. The string itself is the non-interpolating reference channel.
        std::snprintf(probe, sizeof(probe), "PROBE %d S%d", sProbePhase, sSweepTick);
        DrawProbeTextRect(play, probe, kProbeTextX, (int16_t)(kProbeTextY + ProbeDy()), 1.0f, 0, 255, 0, 255);
    }

    sDrawGfxCtx = nullptr;
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
        // No cursor nodes: a greybox page has nothing to select, so the live graph is exactly the
        // two hands and the "empty middle" the design describes is literal at this stage.
        RsMenu_RegisterPage(id, title, nullptr, nullptr, nullptr);
    }
}

static void RegisterRsMenu() {
    RegisterGreyboxPages();
    // So `menu cursor` answers before the menu has ever been opened. The graph reads no PlayState,
    // so it is safe this early.
    RebuildCursorGraph();
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

int32_t RsMenu_RegisterPage(const char* id, const char* title, RsMenuPageDrawFn draw, RsMenuPageNodesFn nodes,
                            void* userData) {
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
    page.nodes = nodes;
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
    return MenuIsUp();
}

int32_t RsMenu_Phase() {
    return sPhase;
}

const char* RsMenu_PhaseName(int32_t phase) {
    switch (phase) {
        case RS_MENU_PHASE_CLOSED:
            return "closed";
        case RS_MENU_PHASE_OPENING:
            return "opening";
        case RS_MENU_PHASE_OPEN:
            return "open";
        case RS_MENU_PHASE_CLOSING:
            return "closing";
    }
    return "unknown";
}

int32_t RsMenu_CurrentPage() {
    return sPage;
}

RsMenuOpenResult RsMenu_Open() {
    if (!RsMenu_IsEnabled()) {
        return RS_MENU_OPEN_DISABLED;
    }
    if (MenuIsUp()) {
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

    // A menu that opens mid-sweep would draw a scroll frozen off-centre. Opening is the one place
    // the animation is reset rather than played out.
    sSweepActive = false;
    sSweepTick = 0;
    sStickLatchX = false;
    sStickLatchY = false;
    RebuildCursorGraph();

    sPhase = RS_MENU_PHASE_OPENING;
    sEntryTick = 0;
    sOpens++;
    return RS_MENU_OPEN_OK;
}

bool RsMenu_BeginClose() {
    if (!MenuIsUp()) {
        return false;
    }
    if (sPhase == RS_MENU_PHASE_CLOSING) {
        return true; // already on its way down; do not restart the slide
    }
    // Reversing out of a half-finished arrival picks up where it got to rather than snapping to the
    // top first, which is one line and the difference between a slide and a jerk.
    sEntryTick = sPhase == RS_MENU_PHASE_OPENING ? kEntryTicks - sEntryTick : 0;
    sPhase = RS_MENU_PHASE_CLOSING;
    return true;
}

bool RsMenu_Close() {
    const bool wasOpen = MenuIsUp();
    sPhase = RS_MENU_PHASE_CLOSED;
    sEntryTick = 0;
    if (!wasOpen) {
        // Closing a closed menu is a genuine no-op, and it has to be: this runs from OnSceneInit on
        // EVERY scene load and from the CVar-off path, and `haltAllActors` is the ENGINE's flag -
        // vanilla sets it for cutscenes (z_demo.c:402/405), Sun's Song (z_parameter.c:6900/6905)
        // and the void-out (z_player.c:4766). Writing a stale sHaltPrev over one of those would
        // un-freeze a freeze that was never ours to hold.
        return false;
    }
    sCloses++;
    // A sweep interrupted by a close lands on whichever page it had already swapped to, which is
    // the page `menu dump` has been reporting since the midpoint. Nothing half-turned survives -
    // and a loop left running would restart the animation the moment the menu reopened.
    sSweepActive = false;
    sSweepTick = 0;
    sSweepLoop = false;
    sSweepHold = false;
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
    // An instant page change abandons any sweep in flight rather than letting it swap again on a
    // tick the caller cannot see, and stops a loop that would immediately swap it away again.
    sSweepActive = false;
    sSweepTick = 0;
    sSweepLoop = false;
    sSweepHold = false;
    // The graph belongs to the visible page, and `menu page` works while the menu is CLOSED - so
    // without this a cursor line read after a closed page change would describe the previous page.
    RebuildCursorGraph();
    return true;
}

bool RsMenu_StartSweep(int32_t delta) {
    if (sSweepActive) {
        return false;
    }
    const int32_t count = RsMenu_PageCount();
    if (count < 2 || delta == 0) {
        return false;
    }
    const int32_t dir = delta > 0 ? 1 : -1;
    int32_t next = (sPage + dir) % count;
    if (next < 0) {
        next += count;
    }
    sSweepActive = true;
    sSweepTick = 0;
    sSweepDir = dir;
    sSweepFrom = sPage;
    sSweepTo = next;
    sSweeps++;
    return true;
}

bool RsMenu_HoldSweep(int32_t delta, int32_t tick) {
    if (tick < 0 || tick > kSweepTicks) {
        return false;
    }
    sSweepLoop = false;
    sSweepHold = false;
    sSweepActive = false;
    sSweepTick = 0;
    if (!RsMenu_StartSweep(delta)) {
        return false;
    }
    // Played forward rather than assigned, so a held frame is one the animation really produces -
    // including the content swap, which happens on the way past the midpoint and not as a separate
    // rule a hold could get wrong.
    for (int32_t i = 0; i < tick; i++) {
        AdvanceSweep();
    }
    sSweepHold = true;
    sSweepActive = true;
    sSweepTick = tick;
    return true;
}

bool RsMenu_StartSweepLoop() {
    sSweepHold = false;
    // Started by kicking the first sweep, so a loop that cannot sweep at all (a one-page ring)
    // refuses here rather than spinning silently.
    if (!sSweepActive && !RsMenu_StartSweep(sSweepDir)) {
        return false;
    }
    sSweepLoop = true;
    return true;
}

void RsMenu_StopSweepLoop() {
    sSweepLoop = false;
    sSweepHold = false;
    sSweepActive = false;
    sSweepTick = 0;
}

RsMenuSweepState RsMenu_SweepState() {
    RsMenuSweepState state;
    const float env = SweepEnv();
    state.active = sSweepActive;
    state.tick = sSweepTick;
    state.ticks = kSweepTicks;
    state.dir = sSweepDir;
    state.fromPage = sSweepFrom;
    state.toPage = sSweepTo;
    state.movingHand = SweepMovingHand();
    state.env = env;
    state.dx = SideDx(SweepMovingHand());
    state.width = SweepWidth();
    state.sweeps = sSweeps;
    state.loop = sSweepLoop;
    state.hold = sSweepHold;
    return state;
}

void RsMenu_SetProbe(bool on) {
    sProbe = on;
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
    status.open = MenuIsUp();
    status.phase = sPhase;
    status.entryTick = sEntryTick;
    status.entryTicks = kEntryTicks;
    status.entryProgress = EntryProgress();
    status.dimAlpha = (int32_t)DimAlpha();
    status.page = sPage;
    status.pages = RsMenu_PageCount();
    status.primary = RsMenu_GetPrimary();
    status.probe = sProbe;
    status.probePhase = sProbePhase;
    status.probeStep = kProbeStep;
    status.probeDy = ProbeDy();
    // Kept from stage 4, where it settled the projection question. It must stand still while the
    // menu is open: anything that bumps it once per game tick has switched interpolation off for
    // everything under the camera node, and a stage-5 regression there would stop the sweep
    // interpolating with no other symptom. `pauseMenuMode` is the one gate that would suppress the
    // bump (z_view.c:404) - vanilla kaleido's exemption, which this menu cannot use because
    // Play_Draw turns that register into the pause prerender capture.
    status.cameraEpoch = FrameInterpolation_GetCameraEpoch();
    status.pauseMenuMode = (int32_t)R_PAUSE_MENU_MODE;
    status.halt = gPlayState != nullptr && gPlayState->haltAllActors != 0;
    status.haltPrev = sHaltPrev != 0;
    status.hudHidden = sHudApplied;
    status.hudPrev = (int32_t)sHudPrev;
    status.hudNow = (int32_t)gSaveContext.hudVisibilityMode;
    status.drawGlyphs = sDrawGlyphs;
    status.drawQuads = sDrawQuads;
    status.dlWords = sDlWords;
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
