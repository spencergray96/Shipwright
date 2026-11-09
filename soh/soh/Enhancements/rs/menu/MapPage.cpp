/*
 * MapPage.cpp - the scroll's World Map page (sturdy-bassoon#303): the whole world, from the world map image
 * (#298), with the fog of war over it and Link's arrow on it, and a FOCUSED view that pans and zooms. Planned
 * 2026-11-06, controls the owner's (2026-11-07); the two design comments on #303 are the agreed version.
 *
 * THE PAGE AT REST. The map body is one cursor node, `map`, between the two hands. A on it FOCUSES the page
 * (RsMenu.h, "focus"): the whole scroll zooms toward the screen and the HUD steps aside. Focused, B is the
 * only way out; the C buttons pan the view and D-pad up/down step its zoom (focusTick below).
 *
 * WHAT IS DRAWN, back to front:
 *   under ONE PLACEMENT MATRIX on top of the content node's base matrix, in the image's own geometry space
 *   (pixels, y negated), so nothing can come apart from the map by a float's worth:
 *     1. the map: RsWorldMap_EmitBlocks of the VIEW's rect of the current layer, widened by a margin (the whole
 *        world, 484 blocks for F2P, at zoom 1 - the worst case #298 measured);
 *     1b. the WALLS as geometry (#309, RsMenuMapWalls): RsWorldMap_EmitEdges over the same rect, thin quads at the
 *        output resolution, with the blocks drawn edgeless under them - so a diagonal is a line at 6x, not the
 *        picture's two-pixels-a-tile staircase. The width is a fraction of an RS tile (RsMenuMapWallTiles), so a
 *        wall thickens as the map zooms, with a floor in render pixels (RsMenuMapWallFloor) under which it never
 *        thins, rounded to whole pixels and snapped onto the pixel grid (RsMenuMapWallSnap). Under the fog, so a
 *        hidden square hides its walls too;
 *     2. the fog: one flat quad per run of hidden chunks along a chunk row;
 *     3. Link's custom arrow, when RsMenuMapArrow is `custom`;
 *   then under the base matrix, in game units:
 *     4. four edge MASKS in the panel's own colour around the map's box, covering whatever the view's margin
 *        spills past it (blocks and fog both run on past the box when zoomed). A scissor would be simpler and
 *        wrong: it is an immediate value, so it would neither follow the focus zoom nor interpolate;
 *     5. a one-unit frame around the box;
 *     6. Link's arrow, vanilla's gCompassArrowDL in Link's minimap colour (the same as #306), when
 *        RsMenuMapArrow is `vanilla` (the default since 2026-11-07).
 *
 * THE VIEW, AND WHY PANNING AND ZOOMING COST NOTHING TO INTERPOLATE. The view is a centre in image pixels and a
 * zoom. Both reach the screen only as the placement matrix's ARGUMENTS - the blocks, the fog and the arrow stay
 * in image pixels - so a pan or a zoom step glides between ticks like every other matrix change. Zoom steps
 * through kZooms, each step eased over kZoomTicks; the first step in from the whole world zooms toward Link.
 * The view is clamped so it never leaves the image (at zoom 1 it cannot move). Out of focus the view eases back
 * to the whole world with the zoom-out (it follows RsMenu_FocusAmount), and is not remembered.
 *
 * WHY THE FOG IS AN OVERLAY, NOT A TLUT SWAP: map blocks are 60 px and chunks 128 px, so fog per block would
 * mean splitting blocks at chunk edges, and every split is a new sub-rect load and so a new texture-cache key
 * (WORLD_MAP.md, "The TMEM split"), toward the 1,024-entry cliff. A dim palette would be a second TLUT pointer,
 * #111's caching trap. Flat quads cost no texture entries at all.
 *
 * THE OPS ARE UNCONDITIONAL (RsMenu.cpp, "EVERYTHING THAT MOVES IS Vtx"): the placement's Push, Translate, Scale,
 * NewMtx, Pop, NewMtx and the vanilla arrow's Push, Translate, Scale, RotateX, RotateY, NewMtx, Pop, NewMtx, on
 * every frame the page body is drawn, map or no map, arrow or no arrow. Only geometry is ever skipped.
 *
 * WHICH MAP: the current scene's if it has one; else the world context's stitched scene (a basement under F2P
 * shows F2P, with no arrow); else a "No map here" line. The fallback is built but has not been run: no stitched
 * world with a world map is registered with the scene picker yet (#308 re-exports F2P and runs it).
 *
 * MAP ICONS (#307): the table's icons (RsMapIcons_ForScene), printed between the fog and the vehicles' arrows under
 * the placement, clipped to the view like the arrows, hidden while their chunk is fogged or their plane is off the
 * layer drawn. kIconUnits game units wide at EVERY zoom step (they would be huge at 6x if they scaled with the map),
 * so they stay readable. Their vertices are image pixels x4 under a nested scale of the placement (Push, Scale,
 * NewMtx, Pop, NewMtx, recorded every frame), so a corner is never rounded to a whole image pixel: at the whole-world
 * view an image pixel is most of a screen pixel, and rounded corners would make the 15 texels uneven. RsMenuMapIcons:
 * on (every zoom) or off, OFF by default.
 *
 * THE CUSTOM ARROW (296d1d210, kept behind RsMenuMapArrow `custom`): two triangles, a fill of 255,226,88 over an
 * edge of 40,24,8 drawn 1.35x larger, kArrowUnits (7) game units long, rotated in software.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-11-06
 */

#include "MapPage.h"
#include "MapReveal.h"
#include "RsMenu.h"
#include "RsMenuRaw.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/rs/maps/WorldContext.h"
#include "soh/Enhancements/rs/actors/RsVehicleMinimap.h"
#include "soh/Enhancements/rs/maps/RsMapIcons.h"
#include "soh/Enhancements/rs/maps/RsMinimap.h"
#include "soh/Enhancements/rs/maps/WorldMap.h"
#include "soh/Enhancements/rs/sprites/RsSprites.h"

extern "C" {
uint32_t OTRGetGameRenderHeight();
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "objects/gameplay_keep/gameplay_keep.h"
extern PlayState* gPlayState;
float OTRGetAspectRatio(void); // OTRGlobals.cpp, declared for C only in OTRGlobals.h
}

namespace {

constexpr float kPi = 3.14159265f;

int32_t sPageIndex = -1;
bool sRegistered = false;

// The last drawn frame, for the dump.
int32_t sFrame = 0;
float sDst[4] = {};
int32_t sBlocks = 0;
int32_t sFogQuads = 0;
int32_t sWords = 0;
int32_t sArrowDrawn = 0;
float sDrawView[4] = {}; // the view rect the last frame drew: x0, y0, w, h in image pixels
// #309, the last drawn frame's walls.
bool sWallsDrawn = false;
bool sEdgeless = false;
int32_t sEdges = 0;
int32_t sEdgeJoins = 0;
int32_t sEdgeWords = 0;
bool sWallSnapped = false;
float sWallNominalPx = 0.0f;
float sWallHalfPx = 0.0f;
float sWallScreenPx = 0.0f;

// Colours. The parchment is still the greybox's dark panel (46, 42, 38); the map's own background is its
// TLUT entry 0, #101114. Hidden fog is a muted tone between the two, so "not explored" reads as neither paper
// nor empty ground. All of them are the owner's to judge from the screenshots.
constexpr uint8_t kFrame[4] = { 150, 140, 110, 255 };
constexpr uint8_t kFogHide[4] = { 74, 68, 58, 255 };
// Dim is the hidden tone as a veil, not black: the map's own background is near-black, so a black veil over it
// reads as no fog at all (the first build's dim, 2026-11-06).
constexpr uint8_t kFogDim[4] = { 74, 68, 58, 200 };
constexpr uint8_t kArrowFill[4] = { 255, 226, 88, 255 };
constexpr uint8_t kArrowEdge[4] = { 40, 24, 8, 255 };
// Link's colour on vanilla's minimap (Minimap_Draw), #306's default.
constexpr uint8_t kCompassLink[3] = { 200, 255, 0 };

constexpr float kMargin = 3.0f;      // game units between the map's frame and the page rect; also the masks' depth
constexpr float kFrameUnits = 1.0f;  // the frame's width, game units
constexpr float kArrowUnits = 7.0f;  // the custom arrow's length, game units
// A map icon's size: the minimap's (RS_MINIMAP_ICON_TEXEL game units an RS PIXEL - RsSpriteInfo.rsWidth, never the
// packed texels, so an upscaled repack (#312) is sharper and the same size; a 15 x 15 icon is 6.7 across), so an icon
// is the same size on both maps before the focus zoom scales the scroll.
constexpr float kIconTexelUnits = RS_MINIMAP_ICON_TEXEL;
constexpr float kIconFix = 4.0f;     // icon vertex units an image pixel (the nested scale is 1 / kIconFix)
constexpr float kCompassScale = 0.4f; // vanilla's minimap arrow scale (Minimap_DrawCompassIcons), as #306 draws it
constexpr float kViewMarginPx = 2.0f;
constexpr float kScissorSlack = 1.5f; // game units the scissor sits outside the box, half the masks' depth // image pixels drawn past the view on every side, under the masks

// The focused view's zoom steps: 1 is the whole world.
constexpr float kZooms[] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f };
constexpr int32_t kZoomCount = (int32_t)(sizeof(kZooms) / sizeof(kZooms[0]));
constexpr int32_t kZoomTicks = 4;
// The C-button pan (#313): a VELOCITY in views a second, which ramps toward the held direction's top speed and coasts
// to 0 once let go (RsMenuMapPage_PanEasing). #303 moved a fixed 1/20 of a view a tick: full speed from the first
// tick and a dead stop on release, about 900 render px/s at 1080p and 1,800 at 4K at every zoom, which the owner saw
// as the map jumping. Per axis, so a diagonal ramps both. Each tick the velocity steps toward its target by at most
// speed / (ramp in ticks) speeding up, or speed / (coast in ticks) slowing down (a reversal coasts through 0 first):
// constant acceleration, so the position is continuous and its rate never jumps.
constexpr float kTickSeconds = 1.0f / 20.0f;

