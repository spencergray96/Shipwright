#pragma once
/*
 * THE HUD MINIMAP IN RS SCENES (sturdy-bassoon#306): a round window onto the world map image (#298, WorldMap.h),
 * centred on Link. Reference: sturdy-bassoon docs/reference/WORLD_MAP.md, "The HUD minimap".
 *
 * WHERE. Minimap_Draw (z_map_exp.c) hands any scene RsWorldMap_Info answers for to RsMinimap_Draw; every vanilla
 * scene is a named case of its switch and keeps vanilla's minimap. Vanilla's gates still apply: the L toggle
 * (R_MINIMAP_DISABLED), HUD.Minimap.PosType hidden, the pause, and minimapAlpha's fades (the scene-entry hold
 * included).
 *
 * HOW. A 64-gon around Link's pixel, clipped to each block RsWorldMap_ForEachBlock loads, as a triangle fan, over
 * an untextured disc in the map's background colour. Vertices are absolute image pixels (x4); the scroll, the
 * rotation and the placement are all in the matrix, so frame interpolation smooths them between ticks. The whole
 * thing is one heap display list called from OVERLAY_DISP, so it costs OVERLAY_DISP one word.
 *
 * THE ARROWS ARE PRINTED ON THE MAP (the owner, 2026-11-07). Minimap_DrawCompassArrow redirects here while the RS
 * minimap draws (RsMinimap_DrawArrow): the rs/ vehicles' arrows (RsVehicleMinimap.cpp) and the red entrance arrow
 * are flat triangles at their world position, under the map's own matrix, so they scroll and turn with it, and
 * clipped to the circle like the map: one crossing the edge is cut off there, one past it is gone (out_of_view).
 * Link's arrow is vanilla's own, at the centre. Nothing is pinned to the rim.
 *
 * MAP ICONS (#307) are printed by the same rule: RsMinimap_DrawIcon, from OnMinimapDrawCompassIconsUnderPlayer (under
 * Link's arrow). The table of icons is RsMapIcons.h. Anything drawn through Minimap_DrawCompassArrow is placed and
 * clipped here.
 */
#include <stdbool.h>
#include <stdint.h>

#include "soh/Enhancements/enhancementTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <z64.h>

// 0 north-up (default), 1 rotate with the camera. Read every draw.
#define CVAR_RS_MINIMAP_ROTATE CVAR_ENHANCEMENT("RsMinimapRotate")
// The radius in RS tiles (default 45: a 64 x 64 chunk whole from its middle). Read every draw.
#define CVAR_RS_MINIMAP_RADIUS CVAR_ENHANCEMENT("RsMinimapRadius")
#define RS_MINIMAP_RADIUS_DEFAULT 45
#define RS_MINIMAP_RADIUS_MIN 8
#define RS_MINIMAP_RADIUS_MAX 160
// On screen: a circle this many N64 pixels across the radius, at the bottom right where vanilla's overworld map is.
#define RS_MINIMAP_SCREEN_RADIUS 32

// True when this scene gets the RS minimap instead of vanilla's (it has a world map).
bool RsMinimap_Handles(PlayState* play);

// Draws it into OVERLAY_DISP. `visible` false (L toggled off, hidden, paused) still records the matrix ops, so
// frame interpolation's matching never shifts; it just draws nothing.
void RsMinimap_Draw(PlayState* play, bool visible);

// Minimap_DrawCompassArrow's redirect: while the RS minimap is drawing, prints the arrow on the map (clipped to the
// circle) and answers true.
bool RsMinimap_DrawArrow(PlayState* play, f32 x, f32 z, s16 yaw, Color_RGB8 color);
// The arrow the last redirect drew was wholly outside the circle, so nothing of it shows. False outside the RS
// minimap's draw. RsVehicleMinimap reads it for `map_arrow=out_of_view`.
bool RsMinimap_LastArrowOutOfView(void);

// Where world (x, z) lands on this frame's minimap, in the HUD's centre-origin, y-up N64 pixels (the space
// Minimap_DrawCompassArrow's Matrix_Translate uses), wherever that is: nothing is pinned. *inside is whether the
// point is within the circle. False when the minimap is not drawn this frame.
bool RsMinimap_WorldToScreen(f32 x, f32 z, f32* sx, f32* sy, bool* inside);

