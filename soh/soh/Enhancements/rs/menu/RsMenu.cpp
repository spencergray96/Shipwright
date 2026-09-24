/*
 * RsMenu.cpp - the mod-owned pause interface (sturdy-bassoon#111), stages 1-8.
 *
 * #128 adds the page stepper: a row of stones, one per page, in the HUD's gap between the hearts and
 * START (DrawStepper). It is drawn in the dim's node under the dim's identity matrix, so it adds no
 * Matrix_* op and no node.
 *
 * #130 raises the vanilla HUD (cosmetics defaults, RegisterRsHudDefaults), drops the whole scroll 3 units
 * to clear the magic meter, and stands the level-1 hands upright with the HUD cut to a moved B. No
 * Matrix_* op is added or made conditional: the drop is a term in the base matrix's one translate, the
 * upright pose is different arguments to the same swivel ops, and the hands' extents for `menu dump`
 * are read with Matrix_MultVec3f, which records nothing. At level 1, B also reads "Return".
 *
 * Spencer's 2026-09-23 play test: C-up no longer flips a house's camera under the menu
 * (VB_TOGGLE_HOUSE_VIEWPOINT), and a page with no `hud` callback - the Quest Journal - dims every C
 * button, as vanilla does on a page with nothing to equip. Neither touches a Matrix_* op.
 *
 * #127 lets a page run state of its own (RsMenu.h: `tick`, `hold`, `reset`) - the Quest Status page's song
 * playback - and hold back the menu's own reactions to START, B, A, L/R and the cursor while it does.
 * Input only; no Matrix_* op changes.
 *
 * #125 keeps the gameplay HUD up the way vanilla pause shows it - the page's buttons live or dimmed,
 * B "Save", A "Decide", the START button, the minimap hidden - and flies an equipped icon to its button
 * (EquipFlight.cpp), drawn over the HUD from a hook inside Interface_Draw. Three VB_ entries and one hook
 * in the engine carry it; none of it touches a Matrix_* op in a menu node.
 *
 * #126 gives the ported pages kaleido's stick (RsMenuStickModel, RsMenu.h): kaleido's hold-to-repeat
 * filter on `input->rel` and each page's own way of resolving a diagonal, where the journal keeps the
 * one-step latch. Input only - no Matrix_* op changes.
 *
 * STAGE 8 ports three vanilla pause pages onto the scroll (VanillaPages.h) and replaces the greybox
 * pages with them: the ring is the Quest Journal, Items, Equipment and Quest Status. What it adds here
 * is plumbing, and none of it adds or skips a Matrix_* op inside a menu node: RsMenu_DrawIcon (a
 * textured Vtx quad beside the glyphs), RsMenu_DrawPauseLink (a quad textured from the pause-Link
 * framebuffer, plus the request that renders Link into it - PauseLink.cpp, called after every menu
 * node has closed, inside its own OPEN_DISPS node), a GRID cursor mode whose edges the page wires
 * from kaleido's rules (RsMenu.h), and a page `claim` callback so a page can take the D-pad for equipping.
 *
 * STAGE 7 is instruments only: `section=view` counters for what the last frame's view drew, a
 * test-only stress body that fills the horizontal page to a chosen glyph count (the console adds a
 * switch for Fast3D's texture-path memo). None of it adds or skips a Matrix_* op.
 *
 * STAGE 6 in one paragraph: START opens it too (by default - `menu primary`), through a
 * VB_OPEN_PAUSE_MENU veto inside KaleidoSetup_Update rather than an input filter; the ring's first
 * page is the quest list (QuestPage.cpp); and pages can have a SECOND LEVEL, a detail view reached
 * by going down - close to the centre, turn counter-clockwise to vertical, open vertically (RsMenu.h
 * § levels). The turn adds a rotation to every scroll and hand chain, always, at zero while the
 * scroll is flat; the page content stays upright on the unrotated base matrix.
 *
 * What it is at this stage: an RS-style scroll - parchment, two roll ends and two blocky hands, all
 * real vertex-coloured geometry - that opened on N64 L (START since stage 6, and ONLY START since
 * sturdy-bassoon#138), hard-freezes the world, hides the HUD, and
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
 *   - TRIGGER: START, through the VB_OPEN_PAUSE_MENU veto (stage 6). Stage 1 opened on BTN_L, read
 *     as an edge off `play->state.input[0].press.button`; #138 took that away (Spencer's call,
 *     2026-09-23), because vanilla DOES read N64 L in gameplay - the minimap toggle (z_map_exp.c) -
 *     and on the press that opened the scroll it had already flipped the minimap that frame. The
 *     minimap keeps L. Inside the open scroll L still rolls left beside Z: the minimap does not draw
 *     there, so its toggle is not reached.
 *
 * The open-ended page ring is the load-bearing structural rule - see RsMenu.h.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-19
 */

#include "RsMenu.h"
#include "PauseLink.h"
#include "QuestPage.h"
#include "VanillaPages.h"
#include "VanillaPagesInternal.h"

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
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
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
// The same, for the page stepper (#128), which sits between a left-anchored and a right-anchored HUD element.
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);
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

// #130: the whole scroll sits this far below where the constants in this file put it, to clear the
// magic meter. The HUD is raised by half the hearts' distance from the top edge (RegisterRsHudDefaults),
// which leaves a double meter's bottom at about game y 44 - into the rolls' tops at 43. It is folded into
// the base matrix's one translate (ApplyBaseMatrix), so the parchment, rolls, hands, content, cursor and
// the vertical pose all move together and no constant here changes; the one thing that places itself
// outside that matrix, the equip flight's starting point, adds it through RsMenu_ScreenDy.
// Drawn in docs/notes/2026-09-17-pause-menu-diagrams/hud-overlap.svg, panel 2.
constexpr float kScrollDropY = 3.0f;

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
// a later stage wants stays a one-line change.
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

// --- going down a level (stage 6) ----------------------------------------------------------------
//
// Close to the centre, turn counter-clockwise to vertical, open vertically - RsMenu.h has the
// gesture. The numbers, each derived rather than typed where it can be:
//
// THE PIVOT is the rolls' midpoint, game (160, 119.5), so the scroll stays centred as it turns.
// 119.5 rather than 120 because the rolls run 43-196, and pivoting anywhere else would walk the
// bundle a half-unit sideways through the turn.
constexpr float kPivotX = (float)(SCREEN_WIDTH / 2);
constexpr float kPivotY = (float)(kRollTopY + kRollBottomY) / 2.0f;
// EACH SIDE TRAVELS HALF THE ONE-HAND L/R TRAVEL: 114, where L/R's single moving side goes 228. Both
// sides moving 114 inward puts the two hands' inner edges together at x 160 - the same "hands meet"
// stopping rule as L/R, reached symmetrically. That is Spencer's "just halfway", derived from the
// hand box through kSweepTravel rather than written as a number.
constexpr float kLevelTravel = kSweepTravel / 2.0f;
// Roll centre to roll centre, fully closed: 288 - 2 * 114 = 60. The bundle that turns.
constexpr float kClosedSpan = kPanelSpan - 2.0f * kLevelTravel;
// THE VERTICAL REST POSE'S SEPARATION, roll centre to roll centre - the first number Spencer is
// expected to tune. It cannot be 288: that is the horizontal open width and the screen is 240 tall.
// So the vertical page is a pose of its own, resting with each side still translated partway in
// from its horizontal home, and this is how far.
//
// What bounds it, since #130 stood the hands upright: the ROLLS. The hands used to bind (each reached
// sideways off its roll, 16 up and 30 down, which capped the span at 165); at rest now they point off
// the top and bottom edges ON PURPOSE, so the thing that has to stay on screen is the roll each one
// holds. Asserted below with the same 8-unit floor, and with #130's drop included, since that is where
// the rolls are drawn. At 164 the rolls run game y 30.5 to 214.5, so the span could grow by about 35;
// it is left where Spencer tuned it. The detail text area (RsMenu_DetailRect) is derived from the rolls
// and the hands' rest pose (HandRestInward) and moves with them.
constexpr float kVerticalSpan = 164.0f;
constexpr float kVerticalMargin = 8.0f;
constexpr float kRollHalfWidth = (float)(kRollX[0][2] - kRollX[0][0]) / 2.0f; // 10
static_assert(kPivotY + kScrollDropY - (kVerticalSpan / 2.0f + kRollHalfWidth) >= kVerticalMargin,
              "the vertical page's top roll runs off the top of the screen - shrink kVerticalSpan");
static_assert(kPivotY + kScrollDropY + (kVerticalSpan / 2.0f + kRollHalfWidth) <=
                  (float)SCREEN_HEIGHT - kVerticalMargin,
              "the vertical page's bottom roll runs off the bottom of the screen - shrink kVerticalSpan");
static_assert(kVerticalSpan > kClosedSpan, "the vertical page must open wider than the closed bundle");
// ...and the other half of "upright": each hand's forearm, grip to cuff end, is longer than its grip
// is far from the screen edge, so it runs OFF that edge rather than stopping short of it on screen. The
// forearm is 83; the grips rest 40.5 from the top and 35.5 from the bottom. Stated without the lean
// (kHandLean), which shortens the vertical reach by under 2%.
constexpr float kHandForearm = (float)(kHandBoxY + kHandBoxH) - kGripY; // 83
static_assert(kPivotY + kScrollDropY - kVerticalSpan / 2.0f < kHandForearm,
              "the vertical page's top hand no longer reaches off the top edge");
static_assert((float)SCREEN_HEIGHT - (kPivotY + kScrollDropY + kVerticalSpan / 2.0f) < kHandForearm,
              "the vertical page's bottom hand no longer reaches off the bottom edge");
// Counter-clockwise on screen is POSITIVE here: in this menu's y-up ortho, Matrix_RotateZ(+theta)
// writes xx = cos, xy = -sin (sys_matrix.c:265-266), which is the standard counter-clockwise
// rotation. Verified on the first held screenshot rather than trusted.
constexpr float kTurnAngle = kPi / 2.0f;
// Eight ticks a phase: the close matches half an L/R roll (kSweepTicks / 2), so the two gestures
// move at the same speed, and the whole descent is 1.2 s. Each phase eases in AND out, so the scroll
// comes to rest for an instant between phases - three motions, which is how Spencer described it,
// rather than one blended swoop.
constexpr int32_t kLevelCloseTicks = kSweepTicks / 2;
constexpr int32_t kLevelTurnTicks = 8;
constexpr int32_t kLevelOpenTicks = 8;
constexpr int32_t kLevelTicks = kLevelCloseTicks + kLevelTurnTicks + kLevelOpenTicks;
// The level changes halfway through the turn, when the scroll is shut and nothing is drawn on it.
constexpr int32_t kLevelSwapTick = kLevelCloseTicks + kLevelTurnTicks / 2;

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
// KALEIDO'S OWN gate, which is a different number from the two above and belongs to the ported half:
// vanilla writes the bare literal 30 in each of the two places it tests the stick - its cursor filter
// (z_kaleido_scope_PAL.c:1538-1588) and its page-switch timer (:1319-1336) - and both are ported here
// (KaleidoStickAxis, PageSwitchOutward). Named once so the two cannot drift apart.
constexpr int32_t kKaleidoStick = 30;

// (Stage 3's greybox pages are gone since stage 8: the three ported vanilla pages took their places.
// A page registered with no draw callback still gets the greybox body - its title, centred.)

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
// stays frozen and the HUD stays in its pause configuration for the whole of both slides, because
// un-freezing halfway down would show the world moving under a menu that is still there.
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
// #125: the button states the menu found on open, put back on close - kaleido's sButtonStatusSave
// (z_kaleido_scope_PAL.c:3871-3873, :4869-4871).
static u8 sButtonStatusSave[9];
// B's "Save" label, waiting for A's label change to finish (RsMenu_Open says why).
static bool sBLabelPending = false;
// Whether the menu shows the HUD (#125) or, opened over an already-hidden one (a cutscene, say), keeps
// it hidden: the mode it holds is HUD_VISIBILITY_ALL or HUD_VISIBILITY_NOTHING_INSTANT.
static bool sHudShown = false;

// The HUD mode the menu holds while it is up.
static u16 HeldHudMode() {
    return sHudShown ? HUD_VISIBILITY_ALL : HUD_VISIBILITY_NOTHING_INSTANT;
}

// Sets the HUD mode through 0 first, as kaleido does (z_kaleido_scope_PAL.c:1288-1289, :4879-4880), so
// the interface re-tweens the alphas even when the mode does not change.
static void RetweenHud(u16 mode) {
    gSaveContext.hudVisibilityMode = HUD_VISIBILITY_NO_CHANGE;
    Interface_ChangeHudVisibilityMode(mode);
}
// Times the HUD's mode had to be put back to ALL while the menu was up (the re-assert in the update).
static int32_t sHudReasserts = 0;
// #130: whether ApplyLevelHud has written A's and B's alphas since the scroll was last flat, so the tick
// it comes back to level 0 puts them back to full once and then leaves them to the interface again.
static bool sLevelHudTouched = false;
// C-up presses the menu refused to let toggle a house's camera (VB_TOGGLE_HOUSE_VIEWPOINT): the veto's
// own witness, beside `viewpoint=` - which alone cannot tell "refused" from "no C-up arrived".
static int32_t sViewpointVetoes = 0;
// #138: free-look reads the menu refused (VB_FREE_LOOK_TAKE_INPUT) - per read, and free look reads in
// more than one place a frame, so this counts reads rather than presses. Non-zero proves the veto ran.
static int32_t sFreeLookVetoes = 0;

// Set by the VB_OPEN_PAUSE_MENU veto, consumed by the update. The veto runs inside
// KaleidoSetup_Update (Play_Update, from gameState->main), and OnGameFrameUpdate fires after
// gameState->main returns (game.c:356), so an edge set by the veto is consumed on the same frame.
// The decision about what START MEANS (open, or close) belongs with the rest of the update logic.
static bool sStartEdge = false;

static bool sBootLineWritten = false;

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

// The level, and the animation between the two. `sLevel` is the logical answer and changes at the
// middle of the turn; `sLevelActive` is whether the scroll is moving between them. Same split, same
// reasons, as sPage and sSweepActive.
static int32_t sLevel = 0;
static bool sLevelActive = false;
static int32_t sLevelTick = 0;
static int32_t sLevelDir = 1;
static int32_t sLevelSwaps = 0;
static bool sLevelLoop = false;
static bool sLevelHold = false;

// The cursor. `sCursorId` rather than the index is the thing that persists: the graph is rebuilt
// every tick, and a page whose item list changes must not silently move the cursor onto a different
// thing that happens to sit at the same index.
static std::string sCursorId = "hand_left";
static int32_t sCursorIndex = 0;
static bool sCursorRebuilding = false;
// Set by the page, from inside its nodes callback, for the rebuild in progress only.
static bool sCursorColumn = false;
static int32_t sCursorEntry = -1;
// Stage 8's grid mode: the edges the page set, one entry per node in the rebuild (hands included,
// unused for them), and where the hands lead in. Same lifetime as the column flag.
struct RsCursorLinks {
    int32_t left, right, up, down;
};
static bool sCursorGrid = false;
static std::vector<RsCursorLinks> sCursorLinks;
static int32_t sHandLinkIn[2] = { RS_MENU_LINK_HAND_RIGHT, RS_MENU_LINK_HAND_LEFT };
static int32_t sCursorMoves = 0;
static int32_t sCursorSelects = 0;
// #129: kaleido's pageSwitchTimer and a counter for the rolls it starts. See PageSwitchOutward.
static int32_t sPageSwitchTimer = -1;
static int32_t sStickRolls = 0;
static int32_t sDpadRolls = 0;
static bool sStickLatchX = false;
static bool sStickLatchY = false;
// #127: kaleido's stick filter stepped this tick (RsMenu_StickStepped) - a song's play-along ends on it.
static bool sStickStepped = false;
static void ResetPage(int32_t index);
// #126: kaleido's per-axis repeat state (D_8082AD4C/D_8082AD44 for x, D_8082AD50/D_8082AD48 for y,
// z_kaleido_scope_PAL.c:1444-1449) - the held direction and the ticks until it fires again.
struct KaleidoStickAxisState {
    int16_t dir = 0;   // the held direction, -1, 0 or +1
    int16_t timer = 0; // ticks until it fires again
};
static KaleidoStickAxisState sKaleidoX;
static KaleidoStickAxisState sKaleidoY;

// Set for the duration of one draw. RsMenu_DrawText is a public entry point a page's draw callback
// calls, and this is how it knows there is a frame to append to - null means "not inside a draw",
// which is a no-op rather than a crash.
static GraphicsContext* sDrawGfxCtx = nullptr;
static int32_t sDrawGlyphs = 0;
static int32_t sDrawQuads = 0;
static int32_t sDlWords = 0;
// Stage 8: icons drawn (RsMenu_DrawIcon, the pause-Link composite included) and whether a page asked
// for pause-Link this frame.
static int32_t sDrawIcons = 0;
// #124: whether the last frame drew the cursor box (on an item or a hand).
static bool sCursorDrawn = false;
static bool sPauseLinkRequested = false;