// --- the view ---------------------------------------------------------------------------------------------------
struct MapView {
    float cx, cy, zoom;
};
// The view now, and the step it is easing along: from -> to over kZoomTicks.
MapView sFrom = { 0, 0, 1 };
MapView sTo = { 0, 0, 1 };
int32_t sZoomIdx = 0;
int32_t sZoomTick = kZoomTicks;
bool sViewReset = true;
int32_t sPans = 0;
float sPanVx = 0.0f; // the pan velocity, views a second (#313)
float sPanVy = 0.0f;
int32_t sZoomSteps = 0;

float Ease(float t) {
    return 0.5f - 0.5f * std::cos(kPi * t);
}

// The scene whose map the page shows, and why.
int16_t MapScene(const char** source) {
    RsWorldMapInfo info;
    if (gPlayState != nullptr && RsWorldMap_Info(gPlayState->sceneNum, &info)) {
        *source = "scene";
        return gPlayState->sceneNum;
    }
    const int32_t ctx = RsWorld_Get();
    if (ctx >= 0 && ctx <= INT16_MAX && RsWorldMap_Info((int16_t)ctx, &info)) {
        *source = "context";
        return (int16_t)ctx;
    }
    *source = "none";
    return -1;
}

// The view clamped to the image at its own zoom.
MapView Clamp(MapView v, float w, float h) {
    v.zoom = std::max(1.0f, v.zoom);
    const float hw = w / (2.0f * v.zoom);
    const float hh = h / (2.0f * v.zoom);
    v.cx = std::min(std::max(v.cx, hw), w - hw);
    v.cy = std::min(std::max(v.cy, hh), h - hh);
    return v;
}

// The view this tick: the eased step, clamped.
MapView CurrentView(float w, float h) {
    const float t = Ease((float)sZoomTick / (float)kZoomTicks);
    MapView v = { sFrom.cx + (sTo.cx - sFrom.cx) * t, sFrom.cy + (sTo.cy - sFrom.cy) * t,
               sFrom.zoom + (sTo.zoom - sFrom.zoom) * t };
    return Clamp(v, w, h);
}

void ResetView(float w, float h) {
    sFrom = sTo = { w / 2.0f, h / 2.0f, 1.0f };
    sZoomIdx = 0;
    sZoomTick = kZoomTicks;
    sPanVx = sPanVy = 0.0f;
}

// The view to DRAW: the focused view blended back to the whole world by the focus zoom's progress, so the map
// un-zooms as the scroll does.
MapView DrawnView(float w, float h) {
    const MapView whole = { w / 2.0f, h / 2.0f, 1.0f };
    const MapView v = CurrentView(w, h);
    const float a = RsMenu_FocusAmount();
    return Clamp({ whole.cx + (v.cx - whole.cx) * a, whole.cy + (v.cy - whole.cy) * a, 1.0f + (v.zoom - 1.0f) * a }, w,
                 h);
}

// Link's image pixel on the map on show, if he is in its scene.
bool LinkPixel(int16_t scene, float* px, float* py) {
    PlayState* play = gPlayState;
    if (play == nullptr || scene != play->sceneNum) {
        return false;
    }
    Player* player = GET_PLAYER(play);
    return player != nullptr && RsWorldMap_WorldToPixel(scene, player->actor.world.pos.x, player->actor.world.pos.z, px, py);
}

// Steps the zoom one notch in (+1) or out (-1). The first step in from the whole world zooms toward Link.
bool StepZoom(int32_t dir) {
    const char* source = "none";
    const int16_t scene = MapScene(&source);
    RsWorldMapInfo info = {};
    if (scene < 0 || !RsWorldMap_Info(scene, &info)) {
        return false;
    }
    const int32_t next = sZoomIdx + (dir > 0 ? 1 : -1);
    if (next < 0 || next >= kZoomCount) {
        return false;
    }
    const float w = (float)info.widthPx;
    const float h = (float)info.heightPx;
    const MapView now = CurrentView(w, h);
    MapView to = { now.cx, now.cy, kZooms[next] };
    float lx = 0.0f;
    float ly = 0.0f;
    if (sZoomIdx == 0 && dir > 0 && LinkPixel(scene, &lx, &ly)) {
        to.cx = lx;
        to.cy = ly;
    }
    sFrom = now;
    sTo = Clamp(to, w, h);
    sZoomIdx = next;
    sZoomTick = 0;
    sZoomSteps++;
    return true;
}

// Pans by (dx, dy) visible widths/heights. `stopped` (optional) says which axes ran into the image's edge.
bool Pan(float dx, float dy, bool* stoppedX = nullptr, bool* stoppedY = nullptr) {
    const char* source = "none";
    const int16_t scene = MapScene(&source);
    RsWorldMapInfo info = {};
    if (scene < 0 || !RsWorldMap_Info(scene, &info)) {
        return false;
    }
    const float w = (float)info.widthPx;
    const float h = (float)info.heightPx;
    const MapView now = CurrentView(w, h);
    const float mx = dx * w / now.zoom;
    const float my = dy * h / now.zoom;
    sFrom = Clamp({ sFrom.cx + mx, sFrom.cy + my, sFrom.zoom }, w, h);
    sTo = Clamp({ sTo.cx + mx, sTo.cy + my, sTo.zoom }, w, h);
    const MapView after = CurrentView(w, h);
    if (stoppedX != nullptr) {
        *stoppedX = mx != 0.0f && std::fabs(after.cx - (now.cx + mx)) > 0.001f;
    }
    if (stoppedY != nullptr) {
        *stoppedY = my != 0.0f && std::fabs(after.cy - (now.cy + my)) > 0.001f;
    }
    sPans++;
    return true;
}

// One axis of the pan's velocity one tick on, toward `target` (views a second).
float StepVelocity(float v, float target, const RsMenuMapPanEasing& e) {
    const float top = (float)e.speed / 100.0f;
    const float up = e.rampMs > 0 ? top * kTickSeconds * 1000.0f / (float)e.rampMs : top;
    const float down = e.coastMs > 0 ? top * kTickSeconds * 1000.0f / (float)e.coastMs : top;
    // Speeding up: the target lies beyond v in v's own direction (or v is 0). Anything else slows v toward 0 first.
    const bool speedingUp = target != 0.0f && (v == 0.0f || (v > 0.0f) == (target > 0.0f)) && std::fabs(target) > std::fabs(v);
    const float step = speedingUp ? up : down;
    if (std::fabs(target - v) <= step) {
        return target;
    }
    // A reversal coasts through 0 and stops there this tick, so speeding up the other way starts from rest.
    const float next = v + (target > v ? step : -step);
    return (v > 0.0f && next < 0.0f) || (v < 0.0f && next > 0.0f) ? 0.0f : next;
}

// The page's layout: where the map's box sits, game space, y down. Shared by the draw and the cursor node.
struct Layout {
    bool map;
    int16_t scene;
    RsWorldMapInfo info;
    float gx, gy, w, h; // the box
    float s;            // game units an image pixel at zoom 1
};

Layout PageLayout() {
    Layout l = {};
    const char* source = "none";
    l.scene = MapScene(&source);
    l.map = l.scene >= 0 && RsWorldMap_Info(l.scene, &l.info);
    const RsMenuRect r = RsMenu_PageRect();
    const float availW = (float)(r.x1 - r.x0) - 2.0f * kMargin;
    const float availH = (float)(r.y1 - r.y0) - 2.0f * kMargin;
    const float imgW = l.map ? (float)l.info.widthPx : 1.0f;
    const float imgH = l.map ? (float)l.info.heightPx : 1.0f;
    l.s = std::min(availW / imgW, availH / imgH);
    l.w = imgW * l.s;
    l.h = imgH * l.s;
    l.gx = (float)(r.x0 + r.x1) / 2.0f - l.w / 2.0f;
    l.gy = (float)(r.y0 + r.y1) / 2.0f - l.h / 2.0f;
    return l;
}

// The image-pixel rect of square (sx, sy), clipped to the image. False if it misses the image.
bool SquareRect(const RsWorldMapInfo& info, int32_t sx, int32_t sy, int32_t* x0, int32_t* y0, int32_t* x1,
                int32_t* y1) {
    const int32_t ppt = info.pxPerTile;
    const int32_t northRs = info.minRsY + info.depthTiles; // the RS row line along the image's top edge
    *x0 = std::max<int32_t>(0, (sx * 64 - info.minRsX) * ppt);
    *x1 = std::min<int32_t>(info.widthPx, (sx * 64 + 64 - info.minRsX) * ppt);
    *y0 = std::max<int32_t>(0, (northRs - (sy * 64 + 64)) * ppt);
    *y1 = std::min<int32_t>(info.heightPx, (northRs - sy * 64) * ppt);
    return *x0 < *x1 && *y0 < *y1;
}

