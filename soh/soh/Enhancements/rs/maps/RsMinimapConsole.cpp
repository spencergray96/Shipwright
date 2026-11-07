#include "RsMinimapConsole.h"

#include <cstdio>
#include <cstdlib>

#include <ship/debug/Console.h>

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
                  "words=%d arrows=%d alpha=%u minimap_off=%d centre=%.1f,%.1f storey_mode=%s link_storey=%d",
                  (unsigned)(uint16_t)scene, s.drawn ? 1 : 0, mode, RsMinimap_RadiusTiles(), s.linkPx, s.linkPy,
                  s.rotDeg, s.blocks, s.words, s.arrows, (unsigned)s.alpha, (int)R_MINIMAP_DISABLED, s.centerX,
                  s.centerY, RsWorldMap_StoreyModeName(RsWorldMap_StoreyMode()), RsWorldMap_LinkStorey());
    return buf;
}

} // namespace

int32_t RsMinimapConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? "status" : args[0];
    if (sub == "status") {
        lines.push_back(Status());
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
    lines.push_back("usage: minimap [status] | mode north|rotate | radius <tiles>|default");
    return 1;
}

namespace {

const ConsoleSink::Command minimapCommand(
    "minimap", RsMinimapConsole_Run,
    "The round HUD minimap in RS scenes (sturdy-bassoon#306): status | mode north|rotate | radius <tiles>|default. "
    "What it drew, Link's map pixel, and its settings.",
    { { "status|mode|radius", Ship::ArgumentType::TEXT, true }, { "argument", Ship::ArgumentType::TEXT, true } });

const ConsoleSink::Hints minimapHints("minimap", R"(
status                  mode, radius, Link's pixel and what last drew
mode                    which way the map faces (saved)
  north                 north up (default)
  rotate                turns with the camera
radius                  how far it reaches, in RS tiles (saved)
  <tiles>               8 to 160
  default               45: a whole chunk from its middle
)");

} // namespace
