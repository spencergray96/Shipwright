// The HUD minimap in RS scenes (sturdy-bassoon#306): see RsMinimap.h.
#include "RsMinimap.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>

#include "WorldMap.h"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/cvar_prefixes.h"
#include "soh/frame_interpolation.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "macros.h"
#include "objects/gameplay_keep/gameplay_keep.h"
extern PlayState* gPlayState;
// z_map_exp.c (sturdy-bassoon#306): where and which way Link came into the scene, as the red arrow uses it.
void Minimap_GetPlayerInitial(s16* x, s16* z, s16* direction);
// OTRGlobals.h declares these for C only.
uint32_t OTRGetGameRenderHeight();
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);
}

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int32_t kSides = 64;  // the circle's polygon
constexpr float kFix = 4.0f;    // vertex units an image pixel: sub-pixel circle edges, the whole image in s16
// Where the circle's bounding box sits by default, in N64 top-left pixels: vanilla's overworld map corner
// (R_OW_MINIMAP_X/Y's 238/164 start, nudged so a 64 px circle sits inside 248..312 x 164..228).
constexpr float kBoxLeft = 248.0f;
constexpr float kBoxTop = 164.0f;
// The map's background colour (TLUT entry 0, #101114), for the disc under it.
constexpr uint8_t kBg[3] = { 0x10, 0x11, 0x14 };

// This frame's mapping, set by RsMinimap_Draw and read by the arrow redirect and WorldToScreen.
struct Frame {
    bool drawing = false; // inside RsMinimap_Draw's arrow section
    bool visible = false;
    int16_t scene = -1;
    float lpx = 0, lpy = 0;    // Link's pixel
    float scale = 1;           // screen px an image px
    float rot = 0;             // radians, applied to geometry space (y up)
    bool mirror = false;
    float cx = 0, cy = 0;      // the centre, centre-origin y-up N64 px
    float rimPx = 0;           // RS_MINIMAP_SCREEN_RADIUS
    std::vector<Gfx>* dl = nullptr;
    GraphicsContext* gfxCtx = nullptr;
    Mtx* mapMtx = nullptr;     // the map's modelview this frame: the map-space arrows draw under it
};
Frame sFrame;
RsMinimapStats sStats = {};
float sRotUnwrapped = 0.0f;     // the map's turn, kept continuous so interpolation never spins it the long way
float sLinkArrowUnwrapped = 0.0f;
bool sBilerp = true;            // `minimap filter`: bilinear (default) or point sampling, for the #306 flicker A/B
bool sSnap = true;              // `minimap snap`: north-up scroll on whole screen pixels (default), for the A/B
float sSnapGrid = 0.0f;         // last frame's snap step in HUD ortho units (0: none), for the status line
bool sLastArrowOut = false;     // the last RsMinimap_DrawArrow left nothing inside the circle
// The arrow's silhouette, measured off vanilla's gCompassArrowDL at its 0.4 scale: a triangle 7.6 N64 px from
// tip to base and 3.8 across the base, its origin in the middle of that length.
constexpr float kArrowHalfLen = RS_MINIMAP_ARROW_HALF_LEN;
constexpr float kArrowHalfWidth = RS_MINIMAP_ARROW_HALF_WIDTH;

std::vector<Gfx>& Dl() {
    static std::vector<Gfx> dl; // the interpreter walks it after the frame is built; cleared at the next draw
    return dl;
}

float WrapPi(float a) {
    while (a > kPi) {
        a -= 2 * kPi;
    }
    while (a < -kPi) {
        a += 2 * kPi;
    }
    return a;
}

// The nearest value to `prev` that is `target` modulo 2 pi.
float Unwrap(float prev, float target) {
    return prev + WrapPi(target - prev);
}

// Image pixel delta -> screen delta (centre-origin, y up): y flipped, scaled, rotated, mirrored.
void PixelDeltaToScreen(float dx, float dy, float* sx, float* sy) {
    const float gx = dx * sFrame.scale;
    const float gy = -dy * sFrame.scale;
    const float c = std::cos(sFrame.rot), s = std::sin(sFrame.rot);
    float rx = gx * c - gy * s;
    const float ry = gx * s + gy * c;
    if (sFrame.mirror) {
        rx = -rx;
    }
    *sx = rx;
    *sy = ry;
}

