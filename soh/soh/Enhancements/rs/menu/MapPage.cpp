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
 * NOT YET: map icons (#307; the toggle is here, off by default, and draws nothing until #307 gives it
 * something to draw).
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
#include <cmath>
#include <cstdio>
#include <string>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/rs/maps/WorldContext.h"
#include "soh/Enhancements/rs/maps/WorldMap.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "objects/gameplay_keep/gameplay_keep.h"
extern PlayState* gPlayState;
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
constexpr float kCompassScale = 0.4f; // vanilla's minimap arrow scale (Minimap_DrawCompassIcons), as #306 draws it
constexpr float kViewMarginPx = 2.0f; // image pixels drawn past the view on every side, under the masks

// The focused view's zoom steps: 1 is the whole world.
constexpr float kZooms[] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f };
constexpr int32_t kZoomCount = (int32_t)(sizeof(kZooms) / sizeof(kZooms[0]));
constexpr int32_t kZoomTicks = 4;
// A held C button pans this share of the visible width (or height) a tick: across a view in one second.
constexpr float kPanShare = 1.0f / 20.0f;

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

// Pans by (dx, dy) visible widths/heights.
bool Pan(float dx, float dy) {
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
    sPans++;
    return true;
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

// The custom arrow at image pixel (px, py), pointing where he faces. `len` is its length in image pixels.
void DrawCustomArrow(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, float px, float py, int16_t yaw, float len) {
    // Facing, in the image's y-DOWN pixel space: OoT +x is east (+px) and +z is south (+py).
    const float a = (float)yaw * kPi / 32768.0f;
    const float dx = std::sin(a);
    const float dy = std::cos(a);
    // Right of facing, in y-down: facing north (0, -1) has east (1, 0) on its right.
    const float rx = -dy;
    const float ry = dx;
    // Outline (a size up) then fill: tip, back-left, notch, back-right, as (across, along) in arrow units.
    const float shape[4][2] = { { 0.0f, 0.55f }, { -0.40f, -0.45f }, { 0.0f, -0.20f }, { 0.40f, -0.45f } };
    Vtx* vtx = (Vtx*)Graph_Alloc(gfxCtx, 8 * sizeof(Vtx));
    for (int32_t pass = 0; pass < 2; pass++) {
        const float k = len * (pass == 0 ? 1.35f : 1.0f);
        const uint8_t* c = pass == 0 ? kArrowEdge : kArrowFill;
        for (int32_t i = 0; i < 4; i++) {
            const float u = shape[i][0] * k;
            const float v = shape[i][1] * k;
            SetV(&vtx[pass * 4 + i], px + u * rx + v * dx, py + u * ry + v * dy, c);
        }
    }
    dl.push_back(gsSPVertex(vtx, 8, 0));
    dl.push_back(gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0));
    dl.push_back(gsSP2Triangles(4, 5, 6, 0, 4, 6, 7, 0));
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
    const float k = l.s * v.zoom;              // game units an image pixel, this view
    const float vx0 = v.cx - imgW / (2.0f * v.zoom); // the view's rect, image pixels
    const float vy0 = v.cy - imgH / (2.0f * v.zoom);
    sDst[0] = l.gx;
    sDst[1] = l.gy;
    sDst[2] = l.w;
    sDst[3] = l.h;
    sDrawView[0] = vx0;
    sDrawView[1] = vy0;
    sDrawView[2] = imgW / v.zoom;
    sDrawView[3] = imgH / v.zoom;

    // THE PLACEMENT: image pixel (px, py), drawn at (px, -py), lands on game (gx + (px - vx0) k, gy + (py - vy0) k).
    // In the menu's centred y-up modelview that is a translate and a uniform scale. Recorded every frame.
    Matrix_Push();
    Matrix_Translate(l.gx - (float)(SCREEN_WIDTH / 2) - vx0 * k, (float)(SCREEN_HEIGHT / 2) - l.gy + vy0 * k, 0.0f,
                     MTXMODE_APPLY);
    Matrix_Scale(k, k, 1.0f, MTXMODE_APPLY);
    dl.push_back(gsSPMatrix(Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));

    float lpx = 0.0f;
    float lpy = 0.0f;
    const bool link = l.map && LinkPixel(l.scene, &lpx, &lpy);
    // Link is on the visible part of the map (the arrow is drawn only there).
    const bool linkShown = link && lpx >= vx0 && lpy >= vy0 && lpx <= vx0 + imgW / v.zoom && lpy <= vy0 + imgH / v.zoom;
    const bool customArrow = RsMenuMapPage_Arrow() == RS_MENU_MAP_ARROW_CUSTOM;

    if (l.map) {
        // 1. the map, the view's rect and a margin, clipped to the image
        PushMap(dl);
        const float sx0 = std::max(0.0f, vx0 - kViewMarginPx);
        const float sy0 = std::max(0.0f, vy0 - kViewMarginPx);
        const float sx1 = std::min(imgW, vx0 + imgW / v.zoom + kViewMarginPx);
        const float sy1 = std::min(imgH, vy0 + imgH / v.zoom + kViewMarginPx);
        const size_t at = dl.size();
        dl.resize(at + (size_t)RsWorldMap_GfxWords(sx1 - sx0, sy1 - sy0) + 2);
        Gfx* p = &dl[at];
        sBlocks = RsWorldMap_EmitBlocks(gfxCtx, &p, l.scene, sx0, sy0, sx1 - sx0, sy1 - sy0);
        dl.resize((size_t)(p - dl.data()));

        // 2. the fog, 3. the custom arrow
        PushFlat(dl);
        const float clip[4] = { sx0, sy0, sx1, sy1 };
        sFogQuads = DrawFog(gfxCtx, dl, l.scene, l.info, clip);
        if (customArrow && linkShown) {
            DrawCustomArrow(gfxCtx, dl, lpx, lpy, GET_PLAYER(play)->actor.shape.rot.y, kArrowUnits / l.s);
            sArrowDrawn = 1;
        }
        // #307 draws its icons here when RsMenuMapPage_Icons() is on.
    }

    Matrix_Pop();
    dl.push_back(gsSPMatrix(Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));

    if (l.map) {
        // 4. the masks, 5. the frame: game units, under the base matrix
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

    // 6. vanilla's compass arrow, in game units under the base matrix. Its ops are recorded every frame.
    {
        const float ax = linkShown ? l.gx + (lpx - vx0) * k : -1000.0f;
        const float ay = linkShown ? l.gy + (lpy - vy0) * k : -1000.0f;
        const int16_t yaw = link ? GET_PLAYER(play)->actor.shape.rot.y : 0;
        // Vanilla's own turn for a north-up map with world x right and z down (Minimap_DrawCompassIcons).
        const float angle = (32767.0f - (float)yaw) / 10240.0f;
        Matrix_Push();
        Matrix_Translate(ax - (float)(SCREEN_WIDTH / 2), (float)(SCREEN_HEIGHT / 2) - ay, 0.0f, MTXMODE_APPLY);
        Matrix_Scale(kCompassScale, kCompassScale, kCompassScale, MTXMODE_APPLY);
        Matrix_RotateX(-1.6f, MTXMODE_APPLY);
        Matrix_RotateY(angle, MTXMODE_APPLY);
        Mtx* arrow = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
        Matrix_Pop();
        Mtx* restored = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
        if (!customArrow && linkShown) {
            Gfx setup[2];
            Gfx_SetupDL(setup, SETUPDL_42);
            dl.push_back(gsDPPipeSync());
            dl.push_back(setup[0]);
            dl.push_back(gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE));
            dl.push_back(gsSPMatrix(arrow, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
            dl.push_back(gsDPSetPrimColor(0, 0xFF, kCompassLink[0], kCompassLink[1], kCompassLink[2], 255));
            dl.push_back(gsSPDisplayListOTRFilePath(gCompassArrowDL));
            dl.push_back(gsSPMatrix(restored, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
            sArrowDrawn = 1;
        }
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
    float dx = 0.0f;
    float dy = 0.0f;
    if (CHECK_BTN_ALL(held, BTN_CLEFT)) {
        dx -= kPanShare;
    }
    if (CHECK_BTN_ALL(held, BTN_CRIGHT)) {
        dx += kPanShare;
    }
    if (CHECK_BTN_ALL(held, BTN_CUP)) {
        dy -= kPanShare;
    }
    if (CHECK_BTN_ALL(held, BTN_CDOWN)) {
        dy += kPanShare;
    }
    if (dx != 0.0f || dy != 0.0f) {
        Pan(dx, dy);
    }
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

bool RsMenuMapPage_StepZoom(int32_t dir) {
    return RsMenu_IsFocused() && StepZoom(dir);
}

bool RsMenuMapPage_Pan(float dx, float dy) {
    return RsMenu_IsFocused() && Pan(dx, dy);
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
    return st;
}
