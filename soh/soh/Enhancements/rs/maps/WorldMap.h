#pragma once
/*
 * THE WORLD MAP IMAGE (sturdy-bassoon#298): one top-down picture of a stitched scene, which the pause
 * scroll's map page (#303) and the HUD minimap (#306) both draw. Reference: sturdy-bassoon
 * docs/reference/WORLD_MAP.md.
 *
 * WHERE IT COMES FROM. The grid tool's world export draws it from the stitched grid
 * (tools/grid-scene-tool/src/shared/worldMapImage.ts) and writes `mods/rs-worldmap-<slug>.o2r` into each
 * built configuration, holding one file, `worldmap/<SCENE_ENUM>.bin`. A scene opts in with an #include
 * of its `<slug>_world.inc` in WorldMap.cpp, as it does for zone music. A tree that has never had the
 * world exported into it has no map: every call below answers false/none (reason=no_map).
 *
 * THE PICTURE. 2 px a tile, north up, row 0 the north edge. CI4 with a 16-entry TLUT (the matrix
 * view's thumbnail greys). Two sets of LAYERS, both precomputed: each storey ALONE (0..S-1), then the
 * STACKS 0..k for k = 1..S-1 (S..2S-2), in painter's order from the bottom up, so an upper floor hides
 * the walls under it and open sky still shows the storey below. The storey CVar picks the layer every
 * draw uses (default below, the owner's pick 2026-11-06): ground (storey 0 alone), current (Link's storey alone), below (Link's storey and everything
 * under it, stacked) or all (the top stack).
 *
 * TWO SPACES:
 *   - IMAGE PIXELS: x east, y south, (0, 0) the image's north-west corner. WorldToPixel answers in it,
 *     sub-pixel.
 *   - THE GEOMETRY SPACE EmitBlocks/ForEachBlock draw in: image pixels with y NEGATED (ob = (px, -py)),
 *     so it is y-up like the menu's and the HUD's ortho. One unit a pixel; block corners are integers,
 *     so neighbouring blocks share exact edges and never crack.
 *
 * DRAWING IS Vtx, NEVER A TEXTURE RECTANGLE. The pause page rides a matrix chain that rolls, scales
 * and frame-interpolates (RsMenu.cpp, "EVERYTHING THAT MOVES IS Vtx"); texture rectangles follow none
 * of that. Vertices come from the frame's graph pool (Graph_Alloc), so they live until the frame is
 * drawn, and the Gfx can go into any buffer: a pool's `POLY_OPA_DISP++`, or a heap std::vector<Gfx>
 * (size it with RsWorldMap_GfxWords first, then hand in `&dl[at]` and shrink to where the pointer
 * stopped - the C++ overload below does exactly that).
 *
 * THE TMEM SPLIT. Drawn in blocks of 60 x 60 px (RsWorldMap_BlockSize), each loaded with a 2-texel
 * apron left/right and 1 top/bottom: 64 x 62 = 3,968 CI4 texels, under the 4,096 that fit TMEM beside
 * a TLUT. The apron is why bilinear filtering does not seam at block borders when a consumer scales
 * the map. Fast3D's texture cache keys a tile load by its start address within the image and its byte
 * size (interpreter.cpp GfxDpLoadTile/ImportTexture), so every block is its own cache entry.
 *
 * WHAT A CALLER SETS: the combiner, the render mode, the filter, prim colour (fog of war, fades) and
 * gSPTexture on. EmitBlocks sets the texture LUT mode to RGBA16 for its blocks and back to NONE after.
 *
 * THE WALLS AS GEOMETRY (sturdy-bassoon#309, file version 4). Each layer also carries its WALL SEGMENTS: the
 * edges the matrix view's canvas strokes, on the tile boundaries, in image pixels, collinear runs merged and,
 * in a stack, dropped where an upper level draws the edge's tile - all worked out at export. EmitEdges draws
 * them as thin quads at the output resolution, so a diagonal stays a line at any zoom where the picture's
 * two-pixels-a-tile staircase shows. The picture keeps its own edge pixels; RS_WORLD_MAP_EDGELESS draws the
 * same blocks through the file's second TLUT, which paints each edge pixel as the floor or background it
 * covers, so the lines are not drawn over a staircase. A version 3 file still loads: no segments, no
 * edgeless TLUT (EdgeCount 0, the flag ignored).
 */
#include <stdbool.h>
#include <stdint.h>

#include "soh/Enhancements/enhancementTypes.h"