// World position -> screen, where it really is (no pin: the owner, 2026-11-07, "arrows belong to the map").
// *inside: the point is within the circle. False with no map.
bool ToScreen(float x, float z, float* sx, float* sy, bool* inside) {
    float px = 0, py = 0;
    if (!RsWorldMap_WorldToPixel(sFrame.scene, x, z, &px, &py)) {
        return false;
    }
    float dx = 0, dy = 0;
    PixelDeltaToScreen(px - sFrame.lpx, py - sFrame.lpy, &dx, &dy);
    *sx = sFrame.cx + dx;
    *sy = sFrame.cy + dy;
    if (inside != nullptr) {
        *inside = std::sqrt(dx * dx + dy * dy) <= sFrame.rimPx;
    }
    return true;
}

// A world heading (yaw, the way Link's own faces: sin/cos on x/z) -> its direction on this minimap's screen,
// as the angle vanilla's arrow rotation takes. Vanilla maps world x to screen right and z to screen down, and turns
// gCompassArrowDL by (0x7FFF - yaw) / 0x400 / 10 radians; this finds the yaw that would point the same way on
// screen and uses the same formula (unquantized).
float ArrowAngle(float x, float z, s16 yaw) {
    const float yr = yaw * (kPi / 32768.0f);
    float p0x = 0, p0y = 0, p1x = 0, p1y = 0;
    RsWorldMap_WorldToPixel(sFrame.scene, x, z, &p0x, &p0y);
    RsWorldMap_WorldToPixel(sFrame.scene, x + std::sin(yr) * 100.0f, z + std::cos(yr) * 100.0f, &p1x, &p1y);
    float ux = 0, uy = 0;
    PixelDeltaToScreen(p1x - p0x, p1y - p0y, &ux, &uy);
    const float screenYaw = std::atan2(ux, -uy); // vanilla's: (sin, -cos) is (right, up)
    return (32767.0f - screenYaw * (32768.0f / kPi)) / 10240.0f;
}