// A vertex in the image's geometry space: (px, -py).
void SetV(Vtx* v, float px, float py, const uint8_t* c) {
    v->v.ob[0] = (int16_t)std::lround(px);
    v->v.ob[1] = (int16_t)-std::lround(py);
    v->v.ob[2] = 0;
    v->v.flag = 0;
    v->v.tc[0] = v->v.tc[1] = 0;
    v->v.cn[0] = c[0];
    v->v.cn[1] = c[1];
    v->v.cn[2] = c[2];
    v->v.cn[3] = c[3];
}

// A vertex in the menu's game space (x right, y down, 320 x 240), as the base matrix's centred y-up wants it.
void SetGameV(Vtx* v, float gx, float gy, const uint8_t* c) {
    SetV(v, gx - (float)(SCREEN_WIDTH / 2), gy - (float)(SCREEN_HEIGHT / 2), c); // SetV negates y
}

void PushFlat(std::vector<Gfx>& dl) {
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF));
    dl.push_back(gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE));
    dl.push_back(gsDPSetOtherMode(G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE |
                                      G_TL_TILE | G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));
    dl.push_back(gsSPLoadGeometryMode(G_SHADE | G_SHADING_SMOOTH));
}

// Bilinear: the blocks carry an apron for it (WORLD_MAP.md), and at 1080p the page minifies the image by two.
void PushMap(std::vector<Gfx>& dl) {
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
    dl.push_back(gsDPSetCombineMode(G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM));
    dl.push_back(gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
    dl.push_back(gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE |
                                      G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));
    dl.push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
}

// Flat quads, sixteen to a vertex load (gSPVertex caps at 64).
void EmitQuads(std::vector<Gfx>& dl, Vtx* vtx, int32_t quads) {
    for (int32_t i = 0; i < quads; i++) {
        if ((i % 16) == 0) {
            dl.push_back(gsSPVertex(&vtx[i * 4], std::min(quads - i, 16) * 4, 0));
        }
        const uint8_t b = (uint8_t)(4 * (i % 16));
        dl.push_back(gsSP1Quadrangle(b + 0, b + 1, b + 3, b + 2, 0));
    }
}

void QuadV(Vtx* v, float x0, float y0, float x1, float y1, const uint8_t* c) {
    SetV(&v[0], x0, y0, c);
    SetV(&v[1], x1, y0, c);
    SetV(&v[2], x0, y1, c);
    SetV(&v[3], x1, y1, c);
}

void QuadGame(Vtx* v, float x0, float y0, float x1, float y1, const uint8_t* c) {
    SetGameV(&v[0], x0, y0, c);
    SetGameV(&v[1], x1, y0, c);
    SetGameV(&v[2], x0, y1, c);
    SetGameV(&v[3], x1, y1, c);
}

// The fog: per chunk row, runs of hidden squares merged into one quad each, CLIPPED to `clip` (the view and its
// margin, image pixels). Unclipped, a zoomed view's runs reach far past the map's box - a whole row of chunks is
// many times the box wide - and the edge masks cover only the margin (the first focused build, 2026-11-07).
int32_t DrawFog(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, int16_t scene, const RsWorldMapInfo& info,
                const float clip[4]) {
    struct Run {
        int32_t x0, y0, x1, y1;
    };
    std::vector<Run> runs;
    std::vector<RsMapChunk> hidden;
    for (const RsMapChunk& c : RsMapReveal_Chunks(scene)) {
        if (!RsMapReveal_IsRevealed(c.sx, c.sy)) {
            hidden.push_back(c);
        }
    }
    std::sort(hidden.begin(), hidden.end(), [](const RsMapChunk& a, const RsMapChunk& b) {
        return a.sy != b.sy ? a.sy > b.sy : a.sx < b.sx;
    });
    int32_t lastSx = INT32_MIN;
    int32_t lastSy = INT32_MIN;
    for (const RsMapChunk& c : hidden) {
        int32_t x0, y0, x1, y1;
        if (!SquareRect(info, c.sx, c.sy, &x0, &y0, &x1, &y1)) {
            continue;
        }
        if (!runs.empty() && c.sy == lastSy && c.sx == lastSx + 1) {
            runs.back().x1 = x1;
        } else {
            runs.push_back({ x0, y0, x1, y1 });
        }
        lastSx = c.sx;
        lastSy = c.sy;
    }
    std::vector<Run> clipped;
    for (const Run& r : runs) {
        const Run c = { std::max(r.x0, (int32_t)std::floor(clip[0])), std::max(r.y0, (int32_t)std::floor(clip[1])),
                        std::min(r.x1, (int32_t)std::ceil(clip[2])), std::min(r.y1, (int32_t)std::ceil(clip[3])) };
        if (c.x0 < c.x1 && c.y0 < c.y1) {
            clipped.push_back(c);
        }
    }
    runs.swap(clipped);
    if (runs.empty()) {
        return 0;
    }
    const uint8_t* colour = RsMenuMapPage_Fog() == RS_MENU_MAP_FOG_DIM ? kFogDim : kFogHide;
    Vtx* vtx = (Vtx*)Graph_Alloc(gfxCtx, runs.size() * 4 * sizeof(Vtx));
    for (size_t i = 0; i < runs.size(); i++) {
        QuadV(&vtx[i * 4], (float)runs[i].x0, (float)runs[i].y0, (float)runs[i].x1, (float)runs[i].y1, colour);
    }
    EmitQuads(dl, vtx, (int32_t)runs.size());
    return (int32_t)runs.size();
}

// --- the arrows -------------------------------------------------------------------------------------------------
//
// Every arrow is clipped to the VIEW, like the map under it (the owner, 2026-11-07: cut at the edge, then gone).
// The vehicles' and the entrance's are PRINTED on the map as #306's minimap prints them - a flat triangle, the
// silhouette of vanilla's arrow at its 0.4 scale, in the minimap's colours (RsMinimap.h, RsVehicleMinimap.h) - at
// their world position under the placement matrix, so they glide with the map, and their polygons are clipped
// to the view (Sutherland-Hodgman). Link's vanilla arrow is a 3D display list that cannot be clipped as geometry;
// the scissor clips it (and everything else in the view) while the scroll stands still, which is whenever the
// view can pan or zoom.

enum ArrowState { ARROW_NONE = 0, ARROW_IN, ARROW_EDGE, ARROW_OUT };

const char* ArrowStateName(int32_t s) {
    switch (s) {
        case ARROW_IN:
            return "in";
        case ARROW_EDGE:
            return "edge";
        case ARROW_OUT:
            return "out";
        default:
            return "none";
    }
}

struct Pt {
    float x, y;
};

// One axis-aligned edge of Sutherland-Hodgman: keep the side >= (keepGreater) or <= `bound` on `axis`.
void ClipEdge(std::vector<Pt>& poly, int axis, float bound, bool keepGreater) {
    std::vector<Pt> out;
    const size_t n = poly.size();
    for (size_t i = 0; i < n; i++) {
        const Pt& a = poly[i];
        const Pt& b = poly[(i + 1) % n];
        const float av = axis == 0 ? a.x : a.y;
        const float bv = axis == 0 ? b.x : b.y;
        const bool ain = keepGreater ? av >= bound : av <= bound;
        const bool bin = keepGreater ? bv >= bound : bv <= bound;
        if (ain) {
            out.push_back(a);
        }
        if (ain != bin) {
            const float t = (bound - av) / (bv - av);
            out.push_back({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t });
        }
    }
    poly.swap(out);
}

// A convex polygon clipped to rect r (x0, y0, x1, y1).
std::vector<Pt> ClipToRect(std::vector<Pt> poly, const float r[4]) {
    ClipEdge(poly, 0, r[0], true);
    ClipEdge(poly, 0, r[2], false);
    ClipEdge(poly, 1, r[1], true);
    ClipEdge(poly, 1, r[3], false);
    return poly;
}

// In, on the edge of, or out of rect r, for a convex polygon.
int32_t StateIn(const std::vector<Pt>& poly, const float r[4]) {
    bool all = true;
    for (const Pt& p : poly) {
        if (p.x < r[0] || p.x > r[2] || p.y < r[1] || p.y > r[3]) {
            all = false;
        }
    }
    if (all) {
        return ARROW_IN;
    }
    return ClipToRect(poly, r).size() >= 3 ? ARROW_EDGE : ARROW_OUT;
}

// Flat primitive colour, untextured, translucent render mode: the printed arrows' state.
void PushPrim(std::vector<Gfx>& dl) {
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF));
    dl.push_back(gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE));
    dl.push_back(gsDPSetOtherMode(G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE |
                                      G_TL_TILE | G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));
    dl.push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
}

// A convex polygon in image pixels, clipped to `clip`, as a fan in the prim colour. False if nothing is left.
bool DrawConvex(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, const std::vector<Pt>& poly, const uint8_t* rgb,
                const float clip[4]) {
    const std::vector<Pt> c = ClipToRect(poly, clip);
    if (c.size() < 3 || c.size() > 32) {
        return false;
    }
    const uint8_t white[4] = { 255, 255, 255, 255 };
    Vtx* v = (Vtx*)Graph_Alloc(gfxCtx, c.size() * sizeof(Vtx));
    for (size_t i = 0; i < c.size(); i++) {
        SetV(&v[i], c[i].x, c[i].y, white);
    }
    dl.push_back(gsDPSetPrimColor(0, 0, rgb[0], rgb[1], rgb[2], 255));
    dl.push_back(gsSPVertex(v, (uint32_t)c.size(), 0));
    for (uint8_t i = 1; i + 1 < (uint8_t)c.size(); i++) {
        dl.push_back(gsSP1Triangle(0, i, (uint8_t)(i + 1), 0));
    }
    return true;
}

