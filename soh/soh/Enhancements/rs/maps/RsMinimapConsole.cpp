#include "RsMinimapConsole.h"

#include <cstdio>
#include <cstdlib>

#include <ship/debug/Console.h>

#include "RsMapIcons.h"
#include "RsMinimap.h"
#include "WorldMap.h"
#include "soh/Enhancements/console/ConsoleSink.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

std::string Status() {
    const int16_t scene = gPlayState != nullptr ? gPlayState->sceneNum : (int16_t)-1;
    const char* mode = RsMinimap_Rotate() ? "rotate" : "north";
    char buf[512];
    if (gPlayState == nullptr || !RsMinimap_Handles(gPlayState)) {
        std::snprintf(buf, sizeof(buf), "op=status scene=0x%X handled=0 reason=%s mode=%s radius=%d",
                      (unsigned)(uint16_t)scene, scene < 0 ? "no_play" : RsWorldMap_Problem(scene), mode,
                      RsMinimap_RadiusTiles());
        return buf;
    }
    RsMinimapStats s;
    RsMinimap_GetStats(&s);
    std::snprintf(buf, sizeof(buf),
                  "op=status scene=0x%X handled=1 drawn=%d mode=%s radius=%d link_px=%.1f,%.1f rot_deg=%.1f blocks=%d "
                  "words=%d arrows=%d icons=%d entrance=%d alpha=%u minimap_off=%d filter=%s snap=%s snap_px=%.4f centre=%.1f,%.1f "
                  "storey_mode=%s link_storey=%d",
                  (unsigned)(uint16_t)scene, s.drawn ? 1 : 0, mode, RsMinimap_RadiusTiles(), s.linkPx, s.linkPy,
                  s.rotDeg, s.blocks, s.words, s.arrows, s.icons, s.entranceShown ? 1 : 0, (unsigned)s.alpha,
                  (int)R_MINIMAP_DISABLED, RsMinimap_Bilerp() ? "bilerp" : "point", RsMinimap_Snap() ? "on" : "off",
                  RsMinimap_SnapGrid(), s.centerX, s.centerY,
                  RsWorldMap_StoreyModeName(RsWorldMap_StoreyMode()), RsWorldMap_LinkStorey());
    return buf;
}

// `minimap icons`: a summary line, then one line per table icon on this scene's map, with what last frame did.
void Icons(std::vector<std::string>& lines) {
    const int16_t scene = gPlayState != nullptr ? gPlayState->sceneNum : (int16_t)-1;
    char buf[512];
    RsMapIcon icons[32];
    const int32_t onMap = scene < 0 ? 0 : RsMapIcons_ForScene(scene, icons, 32);
    RsMapIconFrame frame[32];
    const int32_t n = RsMapIcons_LastFrame(frame, 32);
    RsMinimapStats s;
    RsMinimap_GetStats(&s);
    int32_t inView = 0, clipped = 0;
    for (int32_t i = 0; i < n; i++) {
        inView += frame[i].state == RS_MINIMAP_ICON_DRAWN || frame[i].state == RS_MINIMAP_ICON_CLIPPED;
        clipped += frame[i].state == RS_MINIMAP_ICON_CLIPPED;
    }
    std::snprintf(buf, sizeof(buf),
                  "op=icons scene=0x%X table=%d on_map=%d minimap=%d in_view=%d clipped=%d radius=%d mode=%s "
                  "texel_n64=%.4f",
                  (unsigned)(uint16_t)scene, RsMapIcons_Count(), onMap, s.drawn ? 1 : 0, inView, clipped, RsMinimap_RadiusTiles(), RsMinimap_Rotate() ? "rotate" : "north",
                  RS_MINIMAP_ICON_TEXEL);
    lines.push_back(buf);
    for (int32_t i = 0; i < onMap; i++) {
        const RsMapIconDef* d = icons[i].def;
        const RsMapIconFrame* f = nullptr;
        for (int32_t k = 0; k < n; k++) {
            if (frame[k].index == icons[i].index) {
                f = &frame[k];
            }
        }
        const char* state = f == nullptr ? "no_minimap"
                            : !f->onLayer ? "other_storey"
                                          : RsMinimap_IconStateName((RsMinimapIconState)f->state);
        std::snprintf(buf, sizeof(buf),
                      "op=icon name=%s token=%s rs=%d,%d plane=%d world=%.0f,%.0f state=%s tiles=%.1f screen=%.1f,%.1f",
                      d->name, d->token, d->rsX, d->rsY, d->plane, icons[i].x, icons[i].z, state,
                      f != nullptr ? f->tiles : -1.0f, f != nullptr ? f->sx : 0.0f, f != nullptr ? f->sy : 0.0f);
        lines.push_back(buf);
    }
}

} // namespace

