#ifndef SOH_RS_MENU_LINK_HANDS_DRAW_H
#define SOH_RS_MENU_LINK_HANDS_DRAW_H
// The draw half of LinkHands.h (sturdy-bassoon#299), for RsMenu.cpp and LinkHands.cpp only: it needs z64.h.
#include <vector>

#include "LinkHands.h"

extern "C" {
#include <z64.h>
}

// The depth clear under the 4:3 band: the world wrote that depth, and Link's limbs must not be tested against it.
// Pushed once, ahead of the parchment, because the parchment and the rolls then write depth of their own for the
// hands to go behind (RsLinkHands_PushOccluderState). Not a Matrix_* op, so it costs the interpolation nodes nothing.
void RsLinkHands_PushDepthClear(std::vector<Gfx>& dl, GraphicsContext* gfxCtx);

// The hands' projection. The menu's ortho is a sheet at z 0 (near and far +-1); Link's limbs have depth, so they
// get a deep ortho with the same x and y. Whatever writes depth the hands are tested against - the paper, the
// rolls - must be drawn under this one too, so the two depths are on one scale. Not a Matrix_* op either.
void RsLinkHands_PushProjection(GraphicsContext* gfxCtx, std::vector<Gfx>& dl);

// CALL 2 OF THE OWNER'S SESSION: the hands HOLD the scroll rather than float over it. The paper and each roll are
// drawn a second time, invisibly, writing their depth at z 0 - the plane through the rolls' axes - so whatever part
// of a hand is behind that plane and over the scroll is hidden by the depth test, and whatever wraps round in front
// of it (the thumb, the fingertips) draws over the paper. Per pixel, so it holds at any pose, grip or age and
// follows the parchment through a roll with nothing to re-pick. Push this, then the same quads the visible draw
// used, under the same matrix. `textured`: the parchment art, cut to its own alpha; false for the flat look.
// Returns false (and pushes nothing) when `menu hand occlude off` or the boxes are up.
bool RsLinkHands_PushOccluderState(std::vector<Gfx>& dl, bool textured);

// Draw one side's arm. The side's hand matrix (side matrix and swivel) must be current on the Matrix_* stack, in
// the menu's centred y-up game space; `gripX`/`gripY` is where on the scroll the hand holds, in game units with y
// down; `levelBlend` is 0 at level 0 and 1 in the vertical pose;
// `keyTick`/`keyUp` the level gesture's tick and direction, as `menu level hold <down|up> <tick>` names them (at rest:
// 0 or 24, down), which pick the hand's offset and paper from its key tables. Emits the same Matrix_* ops on every call
// whatever is worn or drawn, as the hands' node requires, and leaves the matrix stack as it found it.
// THE ROLL'S FRAME AROUND THE GRIP, for anchor mode: what RsMenu's hand swivel applies between the roll's matrix and
// the hand's - the slide (x, y in the roll's frame, y up) and the swivel about the grip (radians) - now and at both
// ends of the level turn, and the turn's eased blend (0 flat, 1 upright).
struct RsLinkHandsRollFrame {
    float slide[2];
    float swivel;
    float slideFlat[2];
    float slideUpright[2];
    float swivelUpright;
    float blend;
};

void RsLinkHands_Draw(PlayState* play, GraphicsContext* gfxCtx, std::vector<Gfx>& dl, int32_t side, float gripX,
                      float gripY, float levelBlend, int32_t keyTick, bool keyUp, const RsLinkHandsRollFrame& frame);

// `menu hand anchor <l|r> rigid on`: the hand turns with its roll exactly - RsMenu leaves out its swivel.
bool RsLinkHands_Rigid(int32_t side);

// THE SCROLL'S EDGE (owner look 8, the green sliver): `menu hand scrollfilter point` draws the parchment art point
// sampled, the way its depth occluder is, so the two edges coincide at every sub-texel phase; `menu hand rollinset`
// insets the rolls' depth occluders by that many game units instead, so their edge never reaches past the art's.
bool RsLinkHands_ScrollPoint();
float RsLinkHands_RollInset();

// The rolls' depth for the hands, in game units toward the viewer at a roll's centre line (`menu hand roll`): each roll
// occluder bulges like the cylinder it draws, 0 at its edges, so fingers that wrap it go behind it at its silhouette.
// 0 is the flat plane the first holding build used.
// Per side and per tick (`menu hand keyroll`), read at the same tick the hand's key tables are.
float RsLinkHands_RollRadius(int32_t side, int32_t keyTick, bool keyUp);

#endif // SOH_RS_MENU_LINK_HANDS_DRAW_H
