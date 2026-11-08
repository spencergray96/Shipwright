#ifndef SOH_RS_MENU_LINK_HANDS_DRAW_H
#define SOH_RS_MENU_LINK_HANDS_DRAW_H
// The draw half of LinkHands.h (sturdy-bassoon#299), for RsMenu.cpp and LinkHands.cpp only: it needs z64.h.
#include <vector>

#include "LinkHands.h"

extern "C" {
#include <z64.h>
}

// The depth clear and the hands' own projection, pushed ahead of the hands' interpolation node. The menu's ortho
// is a sheet at z 0 (near and far +-1); Link's limbs have depth, so they get a deep ortho with the same x and y
// and the depth buffer under the 4:3 band cleared, which the world wrote and which the limbs must not be tested
// against. Nothing here is a Matrix_* op, so it costs the interpolation nodes nothing.
void RsLinkHands_PushBegin(GraphicsContext* gfxCtx, std::vector<Gfx>& dl);

// Draw one side's arm. The side's hand matrix (side matrix and swivel) must be current on the Matrix_* stack, in
// the menu's centred y-up game space; `gripX`/`gripY` is where on the scroll the hand holds, in game units with y
// down; `levelBlend` is 0 at level 0 and 1 in the vertical pose. Emits the same Matrix_* ops on every call
// whatever is worn or drawn, as the hands' node requires, and leaves the matrix stack as it found it.
void RsLinkHands_Draw(PlayState* play, GraphicsContext* gfxCtx, std::vector<Gfx>& dl, int32_t side, float gripX,
                      float gripY, float levelBlend);

#endif // SOH_RS_MENU_LINK_HANDS_DRAW_H
