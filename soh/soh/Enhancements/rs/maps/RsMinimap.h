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
 * THE ARROWS. Minimap_DrawCompassArrow redirects here while the RS minimap draws (RsMinimap_DrawArrow), so Link's
 * arrow and the rs/ vehicles' (RsVehicleMinimap.cpp) land on this map at this scale. A vehicle outside the circle
 * is pinned to the rim, still pointing its heading. No red entrance arrow.
 *
 * MAP ICONS (#307). Draw in OnMinimapDrawCompassIconsUnderPlayer (under Link's arrow), placing each with
 * RsMinimap_WorldToScreen. Anything drawn through Minimap_DrawCompassArrow is placed here automatically.
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

// Minimap_DrawCompassArrow's redirect: while the RS minimap is drawing, places the arrow on it and answers true.
bool RsMinimap_DrawArrow(PlayState* play, f32 x, f32 z, s16 yaw, Color_RGB8 color);

// Where world (x, z) lands on this frame's minimap, in the HUD's centre-origin, y-up N64 pixels (the space
// Minimap_DrawCompassArrow's Matrix_Translate uses). *inside is false when it is outside the circle; (sx, sy) is
// then pinned to the rim. False when the minimap is not drawn this frame.
bool RsMinimap_WorldToScreen(f32 x, f32 z, f32* sx, f32* sy, bool* inside);

typedef struct {
    bool handled;     // the scene has a world map
    bool drawn;       // the map was visible last frame
    bool rotate;      // the mode
    int32_t radiusTiles;
    float linkPx, linkPy; // Link's map pixel
    float rotDeg;         // the map's turn on screen (0 north-up)
    int32_t blocks;       // blocks drawn last frame
    int32_t words;        // Gfx words of last frame's list
    int32_t arrows;       // arrows drawn last frame (Link's included)
    float centerX, centerY; // the circle's centre, centre-origin y-up N64 pixels
    uint8_t alpha;          // minimapAlpha last frame
    uint32_t frame;         // the GameState frame it ran
} RsMinimapStats;
void RsMinimap_GetStats(RsMinimapStats* out);

bool RsMinimap_Rotate(void);
void RsMinimap_SetRotate(bool rotate);
int32_t RsMinimap_RadiusTiles(void);
void RsMinimap_SetRadiusTiles(int32_t tiles);

#ifdef __cplusplus
}
#endif
