// RsSpritesConsole.cpp - the `rssprite` debug draw (sturdy-bassoon#297). The contract is RsSpritesConsole.h.

#include "RsSpritesConsole.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "RsSprites.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

constexpr size_t kShownMax = 32;
constexpr float kDefaultWidthCap = 300.0f;

struct Shown {
    std::string token;
    float x, y, w, h; // the HUD 320 x 240 frame, top-left origin, y down
};

std::vector<Shown> sShown;
bool sPoint = false;  // filter: false = bilinear
bool sCutout = false; // blend: false = XLU

const char* kUsage = "op=usage text=\"rssprite status|info <token>|show <token> <x> <y> [<w> [<h>]]|layout|"
                     "filter bilerp|point|blend xlu|cutout|panel on|off|clear\"";

bool ParseFloat(const std::string& s, float* out) {
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (s.empty() || end == nullptr || *end != '\0' || !(v > -10000.0f && v < 10000.0f)) {
        return false;
    }
    *out = v;
    return true;
}

bool sPanel = false; // the flat grey panel behind the layout, for judging edges against one colour

std::string Status() {
    return "op=status shown=" + std::to_string(sShown.size()) + " filter=" + (sPoint ? "point" : "bilerp") +
           " blend=" + (sCutout ? "cutout" : "xlu") + " panel=" + (sPanel ? "on" : "off");
}

// Adds one sprite. `w` or `h` <= 0 means "derive it": the width from the picture (capped), the height from the aspect.
int32_t Show(const std::string& token, float x, float y, float w, float h, std::vector<std::string>& lines) {
    RsSpriteInfo info;
    if (!RsSprites_Info(token.c_str(), &info)) {
        lines.push_back("op=show result=error error=missing");
        return 1;
    }
    if (sShown.size() >= kShownMax) {
        lines.push_back("op=show result=error error=full");
        return 1;
    }
    if (w <= 0.0f) {
        w = std::min((float)info.width, kDefaultWidthCap);
    }
    if (h <= 0.0f) {
        h = w * (float)info.height / (float)info.width;
    }
    sShown.push_back({ token, x, y, w, h });
    Addf(lines, "op=show result=ok token=%s x=%.1f y=%.1f w=%.1f h=%.1f shown=%d", token.c_str(), x, y, w, h,
         (int)sShown.size());
    return 0;
}

// The #297 proof sheet, on a flat grey panel so an edge's fringe (or its absence) reads against one colour.
// The HUD's 320 x 240 frame, below the hearts. Slider pieces at 18 units a 16-px piece (the packed texture is 64 px,
// Scale2x x4),
// an assembled track, the map icons at 2x, the quest-complete parchment (a 2 x 2 texture grid drawn as one) and the
// homepage scroll's three pieces stacked as its stylesheet stacks them.
constexpr float kPanelX = 8.0f, kPanelY = 56.0f, kPanelW = 304.0f, kPanelH = 170.0f;

int32_t Layout(std::vector<std::string>& lines) {
    sShown.clear();
    sPanel = true;
    int32_t rc = 0;
    const char* pieces[] = { "slider_end_left",  "slider_track",         "slider_track_notch", "slider_track_notch_b",
                             "slider_track_notch_c", "slider_track_split_left", "slider_track_split_right",
                             "slider_end_right", "slider_notch",         "slider_knob_blue",   "slider_knob_red",
                             "slider_knob_green", "slider_knob_grey" };
    float x = kPanelX + 6.0f;
    for (const char* p : pieces) {
        rc |= Show(p, x, kPanelY + 6.0f, 18.0f, 18.0f, lines);
        x += 22.0f;
    }
    // The track as the settings screen lays it: an end, plain and notched segments, an end, a knob on a notch.
    const float s = 18.0f, ty = kPanelY + 34.0f;
    const char* track[] = { "slider_end_left", "slider_track",       "slider_track_notch", "slider_track",
                            "slider_track_notch", "slider_track",    "slider_track_notch", "slider_track",
                            "slider_end_right" };
    x = kPanelX + 6.0f;
    for (const char* p : track) {
        rc |= Show(p, x, ty, s, s, lines);
        x += s;
    }
    rc |= Show("slider_knob_blue", kPanelX + 6.0f + 4 * s, ty, s, s, lines);
    // Map icons: 15 px in RS, drawn at 2x.
    rc |= Show("map_general_store", kPanelX + 190.0f, ty - 6.0f, 30.0f, 30.0f, lines);
    rc |= Show("map_water_source", kPanelX + 226.0f, ty - 6.0f, 30.0f, 30.0f, lines);
    rc |= Show("map_fishing_spot", kPanelX + 262.0f, ty - 6.0f, 30.0f, 30.0f, lines);
    // The quest-complete parchment.
    const float py = kPanelY + 64.0f;
    rc |= Show("scroll_quest_complete", kPanelX + 6.0f, py, 140.0f, 0.0f, lines);
    // The homepage scroll: top roll 765 x 50, a 745 x 164 strip, bottom roll; each roll overlaps the strip by 11 px
    // (the stylesheet's top:-11px and bottom:-11px).
    const float hw = 146.0f, k = hw / 765.0f, hx = kPanelX + 152.0f, hy = py + 4.0f;
    const float strip = 164.0f * k;
    rc |= Show("scroll_homepage/middle", hx + 10.0f * k, hy + 39.0f * k, 745.0f * k, strip, lines);
    rc |= Show("scroll_homepage/top", hx, hy, hw, 50.0f * k, lines);
    rc |= Show("scroll_homepage/bottom", hx, hy + (39.0f - 11.0f) * k + strip, hw, 50.0f * k, lines);
    lines.push_back("op=layout result=" + std::string(rc == 0 ? "ok" : "error") + " shown=" + std::to_string(sShown.size()));
    return rc;
}

