#include "WorldMapConsole.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <ship/debug/Console.h>

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

int16_t SceneNow() {
    return gPlayState != nullptr ? gPlayState->sceneNum : (int16_t)-1;
}

// Image pixel -> the RS tile it lies in.
void PixelToRs(const RsWorldMapInfo& i, float px, float py, int32_t* rsX, int32_t* rsY) {
    *rsX = i.minRsX + (int32_t)std::floor(px / i.pxPerTile);
    *rsY = i.minRsY + i.depthTiles - 1 - (int32_t)std::floor(py / i.pxPerTile);
}

std::string Status() {
    const int16_t scene = SceneNow();
    const char* mode = RsWorldMap_StoreyModeName(RsWorldMap_StoreyMode());
    char buf[512];
    RsWorldMapInfo i;
    if (!RsWorldMap_Info(scene, &i)) {
        std::snprintf(buf, sizeof(buf), "op=status scene=0x%X map=0 reason=%s storey_mode=%s", (unsigned)(uint16_t)scene,
                      scene < 0 ? "no_play" : RsWorldMap_Problem(scene), mode);
        return buf;
    }
    float px = 0.0f, py = 0.0f;
    int32_t rsX = 0, rsY = 0;
    Player* player = GET_PLAYER(gPlayState);
    RsWorldMap_WorldToPixel(scene, player->actor.world.pos.x, player->actor.world.pos.z, &px, &py);
    PixelToRs(i, px, py, &rsX, &rsY);
    int16_t bw = 0, bh = 0;
    RsWorldMap_BlockSize(&bw, &bh);
    std::snprintf(buf, sizeof(buf),
                  "op=status scene=0x%X map=1 size=%ux%u px_per_tile=%u tiles=%ux%u storeys=%u layers=%u layer=%u storey_mode=%s "
                  "link_storey=%d link_px=%.1f,%.1f link_rs=%d,%d blocks=%dx%d bytes=%u",
                  (unsigned)(uint16_t)scene, i.widthPx, i.heightPx, i.pxPerTile, i.widthTiles, i.depthTiles, i.storeys, i.layers,
                  i.layer, mode, RsWorldMap_LinkStorey(), px, py, rsX, rsY, bw, bh, i.fileBytes);
    return buf;
}

bool ParseNumber(const std::string& word, float* out) {
    if (word.empty()) {
        return false;
    }
    char* end = nullptr;
    const float v = std::strtof(word.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(v)) {
        return false;
    }
    *out = v;
    return true;
}

const char* kUsage = "usage: worldmap [status] | storey ground|all|current|below | debug off|whole|link | block <w> <h> | "
                     "pixel <x> <z>";

} // namespace

int32_t RsWorldMapConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? "status" : args[0];
    if (sub == "status") {
        lines.push_back(Status());
        return 0;
    }
    if (sub == "storey") {
        for (int32_t m = 0; m < RS_WORLD_MAP_STOREY_MODE_COUNT; m++) {
            if (args.size() >= 2 && args[1] == RsWorldMap_StoreyModeName((RsWorldMapStoreyMode)m)) {
                RsWorldMap_SetStoreyMode((RsWorldMapStoreyMode)m);
                lines.push_back(Status());
                return 0;
            }
        }
        lines.push_back("op=storey result=error error=bad_mode want=ground|all|current|below");
        return 1;
    }
    if (sub == "debug") {
        static const char* kViews[] = { "off", "whole", "link" };
        if (args.size() >= 2) {
            int32_t view = -1;
            for (int32_t v = 0; v < 3; v++) {
                if (args[1] == kViews[v]) {
                    view = v;
                }
            }
            if (view < 0) {
                lines.push_back("op=debug result=error error=bad_view want=off|whole|link");
                return 1;
            }
            RsWorldMap_SetDebugView(view);
        }
        int32_t blocks = 0, words = 0;
        RsWorldMap_DebugStats(&blocks, &words);
        lines.push_back("op=debug view=" + std::string(kViews[RsWorldMap_DebugView()]) +
                        " blocks=" + std::to_string(blocks) + " words=" + std::to_string(words));
        return 0;
    }
    if (sub == "block") {
        float w = 0.0f, h = 0.0f;
        if (args.size() < 3 || !ParseNumber(args[1], &w) || !ParseNumber(args[2], &h) ||
            !RsWorldMap_SetBlockSize((int16_t)w, (int16_t)h)) {
            lines.push_back("op=block result=error error=bad_size want=even_w,(w+4)*(h+2)<=4096");
            return 1;
        }
        lines.push_back("op=block w=" + std::to_string((int32_t)w) + " h=" + std::to_string((int32_t)h));
        return 0;
    }
    if (sub == "pixel") {
        float x = 0.0f, z = 0.0f, px = 0.0f, py = 0.0f;
        RsWorldMapInfo i;
        if (args.size() < 3 || !ParseNumber(args[1], &x) || !ParseNumber(args[2], &z)) {
            lines.push_back("op=pixel result=error error=bad_args want=<x>_<z>");
            return 1;
        }
        if (!RsWorldMap_Info(SceneNow(), &i) || !RsWorldMap_WorldToPixel(SceneNow(), x, z, &px, &py)) {
            lines.push_back("op=pixel result=error error=no_map");
            return 1;
        }
        int32_t rsX = 0, rsY = 0;
        PixelToRs(i, px, py, &rsX, &rsY);
        char buf[200];
        std::snprintf(buf, sizeof(buf), "op=pixel x=%.1f z=%.1f px=%.1f py=%.1f rs=%d,%d", x, z, px, py, rsX, rsY);
        lines.push_back(buf);
        return 0;
    }
    lines.push_back(kUsage);
    return 1;
}

namespace {

const ConsoleSink::Command worldMapCommand(
    "worldmap", RsWorldMapConsole_Run,
    "The world map image (sturdy-bassoon#298): status | storey ground|all|current|below | debug off|whole|link | "
    "block <w> <h> | pixel <x> <z>. Its size, the storey layer drawn, and the pixel under Link.",
    { { "status|storey|debug|block|pixel", Ship::ArgumentType::TEXT, true },
      { "argument", Ship::ArgumentType::TEXT, true },
      { "argument", Ship::ArgumentType::TEXT, true } });

const ConsoleSink::Hints worldMapHints("worldmap", R"(
status                  size, layer, storey mode and Link's pixel
storey                  which storeys the map draws (saved)
  ground                the ground floor only
  all                   every storey, upper floors hiding lower
  current               the storey Link stands on, alone
  below                 Link's storey and every storey under it
debug                   draw the map on screen
  off                   no debug view
  whole                 the whole image in the top-right corner
  link                  a window around Link, his pixel in red
block <w> <h>           the TMEM block size, for measuring
pixel <x> <z>           where a world position lands on the image
)");

} // namespace