// Which render state the heap list is in, so the text and icon calls push theirs only when it
// changes - an icon after an icon, or a glyph after a glyph, costs no state words. Every Push*State
// sets it; a draw that finds the wrong state pushes its own.
enum RsListState {
    RS_LIST_OTHER = 0,
    RS_LIST_TEXT,
    RS_LIST_ICON,
};
static int32_t sListState = RS_LIST_OTHER;

// Stage 7's view counters (RsMenu.h § RsMenuViewStats). `sInView` is set only around the content
// body's call, so RsMenu_DrawText knows which glyphs belong to the view; the row ys are gathered
// there. Plain counters and a vector - invisible to the interpolation recorder.
static bool sInView = false;
static std::vector<int16_t> sViewRowYs;
static const char* sViewName = "none";
static int32_t sViewFrame = -1;
static int32_t sViewRows = 0;
static int32_t sViewGlyphs = 0;
static int32_t sViewWords = 0;
static int32_t sViewIcons = 0;

// The stress body (RsMenu.h). -1 is off.
static int32_t sStressGlyphs = -1;
static bool sStressSame = false;
constexpr float kStressScale = 0.6f; // the journal's scale, the smallest real text in the menu
constexpr int16_t kStressPitch = 10; // and its line pitch
// Cycled through so neighbouring glyphs are different textures, the way real text is.
constexpr char kStressChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
// A mid-width letter, so `same` fits roughly as many as the cycle does and the two compare at 400.
constexpr char kStressSameChar = 'e';

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

// The START veto's own witness. Without these there is no way to tell "the veto took it" from "no
// START ever arrived" - the two produce an identical outcome when the menu simply stays as it was.
// Same discipline as stick_frames above: record what the mechanism was OFFERED. The names are the
// stage-1 filter's, kept per the ADR so older run notes still grep.
static int32_t sFilterArmedFrames = 0;
static int32_t sStartSwallowed = 0;
static int32_t sStartConsumed = 0;
// START and B presses dropped because the scroll was mid-roll or mid-level-gesture (Spencer, 2026-09-23:
// "akin to vanilla", which takes no input while a page turns). The drop's witness: without it, a run
// cannot tell "dropped" from "never arrived".
static int32_t sCloseDropped = 0;
// #138: opens RsMenu_Open refused over a cutscene or a textbox. START's path into it is silent, so this is
// the only witness that a START pressed over a textbox arrived and was turned down.
static int32_t sOpenRefused = 0;
// Every press bit the veto has EVER seen, OR-ed together. Under the old filter this read 0x0000
// after a delivered `agenttest press A`, which is how the hook-order race was caught; the veto runs
// inside KaleidoSetup_Update, after every OnGameStateMainStart hook, so an injected press must now
// appear here - a zero would mean the veto is not being consulted at all.
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

// --- #131: the menu's sounds ---------------------------------------------------------------------
//
// THE one table. An event's sound is changed here and nowhere else, which is what makes the planned
// parchment shuffle a two-line edit rather than a hunt through the call sites.
static const u16 sSfxIds[RS_MENU_SFX_COUNT] = {
    NA_SE_SY_CURSOR,           // RS_MENU_SFX_CURSOR
    NA_SE_SY_DECIDE,           // RS_MENU_SFX_HAND - vanilla's page arrows, MoveCursorToSpecialPos
    NA_SE_SY_WIN_SCROLL_LEFT,  // RS_MENU_SFX_ROLL_LEFT  - placeholder
    NA_SE_SY_WIN_SCROLL_RIGHT, // RS_MENU_SFX_ROLL_RIGHT - placeholder
    NA_SE_SY_WIN_OPEN,         // RS_MENU_SFX_OPEN
    NA_SE_SY_WIN_CLOSE,        // RS_MENU_SFX_CLOSE
};
// PREFIXED, and it is not decoration. Every console line ends with Describe(), which carries its own
// `open=`; `section=cursor` carries `hand=`, and `section=slot` carries `cursor=`. Unprefixed counters
// would put two `open=` on one line and collide with two other sections besides - and the field
// contract every acceptance grep leans on (MenuConsole.h) is that a key means one thing. The first
// #131 run read a slot's `cursor=` and the menu's open flag as sound counts, and scored 8/17 for it.
static const char* const sSfxNames[RS_MENU_SFX_COUNT] = {
    "sfx_cursor", "sfx_hand", "sfx_roll_left", "sfx_roll_right", "sfx_open", "sfx_close",
};
static int32_t sSfxCounts[RS_MENU_SFX_COUNT] = {};

// Vanilla's own six arguments, unchanged from every call kaleido makes - the default position,
// priority 4, the default freq/volume scale twice and the default reverb - and now the only copy of
// them in this directory.
void RsMenu_PlaySfxId(uint16_t sfxId) {
    Audio_PlaySoundGeneral(sfxId, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultReverb);
}

void RsMenu_PlaySfx(int32_t event) {
    if (event < 0 || event >= RS_MENU_SFX_COUNT) {
        return;
    }
    sSfxCounts[event]++;
    RsMenu_PlaySfxId(sSfxIds[event]);
}

int32_t RsMenu_SfxCount(int32_t event) {
    return event >= 0 && event < RS_MENU_SFX_COUNT ? sSfxCounts[event] : 0;
}

const char* RsMenu_SfxEventName(int32_t event) {
    return event >= 0 && event < RS_MENU_SFX_COUNT ? sSfxNames[event] : "unknown";
}

// The three must stay in lockstep across the enum and these two tables, and nothing else would say so:
// a short initializer zero-fills, which would play sfx id 0 and hand a null name to the console.
static_assert(sizeof(sSfxIds) / sizeof(sSfxIds[0]) == RS_MENU_SFX_COUNT, "sSfxIds is not one per event");
static_assert(sizeof(sSfxNames) / sizeof(sSfxNames[0]) == RS_MENU_SFX_COUNT, "sSfxNames is not one per event");

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

// The HUD's share of its alpha over the arrival: linear in the entry tick, as kaleido fades its START button
// over its open and close. START and the page stepper (#128) both ride it, so the two fade as one.
static float HudEntryShown() {
    switch (sPhase) {
        case RS_MENU_PHASE_OPENING:
            return (float)sEntryTick / (float)kEntryTicks;
        case RS_MENU_PHASE_CLOSING:
            return 1.0f - (float)sEntryTick / (float)kEntryTicks;
        default:
            return 1.0f;
    }
}

// Which side of the scroll travels: R (+1) closes with the right hand, L (-1) with the left. A
// "side" is a roll end and the hand holding it, moved as one - which is what makes it impossible
// for a hand to come apart from its roll.
static int32_t SweepMovingHand() {
    return sSweepDir > 0 ? 1 : 0;
}

// --- the level gesture's live numbers --------------------------------------------------------------

// Eased in AND out, per phase: 0 -> 1 with zero velocity at both ends, so each phase starts and
// stops rather than handing its speed to the next. See kLevelCloseTicks for why.
static float EaseInOut(float t) {
    return 0.5f - 0.5f * std::cos(kPi * t);
}

// Where the scroll is along the DOWNWARD path, in ticks: 0 flat and open, kLevelTicks vertical and
// open. Going up plays the same path backwards, which is all "the same sequence reversed" means -
// one function of one number, so up cannot drift from down.
static int32_t LevelPose() {
    if (!sLevelActive) {
        return sLevel == 1 ? kLevelTicks : 0;
    }
    return sLevelDir > 0 ? sLevelTick : kLevelTicks - sLevelTick;
}

// Roll centre to roll centre, in the scroll's own (unrotated) frame: 288 open, 60 shut, then
// kVerticalSpan open again after the turn.
static float LevelSeparation(int32_t pose) {
    if (pose <= kLevelCloseTicks) {
        return kPanelSpan - (kPanelSpan - kClosedSpan) * EaseInOut((float)pose / (float)kLevelCloseTicks);
    }
    if (pose <= kLevelCloseTicks + kLevelTurnTicks) {
        return kClosedSpan;
    }
    const float t = (float)(pose - kLevelCloseTicks - kLevelTurnTicks) / (float)kLevelOpenTicks;
    return kClosedSpan + (kVerticalSpan - kClosedSpan) * EaseInOut(t);
}

// Radians counter-clockwise. Zero for the whole close, so everything horizontal - including every
// L/R roll - runs through the rotation ops at an identity angle.
static float LevelAngle(int32_t pose) {
    if (pose <= kLevelCloseTicks) {
        return 0.0f;
    }
    if (pose >= kLevelCloseTicks + kLevelTurnTicks) {
        return kTurnAngle;
    }
    return kTurnAngle * EaseInOut((float)(pose - kLevelCloseTicks) / (float)kLevelTurnTicks);
}

// A pose ON a phase boundary is labelled with the phase that has just finished in the direction of
// travel: going down, pose 8 is the end of the close; going up, pose 16 is the end of the re-close.
static const char* LevelPhaseName(int32_t pose) {
    if (!sLevelActive) {
        return "rest";
    }
    const int32_t turnStart = kLevelCloseTicks;
    const int32_t turnEnd = kLevelCloseTicks + kLevelTurnTicks;
    if (sLevelDir > 0) {
        return pose <= turnStart ? "close" : pose <= turnEnd ? "turn" : "open";
    }
    return pose >= turnEnd ? "open" : pose >= turnStart ? "turn" : "close";
}

// --- the HUD at level 1 (#130, option B) -----------------------------------------------------------
//
// Upright, the top hand stands over START and B (hud-overlap.svg, panel 5), so level 1 cuts the HUD's
// buttons down to B - "back" - and moves B clear of the hand. All of it hangs off the level pose, so going
// up plays it backwards. START and A fade out over the close and the first half of the turn and are gone
// by the swap tick, where the scroll is shut; B fades out the same way, MOVES at the swap while it is
// invisible (a texture rectangle cannot glide - it would step at 20 Hz), and fades back in over the rest
// of the turn and the open. Hearts, magic and the C buttons are untouched.
//
// B's level-1 spot is A's own (Spencer, 2026-09-23): with A hidden there, B reappears in a place the
// HUD already uses rather than a new one. It is a move rather than a place, because the interface works
// B's position out at four draw sites and applies this to each (Interface_ShiftBButton). The move is
// centre to centre, MEASURED rather than derived - A is a rotated 3D quad and B a texture rectangle, so
// no pair of constants gives their centres - off #130's captures with the HUD raised: B at game
// (228.0, 21.0), A at (260.7, 20.1) (docs/test-runs/2026-09-23-issue-130-hud-raise/measure-out.txt,
// r1-05). B's box then spans x 249-275, clear of the upright top hand (to x 234) and short of the C
// buttons (from x 283). Both buttons are right-anchored, so it lands on A at any aspect ratio, unlike
// the first placeholder spot. (It was +62, +49, to about (290, 70), under the C buttons.)
constexpr s16 kLevelBShiftX = 33;
constexpr s16 kLevelBShiftY = -1;

// START's and A's share of their full alpha: 1 at level 0, 0 from the swap tick on.
static float LevelHudShown() {
    const int32_t pose = LevelPose();
    if (pose >= kLevelSwapTick) {
        return 0.0f;
    }
    return 1.0f - EaseInOut((float)pose / (float)kLevelSwapTick);
}

// START's share of its full alpha, all told: the arrival, the level gesture, and none at all when the menu
// opened over a HUD something else had hidden. The page stepper (#128) draws at exactly this, so the two
// cannot part company.
static float StartShown() {
    return sHudShown ? HudEntryShown() * LevelHudShown() : 0.0f;
}

// Whether B is at its level-1 spot: from the swap tick on, when it is invisible.
static bool LevelBMoved() {
    return LevelPose() >= kLevelSwapTick;
}

// B's share of its full alpha: out with START and A, then back in at the new spot.
static float LevelBShown() {
    const int32_t pose = LevelPose();
    if (pose < kLevelSwapTick) {
        return LevelHudShown();
    }
    return EaseInOut((float)(pose - kLevelSwapTick) / (float)(kLevelTicks - kLevelSwapTick));
}

// Writes A's and B's alphas while the scroll is off level 0. No HUD visibility mode shows everything but
// A, so this sets the alphas directly, as the button check would: the alpha each button settles at under
// HUD_VISIBILITY_ALL (func_80082644: 70 disabled, 255 otherwise), scaled. That holds only while nothing
// re-tweens the HUD, and nothing does off level 0: the page cannot change there, so its button states do
// not, and `hud_reasserts=` counts the one thing that could. Runs after the frame's draw (game.c:356),
// so what it writes is drawn next frame - one tick behind the pose, the same as the START fade.
static void ApplyLevelHud(PlayState* play) {
    const bool offFlat = LevelPose() > 0;
    if (!sHudShown || (!offFlat && !sLevelHudTouched)) {
        return;
    }
    InterfaceContext* ic = &play->interfaceCtx;
    const float aFull = gSaveContext.buttonStatus[4] == BTN_DISABLED ? 70.0f : 255.0f;
    const float bFull = gSaveContext.buttonStatus[0] == BTN_DISABLED ? 70.0f : 255.0f;
    ic->aAlpha = (s16)(aFull * LevelHudShown());
    ic->bAlpha = (s16)(bFull * LevelBShown());
    sLevelHudTouched = offFlat;
}

// B's label, the one place it is set while the menu is up: "Save" as #125 set it, and "Return" at level
// 1, where B goes back up - the stock DO_ACTION_RETURN texture, the one the START button carries. The
// swap happens while B is invisible (LevelBMoved flips at the swap tick, where B's alpha is 0). Only once
// A's label change has finished, since the two share doActionSegment[1] (RsMenu_Open says why): the
// open's first "Save" waits on that as `sBLabelPending`, and so does every swap after it. Checked every
// tick rather than on the swap, so a missed tick cannot strand the wrong label.
static void ApplyLevelBLabel(PlayState* play) {
    if (!sHudShown) {
        return;
    }
    InterfaceContext* ic = &play->interfaceCtx;
    if (ic->unk_1EC != 0) {
        return;
    }
    const u16 want = LevelBMoved() ? DO_ACTION_RETURN : DO_ACTION_SAVE;
    if (sBLabelPending || ic->unk_1FC != want) {
        Interface_LoadActionLabelB(play, want);
        sBLabelPending = false;
    }
}

// How far this side is displaced this tick, in the scroll's own frame. Two independent terms that
// never overlap in practice (a roll is refused off level 0, a level change is refused mid-roll):
//   - the L/R roll: zero for the anchor side on every frame; for the moving side, the full travel
//     scaled by the envelope, signed toward the anchor;
//   - the level gesture: both sides symmetrically, by however far the separation has shrunk from
//     the horizontal 288 - which is zero at level 0 rest and so vanishes from every L/R frame.
static float SweepDx(int32_t side) {
    if (!sSweepActive || side != SweepMovingHand()) {
        return 0.0f;
    }
    return (side == 0 ? 1.0f : -1.0f) * kSweepTravel * SweepEnv();
}