std::vector<Gfx>& DebugDl() {
    static std::vector<Gfx> dl; // the interpreter walks it after the hook returns
    return dl;
}

} // namespace

int32_t RsSpritesConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? "status" : args[0];
    if (sub == "status") {
        lines.push_back(Status());
        return 0;
    }
    if (sub == "info" && args.size() == 2) {
        RsSpriteInfo info;
        if (!RsSprites_Info(args[1].c_str(), &info)) {
            lines.push_back("op=info result=error error=missing");
            return 1;
        }
        Addf(lines, "op=info result=ok token=%s width=%d height=%d cols=%d rows=%d rs=%dx%d", args[1].c_str(),
             (int)info.width, (int)info.height, (int)info.cols, (int)info.rows, (int)info.rsWidth, (int)info.rsHeight);
        return 0;
    }
    if (sub == "show" && args.size() >= 4 && args.size() <= 6) {
        float x, y, w = 0.0f, h = 0.0f;
        if (!ParseFloat(args[2], &x) || !ParseFloat(args[3], &y) || (args.size() >= 5 && !ParseFloat(args[4], &w)) ||
            (args.size() == 6 && !ParseFloat(args[5], &h))) {
            lines.push_back("op=show result=error error=bad_number");
            return 1;
        }
        return Show(args[1], x, y, w, h, lines);
    }
    if (sub == "layout" && args.size() == 1) {
        return Layout(lines);
    }
    if (sub == "filter" && args.size() == 2 && (args[1] == "bilerp" || args[1] == "point")) {
        sPoint = args[1] == "point";
        lines.push_back(Status());
        return 0;
    }
    if (sub == "blend" && args.size() == 2 && (args[1] == "xlu" || args[1] == "cutout")) {
        sCutout = args[1] == "cutout";
        lines.push_back(Status());
        return 0;
    }
    if (sub == "panel" && args.size() == 2 && (args[1] == "on" || args[1] == "off")) {
        sPanel = args[1] == "on";
        lines.push_back(Status());
        return 0;
    }
    if (sub == "clear" && args.size() == 1) {
        sShown.clear();
        sPanel = false;
        lines.push_back("op=clear result=ok shown=0");
        return 0;
    }
    lines.push_back(kUsage);
    return 1;
}

