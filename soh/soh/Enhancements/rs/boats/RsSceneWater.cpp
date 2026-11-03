// A scene's own water, from code (sturdy-bassoon#282, RsSceneWater.h).

#include "soh/frame_interpolation.h" // before OPEN_DISPS: the Tempor Tantrum note's link rule for draws in C++

#include "RsSceneWater.h"

#include <cstdint>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "macros.h"
#include "variables.h"

extern PlayState* gPlayState;
}

namespace {

// One rectangle of water: its x/z corners, in world units (the surface is its scene row's).
struct WaterRect {
    int16_t x0;
    int16_t z0;
    int16_t x1;
    int16_t z1;
};

struct SceneWater {
    int16_t scene;
    int16_t y; // every rectangle's surface
    const WaterRect* rects;
    uint32_t count;
};

// Boat Docks 282 (the grid tool's projects/agent "Boat Docks 282"): the sea around its islands, which are walled and
// floored at 80. 72 deep over the ground at 0: the hull draws 10 and adult Link swims from 56; the deck (4 over the
// water) is a step below the islands' floor, and the walls' tops (80) stand clear of the surface, so a wall still stops
// a boat. Rectangles cover every tile but the islands'.
#include "RsSceneWaterBoatDocks282.inc"

const SceneWater kSceneWater[] = {
    { SCENE_BOAT_DOCKS_282, 72, kBoatDocks282Water, (uint32_t)(sizeof(kBoatDocks282Water) / sizeof(kBoatDocks282Water[0])) },
};

// The waterbox properties every rectangle carries: in every room (bits 13-18, WaterBox_GetSurfaceImpl), camera index
// 0 (no setting of its own: the swimming camera), as the boat console's test water.
constexpr uint32_t kWaterBoxAllRooms = 0x3Fu << 13;
constexpr uint32_t kMaxWaterBoxes = 64;

WaterBox sBoxes[kMaxWaterBoxes];
CollisionHeader sHeader;
const SceneWater* sLive = nullptr; // the row flooding the loaded scene, or nullptr

void OnSceneInitWater(int16_t sceneNum) {
    // The new scene's collision context is its own; the copy is no longer pointed at.
    sLive = nullptr;
}

void RegisterRsSceneWater();

} // namespace

// The sheets, at file scope: OPEN_DISPS in an anonymous namespace does not link (the Tempor Tantrum note).
static void RsSceneWater_Draw() {
    if (sLive == nullptr || gPlayState == nullptr || gPlayState->sceneNum != sLive->scene) {
        return;
    }
    GraphicsContext* gfxCtx = gPlayState->state.gfxCtx;
    // Two units under the surface: level with it, the water rings lose the depth test (#289).
    const int16_t y = (int16_t)(sLive->y - 2);
    Vtx* vtx = (Vtx*)Graph_Alloc(gfxCtx, sizeof(Vtx) * 4 * sLive->count);

    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL_25Xlu(gfxCtx);
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_CULL_BACK | G_LIGHTING);
    gDPSetCombineMode(POLY_XLU_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 40, 96, 150, 150); // the boat console's test water's colour
    for (uint32_t i = 0; i < sLive->count; i++) {
        const WaterRect& r = sLive->rects[i];
        const int16_t xs[4] = { r.x0, r.x1, r.x1, r.x0 };
        const int16_t zs[4] = { r.z0, r.z0, r.z1, r.z1 };
        Vtx* v = &vtx[i * 4];
        for (int k = 0; k < 4; k++) {
            v[k] = {};
            v[k].v.ob[0] = xs[k];
            v[k].v.ob[1] = y;
            v[k].v.ob[2] = zs[k];
            v[k].v.cn[0] = 0;
            v[k].v.cn[1] = 127;
            v[k].v.cn[2] = 0;
            v[k].v.cn[3] = 255;
        }
        gSPVertex(POLY_XLU_DISP++, (uintptr_t)v, 4, 0);
        gSP2Triangles(POLY_XLU_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);
    }
    gSPSetGeometryMode(POLY_XLU_DISP++, G_CULL_BACK | G_LIGHTING);
    CLOSE_DISPS(gfxCtx);
}

namespace {

void RegisterRsSceneWater() {
    static bool hooked = false;
    if (hooked) {
        return;
    }
    hooked = true;
    COND_HOOK(OnSceneInit, true, OnSceneInitWater);
    COND_HOOK(OnPlayDrawEnd, true, RsSceneWater_Draw);
}

RegisterShipInitFunc rsSceneWaterInitFunc(RegisterRsSceneWater);

} // namespace

extern "C" void RsSceneWater_Apply(PlayState* play) {
    if (play == nullptr || sLive != nullptr) {
        return;
    }
    const SceneWater* row = nullptr;
    for (const SceneWater& w : kSceneWater) {
        if (w.scene == play->sceneNum) {
            row = &w;
        }
    }
    if (row == nullptr) {
        return;
    }
    CollisionHeader* scene = play->colCtx.colHeader;
    // A header with no waterboxes may point its list at segment 0 rather than null (WaterBox_GetSurfaceImpl's test),
    // so the count alone says how many there are.
    const uint32_t have = scene->numWaterBoxes;
    if (have + row->count > kMaxWaterBoxes) {
        SPDLOG_ERROR("RsSceneWater: scene 0x{:X} has {} waterboxes and asks for {} more; the copy holds {}",
                     (int)play->sceneNum, have, row->count, kMaxWaterBoxes);
        return;
    }
    for (uint32_t i = 0; i < have; i++) {
        sBoxes[i] = scene->waterBoxes[i];
    }
    for (uint32_t i = 0; i < row->count; i++) {
        const WaterRect& r = row->rects[i];
        WaterBox* box = &sBoxes[have + i];
        box->xMin = MIN(r.x0, r.x1);
        box->zMin = MIN(r.z0, r.z1);
        box->xLength = (s16)ABS(r.x1 - r.x0);
        box->zLength = (s16)ABS(r.z1 - r.z0);
        box->ySurface = row->y;
        box->properties = kWaterBoxAllRooms;
    }
    sHeader = *scene;
    sHeader.waterBoxes = sBoxes;
    sHeader.numWaterBoxes = have + row->count;
    play->colCtx.colHeader = &sHeader;
    sLive = row;
}