static float SideDx(int32_t side) {
    const float inward = side == 0 ? 1.0f : -1.0f;
    return inward * (kPanelSpan - LevelSeparation(LevelPose())) / 2.0f + SweepDx(side);
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

float RsMenu_ScreenDy() {
    return ProbeDy() + EntryDy() + kScrollDropY;
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

// Defined with the scroll's other state presets below.
static void PushTextState();
static void PushIconState();

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
    if (sInView && std::find(sViewRowYs.begin(), sViewRowYs.end(), y) == sViewRowYs.end()) {
        sViewRowYs.push_back(y);
    }

    // A page may interleave icons with its text (stage 8); the glyphs need their own state back.
    if (sListState != RS_LIST_TEXT) {
        PushTextState();
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

// The stress body's layout, shared by the draw and by the capacity check so the two cannot disagree
// on what fits: glyphs left to right across RsMenu_PageRect(), a new row when the next glyph's CELL
// (not just its advance) would cross the right edge, and a stop when a row's cell would cross the
// bottom. Fills `rows` (may be null) with one string per row and returns how many glyphs it placed,
// which is `want` unless the page ran out first.
static int32_t StressLayout(int32_t want, bool same, std::vector<std::string>* rows) {
    const RsMenuRect rect = RsMenu_PageRect();
    const int16_t size = (int16_t)(FONT_CHAR_TEX_WIDTH * kStressScale);
    constexpr int32_t kCycle = (int32_t)sizeof(kStressChars) - 1;
    int32_t placed = 0;
    int16_t y = rect.y0;
    while (placed < want && y + size <= rect.y1) {
        std::string row;
        float pen = (float)rect.x0;
        while (placed < want) {
            const char ch = same ? kStressSameChar : kStressChars[placed % kCycle];
            if (pen + size > (float)rect.x1) {
                break;
            }
            row += ch;
            pen += Ship_GetCharFontWidth((u8)ch) * kStressScale;
            placed++;
        }
        if (rows != nullptr) {
            rows->push_back(row);
        }
        y = (int16_t)(y + kStressPitch);
    }
    return placed;
}

// One RsMenu_DrawText per row, so each row costs one SetPrimColor the way a real line of text does.
static void DrawStressBody() {
    std::vector<std::string> rows;
    StressLayout(sStressGlyphs, sStressSame, &rows);
    const RsMenuRect rect = RsMenu_PageRect();
    for (size_t i = 0; i < rows.size(); i++) {
        RsMenu_DrawText(rows[i].c_str(), rect.x0, (int16_t)(rect.y0 + (int32_t)i * kStressPitch), kStressScale, 240,
                        232, 210, 255);
    }
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
    sListState = RS_LIST_OTHER;
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
// âš  It is authored in ORTHO units directly rather than through SetFlatQuad's game-space conversion,
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

// --- the page stepper (#128) ----------------------------------------------------------------------
//
// One stone per page in the ring, the page the ring is on red and the rest grey - the POC's placeholder
// art. It lives in the HUD's band, in the gap between the hearts/magic block and START (Spencer,
// 2026-09-22), so it is placed from the HUD's numbers, not the scroll's. It does not ride the arrival or
// #130's drop. It draws at START's own alpha (StartShown): it RAMPS with the arrival, fades out with START
// and A on the way down a level, gone from the swap tick on, and is not drawn when the menu opened over a
// hidden HUD, where START is not either. The highlight moves when sPage does, which during a roll is the
// swap tick, while the scroll is shut.
//
// The gap runs from a LEFT-anchored block to a RIGHT-anchored button, so its width changes with the
// window's aspect ratio and its centre does not - both edges move the same distance in opposite
// directions (OTRGetDimensionFromLeftEdge/RightEdge). At 16:9 it is x 70.8-183.6, 113 wide; narrower, the
// row shrinks to fit (LayoutStepper), and at 4:3, about 6 wide, there is no room and it is not drawn.
//
// The two edges are MEASURED, as drawn, rather than derived, for the same reason B's level-1 move is: what
// the eye sees as the gap is not the elements' quads. Off #128's first capture at 16:9, HUD raised, double
// magic, a full row of hearts (docs/test-runs/2026-09-24-issue-128-stepper, `gap-out.txt`): the hearts and
// the magic meter end at x 70.8, and START's "Return" label starts at 183.6 - it overhangs START's disc to
// the left. At 4:3 those are 124.1 and 130.3. The code's own numbers put the edges 1.4 and 1.8 further right,
// which left the row 1.6 off the gap's centre: START's quad starts at startButtonLeftPos (132 in English,
// z_parameter.c:3839) from the right edge, and a full heart row's quad ends at 120 + 5.6 (ten hearts 10
// apart from x 30, 16 units at 0.7, z_lifemeter.c:584, :633). START's height IS derived: its top is 16 less
// the top margin and it is 24 square (32 at 0.75, z_parameter.c:3950-3952).
//
// Drawn in the dim's node under its identity matrix and its prim-colour state, which is the fade's
// mechanism already: alpha is a prim-colour immediate and steps with the tick, as the dim does. The
// stones never move, so they need no Matrix_* op of their own.
// How far 16:9 moves an anchored element from where 4:3 puts it: 120 * 16 / 9 - 160. The two measured edges
// are brought back to 4:3 through it, and OTRGetDimensionFrom*Edge carries them to the live aspect ratio.
constexpr float kShift169 = (float)(SCREEN_HEIGHT / 2) * 16.0f / 9.0f - (float)(SCREEN_WIDTH / 2);
constexpr float kStartLeft43 = 183.6f - kShift169;
constexpr float kStartTopY = 16.0f;
constexpr float kStartSize = 24.0f;
constexpr float kHeartsRight43 = 70.8f + kShift169;
constexpr float kStoneSize = 14.0f; // #128's drawing: 14 x 14, 6 apart
constexpr float kStoneGap = 6.0f;
constexpr float kStepperPad = 4.0f; // air between the row and the hearts or START, at the tightest
// Below this a stone is a speck, and the stepper is not drawn at all - which is what happens at 4:3, where
// the hearts run into START's label and the gap is about 6 units (#128's run, s2 at 4:3: stones of 0).
constexpr float kStoneMin = 4.0f;
constexpr u8 kStoneCurrentColour[3] = { 210, 40, 40 };
constexpr u8 kStoneColour[3] = { 150, 150, 150 };

struct StepperLayout {
    int32_t stones;
    float stone, pitch;
    float x0, y0, x1, y1;
    float gapX0, gapX1;
};

// SIZED FROM THE PAGE COUNT AND THE GAP, per the ring's invariant (RsMenu.h): as many stones as there are
// pages, at 14 + 6 while they fit - five do at 16:9 - and all scaled down together once they do not.
static StepperLayout LayoutStepper() {
    StepperLayout l;
    l.stones = RsMenu_PageCount();
    l.gapX0 = OTRGetDimensionFromLeftEdge(kHeartsRight43);
    l.gapX1 = OTRGetDimensionFromRightEdge(kStartLeft43);
    const float want = l.stones > 0 ? (float)l.stones * kStoneSize + (float)(l.stones - 1) * kStoneGap : 0.0f;
    const float room = std::max(0.0f, l.gapX1 - l.gapX0 - 2.0f * kStepperPad);
    const float scale = want > room ? room / want : 1.0f;
    l.stone = kStoneSize * scale;
    l.pitch = (kStoneSize + kStoneGap) * scale;
    l.x0 = (l.gapX0 + l.gapX1 - want * scale) / 2.0f;
    l.x1 = l.x0 + want * scale;
    // Level with START's middle, whatever the top margin (#130's raise) makes that.
    const int32_t margin = CVarGetInteger(CVAR_COSMETIC("HUD.StartButton.UseMargins"), 0) != 0
                               ? CVarGetInteger(CVAR_COSMETIC("HUD.Margin.T"), 0)
                               : 0;
    l.y0 = kStartTopY - (float)margin + (kStartSize - l.stone) / 2.0f;
    l.y1 = l.y0 + l.stone;
    return l;
}

// What the last drawn frame did with the stepper, for `menu dump`.
static bool sStepperDrawn = false;
static int32_t sStepperAlpha = 0;
// ...and the alpha it drew on each tick of the last opening slide [0] and the last closing one [1], -1 for
// a tick no frame drew. The arrival is 8 ticks and the agent loop reads a command every 10, so no dump can
// land inside a slide; this is what the ramp is asserted against. Cleared as each slide starts.
enum StepperSlide {
    STEPPER_SLIDE_OPEN = 0,
    STEPPER_SLIDE_CLOSE,
    STEPPER_SLIDE_COUNT,
};
static int16_t sStepperRamp[STEPPER_SLIDE_COUNT][kEntryTicks];

static void ClearStepperRamp(int32_t slide) {
    for (int32_t i = 0; i < kEntryTicks; i++) {
        sStepperRamp[slide][i] = -1;
    }
}

static void DrawStepper() {
    const StepperLayout l = LayoutStepper();
    const u8 alpha = (u8)(255.0f * StartShown());
    sStepperAlpha = alpha;
    if ((sPhase == RS_MENU_PHASE_OPENING || sPhase == RS_MENU_PHASE_CLOSING) && sEntryTick < kEntryTicks) {
        sStepperRamp[sPhase == RS_MENU_PHASE_OPENING ? STEPPER_SLIDE_OPEN : STEPPER_SLIDE_CLOSE][sEntryTick] = alpha;
    }
    sStepperDrawn = alpha > 0 && l.stones > 0 && l.stone >= kStoneMin;
    if (!sStepperDrawn) {
        return;
    }
    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, (size_t)l.stones * 4 * sizeof(Vtx));
    std::vector<Gfx>& dl = MenuDl();
    for (int32_t i = 0; i < l.stones; i++) {
        const float x = l.x0 + (float)i * l.pitch;
        const u8* colour = i == sPage ? kStoneCurrentColour : kStoneColour;
        // The colour is the prim colour's, as the dim's is; the vertex colour is ignored under it.
        SetFlatQuad(&vtx[i * 4], (int16_t)std::lround(x), (int16_t)std::lround(l.y0), (int16_t)std::lround(x + l.stone),
                    (int16_t)std::lround(l.y1), colour);
        dl.push_back(gsDPSetPrimColor(0, 0, colour[0], colour[1], colour[2], alpha));
        dl.push_back(gsSPVertex(&vtx[i * 4], 4, 0));
        dl.push_back(gsSP1Quadrangle(0, 2, 3, 1, 0));
        sDrawQuads++;
    }
}

// Flat vertex colour: shade in, shade out. No texture, no lighting, and no Z - the world has
// already written a depth buffer and the menu must never be tested against it.
static void PushFlatState() {
    sListState = RS_LIST_OTHER;
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
    sListState = RS_LIST_TEXT;
    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
    dl.push_back(gsDPSetCombineMode(G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM));
    dl.push_back(gsDPSetOtherMode(G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE |
                                      G_TL_TILE | G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  G_AC_THRESHOLD | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));
    dl.push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
}

// SETUPDL_42 (z_rcp.c), the preset kaleido draws every item icon under (Gfx_SetupDL_42Opa at
// z_kaleido_item.c:437), with the combiner kaleido then sets over it - G_CC_MODULATEIA_PRIM - and
// without its back-face culling, because this ortho is y-up and a quad's winding flips with it (the
// text state has no culling either). Alpha is the texel's times the prim colour's, over XLU_SURF, so
// an icon's transparent corners stay transparent on the parchment.
static void PushIconState() {
    sListState = RS_LIST_ICON;
    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
    dl.push_back(gsDPSetCombineMode(G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM));
    dl.push_back(gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE |
                                      G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));
    dl.push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
}

// THE MATRIX CHAIN, and the rule it exists to obey: the SAME Matrix_* ops are emitted every frame,
// in the same order, whatever the menu is doing. Ops are matched positionally inside an
// interpolation node, so a branch around one misaligns everything after it and kills interpolation
// for the rest of the node (SOH_2D_DRAWING.md). At rest every argument is zero or the identity and
// the chain costs its ops and nothing else. Since stage 6 the scroll and hand chains are
// base -> turn (three ops) -> the side's own ops; the content chain is base alone.
//
// Everything is in the ortho's centred, y-up space, so a game-space pivot comes in already
// converted. `dy` is the probe's offset and is folded into the first translate rather than added as
// a fourth op, for the same reason.
static void ApplyBaseMatrix() {
    // The arrival offset, the probe's and the #130 drop, in one translate. Emitted first in every
    // chain, so the whole assembly rises and falls as one piece and the probe's four-channel
    // measurement still means what it meant at stage 5 (the drop is a constant, so it cancels out of
    // every probe difference).
    Matrix_Translate(0.0f, -RsMenu_ScreenDy(), 0.0f, MTXMODE_NEW);
}

// THE TURN, and the rule it obeys (stage 6): the rotation about the pivot is in EVERY chain that
// draws the scroll or its hands, EVERY frame - three ops at an identity angle whenever the scroll is
// horizontal, which is every L/R frame there has ever been. Ops are matched positionally inside an
// interpolation node, so a chain that grew a rotation only while going down a level would break
// interpolation for everything after it in the node. The page content does NOT get this: it stays
// upright on the unrotated base matrix, which is what the design asks of the journal text.
static void ApplyTurnMatrix() {
    // The pivot, in the ortho's centred y-up space. x is exactly 0; y is +0.5 (game 119.5).
    const float py = (float)(SCREEN_HEIGHT / 2) - kPivotY;
    ApplyBaseMatrix();
    Matrix_Translate(kPivotX - (float)(SCREEN_WIDTH / 2), py, 0.0f, MTXMODE_APPLY);
    Matrix_RotateZ(LevelAngle(LevelPose()), MTXMODE_APPLY);
    Matrix_Translate((float)(SCREEN_WIDTH / 2) - kPivotX, -py, 0.0f, MTXMODE_APPLY);
}

// The parchment, stretched so its two edges sit on the two roll centres wherever the sides are.
// One formula for both gestures, from the two sides' displacements alone:
//   left edge  -> -144 + dxL        (the left roll centre)
//   right edge -> +144 + dxR        (the right roll centre)
// i.e. translate the left edge to its roll, scale by the new span over the old, about the left
// edge. For an L/R roll one of the two is zero and this is exactly stage 5's "scale about the
// stationary edge"; for the level gesture they are equal and opposite and it is a scale about the
// CENTRE. Either way the paper cannot detach from a roll - the identity stage 5 rested on, now
// holding by construction for any pair of displacements.
static void ApplyParchmentMatrix() {
    const float dxL = SideDx(0);
    const float dxR = SideDx(1);
    const float half = kPanelSpan / 2.0f;
    ApplyTurnMatrix();
    Matrix_Translate(dxL - half, 0.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale((kPanelSpan + dxR - dxL) / kPanelSpan, 1.0f, 1.0f, MTXMODE_APPLY);
    Matrix_Translate(half, 0.0f, 0.0f, MTXMODE_APPLY);
}

// One side of the scroll - its roll end and its hand, welded by sharing this one matrix. Emitted
// for BOTH sides every frame, whatever is moving: ops are matched positionally inside a node, so
// the two sides must record the same chain whichever of them is travelling. The translate is in the
// scroll's own frame (after the turn), which is what makes "open vertically" the same op as "open".
static void ApplySideMatrix(int32_t side) {
    ApplyTurnMatrix();
    Matrix_Translate(SideDx(side), 0.0f, 0.0f, MTXMODE_APPLY);
}

// THE HANDS SWIVEL ON THEIR OWN GRIPS while the scroll turns. History, because each pose below was a
// correction of the one before: rigidly rotated (stage 6), both hands ended on the right, so the
// bottom one reached in from the right. Spencer's first correction swivelled the left hand back by
// twice the scroll's angle, so it rested on the bottom roll reaching in from the LEFT, while the right
// hand stayed rigid, reaching in from the right over the top roll - and over the HUD's buttons.
// The mesh is still the left hand's (a mirror of the right), so the poses mirror in position, not in
// handedness - fine for a low-poly placeholder, per Spencer.
//
// #130 (option B, Spencer 2026-09-22): the TURN is kept - the right hand still ends on top and the
// left on the bottom - but both now REST UPRIGHT, pointing off the top and bottom edges, instead of
// reaching off the sides, where the top one sat over the HUD's buttons. Each hand turns about its own
// grip by however much it needs on top of the scroll's turn:
//   - the right (top) hand by +1x the scroll's angle, for a net 180: its forearm, which hangs DOWN at
//     level 0, points UP off the top edge;
//   - the left (bottom) hand by -1x, for a net 0: its forearm stays pointing down, off the bottom
//     edge, while the roll turns under it.
// Both then lean kHandLean clockwise, which tips each forearm toward its own side of the screen - the
// top one up-and-right, the bottom one down-and-left - so they read as two arms coming in from
// outside rather than as posts. The lean ramps in with the turn, as the swivel does, so level 0 is
// untouched. Swapping to option A later is HandSwivelAt and HandSlide: flip which side gets +1.
constexpr float kHandLean = 10.0f * kPi / 180.0f;

// The swivel for a scroll turned `angle` counter-clockwise - the one place the side choice is made.
static float HandSwivelAt(int32_t side, float angle) {
    return (side == 1 ? 1.0f : -1.0f) * angle - kHandLean * (angle / kTurnAngle);
}

static float HandSwivel(int32_t side) {
    return HandSwivelAt(side, LevelAngle(LevelPose()));
}

// The hand's net turn at level 1 rest: the scroll's turn plus the swivel it ends at.
static float HandRestAngle(int32_t side) {
    return kTurnAngle + HandSwivelAt(side, kTurnAngle);
}

// A hand box's x extent as drawn: the left hand's table as written, the right hand's mirrored about
// x = 160. The one mirror every reader of kHandLeft goes through (DrawHand, HandRestInward, MeasureHand).
static void HandBoxX(int32_t hand, const RsHandBox& box, int16_t* x0, int16_t* x1) {
    *x0 = hand == 0 ? box.x0 : (int16_t)(SCREEN_WIDTH - box.x1);
    *x1 = hand == 0 ? box.x1 : (int16_t)(SCREEN_WIDTH - box.x0);
}

// How far a hand at level 1 rest reaches from its grip TOWARD THE PAGE, in game units: the top hand
// down, the bottom hand up. Computed from the authored boxes turned by HandRestAngle, so re-authoring
// the hand or re-tuning the lean moves the journal's text band (RsMenu_DetailRect) with it. Screen y
// is down here, and a counter-clockwise turn by `a` takes (x, y) to (x cos a + y sin a, y cos a - x sin a).
static float HandRestInward(int32_t side) {
    const float a = HandRestAngle(side);
    const float c = std::cos(a);
    const float s = std::sin(a);
    float reach = 0.0f;
    for (int32_t i = 0; i < kHandBoxCount; i++) {
        int16_t x0 = 0;
        int16_t x1 = 0;
        const RsHandBox& box = kHandLeft[i];
        HandBoxX(side, box, &x0, &x1);
        const float xs[2] = { (float)x0 - kGripX[side], (float)x1 - kGripX[side] };
        const float ys[2] = { (float)box.y0 - kGripY, (float)box.y1 - kGripY };
        for (float dx : xs) {
            for (float dy : ys) {
                const float y = dy * c - dx * s;
                reach = std::max(reach, side == 1 ? y : -y);
            }
        }
    }
    return reach;
}

// AND SLIDES ALONG ITS ROLL while it swivels (Spencer, the pass after): swivelling about the grip
// alone left the left hand gripping the MIDDLE of the bottom roll with its forearm lying along it.
// The right hand grips its roll kGripY - kPivotY = 37.5 below the rolls' midpoint, which in the
// vertical pose puts it 37.5 right of centre at the top right. The mirror of that is 37.5 LEFT of
// centre on the bottom roll, so the left hand travels twice that - 75 - up its own roll (toward the
// roll's far end), ramped with the turn exactly as the swivel is. Kept by #130: upright, the two hands
// then stand 37.5 either side of the centre, the top one at game x 197.5 and the bottom at 122.5.
constexpr float kHandSlide = 2.0f * (kGripY - kPivotY);

static float HandSlide(int32_t side) {
    return side == 0 ? kHandSlide * (LevelAngle(LevelPose()) / kTurnAngle) : 0.0f;
}

// Applied after ApplySideMatrix, for BOTH hands on every frame - the right hand's slide is always
// zero, and both angles are zero at level 0, but each chain must record the same ops as the other,
// because ops are matched positionally inside the hands' interpolation node. The roll is drawn BEFORE
// this, so the roll never swivels.
static void ApplyHandSwivel(int32_t side) {
    const float gx = kGripX[side] - (float)(SCREEN_WIDTH / 2);
    const float gy = (float)(SCREEN_HEIGHT / 2) - kGripY;
    // The slide first, in the side's own frame, where "up the roll" is +y (the ortho is y-up).
    Matrix_Translate(0.0f, HandSlide(side), 0.0f, MTXMODE_APPLY);
    Matrix_Translate(gx, gy, 0.0f, MTXMODE_APPLY);
    Matrix_RotateZ(HandSwivel(side), MTXMODE_APPLY);
    Matrix_Translate(-gx, -gy, 0.0f, MTXMODE_APPLY);
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
// table - so re-authoring the hand, or swapping in an equipment-reactive one later, is one
// table and no second edit.
static void DrawHand(int32_t hand) {
    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, kHandVtxCount * sizeof(Vtx));
    for (int32_t i = 0; i < kHandBoxCount; i++) {
        const RsHandBox& box = kHandLeft[i];
        int16_t x0 = 0;
        int16_t x1 = 0;
        HandBoxX(hand, box, &x0, &x1);
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

// #130: where each hand was last drawn, on screen, in game units with y down - x0, y0, x1, y1 over all
// its boxes, through the very matrix DrawHand drew under, so a run can assert "off the top edge" and
// "clear of B" as numbers. Matrix_MultVec3f reads the matrix and records no op, so measuring cannot
// misalign the hands' interpolation node. It is the tick's pose, not an interpolated frame's.
static float sHandExtent[2][4] = {};

static void MeasureHand(int32_t hand) {
    float ext[4] = { 1e9f, 1e9f, -1e9f, -1e9f };
    for (int32_t i = 0; i < kHandBoxCount; i++) {
        int16_t xs[2] = {};
        HandBoxX(hand, kHandLeft[i], &xs[0], &xs[1]);
        const int16_t ys[2] = { kHandLeft[i].y0, kHandLeft[i].y1 };
        for (int16_t x : xs) {
            for (int16_t y : ys) {
                Vec3f src = { (float)(x - SCREEN_WIDTH / 2), (float)(SCREEN_HEIGHT / 2 - y), 0.0f };
                Vec3f dst;
                Matrix_MultVec3f(&src, &dst);
                const float sx = dst.x + (float)(SCREEN_WIDTH / 2);
                const float sy = (float)(SCREEN_HEIGHT / 2) - dst.y;
                ext[0] = std::min(ext[0], sx);
                ext[1] = std::min(ext[1], sy);
                ext[2] = std::max(ext[2], sx);
                ext[3] = std::max(ext[3], sy);
            }
        }
    }
    for (int32_t k = 0; k < 4; k++) {
        sHandExtent[hand][k] = ext[k];
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

void RsMenu_DrawBar(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t r, uint8_t g, uint8_t b) {
    if (sDrawGfxCtx == nullptr) {
        return;
    }
    const u8 colour[3] = { r, g, b };
    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, 4 * sizeof(Vtx));
    SetFlatQuad(vtx, x0, y0, x1, y1, colour);
    PushFlatState();
    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsSPVertex(vtx, 4, 0));
    dl.push_back(gsSP1Quadrangle(0, 2, 3, 1, 0));
    sDrawQuads++;
    PushTextState();
}

// A textured quad over the box, the whole texture mapped onto it: SetGlyphQuad's layout with the
// texture's own size in the texture coordinates (10.5 fixed point) instead of the glyph cell's.
static void SetTexturedQuad(Vtx* v, int16_t x, int16_t y, int16_t w, int16_t h, int16_t texW, int16_t texH) {
    const int16_t ox[4] = { x, (int16_t)(x + w), x, (int16_t)(x + w) };
    const int16_t oy[4] = { y, y, (int16_t)(y + h), (int16_t)(y + h) };
    const int16_t s[4] = { 0, (int16_t)(texW << 5), 0, (int16_t)(texW << 5) };
    const int16_t t[4] = { 0, 0, (int16_t)(texH << 5), (int16_t)(texH << 5) };
    for (int32_t i = 0; i < 4; i++) {
        v[i].v.ob[0] = (int16_t)(ox[i] - SCREEN_WIDTH / 2);
        v[i].v.ob[1] = (int16_t)(SCREEN_HEIGHT / 2 - oy[i]);
        v[i].v.ob[2] = 0;
        v[i].v.flag = 0;
        v[i].v.tc[0] = s[i];
        v[i].v.tc[1] = t[i];
        v[i].v.cn[0] = v[i].v.cn[1] = v[i].v.cn[2] = v[i].v.cn[3] = 255;
    }
}

static void CountIcon() {
    sDrawIcons++;
    if (sInView) {
        sViewIcons++;
    }
}

void RsMenu_DrawIcon(const void* texture, RsMenuTexFormat format, int16_t texW, int16_t texH, int16_t x, int16_t y,
                     int16_t w, int16_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool grey) {
    if (sDrawGfxCtx == nullptr || texture == nullptr) {
        return;
    }
    if (sListState != RS_LIST_ICON) {
        PushIconState();
    }
    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, 4 * sizeof(Vtx));
    SetTexturedQuad(vtx, x, y, w, h, texW, texH);

    // The load is built with the packet macros into a scratch array and copied in, because
    // gDPLoadTextureBlock pastes its size argument into token names (G_IM_SIZ_32b_LOAD_BLOCK...) and so
    // needs one literal per format. CLAMP rather than kaleido's WRAP: the whole texture is mapped onto
    // the quad, and bilinear filtering at a wrapped edge would bleed the opposite edge in.
    Gfx load[16];
    Gfx* p = load;
    const uintptr_t tex = reinterpret_cast<uintptr_t>(texture);
    switch (format) {
        case RS_MENU_TEX_IA8:
            gDPLoadTextureBlock(p++, tex, G_IM_FMT_IA, G_IM_SIZ_8b, texW, texH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        case RS_MENU_TEX_I8:
            gDPLoadTextureBlock(p++, tex, G_IM_FMT_I, G_IM_SIZ_8b, texW, texH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        default:
            gDPLoadTextureBlock(p++, tex, G_IM_FMT_RGBA, G_IM_SIZ_32b, texW, texH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
    }

    std::vector<Gfx>& dl = MenuDl();
    dl.push_back(gsDPSetPrimColor(0, 0, r, g, b, a));
    if (grey) {
        // Kaleido's greyed-for-age draw (z_kaleido_item.c:793-799).
        dl.push_back(gsDPSetGrayscaleColor(109, 109, 109, 255));
        dl.push_back(gsSPGrayscale(true));
    }
    dl.insert(dl.end(), load, p);
    dl.push_back(gsSPVertex(vtx, 4, 0));
    dl.push_back(gsSP1Quadrangle(0, 2, 3, 1, 0));
    if (grey) {
        dl.push_back(gsSPGrayscale(false));
    }
    CountIcon();
}

// KaleidoScope_DrawEquipmentImage (z_kaleido_equipment.c:33-103), as one quad. Kaleido names its
// playerSegment as the texture image, loads a 64 x 32 RGBA16 tile from it, and then swaps the bound
// texture for the framebuffer with gDPSetTextureImageFB - the interpreter's SelectTextureFb - so what
// is drawn is the framebuffer, sampled over the tile's normalised 0..1. Its quad is the first 32-row
// strip stretched 80 further down with its texture coordinates left at 32 rows, which is how one
// strip covers all 112 rows; this quad does the same. Point filtering, as kaleido sets it.
void RsMenu_DrawPauseLink(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (sDrawGfxCtx == nullptr || RsPauseLink_FrameBuffer() < 0) {
        return;
    }
    if (sListState != RS_LIST_ICON) {
        PushIconState();
    }
    constexpr int16_t kTileW = 64; // PAUSE_EQUIP_PLAYER_WIDTH
    constexpr int16_t kTileH = 32; // 4096 / (64 * 2): the rows of RGBA16 that fit a 4 KB load
    Vtx* vtx = (Vtx*)Graph_Alloc(sDrawGfxCtx, 4 * sizeof(Vtx));
    SetTexturedQuad(vtx, x, y, w, h, kTileW, kTileH);

    Gfx cmds[24];
    Gfx* p = cmds;
    gDPSetPrimColor(p++, 0, 0, 255, 255, 255, 255);
    gDPSetTextureFilter(p++, G_TF_POINT);
    gDPSetTileCustom(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kTileW, kTileH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                     G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gDPSetTextureImage(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kTileW, RsPauseLink_Buffer());
    gDPLoadSync(p++);
    gDPLoadTile(p++, G_TX_LOADTILE, 0, 0, (kTileW - 1) << 2, (kTileH - 1) << 2);
    gDPSetTextureImageFB(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kTileW, RsPauseLink_FrameBuffer());
    gSPVertex(p++, (uintptr_t)vtx, 4, 0);
    gSP1Quadrangle(p++, 0, 2, 3, 1, 0);
    gDPPipeSync(p++);
    gDPSetTextureFilter(p++, G_TF_BILERP);
    MenuDl().insert(MenuDl().end(), cmds, p);
    CountIcon();
    sPauseLinkRequested = true;
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
    sCursorLinks.push_back({ RS_MENU_LINK_NONE, RS_MENU_LINK_NONE, RS_MENU_LINK_NONE, RS_MENU_LINK_NONE });
    return (int32_t)CursorNodes().size() - 1;
}

void RsMenu_SetCursorGrid(int32_t entry) {
    if (!sCursorRebuilding) {
        return;
    }
    sCursorGrid = true;
    sCursorEntry = entry;
}

void RsMenu_LinkCursorNode(int32_t node, int32_t left, int32_t right, int32_t up, int32_t down) {
    if (!sCursorRebuilding || node < 1 || node >= (int32_t)sCursorLinks.size()) {
        return;
    }
    sCursorLinks[(size_t)node] = { left, right, up, down };
}

void RsMenu_LinkHands(int32_t leftHandRight, int32_t rightHandLeft) {
    if (!sCursorRebuilding) {
        return;
    }
    sHandLinkIn[0] = leftHandRight;
    sHandLinkIn[1] = rightHandLeft;
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
    sCursorLinks.push_back({ RS_MENU_LINK_NONE, RS_MENU_LINK_NONE, RS_MENU_LINK_NONE, RS_MENU_LINK_NONE });
}

void RsMenu_SetCursorColumn(int32_t entry) {
    if (!sCursorRebuilding) {
        return;
    }
    sCursorColumn = true;
    sCursorEntry = entry;
}

// Rebuilt every update tick, cheaply, because the alternative is an invalidation rule and there is
// nothing here expensive enough to earn one - and again straight after any cursor move, so a page
// that scrolls to follow its cursor never draws a tick behind it. [left hand] + [the visible page's
// items] + [right hand], wired as a row by default or as a column when the page asks.
//
// ADJACENCY IS NOT DERIVED FROM POSITION. Nothing in this function looks at x or y. That is the
// settled design's claim and the reason option A's full-width parchment costs nothing: the leftmost
// item's left neighbour IS the left hand, wherever either is drawn.
static void RebuildCursorGraph() {
    std::vector<RsMenuCursorNode>& nodes = CursorNodes();
    nodes.clear();
    sCursorLinks.clear();
    sCursorColumn = false;
    sCursorGrid = false;
    sCursorEntry = -1;
    sHandLinkIn[0] = RS_MENU_LINK_HAND_RIGHT;
    sHandLinkIn[1] = RS_MENU_LINK_HAND_LEFT;

    // A detail view has no graph at all. The id the cursor was on is kept (the count-0 case below
    // leaves sCursorId alone), so coming back up lands on the item that was entered.
    if (sLevel != 0) {
        sCursorIndex = 0;
        return;
    }

    sCursorRebuilding = true;
    AddHandNode(0);
    const RsMenuPage* page = RsMenu_PageAt(sPage);
    if (page != nullptr && page->nodes != nullptr) {
        page->nodes(sPage, page->userData);
    }
    AddHandNode(1);
    sCursorRebuilding = false;

    const int32_t count = (int32_t)nodes.size();
    const int32_t last = count - 1;
    const bool column = sCursorColumn && !sCursorGrid && count > 2;
    const bool grid = sCursorGrid;
    // Column mode falls back to the first item; grid mode to the left hand, because a grid's entry is
    // the page's own choice and -1 there means "nothing to stand on".
    const int32_t entry = sCursorEntry > 0 && sCursorEntry < last ? sCursorEntry : (grid ? 0 : 1);
    // A grid edge as the page gave it -> an index in this list. The hands are symbolic because the
    // right hand's index is not known until the page has added everything.
    auto resolve = [&](int32_t target) {
        if (target == RS_MENU_LINK_HAND_LEFT) {
            return 0;
        }
        if (target == RS_MENU_LINK_HAND_RIGHT) {
            return last;
        }
        return target > 0 && target < last ? target : -1;
    };
    for (int32_t i = 0; i < count; i++) {
        RsMenuCursorNode& node = nodes[(size_t)i];
        node.up = -1;
        node.down = -1;
        if (grid) {
            // Stage 8: every edge is the page's. The hands keep their outward and vertical edges empty,
            // as on every other page; only where they lead in comes from the page.
            if (i == 0) {
                node.left = -1;
                node.right = resolve(sHandLinkIn[0]);
            } else if (i == last) {
                node.left = resolve(sHandLinkIn[1]);
                node.right = -1;
            } else {
                const RsCursorLinks& links = sCursorLinks[(size_t)i];
                node.left = resolve(links.left);
                node.right = resolve(links.right);
                node.up = resolve(links.up);
                node.down = resolve(links.down);
            }
        } else if (!column) {
            node.left = i > 0 ? i - 1 : -1;
            node.right = i + 1 < count ? i + 1 : -1;
        } else if (i == 0) {
            node.left = -1;
            node.right = entry;
        } else if (i == last) {
            node.left = entry;
            node.right = -1;
        } else {
            // An item: the hands either side, its neighbours in the page's order above and below.
            // The ends of the column REFUSE up/down rather than wrapping - a list that wrapped would
            // jump the view from the last row to the first, which a discrete-scrolling list must not.
            node.left = 0;
            node.right = last;
            node.up = i > 1 ? i - 1 : -1;
            node.down = i + 1 < last ? i + 1 : -1;
        }
    }

    // The cursor follows its ID, not its index. A page change rebuilds the whole list, and landing
    // on "whatever is at index 3 now" would be a different thing every page. On a column page an id
    // that is not here lands on the entry item instead of on a clamped index.
    int32_t found = -1;
    for (int32_t i = 0; i < count; i++) {
        if (nodes[(size_t)i].id == sCursorId) {
            found = i;
            break;
        }
    }
    if (found >= 0) {
        sCursorIndex = found;
    } else if (column || grid) {
        sCursorIndex = entry;
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

const std::string& RsMenu_CursorId() {
    return sCursorId;
}

// The node one step from `node` in one direction (dx wins if both are set), or -1.
static int32_t CursorEdge(const RsMenuCursorNode* node, int32_t dx, int32_t dy) {
    if (node == nullptr) {
        return -1;
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
    return next >= 0 && next < RsMenu_CursorCount() ? next : -1;
}

// Which way a HAND rolls: the left hand back, the right forward - the same mapping the shoulders have,
// and the settled design's "selecting a hand does what L/R does" read literally. Every caller that turns
// a hand into a roll goes through this, so RsMenu.h's claim that the four roll causes behave alike is
// one function rather than four copies of a ternary.
static int32_t HandRollDelta(const RsMenuCursorNode* node) {
    return node != nullptr && node->hand == 0 ? -1 : 1;
}

// #129: arm kaleido's page-switch timer, which is what ARRIVING on a hand does - see PageSwitchOutward.
// Named so the one place that writes the timer from outside it is greppable from both ends.
static void ArmPageSwitchTimer();

static void JumpCursor(int32_t next) {
    sCursorIndex = next;
    sCursorId = CursorNodes()[(size_t)next].id;
    sCursorMoves++;
    // #129: ARRIVING on a hand arms the page-switch timer at 0, so a stick ALREADY held has to hold ten
    // more ticks before it rolls. That is kaleido's KaleidoScope_MoveCursorToSpecialPos
    // (z_kaleido_scope_PAL.c:1198), which every page calls as the cursor steps onto a page arrow, and it
    // is the whole of the difference between the two ways a roll can be reached: a push that STARTS on a
    // hand rolls at once (the timer is -1, reset by PageSwitchOutward while nothing pushes outward), and a
    // stick held across the page onto the far hand does not. Without this the scroll rolled again about
    // ten ticks early on a long hold, which is what the first #129 run caught at 45 frames.
    // #131: and the same arrival is the one vanilla gives its own sound to - MoveCursorToSpecialPos
    // plays DECIDE rather than the CURSOR every other step gets. EVERY cursor move funnels through
    // here - the ported grids, the Quest Journal's list, and its C-left/C-right paging through
    // RsMenu_SetCursorById - and a step the graph refuses never gets here, which is why an edge is
    // silent, as vanilla is.
    if (CursorNodes()[(size_t)next].hand >= 0) {
        ArmPageSwitchTimer();
        RsMenu_PlaySfx(RS_MENU_SFX_HAND);
    } else {
        RsMenu_PlaySfx(RS_MENU_SFX_CURSOR);
    }
    // Rebuilt now rather than next tick, so a page that scrolls to follow its cursor (the quest
    // list) has already scrolled by the time anything draws or a console line reads the graph.
    RebuildCursorGraph();
}

bool RsMenu_StickStepped() {
    return sStickStepped;
}

bool RsMenu_MoveCursor(int32_t dx, int32_t dy) {
    const int32_t next = CursorEdge(RsMenu_CursorAt(sCursorIndex), dx, dy);
    if (next < 0) {
        return false;
    }
    JumpCursor(next);
    return true;
}

// #126: one tick of stick on a ported vanilla page, both axes at once, the way that page's kaleido
// code resolves them (RsMenu.h, RsMenuStickModel). A tick that starts on a hand - kaleido's page
// arrow - only steps off it. On Select Item that is because the step-off leaves the local cursorItem
// at PAUSE_ITEM_NONE, which skips the vertical branch (z_kaleido_item.c:626); on Equipment the
// vertical branch sits inside the not-on-an-arrow block (z_kaleido_equipment.c:212-361). A
// SEQUENTIAL diagonal is two moves, so it counts two in `moves=`.
static void KaleidoStickMove(RsMenuStickModel model, int32_t dx, int32_t dy) {
    const RsMenuCursorNode* start = RsMenu_CursorAt(sCursorIndex);
    if (start == nullptr || (dx == 0 && dy == 0)) {
        return;
    }
    if (start->hand >= 0) {
        if (dx != 0) {
            RsMenu_MoveCursor(dx, 0);
        }
        return;
    }
    if (model == RS_MENU_STICK_KALEIDO_SEQUENTIAL) {
        // Horizontal, then vertical from where it landed - unless it landed on a hand.
        if (dx != 0) {
            RsMenu_MoveCursor(dx, 0);
        }
        const RsMenuCursorNode* mid = RsMenu_CursorAt(sCursorIndex);
        if (dy != 0 && mid != nullptr && mid->hand < 0) {
            RsMenu_MoveCursor(0, dy);
        }
        return;
    }
    // KALEIDO_ORIGIN: both targets from the start; a hand wins, then the vertical target, then the
    // horizontal one (z_kaleido_collect.c:106-152 - the vertical write lands last, but a page arrow is
    // a special position the point write does not leave).
    const int32_t x = dx != 0 ? CursorEdge(start, dx, 0) : -1;
    const int32_t y = dy != 0 ? CursorEdge(start, 0, dy) : -1;
    const RsMenuCursorNode* xNode = RsMenu_CursorAt(x);
    int32_t next = -1;
    if (xNode != nullptr && xNode->hand >= 0) {
        next = x;
    } else if (y >= 0) {
        next = y;
    } else {
        next = x;
    }
    if (next >= 0) {
        JumpCursor(next);
    }
}

bool RsMenu_SetCursorById(const char* id) {
    if (id == nullptr) {
        return false;
    }
    const std::vector<RsMenuCursorNode>& nodes = CursorNodes();
    for (int32_t i = 0; i < (int32_t)nodes.size(); i++) {
        if (nodes[(size_t)i].id == id) {
            // THROUGH JumpCursor, not beside it. This used to hand-copy its body, and every time
            // JumpCursor grew something the copy silently did not: #129's page-switch arming had to be
            // added here by hand, and #131's sound was missed outright - which made the Quest Journal's
            // C-left/C-right paging, which is this function, the one highlight move in the menu that
            // made no noise. An arrival is an arrival, whichever door it came through.
            JumpCursor(i);
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
        case RS_MENU_SELECT_DESCEND:
            return "descend";
    }
    return "unknown";
}

RsMenuSelectResult RsMenu_SelectCursor() {
    const RsMenuCursorNode* node = RsMenu_CursorAt(sCursorIndex);
    if (node == nullptr) {
        return RS_MENU_SELECT_NONE;
    }
    if (sSweepActive || sLevelActive) {
        return RS_MENU_SELECT_BUSY;
    }
    if (node->hand < 0) {
        // An item leads down a level if its page has a detail view for it - and only then.
        if (!RsMenu_Descend()) {
            return RS_MENU_SELECT_ITEM;
        }
        sCursorSelects++;
        return RS_MENU_SELECT_DESCEND;
    }
    sCursorSelects++;
    // "Selecting a hand does what L/R does" - the settled design, verbatim. The left hand rolls
    // back, the right rolls forward, which is the same mapping the shoulders have.
    if (!RsMenu_StartSweep(HandRollDelta(node))) {
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
        std::snprintf(line, sizeof(line), "rs_menu boot button=START mask=0x%04X bindings=%d bound=1", trigger.mask,
                      trigger.bindings);
    } else {
        // Without this line an unbound trigger and a broken menu look identical from the log. It was
        // written for N64 L, which a GameCube pad cannot produce (research D.2); START, the trigger
        // since #138, is bound on every default mapping, so this branch should now never be taken.
        std::snprintf(line, sizeof(line),
                      "rs_menu boot button=START mask=0x%04X bindings=0 bound=0 "
                      "hint=rebind_in_settings_configure_controller", trigger.mask);
    }
    AgentTest_WriteMarker(line);
}

// --- hooks ---------------------------------------------------------------------------------------

// THE START VETO (stage 6; docs/decisions/2026-09-19-pause-menu-start-veto.md). VB_OPEN_PAUSE_MENU
// wraps the bare START check at z_kaleido_setup.c:26, so this is consulted INSIDE
// KaleidoSetup_Update - after every OnGameStateMainStart hook has run, AgentTest's injection
// included - and there is no hook order left to lose. It replaces the stage-1 input filter, which
// cleared BTN_START out of the input struct from an OnGameStateMainStart hook and lost a
// hash-bucket race to the harness: GameInteractor runs a hook type's callbacks in unordered_map
// order (GameInteractor.h:226), the filter ran first, and an injected START was put back after it
// had been wiped. That filter is deleted rather than left as a second mechanism.
//
// Called only when KaleidoSetup_Update's own guard has already passed, which requires
// `pauseCtx.state == 0` - so the save prompt's and the continue prompt's START
// (z_kaleido_scope_PAL.c:4437, :4707) never reach this, which is what the old filter had to gate on
// by hand. `*should` arrives as "START was pressed this frame".
static void RsMenu_OnShouldOpenPauseMenu(bool* should) {
    if (!InNormalPlay()) {
        return;
    }
    const bool take = MenuIsUp() || (RsMenu_IsEnabled() && RsMenu_GetPrimary() == RS_MENU_PRIMARY_CUSTOM);
    if (!take) {
        return;
    }
    sFilterArmedFrames++;
    sFilterPressSeen |= (uint32_t)gPlayState->state.input[0].press.button;
    if (*should) {
        *should = false;
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
        ResetPage(sPage); // #127: the page being rolled away drops any running state
        sPage = sSweepTo;
        sPageChanges++;
        // #129: THE CURSOR LANDS ON THE OPPOSITE HAND, wherever it started - on an item as much as on
        // a hand. That is vanilla's rule verbatim (KaleidoScope_SwitchPage, z_kaleido_scope_PAL.c:1254):
        // rolling forward (R) parks it on PAUSE_CURSOR_PAGE_LEFT, rolling back (L) on
        // PAUSE_CURSOR_PAGE_RIGHT, because the page you were looking at has rotated in from the far
        // side and the node you were on is now at the near edge. Up to #129 this was the reverse - the
        // cursor followed the hand the shoulder moved, and only when it had been on an item.
        sCursorId = sSweepDir > 0 ? "hand_left" : "hand_right";
        // And rebuilt NOW, not at the top of the next tick. Otherwise the frame drawn after the swap
        // pairs the new page with the old page's graph, and the cursor's stale item box draws in
        // the middle of a page that does not own it - the flicker Spencer caught in game after stage 6.
        RebuildCursorGraph();
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

static bool PageHasDetail() {
    const RsMenuPage* page = RsMenu_PageAt(sPage);
    return page != nullptr && page->detailDraw != nullptr;
}

// Starts the level gesture from the level it has to start from. Refused while anything is
// animating, and while the ring is on a page with no detail view - there is nothing to go down to.
static bool StartLevel(int32_t dir) {
    if (sLevelActive || sSweepActive || !PageHasDetail()) {
        return false;
    }
    if ((dir > 0 && sLevel != 0) || (dir < 0 && sLevel != 1)) {
        return false;
    }
    sLevelActive = true;
    sLevelTick = 0;
    sLevelDir = dir > 0 ? 1 : -1;
    return true;
}

// One game tick of the level gesture. The level changes in the middle of the turn, where the scroll
// is shut - the same rule as the page swap in the middle of a roll, and for the same reason: there
// is nothing drawn on the paper to smear.
static void AdvanceLevel() {
    if (!sLevelActive || sLevelHold) {
        return;
    }
    sLevelTick++;
    if (sLevelTick == kLevelSwapTick) {
        sLevel = sLevelDir > 0 ? 1 : 0;
        sLevelSwaps++;
        // The graph belongs to the level: empty in a detail, the page's items back on the way up.
        RebuildCursorGraph();
    }
    if (sLevelTick >= kLevelTicks) {
        sLevelActive = false;
        sLevelTick = 0;
        if (sLevelLoop) {
            StartLevel(-sLevelDir);
        }
    }
}

// Straight back to the top level with nothing moving - what opening, closing and an instant page
// change all want. Not an animation and not counted as a swap.
static void ResetLevel() {
    sLevel = 0;
    sLevelActive = false;
    sLevelTick = 0;
    sLevelLoop = false;
    sLevelHold = false;
}

// #127: tells a page it is no longer the one on show, or that the menu is closing (RsMenu.h, `reset`).
static void ResetPage(int32_t index) {
    const RsMenuPage* page = RsMenu_PageAt(index);
    if (page != nullptr && page->reset != nullptr) {
        page->reset(index, page->userData);
    }
}

// #127: what the page on show is holding this tick (RsMenuHold), set by the update before navigation.
static uint32_t sPageHold = RS_MENU_HOLD_NONE;

// #125: the button states for the page on show - the roll's target from the moment it starts - written
// into gSaveContext.buttonStatus. When they change (and on open, `force`) the HUD mode is taken to 0 and
// back to ALL, which is how kaleido makes the interface re-tween the button alphas on a page switch
// (KaleidoScope_SwitchPage, z_kaleido_scope_PAL.c:1288-1289): enabled buttons rise to 255, disabled
// ones settle at 70. A page with no `hud` callback gets vanilla's buttons for a page with nothing to
// equip (Quest Status, Map): B and A only, every C button and D-pad slot dimmed. The Quest Journal
// starts from this default and dims A too while the cursor is on a hand (QuestPageHud).
//
// It used to leave C-left and C-right lit because they page the journal's list. But a status belongs
// to the BUTTON, not the item on it, so the journal lit whichever items sat on C-left and C-right and
// dimmed C-down. With the Hookshot and Bombs on the sides and the Ocarina below, that read as the
// reverse of indoor gameplay (Spencer's play test, 2026-09-23). The paging still works; it is just
// not advertised.
static void ApplyPageHud(bool force) {
    const int32_t pageIndex = sSweepActive ? sSweepTo : sPage;
    const RsMenuPage* page = RsMenu_PageAt(pageIndex);
    u8 status[9] = { BTN_ENABLED,  BTN_DISABLED, BTN_DISABLED, BTN_DISABLED, BTN_ENABLED,
                     BTN_DISABLED, BTN_DISABLED, BTN_DISABLED, BTN_DISABLED };
    if (page != nullptr && page->hud != nullptr) {
        page->hud(pageIndex, status, page->userData);
    }
    bool changed = force;
    for (int32_t i = 0; i < 9; i++) {
        if (gSaveContext.buttonStatus[i] != status[i]) {
            gSaveContext.buttonStatus[i] = status[i];
            changed = true;
        }
    }
    if (changed) {
        RetweenHud(HeldHudMode());
    }
}

// One axis of kaleido's stick filter (z_kaleido_scope_PAL.c:1538-1588), verbatim but for returning the
// step instead of zeroing stickRel: a push past 30 steps at once, then holds for XREG(8) ticks, then
// steps every XREG(6) + 1 ticks (10 and 2 by default, z_construct.c:327-329: ticks 0, 11, 14, 17...).
// Back inside 30, or a push the other way, starts over. Returns -1, 0 or +1 in the stick's own sign.
//
// Kaleido runs this every frame in state 6, a page turn included, on the live `input->rel`
// (KaleidoScope_Draw reloads stickRel at :3506 before KaleidoScope_DrawPages filters it), and the
// cursor code ignores the result while the page turns (unk_1E4 != 0). The menu does the same: the
// filter runs every tick and UpdateNavigation drops its step while anything animates.
//
// NOT PORTED, around it: kaleido's D-pad repeat (:1492-1535), which exists only under SoH's
// DpadHoldChange + DPadOnPause - the scroll's D-pad walks the cursor whatever DPadOnPause says, one
// step per press. Its neighbour the page-switch timer (:1319-1336) is ported WHOLE, D-pad half
// included, in PageSwitchOutward below (#129).
static int32_t KaleidoStickAxis(int32_t rel, KaleidoStickAxisState* axis) {
    const int16_t want = rel < -kKaleidoStick ? -1 : rel > kKaleidoStick ? 1 : 0;
    if (want == 0) {
        axis->dir = 0;
        return 0;
    }
    if (axis->dir != want) {
        axis->timer = XREG(8);
        axis->dir = want;
        return want;
    }
    if (--axis->timer < 0) {
        axis->timer = XREG(6);
        return want;
    }
    return 0;
}

// #129: kaleido's pageSwitchTimer (KaleidoScope_HandlePageToggles, z_kaleido_scope_PAL.c:1319-1336),
// ported line for line. The stick pushed OUTWARD on a page arrow - left on the left one, right on the
// right one - turns the page: the timer sits at -1 while nothing is pushing, so the first tick of a
// push reaches 0 and fires at once, and a stick held past ten more ticks fires again. Our hands are
// kaleido's arrows, and the roll a hand implies is the one A and its shoulder already start: the left
// hand rolls back, the right rolls forward.
//
// The repeat does arrive, but not on its own: the roll lands the cursor on the OPPOSITE hand, where the
// same held direction points INWARD, so the stick spends the next ticks walking back across the page.
// It rolls again when it reaches the far hand AND has held ten more ticks there - see JumpCursor, which
// arms the timer at 0 on arrival the way kaleido's MoveCursorToSpecialPos does.
//
// THE D-PAD DOES THE SAME, and GATED ON DPadOnPause exactly as vanilla gates it (:1319). That gate is
// not decoration, which is why this follows it even though the scroll ignores the same setting for the
// cursor: SoH makes the two D-pad features MUTUALLY EXCLUSIVE, and the fork already ports the rule.
// RsVanilla_EquipButtons gives the D-pad to equipping when DpadEquips is on and either DPadOnPause is
// OFF or C-up is held (z_kaleido_item.c:696-701) - so "DPadOnPause off" is precisely the configuration
// in which a D-pad press is meant to be an equip. An ungated roll would fire there, over the top of the
// page's own claim on those bits, which is the collision Spencer called out (2026-09-22).
//
// Vanilla's HELD mask, not the press edge, exactly as :1321 reads it - and the raw mask rather than the
// page-claimed one, because kaleido's page toggles run before and regardless of its equip code. That is
// safe only BECAUSE of the gate: with DPadOnPause on, the D-pad is not an equip button unless C-up is
// held, and a hand has no item to equip anyway.
//
// Returns whether it rolled. `sPageSwitchTimer` is frozen, not reset, while a roll runs - UpdateNavigation
// returns before this - which is again kaleido, whose HandlePageToggles is not called during a page turn.
constexpr int32_t kPageSwitchRepeat = 10;

static void ArmPageSwitchTimer() {
    sPageSwitchTimer = 0;
}

static bool PageSwitchOutward(const Input* input) {
    const RsMenuCursorNode* node = RsMenu_CursorAt(sCursorIndex);
    const int32_t rel = input->rel.stick_x;
    const u16 held = input->cur.button;
    // Counted apart, because the counter's whole job is to say WHICH of the things that can roll did:
    // a shoulder, A, the stick and the D-pad all leave the cursor on the same node.
    const bool stickOut =
        node != nullptr && ((node->hand == 0 && rel < -kKaleidoStick) || (node->hand == 1 && rel > kKaleidoStick));
    const bool dpadOnPause = CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) != 0;
    const bool dpadOut = dpadOnPause && node != nullptr &&
                         ((node->hand == 0 && CHECK_BTN_ALL(held, BTN_DLEFT)) ||
                          (node->hand == 1 && CHECK_BTN_ALL(held, BTN_DRIGHT)));
    const bool outward = stickOut || dpadOut;
    if (!outward) {
        sPageSwitchTimer = -1;
        return false;
    }
    if (sPageSwitchTimer < kPageSwitchRepeat) {
        sPageSwitchTimer++;
    }
    if (sPageSwitchTimer != 0 && sPageSwitchTimer < kPageSwitchRepeat) {
        return false;
    }
    if (!RsMenu_StartSweep(HandRollDelta(node))) {
        // Fewer than two pages, or something is already animating. Vanilla's timer would keep climbing
        // here and its switch cannot fail; the clamp above keeps the retry every-tick either way and
        // stops a stick held for an hour on a one-page ring from running the counter off the end.
        return false;
    }
    // Both when both are pushed, which is what vanilla's `||` means: neither is the cause on its own.
    if (stickOut) {
        sStickRolls++;
    }
    if (dpadOut) {
        sDpadRolls++;
    }
    return true;
}

// The cursor's own input, kept apart from the page ring's because they answer different buttons:
// the shoulders roll the scroll directly, the D-pad and the stick walk the graph, and A selects.
// The stick needs an edge of its own - a held stick would otherwise walk the cursor once per tick.
//
// At level 1 there is no graph: up/down are handed to the page as `navY` (the journal scrolls with
// them) and left/right/A do nothing. Nothing here runs while the level gesture is moving.
static void UpdateNavigation(const Input* input) {
    const RsMenuPage* page = RsMenu_PageAt(sPage);
    // Stage 8: D-pad bits the page takes for itself (kaleido's DpadEquips gives D-pad presses to
    // equipping), removed before the cursor can walk on them. Only the cursor loses them - the page's
    // input callback below still sees the whole press.
    u16 navPress = input->press.button;
    if (sLevel == 0 && page != nullptr && page->claim != nullptr) {
        const u16 claimed = page->claim(sPage, input->cur.button, page->userData);
        navPress = (u16)(navPress & ~(claimed & (BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT)));
    }
    const u16 press = input->press.button;
    int32_t navX = 0;
    int32_t navY = 0;
    if (CHECK_BTN_ALL(navPress, BTN_DLEFT)) {
        navX = -1;
    } else if (CHECK_BTN_ALL(navPress, BTN_DRIGHT)) {
        navX = 1;
    } else if (CHECK_BTN_ALL(navPress, BTN_DUP)) {
        navY = -1;
    } else if (CHECK_BTN_ALL(navPress, BTN_DDOWN)) {
        navY = 1;
    }

    const int32_t sx = input->cur.stick_x;
    const int32_t sy = input->cur.stick_y;
    int32_t stickX = 0;
    int32_t stickY = 0;
    if (!sStickLatchX && (sx > kStickPress || sx < -kStickPress)) {
        stickX = sx > 0 ? 1 : -1;
        sStickLatchX = true;
    } else if (sStickLatchX && sx < kStickRelease && sx > -kStickRelease) {
        sStickLatchX = false;
    }
    // The stick is y-UP on this pad (forward is positive), and the graph's `up` is the screen's, so
    // the sign flips here rather than at every call site.
    if (!sStickLatchY && (sy > kStickPress || sy < -kStickPress)) {
        stickY = sy > 0 ? -1 : 1;
        sStickLatchY = true;
    } else if (sStickLatchY && sy < kStickRelease && sy > -kStickRelease) {
        sStickLatchY = false;
    }
    // A ported vanilla page walks on kaleido's filter instead (#126, Spencer: port the repeat). The
    // latch and the filter both run every tick, whichever page is up and whether or not it animates.
    const int32_t kx = KaleidoStickAxis(input->rel.stick_x, &sKaleidoX);
    const int32_t ky = KaleidoStickAxis(input->rel.stick_y, &sKaleidoY);
    sStickStepped = kx != 0 || ky != 0;
    const RsMenuStickModel stickModel =
        sLevel == 0 && page != nullptr ? page->stickModel : RS_MENU_STICK_LATCH;

    // NOTHING NAVIGATES WHILE ANYTHING IS ANIMATING - a roll or a level change. The content is not
    // on screen then, so a press would move a cursor nobody can see, scroll a list behind a shut
    // scroll, or select a row that is being blinked out (Spencer, after stage 6: up/down still walked
    // the quest rows mid-roll). The stick state above is still updated: a latched stick held through
    // the animation does not fire the moment it ends, and a ported page's filter keeps counting, so a
    // held stick repeats on kaleido's schedule once it does. B and START are handled by the caller and stay
    // live on purpose: they are the way out, and closing mid-roll just drops the half-shut scroll.
    if (sLevelActive || sSweepActive || RsVanilla_EquipFlightActive() || (sPageHold & RS_MENU_HOLD_CURSOR)) {
        return;
    }

    if (sLevel != 0) {
        const int32_t dy = navY != 0 ? navY : stickY;
        if (page != nullptr && page->input != nullptr) {
            page->input(sPage, 1, press, dy, page->userData);
        }
        return;
    }

    // #129: the stick pushed OUTWARD on a hand rolls the page, before the cursor is given anything -
    // kaleido's order, where KaleidoScope_HandlePageToggles runs in the Update and the page's own
    // cursor code in the Draw, and a switch sets unk_1E4 so that frame's cursor and equip buttons are
    // both skipped. So a tick that rolls does nothing else, exactly as a shoulder press does.
    if (!(sPageHold & RS_MENU_HOLD_ROLL) && PageSwitchOutward(input)) {
        return;
    }

    if (navX != 0 || navY != 0) {
        RsMenu_MoveCursor(navX, navY);
    }
    if (stickModel != RS_MENU_STICK_LATCH) {
        KaleidoStickMove(stickModel, kx, -ky); // the stick is y-up, the graph's `up` is the screen's
    } else {
        if (stickX != 0) {
            RsMenu_MoveCursor(stickX, 0);
        }
        if (stickY != 0) {
            RsMenu_MoveCursor(0, stickY);
        }
    }
    // The page's own buttons, AFTER the move - kaleido's order: it moves its cursor, then tests the equip
    // buttons against where the cursor now is (z_kaleido_item.c:694). The quest list pages with
    // C-left/C-right here; the ported pages equip.
    if (page != nullptr && page->input != nullptr) {
        page->input(sPage, 0, press, 0, page->userData);
    }

    if (CHECK_BTN_ALL(press, BTN_A) && !(sPageHold & RS_MENU_HOLD_A)) {
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
            // #127: but the pages still drop their state - a song's BGM mute is the audio thread's, and
            // it would outlast the menu otherwise.
            for (int32_t i = 0; i < RsMenu_PageCount(); i++) {
                ResetPage(i);
            }
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
        // #125: the HUD, re-asserted the same way. The page's button states follow the page (the roll's
        // target from the moment it starts, as kaleido switches them when its turn starts), and the mode
        // stays HUD_VISIBILITY_ALL. The interface's per-frame button check (func_80083108) would
        // otherwise rewrite the states and the mode - it forced the HUD back on over the stage-8 scroll -
        // and it stands down through VB_UPDATE_HUD_BUTTON_STATUS while the menu is up, as vanilla skips it
        // under kaleido. So `hud_reasserts=` counts something else changing the mode, and should stay 0.
        if (sHudApplied) {
            ApplyPageHud(false);
            if (gSaveContext.hudVisibilityMode != HeldHudMode()) {
                RetweenHud(HeldHudMode());
                sHudReasserts++;
            }
            // The START button fades with the entry slide, as kaleido fades it over its open and close.
            play->interfaceCtx.startAlpha = (s16)(255.0f * StartShown());
            ApplyLevelHud(play);
            ApplyLevelBLabel(play);
        }
        // The flying equip icon moves on the game tick, before any input: kaleido's UpdateItemEquip runs
        // in its update, and the flight a press starts first moves on the next tick.
        RsVanilla_UpdateEquipFlight(play);
        sOpenFrames++;
        // The probe's clock is the GAME tick, which is the whole point: it is the 20 Hz lattice the
        // rendered frames are measured against. Advanced whether or not the probe is on, so
        // switching it on does not start from a stale phase.
        sProbePhase = (sProbePhase + 1) % kProbePeriod;
        RebuildCursorGraph();
        RecordOfferedInput(input);

        // The arrival, driven off the same game tick as everything else. The world stays frozen and
        // the HUD stays in its pause configuration for the whole of both slides - un-freezing halfway
        // down would show the world moving under a menu that is still on screen.
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
        AdvanceLevel();

        // #125: while an equipped icon is flying to its button nothing else is taken - kaleido is in its
        // sub-state 3 then, and its cursor, L/R, B and START all wait for 0 (z_kaleido_scope_PAL.c:4261).
        // UpdateNavigation still runs, so the stick state keeps counting, but moves nothing.
        if (RsVanilla_EquipFlightActive()) {
            UpdateNavigation(input);
            return;
        }

        // #127: what the page on show is holding, asked BEFORE anything acts on this tick's input, so it
        // describes the state that input arrived in: a B the page spends leaving a song must not also
        // reach the menu once the song has gone (the first p1 run: B in the play-along left the song,
        // then closed the scroll). The page's own tick runs at the end, after the cursor has moved, as
        // kaleido moves its cursor before its song logic looks at it.
        sPageHold = RS_MENU_HOLD_NONE;
        const RsMenuPage* holdPage = RsMenu_PageAt(sPage);
        const bool pageSettled = holdPage != nullptr && sLevel == 0 && !sSweepActive && !sLevelActive;
        if (pageSettled && holdPage->hold != nullptr) {
            sPageHold = holdPage->hold(sPage, holdPage->userData);
        }

        // START closes from anywhere, at either level - it is the "get me out" button - but not while
        // the scroll is animating. Vanilla takes no input while a page turns (kaleido's unk_1E4 != 0), and
        // a close landing mid-roll or mid-turn cut the gesture off halfway (Spencer, 2026-09-23). The
        // entry slides already drop it: the update returns before input while they run.
        const bool animating = sSweepActive || sLevelActive;
        if (startEdge && !(sPageHold & RS_MENU_HOLD_START)) {
            if (animating) {
                sCloseDropped++;
            } else {
                sStartConsumed++;
                RsMenu_BeginClose();
                return;
            }
        }
        // B is "back": up a level from a detail view, and out of the menu from the top. Dropped
        // while anything is animating, like every other press that would start an animation on top of
        // one - otherwise a B landing mid-descent would close a menu that was on its way down into
        // something, and a B mid-roll closed it halfway through the roll.
        if (CHECK_BTN_ALL(input->press.button, BTN_B) && !(sPageHold & RS_MENU_HOLD_B)) {
            if (animating) {
                sCloseDropped++;
            } else if (sLevel != 0) {
                RsMenu_Ascend();
            } else {
                RsMenu_BeginClose();
                return;
            }
        }
        // The shoulders roll the scroll, on the top level only - a detail view is below the ring,
        // not on it. A press arriving mid-animation is dropped rather than queued: a queue would let
        // a run assert a page the animation never actually rolled to.
        //
        // Z rolls LEFT as well as L. On a GameCube pad the left trigger IS N64 Z (SoH's default
        // mapping: lefttrigger -> Z, righttrigger -> R, and N64 L sits on SDL leftshoulder, which no
        // GC pad has), so without this a GC player could roll right and never left. Vanilla kaleido
        // pages with exactly this pair - Z left, R right - and never reads N64 L at all. Z has no
        // other job while the scroll is up: the world, and Z-targeting with it, is frozen.
        if (sLevel == 0 && !sLevelActive && !(sPageHold & RS_MENU_HOLD_ROLL)) {
            if (CHECK_BTN_ALL(input->press.button, BTN_L) || CHECK_BTN_ALL(input->press.button, BTN_Z)) {
                RsMenu_StartSweep(-1);
            } else if (CHECK_BTN_ALL(input->press.button, BTN_R)) {
                RsMenu_StartSweep(1);
            }
        }
        UpdateNavigation(input);
        // Still the same page, still settled (a roll started this tick is not): the page's tick.
        if (pageSettled && RsMenu_PageAt(sPage) == holdPage && !sSweepActive && !sLevelActive &&
            holdPage->tick != nullptr && MenuIsUp() && sPhase != RS_MENU_PHASE_CLOSING) {
            holdPage->tick(sPage, input->press.button, holdPage->userData);
        }
        return;
    }

    // START alone (#138): N64 L is the minimap's.
    if (startEdge) {
        sStartConsumed++;
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
    sDrawIcons = 0;
    sCursorDrawn = false;
    sPauseLinkRequested = false;

    std::vector<Gfx>& dl = MenuDl();
    dl.clear();
    sListState = RS_LIST_OTHER;

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
    // #128: the stepper, in the same state and under the same identity; everything after draws over it.
    DrawStepper();
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

    // --- the page content, on a key CARRYING THE PAGE INDEX AND THE LEVEL -------------------------
    // The tick the content changes, this key no longer matches last tick's tree, so the node
    // interpolates against itself and renders at its exact tick position instead of lerping between
    // two different pages' glyphs (frame_interpolation.cpp:300-307). That is the whole anti-smear
    // mechanism, and it is the case vanilla's single `state + pageIndex * 100` node does not have -
    // vanilla's content changes BETWEEN frames, this one changes MID-SWEEP. The level is in the key
    // for the same reason: the list and a journal are different content on the same page.
    FrameInterpolation_RecordOpenChild(sNodeContent, sPage * 2 + sLevel);
    PushTextState();
    Matrix_Push();
    ApplyBaseMatrix();
    PushCurrentMatrix();
    // 1.0 OF THE CLOSE: the content is simply absent while the scroll is moving. It blinks out the
    // tick the gesture starts and the new page blinks in when the paper is open again, because
    // there is no paper under it in between.
    //
    // âš  This is a branch around GEOMETRY, never around a Matrix_* op - the chain above is emitted
    // on every frame whatever this decides. Glyph count is invisible to the interpolation recorder,
    // which records only Matrix_* calls and child open/close (SOH_2D_DRAWING.md, measured at stage
    // 4 after the opposite was written down first).
    //
    // 2.0 replaces this with a scissor: reveal the content up to the moving roll's trailing edge,
    // so it wipes rather than blinks. The trailing edge matters - a scissor is an immediate value
    // and steps at 20 Hz while the roll glides, so carving at the smaller of last tick's and this
    // tick's edge keeps the roll overdrawing the seam instead of uncovering it. Same problem and
    // same answer as the letterbox's ShrinkWindow_GetSafeVal.
    //
    // The level gesture follows the same 1.0 rule: the list blinks out when the close starts and the
    // journal blinks in when the vertical open has finished, and back again on the way up.
    // Stage 7's view counters bracket exactly the body call, and nothing else in this node.
    sViewName = "none";
    sViewRowYs.clear();
    sViewIcons = 0;
    const int32_t glyphsBeforeView = sDrawGlyphs;
    const size_t wordsBeforeView = dl.size();
    // The cursor is stricter (#124, Spencer's call): no box, on an item or on a hand, for the whole of a
    // roll or a level change. The content's width test alone would leave it up on a roll's tick 0,
    // where the paper is still fully open. It comes back once the gesture is over, on whichever node
    // the gesture left it - the hand, after a roll.
    const bool contentShown = SweepWidth() > 0.999f && !sLevelActive;
    const bool cursorShown = contentShown && !sSweepActive;
    if (contentShown) {
        sInView = true;
        if (sLevel != 0) {
            if (page->detailDraw != nullptr) {
                sViewName = "detail";
                page->detailDraw(play, sPage, page->userData);
            }
        } else if (sStressGlyphs >= 0) {
            sViewName = "stress";
            DrawStressBody();
        } else if (page->draw != nullptr) {
            sViewName = "page";
            page->draw(play, sPage, page->userData);
        } else {
            sViewName = "page";
            DrawGreyboxBody(sPage, *page);
        }
        sInView = false;
    }
    sViewFrame = sDrawFrames;
    sViewRows = (int32_t)sViewRowYs.size();
    sViewGlyphs = sDrawGlyphs - glyphsBeforeView;
    sViewWords = (int32_t)(dl.size() - wordsBeforeView);
    {
        // An item's yellow box - unless its page draws its own marker (the quest list's arrow).
        const RsMenuCursorNode* node = RsMenu_CursorAt(sCursorIndex);
        if (cursorShown && node != nullptr && node->hand < 0 && !page->ownsItemHighlight) {
            PushFlatState();
            DrawCursorOutline(*node);
            sCursorDrawn = true;
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
        // The hand gets its own matrix on top of the side's: the slide and the swivel (both zero at level 0).
        ApplyHandSwivel(side);
        PushCurrentMatrix();
        DrawHand(side);
        MeasureHand(side);
        const RsMenuCursorNode* node = RsMenu_CursorAt(sCursorIndex);
        if (cursorShown && node != nullptr && node->hand == side) {
            DrawCursorOutline(*node);
            sCursorDrawn = true;
        }
        Matrix_Pop();
    }
    FrameInterpolation_RecordCloseChild();

    // --- Link's portrait, rendered into its framebuffer (stage 8) ------------------------------------
    // Only when a page composited it this frame (RsMenu_DrawPauseLink), and AFTER every menu node has
    // closed: Player_DrawPause records a skeleton's worth of Matrix_* ops, and inside the content node
    // they would come and go with the page. PauseLink.cpp wraps them in its own OPEN_DISPS node instead,
    // which is recorded identically on every frame it draws. The draw goes to WORK_DISP, which the RCP
    // runs first, so the quad composited above samples this frame's Link wherever it sits in the list.
    if (sPauseLinkRequested) {
        RsPauseLink_Render(play);
    }

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
        DrawProbeTextRect(play, probe, kProbeTextX, (int16_t)(kProbeTextY + ProbeDy() + kScrollDropY), 1.0f, 0,
                          255, 0, 255);
    }

    sDrawGfxCtx = nullptr;
}

static bool CloseMenu(bool syncPlayer);

static void RsMenu_OnSceneInit(int16_t sceneNum) {
    (void)sceneNum;
    // No Player sync here: OnSceneInit fires from Play_SpawnScene (z_play.c:486), before
    // Actor_InitContext (:595), so GET_PLAYER still reads the new PlayState's unset actor list. The
    // Player this scene spawns reads the save itself (Player_Init, z_player.c:10821).
    CloseMenu(false);
}

static void RsMenu_OnInterfaceDrawItemButtonsEnd() {
    RsVanilla_DrawEquipFlight(gPlayState);
}

// #130: THE VANILLA HUD IS RAISED, by half its distance from the top edge (Spencer, 2026-09-22) - in
// gameplay too, not only under the scroll. SoH already has the lever, so this is defaults, not code: the
// cosmetics' top margin (HUD.Margin.T, positive raises) moves each HUD element whose own "use margins"
// toggle is on, and moves nothing without it - every reader tests HUD.<element>.UseMargins before
// adding the margin (z_parameter.c, z_lifemeter.c). So both are set: the margin, and the toggle on the
// ten top-anchored vanilla elements. The minimap, rupees and keys anchor to the bottom margin and are
// left alone; the timers and scores read only the side margins.
//
// ONE number for all ten, because the margin is one number: 10, half the hearts' 21 (rounded down, since
// it is an integer). The hearts and magic are what the raise is for - it is what lets the scroll clear a
// double magic meter with a 3-unit drop (kScrollDropY) - so the buttons rise 10 too, a little more than
// their own halves (START 7, B and A 8.5).
//
// Registered, not set: CVarRegisterInteger writes only a CVar that does not exist yet
// (ConsoleVariable::RegisterInteger), so a player's own cosmetics win, and ShipInit re-running on every
// config load re-applies nothing that was changed. The Cosmetics editor's reset clears a CVar, which puts
// that element back at 0 until the next config load re-registers it. Kaleido's equip flight
// (z_kaleido_item.c:894) and the scroll's (EquipFlight.cpp, ButtonPositions) read the same CVars, so an
// equip still lands on the moved button.
constexpr int32_t kHudRaise = 10;
static const char* const kHudRaisedElements[] = {
    "Hearts",       "MagicBar",    "BButton",     "AButton",    "StartButton",
    "CLeftButton",  "CDownButton", "CRightButton", "CUpButton", "Dpad",
};

// The element's own "use margins" toggle, e.g. gCosmetics.HUD.MagicBar.UseMargins.
static std::string UseMarginsCVar(const char* element) {
    return std::string(CVAR_COSMETIC("HUD.")) + element + ".UseMargins";
}

static void RegisterRsHudDefaults() {
    CVarRegisterInteger(CVAR_COSMETIC("HUD.Margin.T"), kHudRaise);
    for (const char* element : kHudRaisedElements) {
        CVarRegisterInteger(UseMarginsCVar(element).c_str(), 1);
    }
}

// What the HUD raise leaves in force: the top margin and how many of the ten elements take it. Read back
// from the CVars rather than from the constants, so a player's own cosmetics show as what they are.
void RsMenu_HudRaise(int32_t* marginTop, int32_t* elementsOn, int32_t* elements) {
    *marginTop = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.T"), 0);
    *elementsOn = 0;
    *elements = 0;
    for (const char* element : kHudRaisedElements) {
        *elementsOn += CVarGetInteger(UseMarginsCVar(element).c_str(), 0) != 0 ? 1 : 0;
        (*elements)++;
    }
}

static void RegisterRsMenu() {
    RegisterRsHudDefaults();
    ClearStepperRamp(STEPPER_SLIDE_OPEN);
    ClearStepperRamp(STEPPER_SLIDE_CLOSE);
    // The Quest Journal is the ring's first page and the three ported vanilla pages follow it, and
    // all of them are registered from HERE rather than from ShipInits of their own: ShipInit functions
    // in different translation units run in no promised order, and the ring's order is what L/R walks.
    RsMenuQuestPage_Register();
    RsMenuVanillaPages_Register();
    // So `menu cursor` answers before the menu has ever been opened. The graph reads no PlayState,
    // so it is safe this early.
    RebuildCursorGraph();
    // COND_HOOK unregisters its previous hook before registering, so a ShipInit re-run (every
    // config and preset load) leaves exactly one of each. The hooks are registered unconditionally
    // and the CVar is read inside them: SoH has no console `set`, so a CVar the console writes
    // would never take effect if it were baked into a COND_HOOK condition at ShipInit time.
    COND_HOOK(OnGameFrameUpdate, true, RsMenu_OnGameFrameUpdate);
    COND_HOOK(OnPlayDrawEnd, true, RsMenu_OnPlayDrawEnd);
    COND_HOOK(OnSceneInit, true, RsMenu_OnSceneInit);
    // The START veto. Same unconditional registration and in-handler CVar read, for the same
    // reason: `menu primary` writes a CVar mid-session and nothing re-runs ShipInit when it does.
    COND_VB_SHOULD(VB_OPEN_PAUSE_MENU, true, { RsMenu_OnShouldOpenPauseMenu(should); });
    // #125: while the menu is up the interface's button check stands down (its per-page button states
    // hold), the START button draws, and the flying equip icon draws over the HUD's buttons.
    COND_VB_SHOULD(VB_UPDATE_HUD_BUTTON_STATUS, true, {
        if (MenuIsUp()) {
            *should = false;
        }
    });
    COND_VB_SHOULD(VB_DRAW_PAUSE_START_BUTTON, true, {
        if (MenuIsUp() && sHudShown) {
            *should = true;
        }
    });
    // ...and the pieces vanilla draws only while unpaused (the minimap, horse carrots, minigame scores)
    // hide, as they hide under kaleido.
    COND_VB_SHOULD(VB_DRAW_UNPAUSED_HUD, true, {
        if (MenuIsUp()) {
            *should = false;
        }
    });
    // C-up toggles a house's camera between fixed and pivoting (Play_Update) unless vanilla pause is up.
    // Player is frozen under the scroll but Play_Update is not, so without this C-up moved the camera
    // behind the menu (Spencer's play test, 2026-09-23, in Link's house).
    COND_VB_SHOULD(VB_TOGGLE_HOUSE_VIEWPOINT, true, {
        if (MenuIsUp()) {
            *should = false;
            sViewpointVetoes++;
        }
    });
    // #138: free look's right stick and mouse would turn the camera behind the scroll (Camera_Update runs
    // under it); refused while the scroll is up, which leaves the camera where it was pointed.
    COND_VB_SHOULD(VB_FREE_LOOK_TAKE_INPUT, true, {
        if (MenuIsUp()) {
            *should = false;
            sFreeLookVetoes++;
        }
    });
    // #130: B stands clear of the upright top hand from the level swap on (LevelBMoved), invisible when it
    // moves either way.
    COND_VB_SHOULD(VB_SHIFT_HUD_B_BUTTON, true, {
        if (MenuIsUp() && sHudShown && LevelBMoved()) {
            s16* dx = va_arg(args, s16*);
            s16* dy = va_arg(args, s16*);
            *dx = kLevelBShiftX;
            *dy = kLevelBShiftY;
            *should = true;
        }
    });
    COND_HOOK(OnInterfaceDrawItemButtonsEnd, true, RsMenu_OnInterfaceDrawItemButtonsEnd);
}

static RegisterShipInitFunc rsMenuInitFunc(RegisterRsMenu);

// --- public API ----------------------------------------------------------------------------------

int32_t RsMenu_RegisterPage(const char* id, const char* title, RsMenuPageDrawFn draw, RsMenuPageNodesFn nodes,
                            void* userData) {
    if (id == nullptr || title == nullptr) {
        return -1;
    }
    RsMenuPage page;
    page.id = id;
    page.title = title;
    page.draw = draw;
    page.nodes = nodes;
    page.userData = userData;
    return RsMenu_RegisterPageStruct(page);
}

int32_t RsMenu_RegisterPageStruct(const RsMenuPage& page) {
    if (page.id.empty()) {
        return -1;
    }
    std::vector<RsMenuPage>& pages = Pages();
    for (const RsMenuPage& existing : pages) {
        if (existing.id == page.id) {
            return -1;
        }
    }
    pages.push_back(page);
    return (int32_t)pages.size() - 1;
}

RsMenuRect RsMenu_PageRect() {
    // The horizontal parchment's inside, less the hands: they cover the panel's bottom corners out
    // to x 46 and in from x 274, so a page that stays between them never draws under a finger.
    constexpr int16_t kHandClear = 6;
    RsMenuRect rect;
    rect.x0 = (int16_t)(kHandBoxX + kHandBoxW + kHandClear);
    rect.x1 = (int16_t)(SCREEN_WIDTH - (kHandBoxX + kHandBoxW + kHandClear));
    rect.y0 = (int16_t)(kPanelY0 + kPanelBorder);
    rect.y1 = (int16_t)(kPanelY1 - kPanelBorder);
    return rect;
}

RsMenuRect RsMenu_DetailRect() {
    // The vertical parchment at rest, worked from the same constants the matrix chain uses. Turned
    // counter-clockwise about the pivot, the horizontal panel's top edge (game y 48) becomes its
    // LEFT edge and its bottom edge (y 192) its RIGHT edge; the rolls end up kVerticalSpan apart
    // across y. At each end the text band stops short of whichever reaches further into the page, the
    // roll or the fingers of the hand holding it (#130: upright, the fingers reach about 14 at the top
    // and 10 at the bottom, where the sideways hands reached 30 and 16); the margin keeps a glyph off a
    // knuckle. In the page's game space, like every rect here - the #130 drop is the matrix's.
    constexpr float kMargin = 5.0f;
    const float left = kPivotX - (kPivotY - (float)kPanelY0);
    const float right = kPivotX + ((float)kPanelY1 - kPivotY);
    const float topInward = std::max(kRollHalfWidth, HandRestInward(1));
    const float bottomInward = std::max(kRollHalfWidth, HandRestInward(0));
    RsMenuRect rect;
    rect.x0 = (int16_t)std::ceil(left + (float)kPanelBorder + kMargin);
    rect.x1 = (int16_t)std::floor(right - (float)kPanelBorder - kMargin);
    rect.y0 = (int16_t)std::ceil(kPivotY - kVerticalSpan / 2.0f + topInward + kMargin);
    rect.y1 = (int16_t)std::floor(kPivotY + kVerticalSpan / 2.0f - bottomInward - kMargin);
    return rect;
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
        case RS_MENU_OPEN_CUTSCENE:
            return "cutscene";
        case RS_MENU_OPEN_TEXTBOX:
            return "textbox";
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
    // #138 (Spencer, 2026-09-23): not over a cutscene or a textbox, as vanilla pause will not open over
    // one. The cutscene half is kaleido's own test (KaleidoSetup_Update, z_kaleido_setup.c:16-19); a
    // textbox is refused outright. Opened over a talk or cutscene camera, the scroll's own navigation
    // buttons ended it (Camera_KeepOn3, Camera_Unique0 and friends end on any press), because the
    // scroll freezes actors and not the camera.
    // The textbox first: talking puts Player in a cutscene mode too (Play_InCsMode), and "textbox" is
    // the truer name for a conversation (the first t1 run read `error=cutscene` mid-conversation).
    if (play->msgCtx.msgMode != MSGMODE_NONE) {
        sOpenRefused++;
        return RS_MENU_OPEN_TEXTBOX;
    }
    if (Play_InCsMode(play) || gSaveContext.cutsceneIndex >= 0xFFF0 || gSaveContext.nextCutsceneIndex >= 0xFFF0) {
        sOpenRefused++;
        return RS_MENU_OPEN_CUTSCENE;
    }
    if (RsMenu_PageCount() == 0) {
        return RS_MENU_OPEN_NO_PAGES;
    }
    if (sPage >= RsMenu_PageCount()) {
        sPage = 0;
    }

    sHaltPrev = play->haltAllActors;
    play->haltAllActors = 1;

    // #125 (Spencer: the whole HUD, as vanilla): the gameplay HUD stays up with the page's buttons live
    // or dimmed, B reads "Save" and A "Decide", and the START button fades in with the entry slide - what
    // kaleido does as it opens (z_kaleido_scope_PAL.c:3563-3584, :3981, :4237-4244). The button states
    // it found are put back on close.
    sHudPrev = gSaveContext.hudVisibilityMode;
    for (int32_t i = 0; i < 9; i++) {
        sButtonStatusSave[i] = gSaveContext.buttonStatus[i];
    }
    sHudApplied = true;
    // Opened over a HUD that something else had hidden (a cutscene, say), the menu keeps it hidden:
    // revealing it would be the menu breaking something it did not set, the close path's rule.
    sHudShown = sHudPrev != HUD_VISIBILITY_NOTHING && sHudPrev != HUD_VISIBILITY_NOTHING_ALT &&
                sHudPrev != HUD_VISIBILITY_NOTHING_INSTANT;
    // The page ring is settled here (no sweep yet), so this is the opening page's buttons.
    ApplyPageHud(true);
    // A's label now, B's once A's label change has finished - kaleido's order, which sets A while opening
    // (:3981) and loads B's label only once open (:3584). Both pass through doActionSegment[1], so B
    // loaded first gets A's "Decide" drawn over it (the first s1 run: `b_label=14` and "Decide" on
    // screen), and B loaded straight after would show on A mid-turn. The update loads it.
    if (sHudShown) {
        Interface_SetDoAction(play, DO_ACTION_DECIDE);
        sBLabelPending = true;
    }
    play->interfaceCtx.startAlpha = 0;

    // A menu that opens mid-sweep would draw a scroll frozen off-centre. Opening is the one place
    // the animation is reset rather than played out.
    sSweepActive = false;
    sSweepTick = 0;
    sStickLatchX = false;
    sStickLatchY = false;
    sKaleidoX = KaleidoStickAxisState();
    sKaleidoY = KaleidoStickAxisState();
    // It always opens on the page itself, never in a detail, with the cursor on the page's own entry
    // item - the quest list's last-visited row if it is still on screen, else its top visible row -
    // rather than on whatever node it was left on. An empty id is one no node carries, so the
    // rebuild's not-found rule places it.
    ResetLevel();
    sCursorId.clear();
    sCursorIndex = 0;
    RebuildCursorGraph();
    // Link's skeleton loads again on the first Equipment frame of every open, as kaleido loads it on
    // every pause - so an age change between opens gets the right Link.
    RsPauseLink_Invalidate();

    sPhase = RS_MENU_PHASE_OPENING;
    sEntryTick = 0;
    ClearStepperRamp(STEPPER_SLIDE_OPEN);
    sOpens++;
    // #131: after every refusal above, so a refused open is silent. The rebuild just above does NOT
    // move the cursor through JumpCursor, so opening plays this and nothing else - vanilla likewise
    // has no cursor sound as its menu comes up.
    RsMenu_PlaySfx(RS_MENU_SFX_OPEN);
    return RS_MENU_OPEN_OK;
}

bool RsMenu_BeginClose() {
    if (!MenuIsUp()) {
        return false;
    }
    if (sPhase == RS_MENU_PHASE_CLOSING) {
        return true; // already on its way down; do not restart the slide
    }
    // #127: a page's running state ends when the close begins, not after the slide - a song stops and its
    // mute lifts at once, as vanilla's START turns the instrument off at once (z_kaleido_scope_PAL.c:4313).
    for (int32_t i = 0; i < RsMenu_PageCount(); i++) {
        ResetPage(i);
    }
    // Reversing out of a half-finished arrival picks up where it got to rather than snapping to the
    // top first, which is one line and the difference between a slide and a jerk.
    sEntryTick = sPhase == RS_MENU_PHASE_OPENING ? kEntryTicks - sEntryTick : 0;
    sPhase = RS_MENU_PHASE_CLOSING;
    ClearStepperRamp(STEPPER_SLIDE_CLOSE);
    // #131: the SLIDE plays it, and the instant RsMenu_Close does not - that path is a scene load or
    // the CVar going off, not a player leaving, and it also runs at the END of this slide, where a
    // second sound would double every close.
    RsMenu_PlaySfx(RS_MENU_SFX_CLOSE);
    return true;
}

bool RsMenu_Close() {
    return CloseMenu(true);
}

// The instant close. `syncPlayer` is false only from scene init, where there is no Player yet.
static bool CloseMenu(bool syncPlayer) {
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
    // #129: ARMED, not cleared. -1 is the value that fires on the very next outward tick, so clearing it
    // would be the opposite of what this is for: the scroll can reopen with the cursor already on a
    // hand, which vanilla never does (kaleido opens with cursorSpecialPos 0), and a stick still held
    // from before the close would roll on the first tick. Armed at 0 it has to hold ten more, which is
    // what arriving on a hand costs everywhere else.
    ArmPageSwitchTimer();
    ResetLevel();
    if (gPlayState != nullptr) {
        gPlayState->haltAllActors = sHaltPrev;
    }
    // #127: every page drops its running state (a song stops, its mute lifts).
    for (int32_t i = 0; i < RsMenu_PageCount(); i++) {
        ResetPage(i);
    }
    sPageHold = RS_MENU_HOLD_NONE;
    // An icon still in the air lands now, so the equip the press started is not lost - except from scene
    // init, where the old interface is gone (`syncPlayer` false) and it is dropped.
    RsVanilla_FinishEquipFlight(syncPlayer ? gPlayState : nullptr);
    if (sHudApplied) {
        // Vanilla's close (z_kaleido_scope_PAL.c:4869-4880): the button states back as they were, the
        // rando's swordless temp-B hook, B's label and the START button gone, then the HUD mode.
        for (int32_t i = 0; i < 9; i++) {
            gSaveContext.buttonStatus[i] = sButtonStatusSave[i];
        }
        GameInteractor_Should(VB_TEMP_B_RESTORE_SWORDLESS, true);
        sBLabelPending = false;
        // #130: closed from level 1, A and B are brought back by the mode change below, which re-tweens
        // every alpha that is not already full; the flag must not survive to write them over the next
        // open's tween.
        sLevelHudTouched = false;
        if (syncPlayer && gPlayState != nullptr) {
            // unk_1FA: B draws its label rather than its item; unk_1FC: that label (z_parameter.c:2872-2878).
            gPlayState->interfaceCtx.unk_1FA = gPlayState->interfaceCtx.unk_1FC = 0;
            gPlayState->interfaceCtx.startAlpha = 0;
        }
        // Restore the mode that was there, with ONE rewrite: HUD_VISIBILITY_NO_CHANGE is 0, so restoring
        // it verbatim does nothing at all. The NOTHING modes restore as-is on purpose - the menu opened
        // over an already-hidden HUD (a cutscene, say), and revealing it on close would be the menu
        // breaking something it did not set. The mode is zeroed first, as vanilla does, so the change
        // always takes (the menu has held ALL, which may be the mode being restored).
        u16 restore = sHudPrev;
        if (restore == HUD_VISIBILITY_NO_CHANGE) {
            restore = HUD_VISIBILITY_ALL;
        }
        RetweenHud(restore);
        sHudApplied = false;
    }
    // Last, as in vanilla's close (:4882): the equipment pages wrote the save, and this is where Link
    // in the world puts it on. The portrait never needed it - it draws from the save every frame.
    if (syncPlayer) {
        RsMenuVanillaPages_SyncPlayer(gPlayState);
    }
    return true;
}

bool RsMenu_SetPage(int32_t index) {
    if (index < 0 || index >= RsMenu_PageCount()) {
        return false;
    }
    if (index != sPage) {
        sPageChanges++;
        ResetPage(sPage);
    }
    sPage = index;
    // An instant page change abandons any sweep in flight rather than letting it swap again on a
    // tick the caller cannot see, and stops a loop that would immediately swap it away again.
    sSweepActive = false;
    sSweepTick = 0;
    sSweepLoop = false;
    sSweepHold = false;
    // And back to the top level: a detail view belongs to the page it was entered from.
    ResetLevel();
    // The graph belongs to the visible page, and `menu page` works while the menu is CLOSED - so
    // without this a cursor line read after a closed page change would describe the previous page.
    RebuildCursorGraph();
    return true;
}

bool RsMenu_StartSweep(int32_t delta) {
    // The ring is the top level's; a detail view is below it, and a roll never starts on top of the
    // level gesture.
    if (sSweepActive || sLevelActive || sLevel != 0) {
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
    // #131: ON THE PRESS, not at the swap tick - vanilla plays it inside KaleidoScope_SwitchPage,
    // which is the frame the shoulder went down, half a gesture before the page actually changes.
    // Every roll cause funnels through here, so all five get it from one line.
    RsMenu_PlaySfx(dir > 0 ? RS_MENU_SFX_ROLL_RIGHT : RS_MENU_SFX_ROLL_LEFT);
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

int32_t RsMenu_Level() {
    return sLevel;
}

bool RsMenu_Descend() {
    if (!MenuIsSettled() || sSweepActive || sLevelActive || sLevel != 0) {
        return false;
    }
    const RsMenuPage* page = RsMenu_PageAt(sPage);
    const RsMenuCursorNode* node = RsMenu_CursorAt(sCursorIndex);
    if (page == nullptr || node == nullptr || node->hand >= 0 || page->select == nullptr ||
        page->detailDraw == nullptr) {
        return false;
    }
    if (!page->select(sPage, node, page->userData)) {
        return false;
    }
    return StartLevel(1);
}

bool RsMenu_Ascend() {
    if (!MenuIsSettled() || sLevelActive || sLevel != 1) {
        return false;
    }
    return StartLevel(-1);
}

// The loop and the hold go down into the row the CURSOR is on, through the page's own `select`,
// exactly as A does - otherwise a held or looped journal would show whatever was selected last and
// differ from what real input produces. On a hand (or an empty graph) there is no row to select and
// the page keeps its previous selection.
static void SelectCursorItemQuietly() {
    const RsMenuPage* page = RsMenu_PageAt(sPage);
    const RsMenuCursorNode* node = RsMenu_CursorAt(sCursorIndex);
    if (page != nullptr && page->select != nullptr && node != nullptr && node->hand < 0) {
        page->select(sPage, node, page->userData);
    }
}

bool RsMenu_StartLevelLoop() {
    if (!sLevelActive && sLevel == 0) {
        SelectCursorItemQuietly();
    }
    sLevelHold = false;
    if (!sLevelActive && !StartLevel(sLevel == 0 ? 1 : -1)) {
        return false;
    }
    sLevelLoop = true;
    return true;
}

bool RsMenu_HoldLevel(int32_t dir, int32_t tick) {
    if (tick < 0 || tick > kLevelTicks || dir == 0 || sSweepActive || !PageHasDetail()) {
        return false;
    }
    if (sLevel == 0) {
        SelectCursorItemQuietly();
    }
    sLevelLoop = false;
    sLevelHold = false;
    sLevelActive = false;
    sLevelTick = 0;
    // From the level that direction starts at, then played forward - so a held frame is one the
    // animation really produces, level swap included, exactly as RsMenu_HoldSweep does it.
    sLevel = dir > 0 ? 0 : 1;
    if (!StartLevel(dir)) {
        return false;
    }
    for (int32_t i = 0; i < tick; i++) {
        AdvanceLevel();
    }
    sLevelHold = true;
    sLevelActive = true;
    sLevelTick = tick;
    RebuildCursorGraph();
    return true;
}

void RsMenu_StopLevelLoop() {
    sLevelLoop = false;
    sLevelHold = false;
    sLevelActive = false;
    sLevelTick = 0;
    RebuildCursorGraph();
}

RsMenuLevelState RsMenu_LevelState() {
    RsMenuLevelState state;
    const int32_t pose = LevelPose();
    state.level = sLevel;
    state.active = sLevelActive;
    state.tick = sLevelTick;
    state.ticks = kLevelTicks;
    state.dir = sLevelDir;
    state.pose = pose;
    state.phase = LevelPhaseName(pose);
    state.separation = LevelSeparation(pose);
    state.angle = LevelAngle(pose) * 180.0f / kPi;
    state.swaps = sLevelSwaps;
    state.loop = sLevelLoop;
    state.hold = sLevelHold;
    for (int32_t hand = 0; hand < 2; hand++) {
        for (int32_t k = 0; k < 4; k++) {
            state.hands[hand][k] = sHandExtent[hand][k];
        }
    }
    state.drop = RsMenu_ScreenDy();
    state.hudShown = LevelHudShown();
    state.bShown = LevelBShown();
    state.bMoved = LevelBMoved();
    state.detail = RsMenu_DetailRect();
    return state;
}

RsMenuStepperState RsMenu_StepperState() {
    const StepperLayout l = LayoutStepper();
    const bool up = MenuIsUp();
    RsMenuStepperState state;
    state.shown = up && sStepperDrawn;
    state.alpha = up ? sStepperAlpha : 0;
    state.at = sPage;
    state.stones = l.stones;
    state.stone = l.stone;
    state.x0 = l.x0;
    state.y0 = l.y0;
    state.x1 = l.x1;
    state.y1 = l.y1;
    state.gapX0 = l.gapX0;
    state.gapX1 = l.gapX1;
    state.rampOpen.assign(sStepperRamp[STEPPER_SLIDE_OPEN], sStepperRamp[STEPPER_SLIDE_OPEN] + kEntryTicks);
    state.rampClose.assign(sStepperRamp[STEPPER_SLIDE_CLOSE], sStepperRamp[STEPPER_SLIDE_CLOSE] + kEntryTicks);
    return state;
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
    state.dx = SweepDx(SweepMovingHand()); // the roll's own term only, not the level gesture's
    state.width = SweepWidth();
    state.sweeps = sSweeps;
    state.stickRolls = sStickRolls;
    state.dpadRolls = sDpadRolls;
    state.loop = sSweepLoop;
    state.hold = sSweepHold;
    return state;
}

void RsMenu_SetProbe(bool on) {
    sProbe = on;
}

// THE DEFAULT IS `custom` SINCE STAGE 6 (#111): START opens the scroll, and vanilla pause is
// reachable only by `menu primary vanilla` on the console - no button (#111 comment 10). A SAVED
// value beats this default, so a config that has ever stored `RsMenuPrimary: 0` keeps opening vanilla
// until `menu primary custom` is run once; the flip changes what a fresh config does, nothing else.
int32_t RsMenu_GetPrimary() {
    return CVarGetInteger(CVAR_RS_MENU_PRIMARY, RS_MENU_PRIMARY_CUSTOM) == RS_MENU_PRIMARY_VANILLA
               ? RS_MENU_PRIMARY_VANILLA
               : RS_MENU_PRIMARY_CUSTOM;
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
    info.mask = BTN_START;
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
    auto button = controller->GetButton(BTN_START);
    if (button == nullptr) {
        return info;
    }
    info.bindings = (int32_t)button->GetAllButtonMappings().size();
    info.bound = info.bindings > 0;
    return info;
}

RsMenuViewStats RsMenu_ViewStats() {
    // The same rule as `section=draw`: closed, the last frame drew no view at all.
    const bool up = MenuIsUp();
    RsMenuViewStats stats;
    stats.view = up ? sViewName : "none";
    stats.frame = sViewFrame;
    stats.rows = up ? sViewRows : 0;
    stats.glyphs = up ? sViewGlyphs : 0;
    stats.words = up ? sViewWords : 0;
    stats.icons = up ? sViewIcons : 0;
    return stats;
}

int32_t RsMenu_StressCapacity(bool same) {
    return StressLayout(INT32_MAX, same, nullptr);
}

bool RsMenu_SetStress(int32_t glyphs, bool same) {
    if (glyphs < 0 || glyphs > RsMenu_StressCapacity(same)) {
        return false;
    }
    sStressGlyphs = glyphs;
    sStressSame = same;
    return true;
}

void RsMenu_StopStress() {
    sStressGlyphs = -1;
    sStressSame = false;
}

RsMenuStressState RsMenu_StressState() {
    RsMenuStressState state;
    state.on = sStressGlyphs >= 0;
    state.glyphs = sStressGlyphs;
    state.same = sStressSame;
    state.capacity = RsMenu_StressCapacity(sStressSame);
    state.scale = kStressScale;
    state.pitch = kStressPitch;
    return state;
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
    status.hudHeld = sHudApplied;
    status.hudPrev = (int32_t)sHudPrev;
    status.hudNow = (int32_t)gSaveContext.hudVisibilityMode;
    status.hudReasserts = sHudReasserts;
    status.viewpoint = gPlayState != nullptr ? gPlayState->unk_1242B : 0;
    status.viewpointVetoes = sViewpointVetoes;
    status.freeLookVetoes = sFreeLookVetoes;
    status.manualCamera = gPlayState != nullptr && gPlayState->manualCamera;
    status.minimapOff = R_MINIMAP_DISABLED;
    status.csMode = gPlayState != nullptr ? Play_InCsMode(gPlayState) : 0;
    status.csIndex = gSaveContext.cutsceneIndex;
    status.csNext = gSaveContext.nextCutsceneIndex;
    status.camX = gPlayState != nullptr ? gPlayState->camX : 0.0f;
    status.camY = gPlayState != nullptr ? gPlayState->camY : 0.0f;
    // Zero while the menu is closed: the last frame drew nothing, whatever the last OPEN frame did.
    status.drawGlyphs = status.open ? sDrawGlyphs : 0;
    status.drawQuads = status.open ? sDrawQuads : 0;
    status.dlWords = status.open ? sDlWords : 0;
    status.drawIcons = status.open ? sDrawIcons : 0;
    status.cursorDrawn = status.open && sCursorDrawn;
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
    status.closeDropped = sCloseDropped;
    status.openRefused = sOpenRefused;
    status.filterPressSeen = sFilterPressSeen;
    return status;
}
