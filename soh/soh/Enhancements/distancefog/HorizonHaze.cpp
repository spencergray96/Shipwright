#include "HorizonHaze.h"

#include <cmath>

#include "soh/frame_interpolation.h"
#include "WorldFog.h"

extern "C" {
#include <z64.h>
#include "macros.h"
#include "functions.h"
}

namespace {

// The ring: 64 segments around, fixed in the world's directions (column k at azimuth k * 360/64 from +X), and
// four rows - the nadir, the horizon, the height, and the top of the fade. Three strips between them: the nadir
// fan and horizon-to-height are opaque, height-to-top fades out. Vtx positions are s16, so the mesh is built at
// radius 4096 (rounding under 0.007 degrees) and the matrix scales it to 48 world units: inside the normal
// skybox cube, whose faces stand at +/-64 (z_vr_box.c), and well outside zNear 10.
constexpr int kSegments = 64;
constexpr int kColumns = kSegments + 1; // the last column repeats the first, so every strip closes on itself
constexpr int kRows = 4;
constexpr float kMeshRadius = 4096.0f;
constexpr float kWorldRadius = 48.0f;
// Segments per vertex load: 17 columns from each of a strip's two rows, 34 vertices; the interpreter holds 64.
constexpr int kChunk = 16;
constexpr float kPi = 3.14159265358979f;

bool sEnabled = true;
WorldFogHaze sHaze = { WORLD_FOG_HAZE_DEFAULT_HEIGHT, WORLD_FOG_HAZE_DEFAULT_FADE };

HorizonHazeFrame sFrame = { 0, 0, { 0, 0, 0 } };

// The frame-interpolation child's key: the band's own, so a tick without a skybox never shifts its index.
const char sInterpolationKey = 0;

// One row of the ring at elevation `degrees`, with vertex alpha `alpha`; the colour comes from prim.
void BuildRow(Vtx* row, float degrees, u8 alpha) {
    const float e = degrees * kPi / 180.0f;
    const float y = kMeshRadius * std::sin(e);
    const float r = kMeshRadius * std::cos(e);
    for (int k = 0; k < kColumns; k++) {
        const float a = static_cast<float>(k % kSegments) * 2.0f * kPi / kSegments;
        Vtx_t& v = row[k].v;
        v.ob[0] = static_cast<s16>(std::lround(r * std::cos(a)));
        v.ob[1] = static_cast<s16>(std::lround(y));
        v.ob[2] = static_cast<s16>(std::lround(r * std::sin(a)));
        v.flag = 0;
        v.tc[0] = 0;
        v.tc[1] = 0;
        v.cn[0] = 255;
        v.cn[1] = 255;
        v.cn[2] = 255;
        v.cn[3] = alpha;
    }
}

// The quads between two rows, a chunk of segments per load. Culling is off, so winding does not matter, and the
// nadir row's zero-area halves draw nothing.
Gfx* DrawStrip(Gfx* gfx, Vtx* lower, Vtx* upper) {
    for (int first = 0; first < kSegments; first += kChunk) {
        gSPVertex(gfx++, reinterpret_cast<uintptr_t>(&lower[first]), kChunk + 1, 0);
        gSPVertex(gfx++, reinterpret_cast<uintptr_t>(&upper[first]), kChunk + 1, kChunk + 1);
        for (int i = 0; i < kChunk; i++) {
            const int up = kChunk + 1 + i;
            gSP2Triangles(gfx++, i, i + 1, up + 1, 0, i, up + 1, up, 0);
        }
    }
    return gfx;
}

} // namespace

extern "C" {

void HorizonHaze_Use(uint8_t enabled, WorldFogHaze haze) {
    sEnabled = enabled != 0;
    sHaze = haze;
    sFrame.drawn = 0;
}

const HorizonHazeFrame* HorizonHaze_LastFrame(void) {
    return &sFrame;
}

void HorizonHaze_Draw(PlayState* play) {
    s16 start;
    s16 end;
    if (!sEnabled || !WorldFog_Live(&start, &end)) {
        return;
    }
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    const u8* fog = play->lightCtx.fogColor;
    sFrame.drawn = 1;
    sFrame.draws++;
    sFrame.color[0] = fog[0];
    sFrame.color[1] = fog[1];
    sFrame.color[2] = fog[2];

    // Rebuilt every tick, so a new height or fade shows at once and nothing is kept between ticks.
    Vtx* vtx = static_cast<Vtx*>(Graph_Alloc(gfxCtx, sizeof(Vtx) * kColumns * kRows));
    Vtx* rows[kRows] = { vtx, vtx + kColumns, vtx + 2 * kColumns, vtx + 3 * kColumns };
    BuildRow(rows[0], -90.0f, 255);
    BuildRow(rows[1], 0.0f, 255);
    BuildRow(rows[2], sHaze.height, 255);
    BuildRow(rows[3], sHaze.height + sHaze.fade, 0);

    OPEN_DISPS(gfxCtx);
    // An interpolation child like the skybox's (SkyboxDraw_Draw), under the band's own key: matrices recorded inside
    // it interpolate with the camera, and a camera cut (a new epoch) snaps instead.
    FrameInterpolation_RecordOpenChild(&sInterpolationKey, FrameInterpolation_GetCameraEpoch());

    // Camera-centred like the skybox, but with no rotation: the ring's directions are the world's.
    Mtx* mtx = static_cast<Mtx*>(Graph_Alloc(gfxCtx, sizeof(Mtx)));
    Matrix_Translate(play->view.eye.x, play->view.eye.y, play->view.eye.z, MTXMODE_NEW);
    Matrix_Scale(kWorldRadius / kMeshRadius, kWorldRadius / kMeshRadius, kWorldRadius / kMeshRadius, MTXMODE_APPLY);
    Matrix_ToMtx(mtx, const_cast<char*>(__FILE__), __LINE__);

    // Colour from prim (the live fog colour), alpha from the vertices; blended over the sky with no Z compare and
    // no Z update, and no fog, lighting or culling.
    gDPPipeSync(POLY_OPA_DISP++);
    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, SHADE, 0, 0, 0, PRIMITIVE, 0, 0, 0, SHADE);
    gDPSetOtherMode(POLY_OPA_DISP++,
                    G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gSPLoadGeometryMode(POLY_OPA_DISP++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, fog[0], fog[1], fog[2], 255);
    gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    POLY_OPA_DISP = DrawStrip(POLY_OPA_DISP, rows[0], rows[1]);
    if (sHaze.height > 0.0f) {
        POLY_OPA_DISP = DrawStrip(POLY_OPA_DISP, rows[1], rows[2]);
    }
    if (sHaze.fade > 0.0f) {
        POLY_OPA_DISP = DrawStrip(POLY_OPA_DISP, rows[2], rows[3]);
    }
    gDPPipeSync(POLY_OPA_DISP++);

    FrameInterpolation_RecordCloseChild();
    CLOSE_DISPS(gfxCtx);
}

} // extern "C"