// A world position and heading -> its image pixel and the image-space unit vector it points along (#306's MapPose).
void Pose(int16_t scene, float x, float z, int16_t yaw, float* px, float* py, float* ux, float* uy) {
    const float yr = (float)yaw * (kPi / 32768.0f);
    float qx = 0.0f;
    float qy = 0.0f;
    RsWorldMap_WorldToPixel(scene, x, z, px, py);
    RsWorldMap_WorldToPixel(scene, x + std::sin(yr) * 100.0f, z + std::cos(yr) * 100.0f, &qx, &qy);
    const float dx = qx - *px;
    const float dy = qy - *py;
    const float d = std::sqrt(dx * dx + dy * dy);
    *ux = d > 0.0f ? dx / d : 0.0f;
    *uy = d > 0.0f ? dy / d : -1.0f;
}

// The printed arrow's silhouette at (px, py) along (ux, uy): `k` game units an image pixel, so it is the
// minimap's size in game units whatever the zoom.
std::vector<Pt> ArrowPoly(float px, float py, float ux, float uy, float k) {
    const float len = RS_MINIMAP_ARROW_HALF_LEN / k;
    const float wid = RS_MINIMAP_ARROW_HALF_WIDTH / k;
    const float nx = -uy;
    const float ny = ux;
    const float bx = px - ux * len;
    const float by = py - uy * len;
    return { { px + ux * len, py + uy * len }, { bx + nx * wid, by + ny * wid }, { bx - nx * wid, by - ny * wid } };
}

// The custom arrow (296d1d210) as four convex triangles - outline then fill - so it clips like the printed ones.
void DrawCustomArrow(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, float px, float py, int16_t yaw, float len,
                     const float clip[4]) {
    const float a = (float)yaw * kPi / 32768.0f;
    const float dx = std::sin(a);
    const float dy = std::cos(a);
    const float rx = -dy;
    const float ry = dx;
    const float shape[4][2] = { { 0.0f, 0.55f }, { -0.40f, -0.45f }, { 0.0f, -0.20f }, { 0.40f, -0.45f } };
    for (int32_t pass = 0; pass < 2; pass++) {
        const float k = len * (pass == 0 ? 1.35f : 1.0f);
        Pt p[4];
        for (int32_t i = 0; i < 4; i++) {
            const float u = shape[i][0] * k;
            const float v = shape[i][1] * k;
            p[i] = { px + u * rx + v * dx, py + u * ry + v * dy };
        }
        const uint8_t* c = pass == 0 ? kArrowEdge : kArrowFill;
        DrawConvex(gfxCtx, dl, { p[0], p[1], p[2] }, c, clip);
        DrawConvex(gfxCtx, dl, { p[0], p[2], p[3] }, c, clip);
    }
}

// What the last drawn frame did with each arrow, for the dump.
struct ArrowInfo {
    const char* kind; // link, entrance, boat, balloon
    int32_t state;    // ArrowState
    float px, py;     // image pixel
    bool controls;    // a vehicle Link holds the controls of: not drawn, his arrow stands for both
};
std::vector<ArrowInfo> sArrows;
int32_t sLinkArrow = ARROW_NONE;
bool sScissored = false;

// The view rect of the frame drawn one tick before, and when: the map's frames between two ticks show a view
// INTERPOLATED between that one and this one, so a tick must emit both (the trailing-edge holes, 2026-11-07).
float sPrevView[4] = {};
uint32_t sPrevViewFrame = 0;

bool sPrevViewValid = false;

// --- the per-frame trace (#313, MapPage.h) -----------------------------------------------------------------------
// Each draw (a tick) keeps its view rect; NoteFrame lerps the last two by the frame's step, as the interpolation lerps
// their placement matrices.
struct TraceTick {
    int32_t draw = 0;       // sFrame
    uint32_t game = 0;      // play->state.frames when drawn: NoteFrame records only the frames of that tick
    float x0 = 0, y0 = 0, w = 0, h = 0, renderPerPx = 0, vx = 0, vy = 0;
};
bool sTraceOn = false;
TraceTick sTracePrev;
TraceTick sTraceCur;
std::vector<RsMenuMapTraceRow> sTraceRing;
int32_t sTraceRows = 0;
std::chrono::steady_clock::time_point sTraceStart;