void PushArrow(float sx, float sy, float angle, Color_RGB8 color, bool emit) {
    std::vector<Gfx>& dl = *sFrame.dl;
    // (sx, sy) is already in the HUD ortho's own (widened, centre-origin) space: Centre() applied the anchor.
    Matrix_Translate(sx, sy, 0.0f, MTXMODE_NEW);
    Matrix_Scale(0.4f, 0.4f, 0.4f, MTXMODE_APPLY);
    Matrix_RotateX(-1.6f, MTXMODE_APPLY);
    Matrix_RotateY(angle, MTXMODE_APPLY);
    Mtx* m = Matrix_NewMtx(sFrame.gfxCtx, (char*)__FILE__, __LINE__);
    if (!emit) {
        return;
    }
    dl.push_back(gsSPMatrix(m, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
    dl.push_back(gsDPSetPrimColor(0, 0xFF, color.r, color.g, color.b, 255));
    dl.push_back(gsSPDisplayListOTRFilePath(gCompassArrowDL)); // an OTR path: the gs form does not resolve it
    sStats.arrows++;
}

// The circle's polygon in image pixels, clipped to a rect (Sutherland-Hodgman). Convex in, convex out.
struct P {
    float x, y;
};
void ClipEdge(std::vector<P>& in, std::vector<P>& out, int axis, float bound, bool keepGreater) {
    out.clear();
    const size_t n = in.size();
    for (size_t i = 0; i < n; i++) {
        const P& a = in[i];
        const P& b = in[(i + 1) % n];
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
    in.swap(out);
}

std::vector<P> sCircle;
std::vector<P> sPoly, sScratch;

void SetVtx(Vtx* v, float px, float py, float loadX, float loadY) {
    v->v.ob[0] = (int16_t)std::lround(px * kFix);
    v->v.ob[1] = (int16_t)std::lround(-py * kFix);
    v->v.ob[2] = 0;
    v->v.flag = 0;
    v->v.tc[0] = (int16_t)std::lround((px - loadX) * 32.0f);
    v->v.tc[1] = (int16_t)std::lround((py - loadY) * 32.0f);
    v->v.cn[0] = v->v.cn[1] = v->v.cn[2] = v->v.cn[3] = 255;
}

// A convex polygon as a fan, in gSPVertex loads of at most 32: each load repeats the fan's first vertex.
void PushFan(std::vector<Gfx>& dl, const std::vector<P>& poly, float loadX, float loadY) {
    const int32_t n = (int32_t)poly.size();
    if (n < 3) {
        return;
    }
    for (int32_t start = 1; start < n - 1; start += 30) {
        const int32_t count = std::min(31, n - start); // vertices start..start+count-1, plus the first
        Vtx* v = (Vtx*)Graph_Alloc(sFrame.gfxCtx, (size_t)(count + 1) * sizeof(Vtx));
        SetVtx(&v[0], poly[0].x, poly[0].y, loadX, loadY);
        for (int32_t i = 0; i < count; i++) {
            SetVtx(&v[i + 1], poly[start + i].x, poly[start + i].y, loadX, loadY);
        }
        dl.push_back(gsSPVertex(v, count + 1, 0));
        for (int32_t i = 1; i < count; i++) {
            dl.push_back(gsSP1Triangle(0, i, i + 1, 0));
        }
    }
}

// Keeps the part of convex `in` on the inside of every edge of the convex polygon `clip` (the side its centre
// (cx, cy) is on). Sutherland-Hodgman again, against arbitrary edges.
void ClipConvex(std::vector<P>& in, std::vector<P>& out, const std::vector<P>& clip, float cx, float cy) {
    const size_t m = clip.size();
    for (size_t e = 0; e < m && in.size() >= 3; e++) {
        const P& a = clip[e];
        const P& b = clip[(e + 1) % m];
        const float ex = b.x - a.x, ey = b.y - a.y;
        const float sideC = ex * (cy - a.y) - ey * (cx - a.x);
        auto side = [&](const P& q) { return (ex * (q.y - a.y) - ey * (q.x - a.x)) * sideC; };
        out.clear();
        const size_t n = in.size();
        for (size_t i = 0; i < n; i++) {
            const P& p = in[i];
            const P& q = in[(i + 1) % n];
            const float sp = side(p), sq = side(q);
            if (sp >= 0) {
                out.push_back(p);
            }
            if ((sp >= 0) != (sq >= 0)) {
                const float t = sp / (sp - sq);
                out.push_back({ p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t });
            }
        }
        in.swap(out);
    }
}

// An arrow printed on the map (the owner, 2026-11-07): a flat triangle in image pixels at (px, py), pointing along
// the image-space unit vector (ux, uy), drawn under the map's own matrix so it scrolls and turns with the map, and
// clipped to the circle like the map under it. Each call sets its own state, so it can come from any hook.
// Answers whether any of it is inside the circle.
bool DrawMapArrow(float px, float py, float ux, float uy, Color_RGB8 color) {
    const float len = kArrowHalfLen / sFrame.scale;
    const float wid = kArrowHalfWidth / sFrame.scale;
    const float nx = -uy, ny = ux;
    const float bx = px - ux * len, by = py - uy * len;
    sPoly.assign({ { px + ux * len, py + uy * len }, { bx + nx * wid, by + ny * wid }, { bx - nx * wid, by - ny * wid } });
    ClipConvex(sPoly, sScratch, sCircle, sFrame.lpx, sFrame.lpy);
    if (sPoly.size() < 3) {
        return false;
    }
    std::vector<Gfx>& dl = *sFrame.dl;
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPMatrix(sFrame.mapMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
    dl.push_back(gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF));
    dl.push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
    dl.push_back(gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE |
                                      G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));
    dl.push_back(gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE));
    dl.push_back(gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    PushFan(dl, sPoly, 0.0f, 0.0f);
    sStats.arrows++;
    return true;
}

// A world position and yaw -> its image pixel and the image-space unit vector it points along.
void MapPose(float x, float z, s16 yaw, float* px, float* py, float* ux, float* uy) {
    const float yr = yaw * (kPi / 32768.0f);
    float qx = 0, qy = 0;
    RsWorldMap_WorldToPixel(sFrame.scene, x, z, px, py);
    RsWorldMap_WorldToPixel(sFrame.scene, x + std::sin(yr) * 100.0f, z + std::cos(yr) * 100.0f, &qx, &qy);
    const float dx = qx - *px, dy = qy - *py;
    const float d = std::sqrt(dx * dx + dy * dy);
    *ux = d > 0 ? dx / d : 0.0f;
    *uy = d > 0 ? dy / d : -1.0f;
}

void OnBlock(Gfx** gfx, int16_t bx, int16_t by, int16_t bw, int16_t bh, int16_t loadX, int16_t loadY, void* user) {
    std::vector<Gfx>& dl = *sFrame.dl;
    // ForEachBlock wrote this block's load at *gfx, inside dl's spare room; take it into dl's length.
    dl.resize((size_t)(*gfx - dl.data()));
    sPoly = sCircle;
    ClipEdge(sPoly, sScratch, 0, (float)bx, true);
    ClipEdge(sPoly, sScratch, 0, (float)(bx + bw), false);
    ClipEdge(sPoly, sScratch, 1, (float)by, true);
    ClipEdge(sPoly, sScratch, 1, (float)(by + bh), false);
    PushFan(dl, sPoly, loadX, loadY);
    // Room for the next block's load, which ForEachBlock writes through *gfx.
    const size_t at = dl.size();
    dl.resize(at + 32);
    *gfx = &dl[at];
    (*(int32_t*)user)++;
}

// The circle's centre on screen (centre-origin, y up), following vanilla's minimap cosmetics: the anchor, PosX/Y
// and the HUD margins, the way Minimap_Draw places its overworld map.
void Centre(float* cx, float* cy) {
    const int32_t posType = CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosType"), 0);
    const bool margins = CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.UseMargins"), 0) != 0;
    const float posX = (float)CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosX"), 0);
    const float posY = (float)CVarGetInteger(CVAR_COSMETIC("HUD.Minimap.PosY"), 0);
    const float mL = margins ? (float)CVarGetInteger(CVAR_COSMETIC("HUD.Margin.L"), 0) : 0.0f;
    const float mR = margins ? (float)CVarGetInteger(CVAR_COSMETIC("HUD.Margin.R"), 0) : 0.0f;
    const float mB = margins ? (float)CVarGetInteger(CVAR_COSMETIC("HUD.Margin.B"), 0) : 0.0f;
    const float r = (float)RS_MINIMAP_SCREEN_RADIUS;
    float left = 0.0f; // the box's left edge, in the HUD's widened top-left pixels
    float top = kBoxTop + mB;
    if (posType == ANCHOR_LEFT) {
        left = OTRGetDimensionFromLeftEdge(kBoxLeft + posX + mL);
        top += posY;
    } else if (posType == ANCHOR_RIGHT) {
        left = OTRGetDimensionFromRightEdge(kBoxLeft + posX + mR);
        top += posY;
    } else if (posType == ANCHOR_NONE) {
        left = posX;
        top += posY;
    } else {
        left = OTRGetDimensionFromRightEdge(kBoxLeft + mR);
    }
    *cx = left + r - SCREEN_WIDTH / 2;
    *cy = SCREEN_HEIGHT / 2 - (top + r);
}

} // namespace

extern "C" Color_RGB8 RsMinimap_LinkColor(void) {
    Color_RGB8 color = { 200, 255, 0 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.MinimapPosition.Changed"), 0)) {
        color = CVarGetColor24(CVAR_COSMETIC("HUD.MinimapPosition.Value"), color);
    }
    return color;
}

extern "C" Color_RGB8 RsMinimap_EntranceColor(void) {
    Color_RGB8 color = { 200, 0, 0 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.MinimapEntrance.Changed"), 0)) {
        color = CVarGetColor24(CVAR_COSMETIC("HUD.MinimapEntrance.Value"), color);
    }
    return color;
}

// Vanilla's sPlayerInitial*, its direction kept in vanilla's 1/64-turn steps, as a world yaw.
extern "C" void RsMinimap_EntrancePose(f32* x, f32* z, s16* yaw) {
    s16 ex = 0, ez = 0, edir = 0;
    Minimap_GetPlayerInitial(&ex, &ez, &edir);
    *x = ex;
    *z = ez;
    *yaw = (s16)(0x7FFF - edir * 0x400);
}

extern "C" bool RsMinimap_Handles(PlayState* play) {
    return play != nullptr && RsWorldMap_Info(play->sceneNum, nullptr);
}

extern "C" bool RsMinimap_Rotate(void) {
    return CVarGetInteger(CVAR_RS_MINIMAP_ROTATE, 0) != 0;
}

extern "C" void RsMinimap_SetRotate(bool rotate) {
    CVarSetInteger(CVAR_RS_MINIMAP_ROTATE, rotate ? 1 : 0);
    CVarSave();
}

extern "C" int32_t RsMinimap_RadiusTiles(void) {
    return std::clamp(CVarGetInteger(CVAR_RS_MINIMAP_RADIUS, RS_MINIMAP_RADIUS_DEFAULT), RS_MINIMAP_RADIUS_MIN,
                      RS_MINIMAP_RADIUS_MAX);
}

extern "C" void RsMinimap_SetRadiusTiles(int32_t tiles) {
    CVarSetInteger(CVAR_RS_MINIMAP_RADIUS, std::clamp(tiles, RS_MINIMAP_RADIUS_MIN, RS_MINIMAP_RADIUS_MAX));
    CVarSave();
}

extern "C" void RsMinimap_GetStats(RsMinimapStats* out) {
    if (out == nullptr) {
        return;
    }
    *out = sStats;
    // Stale when the minimap has not run for a frame (a vanilla scene, the pause).
    if (gPlayState == nullptr || gPlayState->state.frames - sStats.frame > 1) {
        out->handled = RsMinimap_Handles(gPlayState);
        out->drawn = false;
        out->blocks = out->arrows = out->words = 0;
    }
}

extern "C" bool RsMinimap_WorldToScreen(f32 x, f32 z, f32* sx, f32* sy, bool* inside) {
    if (!sFrame.drawing) {
        return false;
    }
    float a = 0, b = 0;
    if (!ToScreen(x, z, &a, &b, inside)) {
        return false;
    }
    if (sx != nullptr) {
        *sx = a;
    }
    if (sy != nullptr) {
        *sy = b;
    }
    return true;
}

extern "C" bool RsMinimap_DrawArrow(PlayState* play, f32 x, f32 z, s16 yaw, Color_RGB8 color) {
    if (!sFrame.drawing || play == nullptr) {
        return false;
    }
    float px = 0, py = 0, ux = 0, uy = 0;
    MapPose(x, z, yaw, &px, &py, &ux, &uy);
    sLastArrowOut = !DrawMapArrow(px, py, ux, uy, color);
    return true;
}

extern "C" bool RsMinimap_LastArrowOutOfView(void) {
    return sFrame.drawing && sLastArrowOut;
}

extern "C" bool RsMinimap_Snap(void) {
    return sSnap;
}

extern "C" void RsMinimap_SetSnap(bool snap) {
    sSnap = snap;
}

extern "C" float RsMinimap_SnapGrid(void) {
    return sSnapGrid;
}

extern "C" bool RsMinimap_Bilerp(void) {
    return sBilerp;
}

extern "C" void RsMinimap_SetBilerp(bool bilerp) {
    sBilerp = bilerp;
}

// File-static, not in the anonymous namespace: OPEN_DISPS redeclares FrameInterpolation_RecordOpenChild at block
// scope, and inside an anonymous namespace MSVC gives that C++ linkage (RsMenu.cpp says the same).
extern "C" void RsMinimap_Draw(PlayState* play, bool visible) {
    RsWorldMapInfo info;
    if (play == nullptr || !RsWorldMap_Info(play->sceneNum, &info)) {
        return;
    }
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    Player* player = GET_PLAYER(play);
    std::vector<Gfx>& dl = Dl();
    dl.clear();

    const uint8_t alpha = (uint8_t)play->interfaceCtx.minimapAlpha;
    const bool rotate = RsMinimap_Rotate();
    const int32_t radiusTiles = RsMinimap_RadiusTiles();
    const float radiusPx = (float)(radiusTiles * info.pxPerTile);

    sFrame.scene = play->sceneNum;
    sFrame.gfxCtx = gfxCtx;
    sFrame.dl = &dl;
    sFrame.visible = visible && alpha > 0;
    sFrame.mirror = CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) != 0;
    sFrame.rimPx = (float)RS_MINIMAP_SCREEN_RADIUS;
    sFrame.scale = sFrame.rimPx / radiusPx;
    RsWorldMap_WorldToPixel(play->sceneNum, player->actor.world.pos.x, player->actor.world.pos.z, &sFrame.lpx,
                            &sFrame.lpy);
    Centre(&sFrame.cx, &sFrame.cy);

    // The turn: rotate mode points the camera's forward direction up the screen. Measured through WorldToPixel so
    // the grid frame's own turn never has to be derived by hand.
    float rotTarget = 0.0f;
    if (rotate) {
        const float cy = Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) * (kPi / 32768.0f);
        float fx = 0, fy = 0;
        RsWorldMap_WorldToPixel(play->sceneNum, player->actor.world.pos.x + std::sin(cy) * 100.0f,
                                player->actor.world.pos.z + std::cos(cy) * 100.0f, &fx, &fy);
        const float gx = fx - sFrame.lpx, gy = -(fy - sFrame.lpy); // geometry space, y up
        rotTarget = kPi / 2 - std::atan2(gy, gx);
    }
    sRotUnwrapped = Unwrap(sRotUnwrapped, rotTarget);
    sFrame.rot = sRotUnwrapped;

    sStats.blocks = 0;
    sStats.arrows = 0;

    FrameInterpolation_RecordOpenChild(&sFrame, 0);

    // The map's matrix, recorded every frame whether or not it draws (interpolation matches ops by position).
    // Geometry (px * 4, -py * 4) -> screen: Link's pixel to the origin, scale, turn, mirror, then the centre.
    Matrix_Translate(sFrame.cx, sFrame.cy, 0.0f, MTXMODE_NEW);
    Matrix_Scale(sFrame.mirror ? -1.0f : 1.0f, 1.0f, 1.0f, MTXMODE_APPLY);
    Matrix_RotateZ(sFrame.rot, MTXMODE_APPLY);
    Matrix_Scale(sFrame.scale / kFix, sFrame.scale / kFix, 1.0f, MTXMODE_APPLY);
    Matrix_Translate(-sFrame.lpx * kFix, sFrame.lpy * kFix, 0.0f, MTXMODE_APPLY);
    // The disc's matrix is that, unsnapped: the circle stays put on screen.
    Mtx* discMtx = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
    // North-up, the map's scroll lands on whole SCREEN pixels (the owner, round 3): with the scale fixed, every
    // texel then keeps the same place in the pixel grid frame to frame, so a wall line keeps its width. The step is
    // one output pixel in the HUD ortho's units (240 of them span the render height). Recorded every frame, a zero
    // step in rotate mode, and snapped again on every interpolated frame (FrameInterpolation_MatrixSnapTranslation),
    // so 60 fps snaps the in-between positions too. Only the map and what is printed on it ride it: the disc, the
    // circle's position and Link's arrow do not move.
    const uint32_t renderH = OTRGetGameRenderHeight();
    sSnapGrid = (sSnap && !rotate && renderH > 0) ? (float)SCREEN_HEIGHT / (float)renderH : 0.0f;
    FrameInterpolation_MatrixSnapTranslation(sSnapGrid, sSnapGrid);
    Mtx* mapMtx = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
    sFrame.mapMtx = mapMtx;

    if (sFrame.visible) {
        // The circle in image pixels, around Link.
        sCircle.resize(kSides);
        for (int32_t i = 0; i < kSides; i++) {
            const float a = (2 * kPi * i) / kSides;
            sCircle[i] = { sFrame.lpx + std::cos(a) * radiusPx, sFrame.lpy - std::sin(a) * radiusPx };
        }
        dl.reserve(1024);
        dl.push_back(gsDPPipeSync());
        dl.push_back(gsSPMatrix(discMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
        dl.push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
        dl.push_back(gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT |
                                          (sBilerp ? G_TF_BILERP : G_TF_POINT) | G_TT_NONE | G_TL_TILE |
                                          G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                      G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));
        // The disc under the map, a pixel wider on screen: the rim, and dark wherever the circle is off the image.
        {
            dl.push_back(gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF));
            dl.push_back(gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE));
            dl.push_back(gsDPSetPrimColor(0, 0, kBg[0], kBg[1], kBg[2], alpha));
            const float k = (sFrame.rimPx + 1.0f) / sFrame.rimPx;
            std::vector<P> disc(kSides);
            for (int32_t i = 0; i < kSides; i++) {
                disc[i] = { sFrame.lpx + (sCircle[i].x - sFrame.lpx) * k, sFrame.lpy + (sCircle[i].y - sFrame.lpy) * k };
            }
            PushFan(dl, disc, 0.0f, 0.0f);
        }
        // The map, block by block, under the snapped matrix. Its clip edge can sit up to a pixel off the disc's; both
        // are the background colour there, so it never shows.
        dl.push_back(gsDPPipeSync());
        dl.push_back(gsSPMatrix(mapMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
        dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
        dl.push_back(gsDPSetCombineMode(G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM));
        dl.push_back(gsDPSetPrimColor(0, 0, 255, 255, 255, alpha));
        int32_t blocks = 0;
        const size_t at = dl.size();
        dl.resize(at + 32);
        Gfx* p = &dl[at];
        RsWorldMap_ForEachBlock(&p, play->sceneNum, sFrame.lpx - radiusPx, sFrame.lpy - radiusPx, 2 * radiusPx,
                                2 * radiusPx, OnBlock, &blocks);
        dl.resize((size_t)(p - dl.data()));
        sStats.blocks = blocks;
        dl.push_back(gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF));
    }

    // The arrows, behind vanilla's gate (minimapAlpha >= 0xAA). The vehicles' (and #307's icons) are printed on the
    // map under Link's; Link's is vanilla's own arrow at the centre; the red entrance arrow, printed on the map, goes
    // on top as vanilla's does.
    const bool arrows = sFrame.visible && alpha >= 0xAA;
    sFrame.drawing = true;
    if (arrows) {
        GameInteractor_ExecuteOnMinimapDrawCompassIconsUnderPlayer();
        Gfx setup[2];
        Gfx_SetupDL(setup, SETUPDL_42);
        dl.push_back(gsDPPipeSync());
        dl.push_back(setup[0]);
        dl.push_back(gsDPSetCombineLERP(PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                                        PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0));
        dl.push_back(gsDPSetEnvColor(0, 0, 0, 255));
        dl.push_back(gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE));
    }
    // Link's arrow, at the centre: recorded every frame, emitted only when shown.
    const Color_RGB8 linkColor = RsMinimap_LinkColor();
    sLinkArrowUnwrapped =
        Unwrap(sLinkArrowUnwrapped, ArrowAngle(player->actor.world.pos.x, player->actor.world.pos.z,
                                               player->actor.shape.rot.y));
    PushArrow(sFrame.cx, sFrame.cy, sLinkArrowUnwrapped, linkColor, arrows);
    if (arrows) {
        // The red entrance arrow: where Link came into the scene, facing the way he faced (vanilla's
        // sPlayerInitial*, its direction kept in vanilla's 1/64-turn steps).
        f32 ex = 0, ez = 0;
        s16 eyaw = 0;
        RsMinimap_EntrancePose(&ex, &ez, &eyaw);
        float px = 0, py = 0, ux = 0, uy = 0;
        MapPose(ex, ez, eyaw, &px, &py, &ux, &uy);
        sStats.entranceShown = DrawMapArrow(px, py, ux, uy, RsMinimap_EntranceColor());
        GameInteractor_ExecuteOnMinimapDrawCompassIcons();
    } else {
        sStats.entranceShown = false;
    }
    sFrame.drawing = false;

    FrameInterpolation_RecordCloseChild();

    sStats.handled = true;
    sStats.drawn = sFrame.visible;
    sStats.rotate = rotate;
    sStats.radiusTiles = radiusTiles;
    sStats.linkPx = sFrame.lpx;
    sStats.linkPy = sFrame.lpy;
    sStats.rotDeg = WrapPi(sFrame.rot) * (180.0f / kPi);
    sStats.centerX = sFrame.cx;
    sStats.centerY = sFrame.cy;
    sStats.alpha = alpha;
    sStats.frame = play->state.frames;

    if (dl.empty()) {
        sStats.words = 0;
        return;
    }
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPEndDisplayList());
    sStats.words = (int32_t)dl.size();
    OPEN_DISPS(gfxCtx);
    gSPDisplayList(OVERLAY_DISP++, dl.data());
    CLOSE_DISPS(gfxCtx);
}
