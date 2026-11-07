/*
 * MapPage.cpp - the scroll's World Map page (sturdy-bassoon#303): the whole world, from the world map image
 * (#298), with the fog of war over it and Link's arrow on it. Planned 2026-11-06; the design comment on #303
 * is the agreed version, and this file is it.
 *
 * WHAT IS DRAWN, back to front, all under ONE placement matrix on top of the content node's base matrix:
 *   1. a frame quad a little larger than the map, so the map's edge reads against the parchment;
 *   2. the map: RsWorldMap_EmitBlocks of the whole current layer (484 blocks for F2P - the worst case #298
 *      measured), in the image's own geometry space (pixels, y negated);
 *   3. the fog: one flat quad per run of hidden chunks along a chunk row, in the same space, so its edges sit
 *      on the map's chunk edges exactly. Hidden (opaque) or dimmed (translucent), CVAR_RS_MENU_MAP_FOG;
 *   4. Link's arrow, two triangles over a darker outline, rotated in software (it needs no matrix of its own).
 *
 * WHY ONE PLACEMENT MATRIX AND EmitBlocks, NOT RsWorldMap_DrawRect. DrawRect is EmitBlocks under a
 * Push/Translate/Scale/NewMtx/Pop/NewMtx of its own. The frame, the fog and the arrow need the very same
 * transform, so the page builds it once and draws all four in it: nothing can come apart from the map by a
 * float's worth, and the page records one set of ops instead of two.
 *
 * WHY THE FOG IS AN OVERLAY, NOT A TLUT SWAP (the design comment): map blocks are 60 px and chunks 128 px, so
 * fog per block would mean splitting blocks at chunk edges, and every split is a new sub-rect load and so a
 * new texture-cache key (WORLD_MAP.md, "The TMEM split"), toward the 1,024-entry cliff. A dim palette would be
 * a second TLUT pointer, #111's caching trap. Flat quads cost no texture entries at all.
 *
 * THE OPS ARE UNCONDITIONAL (RsMenu.cpp, "EVERYTHING THAT MOVES IS Vtx"): Push, Translate, Scale, NewMtx, Pop,
 * NewMtx on every frame the page body is drawn, map or no map. Only geometry is ever skipped.
 *
 * WHICH MAP: the current scene's if it has one; else the world context's stitched scene (a basement under F2P
 * shows F2P, with no arrow); else a "No map here" line.
 *
 * NOT YET: panning, zooming and sliding (explore-only: the write-up is on #303), and map icons (#307; the
 * toggle is here, off by default, and draws nothing until #307 gives it something to draw).
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
extern PlayState* gPlayState;
}

namespace {

int32_t sPageIndex = -1;
bool sRegistered = false;

// The last drawn frame, for the dump.
int32_t sFrame = 0;
float sDst[4] = {};
int32_t sBlocks = 0;
int32_t sFogQuads = 0;
int32_t sWords = 0;
int32_t sArrowDrawn = 0;

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

constexpr float kMargin = 3.0f;      // game units between the map's frame and the page rect
constexpr float kFrameUnits = 1.0f;  // the frame's width, game units
constexpr float kArrowUnits = 7.0f;  // the arrow's length, game units

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

// The fog: per chunk row, runs of hidden squares merged into one quad each.
int32_t DrawFog(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, int16_t scene, const RsWorldMapInfo& info) {
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

// Link's arrow at image pixel (px, py), pointing where he faces. `len` is its length in image pixels.
void DrawArrow(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, float px, float py, int16_t yaw, float len) {
    // Facing, in the image's y-DOWN pixel space: OoT +x is east (+px) and +z is south (+py).
    const float a = (float)yaw * 3.14159265f / 32768.0f;
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

    const char* source = "none";
    const int16_t scene = MapScene(&source);
    RsWorldMapInfo info = {};
    const bool haveMap = scene >= 0 && RsWorldMap_Info(scene, &info);

    // Fit the image into the page rect, keeping its aspect. Game space, y down.
    const RsMenuRect r = RsMenu_PageRect();
    const float availW = (float)(r.x1 - r.x0) - 2.0f * kMargin;
    const float availH = (float)(r.y1 - r.y0) - 2.0f * kMargin;
    const float imgW = haveMap ? (float)info.widthPx : 1.0f;
    const float imgH = haveMap ? (float)info.heightPx : 1.0f;
    const float s = std::min(availW / imgW, availH / imgH); // game units an image pixel
    const float dstW = imgW * s;
    const float dstH = imgH * s;
    const float gx = (float)(r.x0 + r.x1) / 2.0f - dstW / 2.0f;
    const float gy = (float)(r.y0 + r.y1) / 2.0f - dstH / 2.0f;
    sDst[0] = gx;
    sDst[1] = gy;
    sDst[2] = dstW;
    sDst[3] = dstH;

    // THE PLACEMENT: image pixel (px, py), drawn at (px, -py), lands on game (gx + px s, gy + py s). In the
    // menu's centred y-up modelview that is a translate to (gx - 160, 120 - gy) and a uniform scale. Recorded on
    // every frame, map or no map.
    Matrix_Push();
    Matrix_Translate(gx - (float)(SCREEN_WIDTH / 2), (float)(SCREEN_HEIGHT / 2) - gy, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(s, s, 1.0f, MTXMODE_APPLY);
    dl.push_back(gsSPMatrix(Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));

    if (haveMap) {
        // 1. the frame
        const float pad = kFrameUnits / s;
        PushFlat(dl);
        Vtx* frame = (Vtx*)Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
        QuadV(frame, -pad, -pad, imgW + pad, imgH + pad, kFrame);
        EmitQuads(dl, frame, 1);

        // 2. the map
        PushMap(dl);
        const size_t at = dl.size();
        dl.resize(at + (size_t)RsWorldMap_GfxWords(imgW, imgH) + 2);
        Gfx* p = &dl[at];
        sBlocks = RsWorldMap_EmitBlocks(gfxCtx, &p, scene, 0.0f, 0.0f, imgW, imgH);
        dl.resize((size_t)(p - dl.data()));

        // 3. the fog, 4. Link
        PushFlat(dl);
        sFogQuads = DrawFog(gfxCtx, dl, scene, info);
        if (play != nullptr && scene == play->sceneNum) {
            Player* player = GET_PLAYER(play);
            float px = 0.0f;
            float py = 0.0f;
            if (player != nullptr &&
                RsWorldMap_WorldToPixel(scene, player->actor.world.pos.x, player->actor.world.pos.z, &px, &py)) {
                DrawArrow(gfxCtx, dl, px, py, player->actor.shape.rot.y, kArrowUnits / s);
                sArrowDrawn = 1;
            }
        }
        // #307 draws its icons here when RsMenuMapPage_Icons() is on.
    }

    Matrix_Pop();
    dl.push_back(gsSPMatrix(Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
    sWords = (int32_t)(dl.size() - wordsBefore);

    if (!haveMap) {
        RsMenu_DrawTextCentred("No map here", (int16_t)((r.x0 + r.x1) / 2), (int16_t)((r.y0 + r.y1) / 2 - 4), 1.0f, 255,
                               255, 255, 255);
    }
}

// #125: B closes; nothing else is live on the map page yet.
void MapPageHud(int32_t pageIndex, uint8_t status[9], void* userData) {
    (void)pageIndex;
    (void)userData;
    status[0] = BTN_ENABLED;
    for (int32_t i = 1; i < 9; i++) {
        status[i] = BTN_DISABLED;
    }
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
    page.nodes = nullptr; // the two hands only, until a pan/zoom scheme is picked
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
        Player* player = GET_PLAYER(gPlayState);
        if (player != nullptr) {
            st.link = RsWorldMap_WorldToPixel(st.scene, player->actor.world.pos.x, player->actor.world.pos.z,
                                              &st.linkPx, &st.linkPy);
        }
        int32_t sx = 0;
        int32_t sy = 0;
        if (RsMapReveal_LinkSquare(&sx, &sy)) {
            st.linkSx = sx;
            st.linkSy = sy;
            st.linkFlag = RsMapReveal_Flag(sx, sy);
            st.linkRevealed = RsMapReveal_IsRevealed(sx, sy);
        }
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
