// Map icons placed by hand (sturdy-bassoon#307): see RsMapIcons.h.
#include "RsMapIcons.h"

#include <cmath>
#include <iterator>
#include <vector>

#include "RsMinimap.h"
#include "WorldMap.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

// THE TABLE. Each tile is RS's own: the map-function loc the client draws that icon for (an object definition with a
// mapAreaId, read offline from the local cache), checked against the building or feature in the grid tool's F2P map.
// sturdy-bassoon docs/reference/WORLD_MAP.md, "Map icons", has how each was found and the recipe for a new row.
const RsMapIconDef kMapIcons[] = {
    // The general store: RS's marker (loc 2733, area 0) inside the octagonal shop in the Lumbridge chunk, local 12,46.
    { "lumbridge_general_store", "map_general_store", 3212, 3246, 0 },
    // The castle's north courtyard fountain (loc 879 at 3221,3226, 2 x 2): RS's water-source marker (2771, area 41).
    { "lumbridge_courtyard_fountain", "map_water_source", 3222, 3226, 0 },
    // The River Lum west of the goblin house (3242-3250, 3244-3248): the nearer of RS's two Lum fishing markers (2759,
    // area 28); the other is 3238,3253.
    { "lumbridge_river_lum_fishing", "map_fishing_spot", 3239, 3241, 0 },
};
constexpr int32_t kCount = (int32_t)std::size(kMapIcons);

std::vector<RsMapIconFrame> sLast;
uint32_t sLastFrame = 0;
bool sEverDrawn = false;

// The plane is in the layer drawn: alone k is plane k, a stack 0..k is planes 0..k (WorldMap.h's layer order).
bool OnLayer(const RsWorldMapInfo& info, uint8_t plane) {
    if (info.layer < info.storeys) {
        return plane == info.layer;
    }
    return plane <= info.layer - info.storeys + 1;
}

void DrawMinimapIcons() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    sLast.clear();
    sLastFrame = play->state.frames;
    sEverDrawn = true;
    RsMapIcon icons[kCount];
    const int32_t n = RsMapIcons_ForScene(play->sceneNum, icons, kCount);
    RsWorldMapInfo info;
    RsWorldMap_Info(play->sceneNum, &info);
    const Player* player = GET_PLAYER(play);
    float lpx = 0, lpy = 0;
    RsWorldMap_WorldToPixel(play->sceneNum, player->actor.world.pos.x, player->actor.world.pos.z, &lpx, &lpy);
    for (int32_t i = 0; i < n; i++) {
        RsMapIconFrame f = {};
        f.index = icons[i].index;
        f.onLayer = icons[i].onLayer;
        f.state = RS_MINIMAP_ICON_NOT_DRAWING;
        float px = 0, py = 0;
        RsWorldMap_WorldToPixel(play->sceneNum, icons[i].x, icons[i].z, &px, &py);
        f.tiles = std::sqrt((px - lpx) * (px - lpx) + (py - lpy) * (py - lpy)) / (float)info.pxPerTile;
        if (f.onLayer) {
            f.state = RsMinimap_DrawIcon(icons[i].def->token, icons[i].x, icons[i].z, &f.sx, &f.sy);
        }
        sLast.push_back(f);
    }
}

void RegisterRsMapIcons() {
    COND_HOOK(OnMinimapDrawCompassIconsUnderPlayer, true, DrawMinimapIcons);
}

RegisterShipInitFunc rsMapIconsInitFunc(RegisterRsMapIcons);

} // namespace

extern "C" int32_t RsMapIcons_Count(void) {
    return kCount;
}

extern "C" const RsMapIconDef* RsMapIcons_Def(int32_t index) {
    return index >= 0 && index < kCount ? &kMapIcons[index] : nullptr;
}

extern "C" int32_t RsMapIcons_ForScene(int16_t sceneId, RsMapIcon* out, int32_t max) {
    RsWorldMapInfo info;
    if (out == nullptr || !RsWorldMap_Info(sceneId, &info)) {
        return 0;
    }
    int32_t n = 0;
    for (int32_t i = 0; i < kCount && n < max; i++) {
        const RsMapIconDef& d = kMapIcons[i];
        if (d.rsX < info.minRsX || d.rsX >= info.minRsX + info.widthTiles || d.rsY < info.minRsY ||
            d.rsY >= info.minRsY + info.depthTiles) {
            continue;
        }
        // The tile's centre, through the image: px x east, py y south from the image's north-west corner.
        const float px = ((float)(d.rsX - info.minRsX) + 0.5f) * info.pxPerTile;
        const float py = ((float)(info.minRsY + info.depthTiles - d.rsY) - 0.5f) * info.pxPerTile;
        RsMapIcon& o = out[n++];
        o.index = i;
        o.def = &d;
        RsWorldMap_PixelToWorld(sceneId, px, py, &o.x, &o.z);
        o.onLayer = OnLayer(info, d.plane);
    }
    return n;
}

extern "C" int32_t RsMapIcons_LastFrame(RsMapIconFrame* out, int32_t max) {
    if (!sEverDrawn || gPlayState == nullptr || gPlayState->state.frames - sLastFrame > 1) {
        return 0;
    }
    int32_t n = 0;
    for (const RsMapIconFrame& f : sLast) {
        if (n >= max) {
            break;
        }
        out[n++] = f;
    }
    return n;
}