int32_t RsMinimapConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? "status" : args[0];
    if (sub == "status") {
        lines.push_back(Status());
        return 0;
    }
    if (sub == "icons") {
        Icons(lines);
        return 0;
    }
    if (sub == "mode") {
        if (args.size() >= 2 && (args[1] == "north" || args[1] == "rotate")) {
            RsMinimap_SetRotate(args[1] == "rotate");
            lines.push_back(Status());
            return 0;
        }
        lines.push_back("op=mode result=error error=bad_mode want=north|rotate");
        return 1;
    }
    if (sub == "radius") {
        if (args.size() >= 2 && args[1] == "default") {
            RsMinimap_SetRadiusTiles(RS_MINIMAP_RADIUS_DEFAULT);
            lines.push_back(Status());
            return 0;
        }
        char* end = nullptr;
        const long v = args.size() >= 2 ? std::strtol(args[1].c_str(), &end, 10) : 0;
        if (args.size() < 2 || end == args[1].c_str() || *end != '\0' || v < RS_MINIMAP_RADIUS_MIN ||
            v > RS_MINIMAP_RADIUS_MAX) {
            lines.push_back("op=radius result=error error=bad_radius want=8..160|default");
            return 1;
        }
        RsMinimap_SetRadiusTiles((int32_t)v);
        lines.push_back(Status());
        return 0;
    }
    if (sub == "filter") {
        if (args.size() >= 2 && (args[1] == "bilerp" || args[1] == "point")) {
            RsMinimap_SetBilerp(args[1] == "bilerp");
            lines.push_back(Status());
            return 0;
        }
        lines.push_back("op=filter result=error error=bad_filter want=bilerp|point");
        return 1;
    }
    if (sub == "snap") {
        if (args.size() >= 2 && (args[1] == "on" || args[1] == "off")) {
            RsMinimap_SetSnap(args[1] == "on");
            lines.push_back(Status());
            return 0;
        }
        lines.push_back("op=snap result=error error=bad_snap want=on|off");
        return 1;
    }
    lines.push_back(
        "usage: minimap [status] | icons | mode north|rotate | radius <tiles>|default | filter bilerp|point | snap "
        "on|off");
    return 1;
}

namespace {

const ConsoleSink::Command minimapCommand(
    "minimap", RsMinimapConsole_Run,
    "The round HUD minimap in RS scenes (sturdy-bassoon#306): status | icons | mode north|rotate | radius "
    "<tiles>|default | filter bilerp|point | snap on|off. What it drew, Link's map pixel, its map icons (#307) and its "
    "settings; radius is the zoom.",
    { { "status|icons|mode|radius|filter|snap", Ship::ArgumentType::TEXT, true },
      { "argument", Ship::ArgumentType::TEXT, true } });

const ConsoleSink::Hints minimapHints("minimap", R"(
status                  mode, radius, Link's pixel and what last drew
icons                   the map icons on this map: where, and drawn, clipped or out of view
mode                    which way the map faces (saved)
  north                 north up (default)
  rotate                turns with the camera
radius                  the zoom: how far it reaches, in RS tiles (saved)
  <tiles>               8 to 160
  default               45: a whole chunk from its middle
filter                  the map's texture filter (not saved)
  bilerp                bilinear, steady lines (default)
  point                 nearest texel, the flicker A/B
snap                    north-up scroll on whole screen pixels (not saved)
  on                    lines keep their width as the map moves (default)
  off                   sub-pixel scroll, the shimmer A/B
)");

} // namespace
