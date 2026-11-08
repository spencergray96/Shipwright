#ifndef SOH_RS_SPRITES_H
#define SOH_RS_SPRITES_H

/*
 * RS interface art by token (sturdy-bassoon#297): the textures `mods/rs-sprites.o2r` holds, which sturdy-bassoon's
 * `npm run pack:rs-sprites` makes on this machine from the player's own cache (and, for a web row, from website files
 * the player agreed to download). Nothing here is compiled in; a tree without the archive simply finds nothing.
 *
 * TOKENS are the manifest's (sturdy-bassoon tools/sprites/rs-sprites.json): `slider_track`, `map_general_store`,
 * `scroll_quest_complete`. A web row's parts are `<token>/<part>`: `scroll_homepage/top`, `.../middle`, `.../bottom`.
 *
 * ONE PICTURE, ONE OR MORE TEXTURES. The game cannot address one texture past 1023 texels a side (LoadTile and
 * SetTileSize carry 12-bit 10.2 coordinates; a vertex's s10.5 texture coordinate overflows an s16 at 1024), so the
 * packer splits anything over 1016 px into a grid, `rs/sprites/<token>/<col>_<row>`. RsSprites_Info reports the whole
 * picture, and RsSprites_EmitRect draws the grid as one rectangle. A caller never sees the tiles.
 *
 * EVERY TEXTURE IS RGBA32 (8 bits a channel). The art's alpha is binary today (no approved sprite has an alpha
 * plane), but the transparent texels round it carry the edge's colour (post.py's bleed), so a BILINEAR draw blends
 * the edge towards its own colour instead of a dark fringe. Draw it with a blending render mode (G_RM_XLU_SURF) for
 * a smooth edge, or a cutout one for a hard edge.
 *
 * Resolved on first ask and cached: a missing token is asked about once, not every frame.
 */

#include <stdint.h>

#ifdef __cplusplus
#include <vector>
extern "C" {
#endif

#include <z64.h>

typedef struct RsSpriteInfo {
    int32_t width;  // the whole picture, in texels
    int32_t height;
    int32_t cols;   // its texture grid: 1 x 1 for anything up to 1016 px a side
    int32_t rows;
} RsSpriteInfo;

// 1 and fills `out` when the token is in a mounted archive, else 0 (and `out` is zeroed). A token outside
// [a-z0-9_/]{1,48} is 0, never a lookup.
int32_t RsSprites_Info(const char* token, RsSpriteInfo* out);

// The archive path of a single-texture sprite, "__OTR__rs/sprites/<token>", 2-aligned as the renderer needs
// (RsItemArt.h says why), or NULL when the token is missing or is a grid. For a caller loading the texture itself.
const char* RsSprites_Path(const char* token);

// How many Gfx words RsSprites_EmitRect writes for this token at most (0 when it is missing).
int32_t RsSprites_GfxWords(const char* token);

// Draws the token's whole picture as one rectangle in the CURRENT modelview, y up: top-left corner at (x, y),
// w x h units. It loads its own tile(s), with point or bilinear filtering as the render state already says, and draws
// quads; the caller sets the render mode, the combiner and the prim colour first (G_CC_MODULATERGBA_PRIM with prim
// white draws the art as it is). Returns the number of quads drawn, 0 when the token is missing (nothing is written).
int32_t RsSprites_EmitRect(GraphicsContext* gfxCtx, Gfx** gfx, const char* token, float x, float y, float w, float h);

#ifdef __cplusplus
}

// Appends to a heap display list: reserves RsSprites_GfxWords, draws, trims to what was written.
int32_t RsSprites_EmitRect(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, const char* token, float x, float y, float w,
                           float h);
#endif

#endif // SOH_RS_SPRITES_H
