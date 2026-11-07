#ifndef SOH_RS_MENU_RAW_H
#define SOH_RS_MENU_RAW_H
// A PAGE'S OWN GEOMETRY (sturdy-bassoon#303): the menu's shared heap display list and the frame's graphics
// context, for a page whose body is more than the RsMenu_Draw* helpers can say - the world map page's map
// blocks, fog quads and Link's arrow, each under a matrix of its own.
//
// Valid ONLY inside a page's RsMenuPageDrawFn, like the text calls: it answers false anywhere else. Calling it
// tells the menu the list's render state is now the page's, so the next RsMenu_DrawText/Icon/Bar call pushes
// its own state again. The page writes Vtx into the list (never a texture rectangle: RsMenu.cpp, "EVERYTHING
// THAT MOVES IS Vtx"), emits its Matrix_* ops on every frame it draws, and leaves the modelview it found loaded
// when it is done (push a gSPMatrix of Matrix_NewMtx after its last Matrix_Pop).
#include <vector>

extern "C" {
#include <z64.h>
}

bool RsMenu_BeginRawDraw(GraphicsContext** gfxCtx, std::vector<Gfx>** dl);

#endif // SOH_RS_MENU_RAW_H