// Drawn as #298's debug view is (WorldMap.cpp): Vtx in a heap list under our own ortho at OnPlayDrawEnd.
// File-static, not in the anonymous namespace: OPEN_DISPS redeclares FrameInterpolation_RecordOpenChild at block
// scope, and inside an anonymous namespace MSVC gives that C++ linkage (RsMenu.cpp says the same).
static void OnPlayDrawEndRsSprites() {
    PlayState* play = gPlayState;
    if (sShown.empty() || play == nullptr) {
        return;
    }
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    std::vector<Gfx>& dl = DebugDl();
    dl.clear();

    Mtx* ortho = (Mtx*)Graph_Alloc(gfxCtx, sizeof(Mtx));
    Vp* vp = (Vp*)Graph_Alloc(gfxCtx, sizeof(Vp));
    vp->vp.vscale[0] = SCREEN_WIDTH * 2;
    vp->vp.vscale[1] = SCREEN_HEIGHT * 2;
    vp->vp.vscale[2] = G_MAXZ / 2;
    vp->vp.vscale[3] = 0;
    vp->vp.vtrans[0] = SCREEN_WIDTH * 2;
    vp->vp.vtrans[1] = SCREEN_HEIGHT * 2;
    vp->vp.vtrans[2] = G_MAXZ / 2;
    vp->vp.vtrans[3] = 0;
    // The HUD's frame: 320 x 240 with square units. The renderer maps it onto a 4:3 area centred in the window
    // (measured: a 40 x 40 square draws 172 x 172 px in a 1838 x 1037 window), so nothing here corrects for aspect.
    const float frameW = (float)SCREEN_WIDTH;
    guOrtho(ortho, -frameW / 2.0f, frameW / 2.0f, -(f32)(SCREEN_HEIGHT / 2), (f32)(SCREEN_HEIGHT / 2), -1.0f, 1.0f,
            1.0f);
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPViewport(vp));
    dl.push_back(gsSPMatrix(ortho, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
    dl.push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
    const uint32_t filter = sPoint ? G_TF_POINT : G_TF_BILERP;
    const uint32_t blend = sCutout ? (G_RM_TEX_EDGE | G_RM_TEX_EDGE2) : (G_RM_XLU_SURF | G_RM_XLU_SURF2);
    dl.push_back(gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | filter | G_TT_NONE | G_TL_TILE |
                                      G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  (sCutout ? G_AC_THRESHOLD : G_AC_NONE) | G_ZS_PIXEL | blend));
    dl.push_back(gsDPSetBlendColor(0, 0, 0, 1)); // the cutout's alpha threshold: any alpha above 0 draws

    FrameInterpolation_RecordOpenChild(&sShown, 0);
    Matrix_Push();
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    dl.push_back(gsSPMatrix(Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
    if (sPanel) {
        Vtx* v = (Vtx*)Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
        const float l = kPanelX - frameW / 2.0f, r = l + kPanelW, t = SCREEN_HEIGHT / 2.0f - kPanelY, b = t - kPanelH;
        const float xs[4] = { l, r, l, r }, ys[4] = { t, t, b, b };
        for (int32_t c = 0; c < 4; c++) {
            v[c].v.ob[0] = (int16_t)xs[c];
            v[c].v.ob[1] = (int16_t)ys[c];
            v[c].v.ob[2] = 0;
            v[c].v.flag = 0;
            v[c].v.tc[0] = v[c].v.tc[1] = 0;
            v[c].v.cn[0] = v[c].v.cn[1] = v[c].v.cn[2] = v[c].v.cn[3] = 255;
        }
        dl.push_back(gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF));
        dl.push_back(gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE));
        dl.push_back(gsDPSetPrimColor(0, 0, 96, 96, 96, 255));
        dl.push_back(gsSPVertex((uintptr_t)v, 4, 0));
        dl.push_back(gsSP2Triangles(0, 2, 3, 0, 0, 3, 1, 0));
        dl.push_back(gsDPPipeSync());
    }
    dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
    dl.push_back(gsDPSetCombineMode(G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM));
    dl.push_back(gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
    for (const Shown& s : sShown) {
        // Top-left, y down, to the ortho's centred, y-up frame.
        RsSprites_EmitRect(gfxCtx, dl, s.token.c_str(), s.x - frameW / 2.0f, SCREEN_HEIGHT / 2.0f - s.y, s.w, s.h);
    }
    Matrix_Pop();
    FrameInterpolation_RecordCloseChild();

    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPEndDisplayList());
    OPEN_DISPS(gfxCtx);
    gSPDisplayList(OVERLAY_DISP++, dl.data());
    gDPPipeSync(OVERLAY_DISP++);
    CLOSE_DISPS(gfxCtx);
}

namespace {

void RegisterRsSpritesHooks() {
    COND_HOOK(OnPlayDrawEnd, true, OnPlayDrawEndRsSprites);
}

RegisterShipInitFunc rsSpritesHooksInitFunc(RegisterRsSpritesHooks);

// `rssprite` collides with nothing in debugger/debugconsole.cpp's CMD_REGISTER list.
const ConsoleSink::Command rsSpriteCommand(
    "rssprite", RsSpritesConsole_Run,
    "Draw packed RS interface sprites on screen by token (sturdy-bassoon#297): status | info <token> | "
    "show <token> <x> <y> [<w> [<h>]] | layout | filter bilerp|point | blend xlu|cutout | panel on|off | clear. "
    "A debug draw: "
    "it proves mods/rs-sprites.o2r loads and shows how the sprite edges sample; units are square, 240 to the screen height.",
    { { "status|info|show|layout|filter|blend|panel|clear", Ship::ArgumentType::TEXT },
      { "argument", Ship::ArgumentType::TEXT, true } });

// What the dev console lists for the next word (sturdy-bassoon#292).
const ConsoleSink::Hints rsSpriteHints("rssprite", R"(
status                      what is drawn, and how it samples
info <token>                a packed sprite's size and texture grid
show <token> <x> <y> [w] [h]   draw a sprite at a screen place
layout                      the slider, map icons and parchments
filter                      how the debug draw samples
  bilerp                    bilinear, as the game's 2D art draws
  point                     nearest texel, to see the raw pixels
blend                       how the edges draw
  xlu                       blended by alpha
  cutout                    alpha-tested, a hard edge
panel                       the grey panel behind the layout
  on                        draw it, to judge edges against one colour
  off                       hide it
clear                       remove every sprite drawn
)");

} // namespace