// One placement push: image pixel (px, py), drawn at (px, -py), lands on game (gx + (px - vx0) k, gy + (py - vy0) k).
void PushPlacement(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, float gx, float gy, float vx0, float vy0, float k) {
    Matrix_Push();
    Matrix_Translate(gx - (float)(SCREEN_WIDTH / 2) - vx0 * k, (float)(SCREEN_HEIGHT / 2) - gy + vy0 * k, 0.0f,
                     MTXMODE_APPLY);
    Matrix_Scale(k, k, 1.0f, MTXMODE_APPLY);
    dl.push_back(gsSPMatrix(Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
}

void PopPlacement(GraphicsContext* gfxCtx, std::vector<Gfx>& dl) {
    Matrix_Pop();
    dl.push_back(gsSPMatrix(Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
}

// --- the map icons (#307) ---------------------------------------------------------------------------------------
struct IconInfo {
    const char* name;
    const char* token;
    const char* state; // in | edge | out | fog | storey | zoom | off | no_sprite
    float px, py;
    uint8_t alpha;
};
std::vector<IconInfo> sIcons;
int32_t sIconsShown = 0;
float sIconUnits = 0.0f; // the last icon's width in game units, for the dump

// The icons on `scene`'s map, under the nested icon matrix (already loaded): each an axis-aligned quad centred on its
// tile, kIconTexelUnits game units an RS pixel whatever the zoom (`k` game units an image pixel), clipped to `emit` and
// judged against `view` as the arrows are. Lists every icon in sIcons, drawn or not.
void DrawIcons(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, int16_t scene, float k, float zoom, const float view[4],
               const float emit[4]) {
    RsMapIcon icons[32];
    const int32_t n = RsMapIcons_ForScene(scene, icons, 32);
    const bool on = RsMenuMapPage_Icons();
    (void)zoom;
    const uint8_t alpha = 255;
    bool stateSet = false;
    for (int32_t i = 0; i < n; i++) {
        const RsMapIconDef* d = icons[i].def;
        float px = 0.0f;
        float py = 0.0f;
        RsWorldMap_WorldToPixel(scene, icons[i].x, icons[i].z, &px, &py);
        IconInfo info = { d->name, d->token, "in", px, py, alpha };
        RsSpriteInfo sprite = {};
        const char* path = RsSprites_Path(d->token);
        if (!on) {
            info.state = "off";
        } else if (!icons[i].onLayer) {
            info.state = "storey";
        } else if (!RsMapReveal_IsRevealed(d->rsX >> 6, d->rsY >> 6)) {
            info.state = "fog";
        } else if (path == nullptr || !RsSprites_Info(d->token, &sprite)) {
            info.state = "no_sprite";
        }
        if (std::string(info.state) != "in") {
            sIcons.push_back(info);
            continue;
        }
        const float hw = (float)sprite.rsWidth * kIconTexelUnits / (2.0f * k); // image pixels
        const float hh = (float)sprite.rsHeight * kIconTexelUnits / (2.0f * k);
        sIconUnits = (float)sprite.rsWidth * kIconTexelUnits;
        const float r[4] = { px - hw, py - hh, px + hw, py + hh };
        const std::vector<Pt> quad = { { r[0], r[1] }, { r[2], r[1] }, { r[2], r[3] }, { r[0], r[3] } };
        const int32_t st = StateIn(quad, view);
        info.state = ArrowStateName(st);
        sIcons.push_back(info);
        // Clipped to the emitted rect: axis-aligned, so the texture coordinates follow linearly.
        const float c[4] = { std::max(r[0], emit[0]), std::max(r[1], emit[1]), std::min(r[2], emit[2]),
                             std::min(r[3], emit[3]) };
        if (st == ARROW_OUT || c[0] >= c[2] || c[1] >= c[3]) {
            continue;
        }
        if (!stateSet) {
            dl.push_back(gsDPPipeSync());
            dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
            dl.push_back(gsDPSetCombineMode(G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM));
            dl.push_back(gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP |
                                              G_TT_NONE | G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE |
                                              G_PM_NPRIMITIVE,
                                          G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));
            dl.push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
            dl.push_back(gsDPSetPrimColor(0, 0, 255, 255, 255, alpha));
            stateSet = true;
        }
        dl.push_back(gsDPPipeSync());
        Gfx load[24];
        Gfx* p = load;
        gDPLoadTextureTile(p++, (u8*)path, G_IM_FMT_RGBA, G_IM_SIZ_32b, sprite.width, sprite.height, 0, 0,
                           sprite.width - 1, sprite.height - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                           G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        dl.insert(dl.end(), load, p);
        Vtx* v = (Vtx*)Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
        const float xs[4] = { c[0], c[2], c[0], c[2] };
        const float ys[4] = { c[1], c[1], c[3], c[3] };
        for (int32_t j = 0; j < 4; j++) {
            v[j].v.ob[0] = (int16_t)std::lround(xs[j] * kIconFix);
            v[j].v.ob[1] = (int16_t)-std::lround(ys[j] * kIconFix);
            v[j].v.ob[2] = 0;
            v[j].v.flag = 0;
            v[j].v.tc[0] = (int16_t)std::lround((xs[j] - r[0]) / (r[2] - r[0]) * sprite.width * 32.0f);
            v[j].v.tc[1] = (int16_t)std::lround((ys[j] - r[1]) / (r[3] - r[1]) * sprite.height * 32.0f);
            v[j].v.cn[0] = v[j].v.cn[1] = v[j].v.cn[2] = v[j].v.cn[3] = 255;
        }
        dl.push_back(gsSPVertex(v, 4, 0));
        dl.push_back(gsSP2Triangles(0, 2, 3, 0, 0, 3, 1, 0));
        sIconsShown++;
    }
    if (stateSet) {
        dl.push_back(gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF));
    }
}

void MapPageDraw(PlayState* play, int32_t pageIndex, void* userData) {
    (void)pageIndex;
    (void)userData;
    GraphicsContext* gfxCtx = nullptr;
    std::vector<Gfx>* dlp = nullptr;
    if (!RsMenu_BeginRawDraw(&gfxCtx, &dlp)) {
        return;
    }
    std::vector<Gfx>& dl = *dlp;
    const size_t wordsBefore = dl.size();
    sFrame++;
    sBlocks = 0;
    sFogQuads = 0;
    sArrowDrawn = 0;
    sArrows.clear();
    sIcons.clear();
    sIconsShown = 0;
    sLinkArrow = ARROW_NONE;
    sScissored = false;
    sWallsDrawn = false;
    sEdgeless = false;
    sEdges = 0;
    sEdgeJoins = 0;
    sEdgeWords = 0;
    sWallSnapped = false;
    sWallNominalPx = 0.0f;
    sWallHalfPx = 0.0f;
    sWallScreenPx = 0.0f;

    const Layout l = PageLayout();
    const float imgW = l.map ? (float)l.info.widthPx : 1.0f;
    const float imgH = l.map ? (float)l.info.heightPx : 1.0f;
    if (!RsMenu_IsFocused() && !sViewReset) {
        ResetView(imgW, imgH);
        sViewReset = true;
    } else if (RsMenu_IsFocused()) {
        sViewReset = false;
    }
    const MapView v = l.map ? DrawnView(imgW, imgH) : MapView{ 0.5f, 0.5f, 1.0f };
    const float k = l.s * v.zoom; // game units an image pixel, this view
    const float vw = imgW / v.zoom;
    const float vh = imgH / v.zoom;
    const float vx0 = v.cx - vw / 2.0f; // the view's rect, image pixels
    const float vy0 = v.cy - vh / 2.0f;
    const float view[4] = { vx0, vy0, vx0 + vw, vy0 + vh };
    sDst[0] = l.gx;
    sDst[1] = l.gy;
    sDst[2] = l.w;
    sDst[3] = l.h;
    sDrawView[0] = vx0;
    sDrawView[1] = vy0;
    sDrawView[2] = vw;
    sDrawView[3] = vh;

    // What this tick emits: its own view and the last tick's (the frames between them show a view interpolated
    // between the two), a margin round both, clipped to the image.
    float emit[4] = { view[0], view[1], view[2], view[3] };
    const uint32_t frames = play != nullptr ? play->state.frames : 0;
    if (sPrevViewValid && frames == sPrevViewFrame + 1) {
        emit[0] = std::min(emit[0], sPrevView[0]);
        emit[1] = std::min(emit[1], sPrevView[1]);
        emit[2] = std::max(emit[2], sPrevView[2]);
        emit[3] = std::max(emit[3], sPrevView[3]);
    }
    for (int32_t i = 0; i < 4; i++) {
        sPrevView[i] = view[i];
    }
    sPrevViewFrame = frames;
    sPrevViewValid = l.map;
    emit[0] = std::max(0.0f, emit[0] - kViewMarginPx);
    emit[1] = std::max(0.0f, emit[1] - kViewMarginPx);
    emit[2] = std::min(imgW, emit[2] + kViewMarginPx);
    emit[3] = std::min(imgH, emit[3] + kViewMarginPx);

    // THE SCISSOR, while the scroll stands still: the map's box on screen, through the base matrix this page draws
    // under (Matrix_MultVec3f reads it and records no op). Everything inside - the map, the fog, every arrow - is
    // cut exactly at the box, the emitted margin and last tick's strip included.
    // It is set kScissorSlack OUTSIDE the box, inside the masks: the scissor's own placement lands within a few
    // screen pixels of the geometry's (measured 2026-11-07: up to 5 px short at 1808 x 996), so a scissor ON the box
    // left a strip inside it empty. Slack outside, the masks (kMargin deep, drawn after everything) cover the band
    // between, and nothing past them can show.
    if (l.map && RsMenu_ScrollStill()) {
        const float sl = kScissorSlack;
        Vec3f c0 = { l.gx - sl - (float)(SCREEN_WIDTH / 2), (float)(SCREEN_HEIGHT / 2) - (l.gy - sl), 0.0f };
        Vec3f c1 = { l.gx + l.w + sl - (float)(SCREEN_WIDTH / 2), (float)(SCREEN_HEIGHT / 2) - (l.gy + l.h + sl), 0.0f };
        Vec3f s0;
        Vec3f s1;
        Matrix_MultVec3f(&c0, &s0);
        Matrix_MultVec3f(&c1, &s1);
        // Fast3D stretches a scissor's x across the whole window (the frame's own full-screen one is 0..320 and still
        // reaches the widescreen sides), while this ortho's geometry is aspect-corrected about the centre: so x goes
        // into scissor space by the ratio of the 4:3 aspect to the window's. The first build of this missed it and
        // cut nothing (2026-11-07).
        const float toScissor = ((float)SCREEN_WIDTH / (float)SCREEN_HEIGHT) / OTRGetAspectRatio();
        const float ulx = std::max(0.0f, (float)(SCREEN_WIDTH / 2) + std::min(s0.x, s1.x) * toScissor);
        const float lrx = std::min((float)SCREEN_WIDTH, (float)(SCREEN_WIDTH / 2) + std::max(s0.x, s1.x) * toScissor);
        const float uly = std::max(0.0f, (float)(SCREEN_HEIGHT / 2) - std::max(s0.y, s1.y));
        const float lry = std::min((float)SCREEN_HEIGHT, (float)(SCREEN_HEIGHT / 2) - std::min(s0.y, s1.y));
        dl.push_back(gsDPPipeSync());
        dl.push_back(gsDPSetScissor(G_SC_NON_INTERLACE, ulx, uly, lrx, lry));
        sScissored = true;
    }

    float lpx = 0.0f;
    float lpy = 0.0f;
    const bool link = l.map && LinkPixel(l.scene, &lpx, &lpy);
    const int16_t linkYaw = link ? GET_PLAYER(play)->actor.shape.rot.y : 0;
    const bool customArrow = RsMenuMapPage_Arrow() == RS_MENU_MAP_ARROW_CUSTOM;
    if (link) {
        // Link's arrow against the view: the silhouette's own extent, centred on him.
        float ux = 0.0f;
        float uy = 0.0f;
        float px = 0.0f;
        float py = 0.0f;
        Pose(l.scene, GET_PLAYER(play)->actor.world.pos.x, GET_PLAYER(play)->actor.world.pos.z, linkYaw, &px, &py, &ux,
             &uy);
        sLinkArrow = StateIn(ArrowPoly(lpx, lpy, ux, uy, k), view);
        sArrows.push_back({ "link", sLinkArrow, lpx, lpy, false });
    }

    // #309: the walls' width in image pixels at this zoom. The base matrix (the scroll's roll, x-scale and focus
    // zoom) is the Matrix_* top here, so a game unit's height through it is N64 screen pixels a game unit; k is game
    // units an image pixel. Read with Matrix_MultVec3f, which records no op.
    // The width is a fraction of an RS tile, so it grows with the map as it zooms (the first look: walls heavier
    // than the picture at the whole-world view); under the floor (render px) it stops thinning, since a line under
    // a pixel drops out and shimmers as it pans. With snap on it is then rounded to whole render pixels, and
    // EmitEdges puts the endpoints on the pixel grid, so equal walls cover equal pixels.
    const RsMenuMapWalls wallsMode = RsMenuMapPage_Walls();
    const bool lines = l.map && l.info.version >= 4 &&
                       (wallsMode == RS_MENU_MAP_WALLS_LINES ||
                        (wallsMode == RS_MENU_MAP_WALLS_ZOOMED && v.zoom >= RS_MENU_MAP_WALLS_ZOOMED_FROM - 0.001f));
    float wallHalfPx = 0.0f;
    Vec3f a0 = { 0.0f, 0.0f, 0.0f };
    Vec3f a1 = { 0.0f, 100.0f, 0.0f };
    Vec3f b0;
    Vec3f b1;
    Matrix_MultVec3f(&a0, &b0);
    Matrix_MultVec3f(&a1, &b1);
    const float n64PerGame = std::fabs(b1.y - b0.y) / 100.0f;
    const float renderPerPx = n64PerGame * k * (float)OTRGetGameRenderHeight() / (float)SCREEN_HEIGHT;
    if (sTraceOn && l.map) {
        sTracePrev = sTraceCur;
        sTraceCur = { sFrame, frames, vx0, vy0, vw, vh, renderPerPx, sPanVx, sPanVy };
    }
    if (lines) {
        if (renderPerPx > 0.0f) {
            sWallNominalPx = RsMenuMapPage_WallTiles() * (float)l.info.pxPerTile * renderPerPx;
            float w = std::max(RsMenuMapPage_WallFloor(), sWallNominalPx);
            if (RsMenuMapPage_WallSnap()) {
                w = std::max(1.0f, std::round(w));
            }
            sWallScreenPx = w;
            wallHalfPx = w * 0.5f / renderPerPx;
        }
    }

    // THE MAP LAYER, under the placement matrix: map, walls, fog, the vehicles' arrows, the custom Link arrow.
    PushPlacement(gfxCtx, dl, l.gx, l.gy, vx0, vy0, k);
    if (l.map) {
        PushMap(dl);
        const size_t at = dl.size();
        dl.resize(at + (size_t)RsWorldMap_GfxWords(emit[2] - emit[0], emit[3] - emit[1]) + 2);
        Gfx* p = &dl[at];
        sEdgeless = lines;
        sBlocks = RsWorldMap_EmitBlocksEx(gfxCtx, &p, l.scene, emit[0], emit[1], emit[2] - emit[0], emit[3] - emit[1],
                                          lines ? RS_WORLD_MAP_EDGELESS : 0u);
        dl.resize((size_t)(p - dl.data()));
    }
    // #309: the walls, over the blocks and under the fog. Called EVERY frame, map or no map, lines or not: it records
    // its nested matrix's ops unconditionally (WorldMap.h), and a half width of 0 draws nothing.
    {
        const size_t before = dl.size();
        if (lines) {
            uint8_t rgb[3] = { 226, 232, 240 };
            RsWorldMap_WallColour(l.scene, rgb);
            PushPrim(dl);
            dl.push_back(gsDPSetPrimColor(0, 0, rgb[0], rgb[1], rgb[2], 255));
        }
        const size_t at = dl.size();
        dl.resize(at + (size_t)RsWorldMap_EdgeGfxWords(l.scene) + 2);
        Gfx* p = &dl[at];
        sEdges = RsWorldMap_EmitEdgesEx(gfxCtx, &p, l.scene, emit[0], emit[1], emit[2] - emit[0], emit[3] - emit[1],
                                        wallHalfPx, RsMenuMapPage_WallSnap() ? RS_WORLD_MAP_EDGES_SNAP : 0u);
        dl.resize((size_t)(p - dl.data()));
        const RsWorldMapEdgeStats es = RsWorldMap_EdgeStats();
        sEdgeJoins = es.joins;
        sWallSnapped = es.snapped;
        if (sEdges == 0) {
            dl.resize(before); // nothing drawn: drop the state too
        }
        sEdgeWords = (int32_t)(dl.size() - before);
        sWallsDrawn = lines;
        sWallHalfPx = wallHalfPx;
    }
    if (l.map) {
        PushFlat(dl);
        sFogQuads = DrawFog(gfxCtx, dl, l.scene, l.info, emit);
    }
    // The map icons (#307), over the fog and under every arrow, under a nested 1/kIconFix scale of the placement:
    // its ops are recorded every frame, map or no map, icons or none.
    {
        Matrix_Push();
        Matrix_Scale(1.0f / kIconFix, 1.0f / kIconFix, 1.0f, MTXMODE_APPLY);
        Mtx* iconMtx = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
        Matrix_Pop();
        Mtx* placement = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
        if (l.map) {
            const size_t at = dl.size();
            dl.push_back(gsSPMatrix(iconMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
            DrawIcons(gfxCtx, dl, l.scene, k, v.zoom, view, emit);
            if (sIconsShown == 0) {
                dl.resize(at); // nothing drawn: drop the matrix load too
            } else {
                dl.push_back(gsSPMatrix(placement, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
            }
        }
    }
    if (l.map) {
        // The vehicles' arrows, under Link's (the minimap's order), in the scene they are in only.
        if (play != nullptr && l.scene == play->sceneNum) {
            std::vector<RsVehicleMapArrow> vehicles;
            RsVehicleMinimap_MapArrows(vehicles);
            PushPrim(dl);
            for (const RsVehicleMapArrow& a : vehicles) {
                float px = 0.0f;
                float py = 0.0f;
                float ux = 0.0f;
                float uy = 0.0f;
                Pose(l.scene, a.x, a.z, a.yaw, &px, &py, &ux, &uy);
                const std::vector<Pt> poly = ArrowPoly(px, py, ux, uy, k);
                const int32_t state = StateIn(poly, view);
                if (!a.controls && state != ARROW_OUT) {
                    DrawConvex(gfxCtx, dl, poly, a.rgb, emit);
                }
                sArrows.push_back({ a.balloon ? "balloon" : "boat", state, px, py, a.controls });
            }
        }
        if (customArrow && link && sLinkArrow != ARROW_OUT) {
            PushPrim(dl);
            DrawCustomArrow(gfxCtx, dl, lpx, lpy, linkYaw, kArrowUnits / l.s, emit);
            sArrowDrawn = 1;
        }
    }
    PopPlacement(gfxCtx, dl);

    // Link's vanilla arrow, in game units under the base matrix; its ops are recorded every frame. The scissor
    // cuts it at the box while the scroll is still.
    {
        const bool shown = link && !customArrow && sLinkArrow != ARROW_OUT;
        const float ax = link ? l.gx + (lpx - vx0) * k : -1000.0f;
        const float ay = link ? l.gy + (lpy - vy0) * k : -1000.0f;
        // Vanilla's own turn for a north-up map with world x right and z down (Minimap_DrawCompassIcons).
        const float angle = (32767.0f - (float)linkYaw) / 10240.0f;
        Matrix_Push();
        Matrix_Translate(ax - (float)(SCREEN_WIDTH / 2), (float)(SCREEN_HEIGHT / 2) - ay, 0.0f, MTXMODE_APPLY);
        Matrix_Scale(kCompassScale, kCompassScale, kCompassScale, MTXMODE_APPLY);
        Matrix_RotateX(-1.6f, MTXMODE_APPLY);
        Matrix_RotateY(angle, MTXMODE_APPLY);
        Mtx* arrow = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
        Matrix_Pop();
        Mtx* restored = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
        if (shown) {
            const Color_RGB8 c = RsMinimap_LinkColor();
            Gfx setup[2];
            Gfx_SetupDL(setup, SETUPDL_42);
            dl.push_back(gsDPPipeSync());
            dl.push_back(setup[0]);
            dl.push_back(gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE));
            dl.push_back(gsSPMatrix(arrow, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
            dl.push_back(gsDPSetPrimColor(0, 0xFF, c.r, c.g, c.b, 255));
            dl.push_back(gsSPDisplayListOTRFilePath(gCompassArrowDL));
            dl.push_back(gsSPMatrix(restored, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
            sArrowDrawn = 1;
        }
    }

    // THE TOP LAYER, under the placement again: the red entrance arrow goes over Link's, as on the minimap.
    PushPlacement(gfxCtx, dl, l.gx, l.gy, vx0, vy0, k);
    if (link) {
        f32 ex = 0.0f;
        f32 ez = 0.0f;
        s16 eyaw = 0;
        RsMinimap_EntrancePose(&ex, &ez, &eyaw);
        float px = 0.0f;
        float py = 0.0f;
        float ux = 0.0f;
        float uy = 0.0f;
        Pose(l.scene, ex, ez, eyaw, &px, &py, &ux, &uy);
        const std::vector<Pt> poly = ArrowPoly(px, py, ux, uy, k);
        const int32_t state = StateIn(poly, view);
        if (state != ARROW_OUT) {
            const Color_RGB8 c = RsMinimap_EntranceColor();
            const uint8_t rgb[3] = { c.r, c.g, c.b };
            PushPrim(dl);
            DrawConvex(gfxCtx, dl, poly, rgb, emit);
        }
        sArrows.push_back({ "entrance", state, px, py, false });
    }
    PopPlacement(gfxCtx, dl);

    if (sScissored) {
        // Back to the frame's own (z_rcp.c sets OVERLAY_DISP's to the whole screen each frame).
        dl.push_back(gsDPPipeSync());
        dl.push_back(gsDPSetScissor(G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT));
    }

    if (l.map) {
        // The masks and the frame, game units, under the base matrix. With the scissor on they cover nothing the
        // scissor has not already cut; while the scroll moves (no scissor) they cover the emitted margin.
        uint8_t panel[4] = { 0, 0, 0, 255 };
        RsMenu_PanelColour(panel);
        const float x0 = l.gx;
        const float y0 = l.gy;
        const float x1 = l.gx + l.w;
        const float y1 = l.gy + l.h;
        const float m = kMargin;
        const float f = kFrameUnits;
        Vtx* vtx = (Vtx*)Graph_Alloc(gfxCtx, 8 * 4 * sizeof(Vtx));
        QuadGame(&vtx[0], x0 - m, y0 - m, x1 + m, y0, panel);  // top
        QuadGame(&vtx[4], x0 - m, y1, x1 + m, y1 + m, panel);  // bottom
        QuadGame(&vtx[8], x0 - m, y0, x0, y1, panel);          // left
        QuadGame(&vtx[12], x1, y0, x1 + m, y1, panel);         // right
        QuadGame(&vtx[16], x0 - f, y0 - f, x1 + f, y0, kFrame); // the frame, over the masks
        QuadGame(&vtx[20], x0 - f, y1, x1 + f, y1 + f, kFrame);
        QuadGame(&vtx[24], x0 - f, y0, x0, y1, kFrame);
        QuadGame(&vtx[28], x1, y0, x1 + f, y1, kFrame);
        PushFlat(dl);
        EmitQuads(dl, vtx, 8);
    }
    sWords = (int32_t)(dl.size() - wordsBefore);

    const RsMenuRect r = RsMenu_PageRect();
    if (!l.map) {
        RsMenu_DrawTextCentred("No map here", (int16_t)((r.x0 + r.x1) / 2), (int16_t)((r.y0 + r.y1) / 2 - 4), 1.0f, 255,
                               255, 255, 255);
    }
}

// The map body is the one item: the box the frame goes round.
void MapPageNodes(int32_t pageIndex, void* userData) {
    (void)pageIndex;
    (void)userData;
    const Layout l = PageLayout();
    if (!l.map) {
        return; // nothing to focus: the hands only
    }
    RsMenu_AddCursorNode("map", (int16_t)std::floor(l.gx - kFrameUnits), (int16_t)std::floor(l.gy - kFrameUnits),
                         (int16_t)std::ceil(l.w + 2.0f * kFrameUnits), (int16_t)std::ceil(l.h + 2.0f * kFrameUnits));
}

// A on the map: yes, focus it (selectFocus).
bool MapPageSelect(int32_t pageIndex, const RsMenuCursorNode* node, void* userData) {
    (void)pageIndex;
    (void)userData;
    return node != nullptr && node->id == "map";
}

// The focused view's controls, once a settled tick: the C buttons pan, D-pad up/down step the zoom, and the
// zoom step in progress eases on.
void MapPageFocusTick(int32_t pageIndex, uint16_t held, uint16_t press, void* userData) {
    (void)pageIndex;
    (void)userData;
    if (sZoomTick < kZoomTicks) {
        sZoomTick++;
    }
    if (CHECK_BTN_ALL(press, BTN_DUP)) {
        StepZoom(1);
    } else if (CHECK_BTN_ALL(press, BTN_DDOWN)) {
        StepZoom(-1);
    }
    // The held direction, -1/0/+1 an axis, sets the velocity's target; the velocity steps toward it (#313).
    float ax = 0.0f;
    float ay = 0.0f;
    if (CHECK_BTN_ALL(held, BTN_CLEFT)) {
        ax -= 1.0f;
    }
    if (CHECK_BTN_ALL(held, BTN_CRIGHT)) {
        ax += 1.0f;
    }
    if (CHECK_BTN_ALL(held, BTN_CUP)) {
        ay -= 1.0f;
    }
    if (CHECK_BTN_ALL(held, BTN_CDOWN)) {
        ay += 1.0f;
    }
    const RsMenuMapPanEasing e = RsMenuMapPage_PanEasing();
    const float top = (float)e.speed / 100.0f;
    sPanVx = StepVelocity(sPanVx, ax * top, e);
    sPanVy = StepVelocity(sPanVy, ay * top, e);
    if (sPanVx != 0.0f || sPanVy != 0.0f) {
        bool stoppedX = false;
        bool stoppedY = false;
        if (!Pan(sPanVx * kTickSeconds, sPanVy * kTickSeconds, &stoppedX, &stoppedY)) {
            sPanVx = sPanVy = 0.0f;
        }
        // Run into the image's edge: that axis stops dead rather than pushing on into the clamp.
        if (stoppedX) {
            sPanVx = 0.0f;
        }
        if (stoppedY) {
            sPanVy = 0.0f;
        }
    }
}

// B in the focused view: out at once if the view is the whole world, else ease it there first (kZoomTicks, the
// zoom step's own ease) while the scroll still stands still, so the scissor holds the edges the whole way; the menu
// asks again each tick (RsMenuPage::focusLeave), and focusTick runs the ease.
bool MapPageFocusLeave(int32_t pageIndex, void* userData) {
    (void)pageIndex;
    (void)userData;
    if (sZoomIdx == 0 && sZoomTick >= kZoomTicks) {
        return true;
    }
    const char* source = "none";
    const int16_t scene = MapScene(&source);
    RsWorldMapInfo info = {};
    if (scene < 0 || !RsWorldMap_Info(scene, &info)) {
        return true;
    }
    if (sZoomIdx != 0) {
        const float w = (float)info.widthPx;
        const float h = (float)info.heightPx;
        sFrom = CurrentView(w, h);
        sTo = { w / 2.0f, h / 2.0f, 1.0f };
        sZoomIdx = 0;
        sZoomTick = 0;
    }
    return false;
}

// #125: B closes (or, focused, comes back out); A focuses the map. Nothing else is live.
void MapPageHud(int32_t pageIndex, uint8_t status[9], void* userData) {
    (void)pageIndex;
    (void)userData;
    status[0] = BTN_ENABLED;
    for (int32_t i = 1; i < 9; i++) {
        status[i] = BTN_DISABLED;
    }
    status[4] = BTN_ENABLED;
}

} // namespace

void RsMenuMapPage_Register() {
    if (sRegistered) {
        return;
    }
    sRegistered = true;
    RsMenuPage page;
    page.id = "world_map";
    page.title = "World Map";
    page.draw = MapPageDraw;
    page.nodes = MapPageNodes;
    page.select = MapPageSelect;
    page.selectFocus = true;
    page.focusTick = MapPageFocusTick;
    page.focusLeave = MapPageFocusLeave;
    page.hud = MapPageHud;
    page.toText = "To World Map";
    sPageIndex = RsMenu_RegisterPageStruct(page);
}

RsMenuMapFog RsMenuMapPage_Fog() {
    return CVarGetInteger(CVAR_RS_MENU_MAP_FOG, RS_MENU_MAP_FOG_HIDE) == RS_MENU_MAP_FOG_DIM ? RS_MENU_MAP_FOG_DIM
                                                                                            : RS_MENU_MAP_FOG_HIDE;
}

void RsMenuMapPage_SetFog(RsMenuMapFog fog) {
    CVarSetInteger(CVAR_RS_MENU_MAP_FOG, (int32_t)fog);
    CVarSave();
}

const char* RsMenuMapPage_FogName(RsMenuMapFog fog) {
    return fog == RS_MENU_MAP_FOG_DIM ? "dim" : "hide";
}

bool RsMenuMapPage_Icons() {
    return CVarGetInteger(CVAR_RS_MENU_MAP_ICONS, 0) != 0;
}

void RsMenuMapPage_SetIcons(bool on) {
    CVarSetInteger(CVAR_RS_MENU_MAP_ICONS, on ? 1 : 0);
    CVarSave();
}

RsMenuMapArrow RsMenuMapPage_Arrow() {
    return CVarGetInteger(CVAR_RS_MENU_MAP_ARROW, RS_MENU_MAP_ARROW_VANILLA) == RS_MENU_MAP_ARROW_CUSTOM
               ? RS_MENU_MAP_ARROW_CUSTOM
               : RS_MENU_MAP_ARROW_VANILLA;
}

void RsMenuMapPage_SetArrow(RsMenuMapArrow arrow) {
    CVarSetInteger(CVAR_RS_MENU_MAP_ARROW, (int32_t)arrow);
    CVarSave();
}

const char* RsMenuMapPage_ArrowName(RsMenuMapArrow arrow) {
    return arrow == RS_MENU_MAP_ARROW_CUSTOM ? "custom" : "vanilla";
}

RsMenuMapWalls RsMenuMapPage_Walls() {
    const int32_t v = CVarGetInteger(CVAR_RS_MENU_MAP_WALLS, RS_MENU_MAP_WALLS_DEFAULT);
    return v >= RS_MENU_MAP_WALLS_IMAGE && v <= RS_MENU_MAP_WALLS_ZOOMED ? (RsMenuMapWalls)v : RS_MENU_MAP_WALLS_DEFAULT;
}

void RsMenuMapPage_SetWalls(RsMenuMapWalls walls) {
    CVarSetInteger(CVAR_RS_MENU_MAP_WALLS, (int32_t)walls);
    CVarSave();
}

const char* RsMenuMapPage_WallsName(RsMenuMapWalls walls) {
    switch (walls) {
        case RS_MENU_MAP_WALLS_IMAGE:
            return "image";
        case RS_MENU_MAP_WALLS_ZOOMED:
            return "zoomed";
        default:
            return "lines";
    }
}

float RsMenuMapPage_WallTiles() {
    const int32_t v = CVarGetInteger(CVAR_RS_MENU_MAP_WALL_TILES, RS_MENU_MAP_WALL_TILES_DEFAULT);
    return (float)std::clamp<int32_t>(v, RS_MENU_MAP_WALL_TILES_MIN, RS_MENU_MAP_WALL_TILES_MAX) / 100.0f;
}

bool RsMenuMapPage_SetWallTiles(int32_t hundredths) {
    if (hundredths < RS_MENU_MAP_WALL_TILES_MIN || hundredths > RS_MENU_MAP_WALL_TILES_MAX) {
        return false;
    }
    CVarSetInteger(CVAR_RS_MENU_MAP_WALL_TILES, hundredths);
    CVarSave();
    return true;
}

float RsMenuMapPage_WallFloor() {
    const int32_t v = CVarGetInteger(CVAR_RS_MENU_MAP_WALL_FLOOR, RS_MENU_MAP_WALL_FLOOR_DEFAULT);
    return (float)std::clamp<int32_t>(v, RS_MENU_MAP_WALL_FLOOR_MIN, RS_MENU_MAP_WALL_FLOOR_MAX) / 100.0f;
}

bool RsMenuMapPage_SetWallFloor(int32_t hundredths) {
    if (hundredths < RS_MENU_MAP_WALL_FLOOR_MIN || hundredths > RS_MENU_MAP_WALL_FLOOR_MAX) {
        return false;
    }
    CVarSetInteger(CVAR_RS_MENU_MAP_WALL_FLOOR, hundredths);
    CVarSave();
    return true;
}

bool RsMenuMapPage_WallSnap() {
    return CVarGetInteger(CVAR_RS_MENU_MAP_WALL_SNAP, 1) != 0;
}

void RsMenuMapPage_SetWallSnap(bool on) {
    CVarSetInteger(CVAR_RS_MENU_MAP_WALL_SNAP, on ? 1 : 0);
    CVarSave();
}

bool RsMenuMapPage_StepZoom(int32_t dir) {
    return RsMenu_IsFocused() && StepZoom(dir);
}

bool RsMenuMapPage_Pan(float dx, float dy) {
    return RsMenu_IsFocused() && Pan(dx, dy);
}

RsMenuMapPanEasing RsMenuMapPage_PanEasing() {
    RsMenuMapPanEasing e;
    e.speed = std::clamp<int32_t>(CVarGetInteger(CVAR_RS_MENU_MAP_PAN_SPEED, RS_MENU_MAP_PAN_SPEED_DEFAULT),
                                  RS_MENU_MAP_PAN_SPEED_MIN, RS_MENU_MAP_PAN_SPEED_MAX);
    e.rampMs = std::clamp<int32_t>(CVarGetInteger(CVAR_RS_MENU_MAP_PAN_RAMP, RS_MENU_MAP_PAN_RAMP_DEFAULT), 0,
                                   RS_MENU_MAP_PAN_EASE_MAX);
    e.coastMs = std::clamp<int32_t>(CVarGetInteger(CVAR_RS_MENU_MAP_PAN_COAST, RS_MENU_MAP_PAN_COAST_DEFAULT), 0,
                                    RS_MENU_MAP_PAN_EASE_MAX);
    return e;
}

bool RsMenuMapPage_SetPanEasing(RsMenuMapPanEasing e) {
    if (e.speed < RS_MENU_MAP_PAN_SPEED_MIN || e.speed > RS_MENU_MAP_PAN_SPEED_MAX || e.rampMs < 0 ||
        e.rampMs > RS_MENU_MAP_PAN_EASE_MAX || e.coastMs < 0 || e.coastMs > RS_MENU_MAP_PAN_EASE_MAX) {
        return false;
    }
    CVarSetInteger(CVAR_RS_MENU_MAP_PAN_SPEED, e.speed);
    CVarSetInteger(CVAR_RS_MENU_MAP_PAN_RAMP, e.rampMs);
    CVarSetInteger(CVAR_RS_MENU_MAP_PAN_COAST, e.coastMs);
    CVarSave();
    return true;
}

void RsMenuMapPage_SetTrace(bool on) {
    if (on) {
        sTraceRing.clear();
        sTraceRing.reserve(RS_MENU_MAP_TRACE_ROWS);
        sTraceRows = 0;
        sTracePrev = sTraceCur = TraceTick{};
        sTraceStart = std::chrono::steady_clock::now();
    }
    sTraceOn = on;
}

bool RsMenuMapPage_Trace() {
    return sTraceOn;
}

std::vector<RsMenuMapTraceRow> RsMenuMapPage_TraceRows() {
    if ((int32_t)sTraceRing.size() < RS_MENU_MAP_TRACE_ROWS) {
        return sTraceRing;
    }
    std::vector<RsMenuMapTraceRow> out;
    out.reserve(sTraceRing.size());
    const size_t start = (size_t)(sTraceRows % RS_MENU_MAP_TRACE_ROWS);
    out.insert(out.end(), sTraceRing.begin() + start, sTraceRing.end());
    out.insert(out.end(), sTraceRing.begin(), sTraceRing.begin() + start);
    return out;
}

void RsMenuMapPage_NoteFrame(float t) {
    // Only a frame of a tick the page drew: its display list holds the map at sTraceCur's view. The game's frame
    // counter goes up after the draw and before the frames are presented (GameState_Update), so the frames of the
    // tick the page drew see it one past the draw's; equal is accepted too, should that order ever change. Two or
    // more past means the page has stopped drawing.
    if (!sTraceOn || sTraceCur.draw == 0 || gPlayState == nullptr ||
        (gPlayState->state.frames != sTraceCur.game + 1 && gPlayState->state.frames != sTraceCur.game)) {
        return;
    }
    // The previous tick's view, if the page drew that tick too; else the frame shows this tick's view throughout.
    const bool lerp = sTracePrev.draw == sTraceCur.draw - 1 && sTracePrev.game + 1 == sTraceCur.game;
    const TraceTick& a = lerp ? sTracePrev : sTraceCur;
    const TraceTick& b = sTraceCur;
    auto mix = [t](float x, float y) { return x + (y - x) * t; };
    RsMenuMapTraceRow r;
    r.row = sTraceRows;
    r.draw = b.draw;
    r.t = t;
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sTraceStart).count();
    r.x0 = mix(a.x0, b.x0);
    r.y0 = mix(a.y0, b.y0);
    r.w = mix(a.w, b.w);
    r.h = mix(a.h, b.h);
    r.renderPerPx = mix(a.renderPerPx, b.renderPerPx);
    r.vx = b.vx;
    r.vy = b.vy;
    if ((int32_t)sTraceRing.size() < RS_MENU_MAP_TRACE_ROWS) {
        sTraceRing.push_back(r);
    } else {
        sTraceRing[(size_t)(sTraceRows % RS_MENU_MAP_TRACE_ROWS)] = r;
    }
    sTraceRows++;
}

RsMenuMapPageStatus RsMenuMapPage_Status() {
    RsMenuMapPageStatus st = {};
    st.pageIndex = sPageIndex;
    st.current = sPageIndex >= 0 && RsMenu_CurrentPage() == sPageIndex;
    const char* source = "none";
    st.scene = MapScene(&source);
    st.source = source;
    st.reason = gPlayState != nullptr ? RsWorldMap_Problem(gPlayState->sceneNum) : "no_play";
    RsWorldMapInfo info = {};
    st.map = st.scene >= 0 && RsWorldMap_Info(st.scene, &info);
    st.layer = st.map ? info.layer : -1;
    st.storeyMode = RsWorldMap_StoreyModeName(RsWorldMap_StoreyMode());
    for (const RsMapChunk& c : RsMapReveal_Chunks(st.scene)) {
        st.chunks++;
        if (RsMapReveal_IsRevealed(c.sx, c.sy)) {
            st.revealed++;
            if (!st.revealedList.empty()) {
                st.revealedList += ",";
            }
            st.revealedList += std::to_string(c.sx) + ":" + std::to_string(c.sy);
        }
    }
    st.linkSx = st.linkSy = st.linkFlag = -1;
    st.linkPx = st.linkPy = -1.0f;
    if (st.map && gPlayState != nullptr && st.scene == gPlayState->sceneNum) {
        st.link = LinkPixel(st.scene, &st.linkPx, &st.linkPy);
        int32_t sx = 0;
        int32_t sy = 0;
        if (RsMapReveal_LinkSquare(&sx, &sy)) {
            st.linkSx = sx;
            st.linkSy = sy;
            st.linkFlag = RsMapReveal_Flag(sx, sy);
            st.linkRevealed = RsMapReveal_IsRevealed(sx, sy);
        }
    }
    if (st.map) {
        const MapView v = CurrentView((float)info.widthPx, (float)info.heightPx);
        st.viewCx = v.cx;
        st.viewCy = v.cy;
        st.viewZoom = v.zoom;
    }
    st.zoomStep = sZoomIdx;
    st.zoomSteps = sZoomSteps;
    st.pans = sPans;
    st.panVx = sPanVx;
    st.panVy = sPanVy;
    st.arrow = RsMenuMapPage_ArrowName(RsMenuMapPage_Arrow());
    for (int32_t i = 0; i < 4; i++) {
        st.drawnView[i] = sDrawView[i];
    }
    st.frame = sFrame;
    st.dstX = sDst[0];
    st.dstY = sDst[1];
    st.dstW = sDst[2];
    st.dstH = sDst[3];
    st.blocks = sBlocks;
    st.fogQuads = sFogQuads;
    st.words = sWords;
    st.arrowDrawn = sArrowDrawn;
    st.linkArrow = ArrowStateName(sLinkArrow);
    st.scissored = sScissored;
    for (const ArrowInfo& a : sArrows) {
        st.arrows.push_back({ a.kind, ArrowStateName(a.state), a.px, a.py, a.controls });
    }
    st.walls = RsMenuMapPage_WallsName(RsMenuMapPage_Walls());
    st.wallsDrawn = sWallsDrawn;
    st.edgeless = sEdgeless;
    st.edges = sEdges;
    st.joins = sEdgeJoins;
    st.edgeWords = sEdgeWords;
    st.wallTiles = RsMenuMapPage_WallTiles();
    st.wallFloor = RsMenuMapPage_WallFloor();
    st.wallSnap = RsMenuMapPage_WallSnap();
    st.wallSnapped = sWallSnapped;
    st.wallNominalPx = sWallNominalPx;
    st.wallHalfPx = sWallHalfPx;
    st.wallScreenPx = sWallScreenPx;
    st.iconsShown = sIconsShown;
    st.iconUnits = sIconUnits;
    for (const IconInfo& i : sIcons) {
        st.icons.push_back({ i.name, i.token, i.state, i.px, i.py, i.alpha });
    }
    return st;
}