#ifdef __cplusplus
#include <vector>
extern "C" {
#endif

#include <z64.h>

#define CVAR_RS_WORLD_MAP_STOREY CVAR_ENHANCEMENT("RsWorldMapStorey")

typedef enum {
    RS_WORLD_MAP_STOREY_GROUND = 0,
    RS_WORLD_MAP_STOREY_ALL = 1,
    RS_WORLD_MAP_STOREY_CURRENT = 2,
    RS_WORLD_MAP_STOREY_BELOW = 3,
    RS_WORLD_MAP_STOREY_MODE_COUNT
} RsWorldMapStoreyMode;

// The mode with no CVar saved: Link's storey and everything under it (the owner, 2026-11-06).
#define RS_WORLD_MAP_STOREY_DEFAULT RS_WORLD_MAP_STOREY_BELOW

typedef struct {
    int16_t sceneId;
    uint16_t widthPx, heightPx;     // the whole image
    uint16_t widthTiles, depthTiles; // the stitched grid, in RS tiles
    uint8_t pxPerTile;               // 2
    uint8_t storeys;                 // levels in the image; layer k < storeys is level k alone
    uint8_t layers;                  // 2 * storeys - 1: layer storeys + k - 1 is the stack 0..k
    uint8_t layer;                   // the layer the storey CVar picks right now
    int32_t minRsX, minRsY;          // the RS tile of the image's south-west corner
    int32_t originRsX, originRsY;    // the matrix's origin: the RS tile whose SOUTH-west corner is OoT (0, 0)
    uint32_t fileBytes;
    uint16_t version;                // the file's: 4 carries wall segments (#309), 3 does not
    uint16_t edges;                  // the current layer's wall segments (0 in a version 3 file)
} RsWorldMapInfo;

// The scene's map, or false when it has none (a vanilla scene, a single-map scene, an unexported tree, a
// stale or broken archive - RsWorldMap_Problem says which).
bool RsWorldMap_Info(int16_t sceneId, RsWorldMapInfo* out);
// Why RsWorldMap_Info answered false: no_scene (not opted in), no_map (no file in any archive),
// bad_file, stale_archive (its origin is not the include's), or "" when it answered true.
const char* RsWorldMap_Problem(int16_t sceneId);

// World position (OoT x, z) -> image pixel, sub-pixel. A position off the image still answers (off the
// edge); false only with no map.
bool RsWorldMap_WorldToPixel(int16_t sceneId, float x, float z, float* px, float* py);
bool RsWorldMap_PixelToWorld(int16_t sceneId, float px, float py, float* x, float* z);

// The block size in image pixels, and the most Gfx words EmitBlocks writes for a source rect of this
// size (about 10 a block, plus a dozen around them).
void RsWorldMap_BlockSize(int16_t* w, int16_t* h);
int32_t RsWorldMap_GfxWords(float srcW, float srcH);

// Draws image rect (srcX, srcY, srcW, srcH) of the current layer as one textured quad a block, in the
// GEOMETRY SPACE above, under whatever modelview is loaded now. The rect is rounded outward to whole
// pixels. Returns the blocks drawn (0 with no map).
int32_t RsWorldMap_EmitBlocks(GraphicsContext* gfxCtx, Gfx** gfx, int16_t sceneId, float srcX, float srcY,
                              float srcW, float srcH);

// The same, placed: the source rect lands on (dstX, dstY, dstW, dstH) in the CALLER'S modelview space,
// which is y-up - (dstX, dstY) is the top-left corner and the image runs down to dstY - dstH. It records
// Matrix_Push, Matrix_Translate, Matrix_Scale, Matrix_NewMtx, Matrix_Pop and Matrix_NewMtx (the caller's
// matrix put back) on EVERY call, map or no map, so frame interpolation's positional matching holds.
int32_t RsWorldMap_DrawRect(GraphicsContext* gfxCtx, Gfx** gfx, int16_t sceneId, float srcX, float srcY, float srcW,
                            float srcH, float dstX, float dstY, float dstW, float dstH);

// For custom geometry (#306's round, rotating window): every block the source rect touches, after that
// block is loaded into G_TX_RENDERTILE. (blockX, blockY, blockW, blockH) is the block's rect in image
// pixels; (loadX, loadY) is the image pixel the load starts at, the texture's (0, 0). A vertex's texture
// coordinate for image pixel (px, py) inside the block is ((px - loadX) << 5, (py - loadY) << 5). The load
// usually starts at the apron (blockX - 2, blockY - 1), but at the image's west and north edges there is
// no apron and it starts at the block itself (0) - so use loadX/loadY, never blockX - 2. LUT mode as
// EmitBlocks.
typedef void (*RsWorldMapBlockFn)(Gfx** gfx, int16_t blockX, int16_t blockY, int16_t blockW, int16_t blockH,
                                  int16_t loadX, int16_t loadY, void* user);
int32_t RsWorldMap_ForEachBlock(Gfx** gfx, int16_t sceneId, float srcX, float srcY, float srcW, float srcH,
                                RsWorldMapBlockFn fn, void* user);

// Flags for the *Ex draws. RS_WORLD_MAP_EDGELESS: the blocks through the edgeless TLUT, every edge pixel drawn as
// what it covers - for a caller that draws the walls with EmitEdges on top (#309). Ignored by a version 3 file.
#define RS_WORLD_MAP_EDGELESS 1u
int32_t RsWorldMap_EmitBlocksEx(GraphicsContext* gfxCtx, Gfx** gfx, int16_t sceneId, float srcX, float srcY,
                                float srcW, float srcH, uint32_t flags);
int32_t RsWorldMap_ForEachBlockEx(Gfx** gfx, int16_t sceneId, float srcX, float srcY, float srcW, float srcH,
                                  RsWorldMapBlockFn fn, void* user, uint32_t flags);

// --- the walls as geometry (#309) ---
// The wall colour: the TLUT's wall entry (2), so the lines match the picture's walls. False with no map.
bool RsWorldMap_WallColour(int16_t sceneId, uint8_t rgb[3]);

// Every wall segment of the current layer that touches source rect (srcX, srcY, srcW, srcH), clipped to it, in
// IMAGE PIXELS (y down). For custom geometry, the way ForEachBlock is: #306's round minimap would clip these to
// its circle and build its own quads. Returns the segments handed over (0 with no map or a version 3 file).
typedef void (*RsWorldMapEdgeFn)(float x0, float y0, float x1, float y1, void* user);
int32_t RsWorldMap_ForEachEdge(int16_t sceneId, float srcX, float srcY, float srcW, float srcH, RsWorldMapEdgeFn fn,
                               void* user);

// Draws those segments as quads `halfWidthPx` IMAGE pixels either side of the line, square-capped (each end
// pushed out by the half width, so corners close), in the GEOMETRY SPACE under the modelview loaded now. The
// caller turns its line width in SCREEN terms into image pixels at its own zoom (MapPage.cpp does), which is
// what keeps a wall the same width on screen at 1x and 6x. Untextured: the caller sets the combiner (e.g.
// G_CC_PRIMITIVE with RsWorldMap_WallColour), the render mode and gSPTexture off.
//
// Integer vertices would round a line to a whole image pixel, so the vertices are image pixels times a fixed
// per-map factor under a nested 1/factor scale. That needs the Matrix_* stack's top to BE the modelview loaded
// now, and it records Matrix_Push, Matrix_Scale, Matrix_NewMtx, Matrix_Pop, Matrix_NewMtx on EVERY call - map or
// no map, halfWidthPx 0 or not - so frame interpolation's positional matching holds; a caller that wants no lines
// this frame passes halfWidthPx 0 and still calls. Returns the quads drawn.
int32_t RsWorldMap_EmitEdges(GraphicsContext* gfxCtx, Gfx** gfx, int16_t sceneId, float srcX, float srcY, float srcW,
                             float srcH, float halfWidthPx);
// The most Gfx words EmitEdges writes on the current layer, whatever the rect.
int32_t RsWorldMap_EdgeGfxWords(int16_t sceneId);

// Link's storey as `current` reads it: the level whose floor, on his tile, is within 20 units of where
// he stands, kept while he is airborne, swimming or between floors. -1 with no map.
int8_t RsWorldMap_LinkStorey(void);
// The storey CVar, clamped.
RsWorldMapStoreyMode RsWorldMap_StoreyMode(void);
void RsWorldMap_SetStoreyMode(RsWorldMapStoreyMode mode);
const char* RsWorldMap_StoreyModeName(RsWorldMapStoreyMode mode);

// The debug view (`worldmap debug`): 0 off, 1 the whole image in a corner, 2 a window around Link.
void RsWorldMap_SetDebugView(int32_t view);
int32_t RsWorldMap_DebugView(void);
// What the debug view drew last frame: blocks, and the Gfx words its list took.
void RsWorldMap_DebugStats(int32_t* blocks, int32_t* words);
// Tune the block size (`worldmap block <w> <h>`): even w, (w + 4) * (h + 2) <= 4096. False if refused.
bool RsWorldMap_SetBlockSize(int16_t w, int16_t h);

#ifdef __cplusplus
}

// Appends to a heap display list: reserves RsWorldMap_GfxWords, draws, trims to what was written.
int32_t RsWorldMap_DrawRect(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, int16_t sceneId, float srcX, float srcY,
                            float srcW, float srcH, float dstX, float dstY, float dstW, float dstH);
#endif