// MAP ICONS (#307): an RS sprite (RsSprites.h token) printed on the map at world (x, z), centred there, upright on
// screen whichever way the map turns, under the map's own matrix (so it scrolls, turns and rides the north-up snap
// with the map) and clipped to the circle. Call it from OnMinimapDrawCompassIconsUnderPlayer; RsMapIcons.cpp does,
// for its table. Size: RS_MINIMAP_ICON_TEXEL N64 px a texel - 4/9, which is exactly 2 screen px a texel at 1080p and
// 4 at 4K (a 15 x 15 RS map icon is 6.7 N64 px, 30 / 60 screen px).
#define RS_MINIMAP_ICON_TEXEL (4.0f / 9.0f)
typedef enum {
    RS_MINIMAP_ICON_NOT_DRAWING = 0, // called outside the minimap's draw (no minimap this frame)
    RS_MINIMAP_ICON_DRAWN,           // wholly inside the circle
    RS_MINIMAP_ICON_CLIPPED,         // crossing the circle's edge: the part inside shows
    RS_MINIMAP_ICON_OUT_OF_VIEW,     // wholly past the edge: nothing shows
    RS_MINIMAP_ICON_NO_SPRITE,       // the token is not in a mounted archive (or is a grid), nothing drawn
} RsMinimapIconState;
// *sx, *sy (optional): the icon centre in centre-origin, y-up N64 px, as RsMinimap_WorldToScreen.
RsMinimapIconState RsMinimap_DrawIcon(const char* token, f32 x, f32 z, f32* sx, f32* sy);
const char* RsMinimap_IconStateName(RsMinimapIconState state);

// The map's texture filter: bilinear (true, the default) or point. Not saved; `minimap filter` switches it, for
// the north-up flicker A/B (#306).
bool RsMinimap_Bilerp(void);
void RsMinimap_SetBilerp(bool bilerp);

// North-up, the map's scroll is snapped to whole screen pixels (default on; round 3 of #306), interpolated frames
// included. Not saved; `minimap snap` switches it for the A/B. SnapGrid: last frame's step in HUD ortho units
// (240 / the render height), 0 when nothing was snapped (rotate mode, or snap off).
bool RsMinimap_Snap(void);
void RsMinimap_SetSnap(bool snap);
float RsMinimap_SnapGrid(void);

typedef struct {
    bool handled;     // the scene has a world map
    bool drawn;       // the map was visible last frame
    bool rotate;      // the mode
    int32_t radiusTiles;
    float linkPx, linkPy; // Link's map pixel
    float rotDeg;         // the map's turn on screen (0 north-up)
    int32_t blocks;       // blocks drawn last frame
    int32_t words;        // Gfx words of last frame's list
    int32_t arrows;       // arrows drawn last frame with any part showing (Link's included)
    int32_t icons;        // map icons drawn last frame with any part showing (#307)
    bool entranceShown;   // the red entrance arrow showed last frame
    float centerX, centerY; // the circle's centre, centre-origin y-up N64 pixels
    uint8_t alpha;          // minimapAlpha last frame
    uint32_t frame;         // the GameState frame it ran
} RsMinimapStats;
void RsMinimap_GetStats(RsMinimapStats* out);

// The arrows' look, shared with the pause scroll's World Map (#303) so both maps match: Link's colour and the red
// entrance arrow's, each with its cosmetic override applied, and where the entrance arrow points from (vanilla's
// sPlayerInitial: where Link came into the scene, its heading as a world yaw). The printed arrows' silhouette is
// vanilla's arrow measured at its 0.4 scale: RS_MINIMAP_ARROW_HALF_LEN tip to middle, HALF_WIDTH middle to corner,
// in N64 pixels (game units).
#define RS_MINIMAP_ARROW_HALF_LEN 3.8f
#define RS_MINIMAP_ARROW_HALF_WIDTH 1.9f
Color_RGB8 RsMinimap_LinkColor(void);
Color_RGB8 RsMinimap_EntranceColor(void);
void RsMinimap_EntrancePose(f32* x, f32* z, s16* yaw);

bool RsMinimap_Rotate(void);
void RsMinimap_SetRotate(bool rotate);
int32_t RsMinimap_RadiusTiles(void);
void RsMinimap_SetRadiusTiles(int32_t tiles);

#ifdef __cplusplus
}
#endif
