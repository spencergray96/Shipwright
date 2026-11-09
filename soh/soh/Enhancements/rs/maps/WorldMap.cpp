// The world map image (sturdy-bassoon#298). The contract is WorldMap.h; the file format is the grid
// tool's src/shared/worldMapImage.ts (encodeWorldMap), and the two change together.
#include "WorldMap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <spdlog/spdlog.h>

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
uint32_t OTRGetGameRenderWidth();
uint32_t OTRGetGameRenderHeight();
}

namespace {

// --- the scenes that opt in -----------------------------------------------------------------------
// Each stitched scene's row from the world export's include, as MusicZoneTable.cpp and SwimStamina.cpp
// read it. The map's path is built from the scene's enum name, so the include needs nothing new.
struct WorldScene {
    int16_t sceneId;
    const char* path;
    int32_t originRsX;
    int32_t originRsY;
};

#define RS_WORLD_SCENE(scene_, originRsX_, originRsY_, directorRsX_, directorRsY_, unitsPerTile_) \
    { scene_, "worldmap/" #scene_ ".bin", originRsX_, originRsY_ },
#define RS_WORLD_CHUNK(scene_, squareX_, squareY_, title_, nodeTitle_, nodeRsX_, nodeRsY_, nodeX_, nodeZ_)
const WorldScene kScenes[] = {
#include "soh/custom/scenes/grid_tool/f2p_overworld/f2p_overworld_world.inc"
// The #298 test fixture world (sturdy-bassoon world/agent_298_storeys.json: storeys of mixed height
// classes), opted in only in a tree it was exported into, so every other tree builds without it.
#if __has_include("soh/custom/scenes/grid_tool/agent298_storeys/agent298_storeys_world.inc")
#include "soh/custom/scenes/grid_tool/agent298_storeys/agent298_storeys_world.inc"
#endif
// The #313 detail test world (sturdy-bassoon world/agent_313_detail.json: Lumbridge with props and ground areas), the
// same way: opted in only where it was exported.
#if __has_include("soh/custom/scenes/grid_tool/agent313_detail/agent313_detail_world.inc")
#include "soh/custom/scenes/grid_tool/agent313_detail/agent313_detail_world.inc"
#endif
};
#undef RS_WORLD_SCENE
#undef RS_WORLD_CHUNK

const WorldScene* SceneOf(int16_t sceneId) {
    for (const WorldScene& s : kScenes) {
        if (s.sceneId == sceneId) {
            return &s;
        }
    }
    return nullptr;
}

// --- the file ---------------------------------------------------------------------------------------
// encodeWorldMap's header, little-endian. Read field by field with memcpy, never cast onto the buffer.
constexpr uint32_t kHeaderBytes = 64;
constexpr uint32_t kTlutBytes = 32;
constexpr uint16_t kVersion = 4;      // wall segments and the edgeless TLUT (#309)
constexpr uint16_t kVersionNoEdges = 3; // still read: no segments
constexpr int32_t kMaxClasses = 4;

struct LoadedMap {
    std::string problem; // "" when usable
    std::vector<uint8_t> bytes;
    RsWorldMapInfo info{};
    uint16_t unitsPerTile = 40;
    uint16_t classCount = 0;
    uint16_t classHeight[kMaxClasses] = {};
    uint32_t layerBytes = 0;
    uint32_t classLevels = 0;
    uint32_t layerOffset = 0;
    // The edge section (#309, version 4): the edgeless TLUT, then a count a layer, then the segments. 0 if none.
    uint32_t edgeOffset = 0;
    std::vector<uint32_t> edgeFirst; // each layer's first segment index, plus one past the last
    float edgeFix = 1.0f;            // EmitEdges' vertex units an image pixel (fixed for the map)

    // EmitEdges' JOINS (#309 look 1), worked out per layer from its segments alone, on the layer's first draw.
    // butt[2 i + end]: that end meets another segment, so it ends flat at the point instead of square-capped.
    // A join is a point where two or more segments END and no segment runs through: EmitEdges fills its outside
    // with a mitre (or a bevel past 90 degrees of turn). Where a segment runs through the point, its own quad
    // covers the joint and there is no join.
    struct EdgeJoin {
        uint16_t x, y;
        uint8_t n;
        uint16_t seg[8];
        uint8_t end[8];
    };
    struct EdgeTopo {
        bool built = false;
        std::vector<uint8_t> butt;
        std::vector<EdgeJoin> joins;
        int32_t vtxBound = 0;  // the most vertices EmitEdges puts down on this layer
        int32_t wordBound = 0; // the most Gfx words, the two matrix loads included
    };
    mutable std::vector<EdgeTopo> topo;

    const uint8_t* Tlut() const { return bytes.data() + kHeaderBytes; }
    const uint8_t* EdgelessTlut() const { return bytes.data() + edgeOffset; }
    int32_t EdgeCount(int32_t k) const {
        return edgeFirst.empty() ? 0 : (int32_t)(edgeFirst[k + 1] - edgeFirst[k]);
    }
    // Segment i of layer k: x0, y0, x1, y1 in image pixels.
    void Edge(int32_t k, int32_t i, uint16_t out[4]) const {
        const size_t at = (size_t)edgeOffset + kTlutBytes + 4 * (size_t)info.layers + 8 * (size_t)(edgeFirst[k] + i);
        std::memcpy(out, bytes.data() + at, 8); // little-endian, as the game runs
    }
    const uint8_t* Layer(int32_t k) const { return bytes.data() + layerOffset + (size_t)k * layerBytes; }
    const uint8_t* Classes(int32_t level) const {
        return bytes.data() + layerOffset + (size_t)info.layers * layerBytes +
               (size_t)level * info.widthTiles * info.depthTiles;
    }
};

std::map<int16_t, std::unique_ptr<LoadedMap>> sMaps;

// #313's detail test: the export can write VARIANTS of a map beside it, `worldmap/<SCENE>.<variant>.bin`, each the
// same picture with more detail baked in (props, ground colour). `worldmap detail <variant>` reads that file instead
// of the main one, for every scene, until set back; not saved. A switch never frees the map it replaces: Fast3D
// keys a texture by its address, and a new map allocated where the old one was would draw the old one's cached
// blocks; and this tick's display list still points into it. So a replaced map is kept for the session (a test
// knob: a few switches at most).
std::string sDetail;
std::vector<std::unique_ptr<LoadedMap>> sRetiredMaps;

std::string PathFor(const WorldScene& scene, const std::string& detail) {
    std::string path = scene.path;
    if (!detail.empty() && path.size() > 4) {
        path = path.substr(0, path.size() - 4) + "." + detail + ".bin";
    }
    return path;
}

template <typename T> T ReadLe(const std::vector<uint8_t>& b, size_t at) {
    T v;
    std::memcpy(&v, b.data() + at, sizeof(T)); // the game runs little-endian, as the file is written
    return v;
}

std::unique_ptr<LoadedMap> Load(const WorldScene& scene) {
    auto out = std::make_unique<LoadedMap>();
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    // The detail setting is global (#313): a scene whose export wrote no such variant draws its main file.
    std::string path = PathFor(scene, sDetail);
    if (!sDetail.empty() && !archives->HasFile(path)) {
        path = scene.path;
    }
    if (!archives->HasFile(path)) {
        out->problem = "no_map";
        return out;
    }
    auto file = archives->LoadFile(path);
    if (file == nullptr || file->Buffer == nullptr) {
        out->problem = "bad_file";
        return out;
    }
    out->bytes.assign(file->Buffer->begin() + file->BufferOffset, file->Buffer->end());
    const std::vector<uint8_t>& b = out->bytes;
    const uint16_t version = b.size() >= 6 ? ReadLe<uint16_t>(b, 4) : 0;
    if (b.size() < kHeaderBytes + kTlutBytes || std::memcmp(b.data(), "RSWM", 4) != 0 ||
        (version != kVersion && version != kVersionNoEdges)) {
        out->problem = "bad_file";
        return out;
    }
    RsWorldMapInfo& i = out->info;
    i.version = version;
    i.sceneId = scene.sceneId;
    out->layerOffset = ReadLe<uint16_t>(b, 6);
    i.pxPerTile = (uint8_t)ReadLe<uint16_t>(b, 8);
    i.layers = (uint8_t)ReadLe<uint16_t>(b, 10);
    i.widthTiles = ReadLe<uint16_t>(b, 12);
    i.depthTiles = ReadLe<uint16_t>(b, 14);
    i.widthPx = ReadLe<uint16_t>(b, 16);
    i.heightPx = ReadLe<uint16_t>(b, 18);
    i.minRsX = ReadLe<int32_t>(b, 20);
    i.minRsY = ReadLe<int32_t>(b, 24);
    i.originRsX = ReadLe<int32_t>(b, 28);
    i.originRsY = ReadLe<int32_t>(b, 32);
    out->unitsPerTile = ReadLe<uint16_t>(b, 36);
    out->classCount = std::min<uint16_t>(ReadLe<uint16_t>(b, 38), kMaxClasses);
    for (int32_t c = 0; c < out->classCount; c++) {
        out->classHeight[c] = ReadLe<uint16_t>(b, 40 + 2 * c);
    }
    out->layerBytes = ReadLe<uint32_t>(b, 48);
    out->classLevels = ReadLe<uint32_t>(b, 52);
    i.fileBytes = (uint32_t)b.size();
    const size_t want = (size_t)out->layerOffset + (size_t)i.layers * out->layerBytes +
                        (size_t)out->classLevels * i.widthTiles * i.depthTiles;
    i.storeys = (uint8_t)ReadLe<uint16_t>(b, 56);
    if (i.storeys == 0 || i.layers != 2 * i.storeys - 1 || i.pxPerTile == 0 || out->unitsPerTile == 0 || (i.widthPx % 2) != 0 ||
        out->layerBytes != (uint32_t)i.widthPx * i.heightPx / 2 || i.widthPx != i.widthTiles * i.pxPerTile ||
        i.heightPx != i.depthTiles * i.pxPerTile || out->layerOffset < kHeaderBytes + kTlutBytes ||
        b.size() < want) {
        out->problem = "bad_file";
        return out;
    }
    // The cross-check the design promised: the archive and the built C both came from an export, and if
    // they disagree on where the world sits, one of them is stale. A map drawn at the wrong offset would
    // put Link on the wrong pixel without anything looking broken, so it is refused instead.
    if (i.originRsX != scene.originRsX || i.originRsY != scene.originRsY) {
        out->problem = "stale_archive";
        return out;
    }
    // The wall segments (#309): every count and the segments themselves inside the file, after the classes.
    if (version >= kVersion) {
        out->edgeOffset = ReadLe<uint32_t>(b, 60);
        const size_t counts = (size_t)out->edgeOffset + kTlutBytes;
        if (out->edgeOffset < want || counts + 4 * (size_t)i.layers > b.size()) {
            out->problem = "bad_file";
            return out;
        }
        out->edgeFirst.assign(1, 0);
        for (int32_t k = 0; k < i.layers; k++) {
            out->edgeFirst.push_back(out->edgeFirst.back() + ReadLe<uint32_t>(b, counts + 4 * k));
        }
        if (counts + 4 * (size_t)i.layers + 8 * (size_t)out->edgeFirst.back() > b.size()) {
            out->problem = "bad_file";
            return out;
        }
        // As fine as an s16 allows across the whole image, with room for a line's caps past its edges.
        out->edgeFix = std::clamp(std::floor(30000.0f / (float)std::max(i.widthPx, i.heightPx)), 1.0f, 64.0f);
    }
    SPDLOG_INFO("[worldmap] {}: {}x{} px, {} layers, {} bytes, version {}, {} wall segments", path, i.widthPx,
                i.heightPx, i.layers, i.fileBytes, version, out->edgeFirst.empty() ? 0u : out->edgeFirst.back());
    return out;
}

const LoadedMap* MapOf(int16_t sceneId) {
    auto it = sMaps.find(sceneId);
    if (it == sMaps.end()) {
        const WorldScene* scene = SceneOf(sceneId);
        if (scene == nullptr) {
            return nullptr;
        }
        it = sMaps.emplace(sceneId, Load(*scene)).first;
    }
    return it->second.get();
}

const LoadedMap* UsableMap(int16_t sceneId) {
    const LoadedMap* m = MapOf(sceneId);
    return m != nullptr && m->problem.empty() ? m : nullptr;
}

// --- Link's storey ----------------------------------------------------------------------------------
int8_t sLinkStorey = 0;
int16_t sLinkStoreyScene = -1;
constexpr float kStoreyTolerance = 20.0f;

// The level whose floor is nearest `y` on RS tile (tx, ty) of the grid, within the tolerance, or -1.
int32_t StoreyAt(const LoadedMap& m, int32_t tx, int32_t ty, float y) {
    if (tx < 0 || ty < 0 || tx >= m.info.widthTiles || ty >= m.info.depthTiles) {
        return -1;
    }
    float base = 0.0f;
    int32_t best = -1;
    float bestDist = kStoreyTolerance;
    for (int32_t k = 0; k < m.info.storeys; k++) {
        const float d = std::fabs(y - base);
        if (d <= bestDist) {
            best = k;
            bestDist = d;
        }
        if (k < (int32_t)m.classLevels) {
            const uint8_t c = m.Classes(k)[(size_t)ty * m.info.widthTiles + tx];
            base += (float)(c < m.classCount ? m.classHeight[c] : m.classHeight[0]);
        }
    }
    return best;
}

void OnPlayerUpdateWorldMap() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    const int16_t scene = play->sceneNum;
    if (scene != sLinkStoreyScene) {
        sLinkStoreyScene = scene;
        sLinkStorey = 0;
    }
    const LoadedMap* m = UsableMap(scene);
    Player* player = GET_PLAYER(play);
    if (m == nullptr || player == nullptr) {
        return;
    }
    // On the ground only: airborne, swimming or mid-flight on a staircase keeps the last storey.
    if (!(player->actor.bgCheckFlags & 1) || (player->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
        return;
    }
    float px = 0.0f, py = 0.0f;
    RsWorldMap_WorldToPixel(scene, player->actor.world.pos.x, player->actor.world.pos.z, &px, &py);
    const int32_t tx = (int32_t)std::floor(px / m->info.pxPerTile);
    const int32_t ty = m->info.depthTiles - 1 - (int32_t)std::floor(py / m->info.pxPerTile);
    const int32_t k = StoreyAt(*m, tx, ty, player->actor.world.pos.y);
    if (k >= 0) {
        sLinkStorey = (int8_t)k;
    }
}

int32_t LayerOf(const LoadedMap& m) {
    switch (RsWorldMap_StoreyMode()) {
        // The layers: storeys alone (0..S-1), then the stacks 0..k for k = 1..S-1 (S..2S-2). Stack 0 is
        // alone 0, so it is not stored.
        case RS_WORLD_MAP_STOREY_ALL:
            return m.info.layers - 1; // the top stack
        case RS_WORLD_MAP_STOREY_CURRENT:
            return std::clamp<int32_t>(sLinkStorey, 0, m.info.storeys - 1);
        case RS_WORLD_MAP_STOREY_BELOW: {
            const int32_t k = std::clamp<int32_t>(sLinkStorey, 0, m.info.storeys - 1);
            return k == 0 ? 0 : m.info.storeys + k - 1;
        }
        default:
            return 0;
    }
}

// --- blocks -----------------------------------------------------------------------------------------
int16_t sBlockW = 60;
int16_t sBlockH = 60;
constexpr int16_t kApronX = 2; // keeps every 4-bit load on a byte boundary
constexpr int16_t kApronY = 1;
constexpr int32_t kWordsPerBlock = 10; // LoadTextureTile_4b (7) + SetTileSize + quad + a vertex load amortised
constexpr int32_t kWordsAround = 12;

struct Block {
    int16_t bx, by, bw, bh; // the block, in image pixels
    int16_t lx, ly, lw, lh; // the load with its apron, clipped to the image
};

// Calls fn for every block the (pixel-rounded) rect [x0, x1) x [y0, y1) touches.
template <typename F> int32_t VisitBlocks(const LoadedMap& m, int32_t x0, int32_t y0, int32_t x1, int32_t y1, F&& fn) {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min<int32_t>(x1, m.info.widthPx);
    y1 = std::min<int32_t>(y1, m.info.heightPx);
    int32_t n = 0;
    for (int32_t by = (y0 / sBlockH) * sBlockH; by < y1; by += sBlockH) {
        for (int32_t bx = (x0 / sBlockW) * sBlockW; bx < x1; bx += sBlockW) {
            Block b;
            b.bx = (int16_t)bx;
            b.by = (int16_t)by;
            b.bw = (int16_t)std::min<int32_t>(sBlockW, m.info.widthPx - bx);
            b.bh = (int16_t)std::min<int32_t>(sBlockH, m.info.heightPx - by);
            b.lx = (int16_t)std::max(bx - kApronX, 0);
            b.ly = (int16_t)std::max(by - kApronY, 0);
            b.lw = (int16_t)(std::min<int32_t>(bx + b.bw + kApronX, m.info.widthPx) - b.lx);
            b.lh = (int16_t)(std::min<int32_t>(by + b.bh + kApronY, m.info.heightPx) - b.ly);
            fn(b);
            n++;
        }
    }
    return n;
}

// The TLUT, once per draw, into palette 0: the edgeless one when asked and the file has it (#309).
Gfx* LoadTlut(Gfx* p, const LoadedMap& m, uint32_t flags = 0) {
    gDPSetTextureLUT(p++, G_TT_RGBA16);
    gDPLoadTLUT_pal16(p++, 0, (flags & RS_WORLD_MAP_EDGELESS) && m.edgeOffset != 0 ? m.EdgelessTlut() : m.Tlut());
    return p;
}

// One block into the render tile. Texture coordinates are relative to the load's own start: the render
// tile's size is reset to start at 0, so an S10.5 coordinate never has to reach the image's far edge.
Gfx* LoadBlock(Gfx* p, const LoadedMap& m, const uint8_t* layer, const Block& b) {
    gDPLoadTextureTile_4b(p++, layer, G_IM_FMT_CI, m.info.widthPx, m.info.heightPx, b.lx, b.ly, b.lx + b.lw - 1,
                          b.ly + b.lh - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                          G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gDPSetTileSize(p++, G_TX_RENDERTILE, 0, 0, (b.lw - 1) << G_TEXTURE_IMAGE_FRAC, (b.lh - 1) << G_TEXTURE_IMAGE_FRAC);
    return p;
}

int32_t sDebugView = 0;
int32_t sDebugBlocks = 0;
int32_t sDebugWords = 0;

} // namespace

// --- the C interface ----------------------------------------------------------------------------------

extern "C" bool RsWorldMap_Info(int16_t sceneId, RsWorldMapInfo* out) {
    const LoadedMap* m = UsableMap(sceneId);
    if (m == nullptr) {
        return false;
    }
    if (out != nullptr) {
        *out = m->info;
        out->layer = (uint8_t)LayerOf(*m);
        out->edges = (uint16_t)std::min<int32_t>(m->EdgeCount(out->layer), UINT16_MAX);
    }
    return true;
}

extern "C" const char* RsWorldMap_Problem(int16_t sceneId) {
    const LoadedMap* m = MapOf(sceneId);
    return m == nullptr ? "no_scene" : m->problem.c_str();
}

extern "C" bool RsWorldMap_WorldToPixel(int16_t sceneId, float x, float z, float* px, float* py) {
    const LoadedMap* m = UsableMap(sceneId);
    if (m == nullptr) {
        return false;
    }
    const RsWorldMapInfo& i = m->info;
    // The origin tile's south-west corner is OoT (0, 0); RS east is +X and RS north is -Z.
    const float rsX = (float)i.originRsX + x / m->unitsPerTile;
    const float rsY = (float)i.originRsY - z / m->unitsPerTile;
    if (px != nullptr) {
        *px = (rsX - (float)i.minRsX) * i.pxPerTile;
    }
    if (py != nullptr) {
        *py = ((float)(i.minRsY + i.depthTiles) - rsY) * i.pxPerTile;
    }
    return true;
}

extern "C" bool RsWorldMap_PixelToWorld(int16_t sceneId, float px, float py, float* x, float* z) {
    const LoadedMap* m = UsableMap(sceneId);
    if (m == nullptr) {
        return false;
    }
    const RsWorldMapInfo& i = m->info;
    const float rsX = (float)i.minRsX + px / i.pxPerTile;
    const float rsY = (float)(i.minRsY + i.depthTiles) - py / i.pxPerTile;
    if (x != nullptr) {
        *x = (rsX - (float)i.originRsX) * m->unitsPerTile;
    }
    if (z != nullptr) {
        *z = ((float)i.originRsY - rsY) * m->unitsPerTile;
    }
    return true;
}

extern "C" void RsWorldMap_BlockSize(int16_t* w, int16_t* h) {
    if (w != nullptr) {
        *w = sBlockW;
    }
    if (h != nullptr) {
        *h = sBlockH;
    }
}

extern "C" bool RsWorldMap_SetDetail(int16_t sceneId, const char* variant) {
    std::string want = variant != nullptr ? variant : "";
    if (want == "none") {
        want.clear();
    }
    for (char c : want) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    // A variant must exist for the scene asked about; going back to the main file is always allowed.
    if (!want.empty()) {
        const WorldScene* scene = SceneOf(sceneId);
        auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
        if (scene == nullptr || !archives->HasFile(PathFor(*scene, want))) {
            return false;
        }
    }
    if (want != sDetail) {
        sDetail = want;
        for (auto& it : sMaps) {
            sRetiredMaps.push_back(std::move(it.second));
        }
        sMaps.clear();
    }
    return true;
}

extern "C" const char* RsWorldMap_Detail(void) {
    return sDetail.empty() ? "none" : sDetail.c_str();
}

extern "C" bool RsWorldMap_SetBlockSize(int16_t w, int16_t h) {
    if (w < 8 || h < 8 || (w % 2) != 0 || (int32_t)(w + 2 * kApronX) * (h + 2 * kApronY) > 4096) {
        return false;
    }
    sBlockW = w;
    sBlockH = h;
    return true;
}

extern "C" int32_t RsWorldMap_GfxWords(float srcW, float srcH) {
    const int32_t across = (int32_t)std::ceil(std::max(srcW, 0.0f) / sBlockW) + 1;
    const int32_t down = (int32_t)std::ceil(std::max(srcH, 0.0f) / sBlockH) + 1;
    return across * down * kWordsPerBlock + kWordsAround;
}

extern "C" int32_t RsWorldMap_EmitBlocks(GraphicsContext* gfxCtx, Gfx** gfx, int16_t sceneId, float srcX, float srcY,
                                         float srcW, float srcH) {
    return RsWorldMap_EmitBlocksEx(gfxCtx, gfx, sceneId, srcX, srcY, srcW, srcH, 0);
}

extern "C" int32_t RsWorldMap_EmitBlocksEx(GraphicsContext* gfxCtx, Gfx** gfx, int16_t sceneId, float srcX, float srcY,
                                           float srcW, float srcH, uint32_t flags) {
    const LoadedMap* m = UsableMap(sceneId);
    if (m == nullptr || gfx == nullptr || gfxCtx == nullptr || srcW <= 0.0f || srcH <= 0.0f) {
        return 0;
    }
    const int32_t x0 = (int32_t)std::floor(srcX);
    const int32_t y0 = (int32_t)std::floor(srcY);
    const int32_t x1 = (int32_t)std::ceil(srcX + srcW);
    const int32_t y1 = (int32_t)std::ceil(srcY + srcH);
    const uint8_t* layer = m->Layer(LayerOf(*m));
    // Count first, so the vertices are one allocation.
    const int32_t count = VisitBlocks(*m, x0, y0, x1, y1, [](const Block&) {});
    if (count == 0) {
        return 0;
    }
    Vtx* vtx = (Vtx*)Graph_Alloc(gfxCtx, (size_t)count * 4 * sizeof(Vtx));
    Gfx* p = *gfx;
    p = LoadTlut(p, *m, flags);
    int32_t i = 0;
    VisitBlocks(*m, x0, y0, x1, y1, [&](const Block& b) {
        // The part of the block inside the rect: whole pixels, so neighbours share exact edges.
        const int16_t qx0 = (int16_t)std::max<int32_t>(b.bx, x0);
        const int16_t qy0 = (int16_t)std::max<int32_t>(b.by, y0);
        const int16_t qx1 = (int16_t)std::min<int32_t>(b.bx + b.bw, x1);
        const int16_t qy1 = (int16_t)std::min<int32_t>(b.by + b.bh, y1);
        Vtx* v = &vtx[i * 4];
        const int16_t xs[4] = { qx0, qx1, qx0, qx1 };
        const int16_t ys[4] = { qy0, qy0, qy1, qy1 };
        for (int32_t c = 0; c < 4; c++) {
            v[c].v.ob[0] = xs[c];
            v[c].v.ob[1] = (int16_t)-ys[c];
            v[c].v.ob[2] = 0;
            v[c].v.flag = 0;
            v[c].v.tc[0] = (int16_t)((xs[c] - b.lx) << 5);
            v[c].v.tc[1] = (int16_t)((ys[c] - b.ly) << 5);
            v[c].v.cn[0] = v[c].v.cn[1] = v[c].v.cn[2] = v[c].v.cn[3] = 255;
        }
        // gSPVertex caps at 64 vertices: sixteen blocks a load.
        if ((i % 16) == 0) {
            gSPVertex(p++, (uintptr_t)&vtx[i * 4], std::min(count - i, 16) * 4, 0);
        }
        p = LoadBlock(p, *m, layer, b);
        const int16_t base = (int16_t)(4 * (i % 16));
        gSP1Quadrangle(p++, base, base + 2, base + 3, base + 1, 0);
        i++;
    });
    gDPPipeSync(p++);
    gDPSetTextureLUT(p++, G_TT_NONE);
    *gfx = p;
    return count;
}

extern "C" int32_t RsWorldMap_DrawRect(GraphicsContext* gfxCtx, Gfx** gfx, int16_t sceneId, float srcX, float srcY,
                                       float srcW, float srcH, float dstX, float dstY, float dstW, float dstH) {
    // The same Matrix_* ops every call, map or no map: interpolation matches them by position.
    const float sx = srcW > 0.0f ? dstW / srcW : 0.0f;
    const float sy = srcH > 0.0f ? dstH / srcH : 0.0f;
    Matrix_Push();
    // Geometry is (px, -py): px = srcX lands on dstX, py = srcY on dstY, growing down (y-up space).
    Matrix_Translate(dstX - srcX * sx, dstY + srcY * sy, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(sx, sy, 1.0f, MTXMODE_APPLY);
    Mtx* placed = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
    Matrix_Pop();
    Mtx* restored = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
    Gfx* p = *gfx;
    gSPMatrix(p++, placed, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    const int32_t n = RsWorldMap_EmitBlocks(gfxCtx, &p, sceneId, srcX, srcY, srcW, srcH);
    gSPMatrix(p++, restored, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    *gfx = p;
    return n;
}

int32_t RsWorldMap_DrawRect(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, int16_t sceneId, float srcX, float srcY,
                            float srcW, float srcH, float dstX, float dstY, float dstW, float dstH) {
    const size_t at = dl.size();
    dl.resize(at + (size_t)RsWorldMap_GfxWords(srcW, srcH) + 2);
    Gfx* p = &dl[at];
    const int32_t n = RsWorldMap_DrawRect(gfxCtx, &p, sceneId, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH);
    dl.resize((size_t)(p - dl.data()));
    return n;
}

extern "C" int32_t RsWorldMap_ForEachBlock(Gfx** gfx, int16_t sceneId, float srcX, float srcY, float srcW, float srcH,
                                           RsWorldMapBlockFn fn, void* user) {
    return RsWorldMap_ForEachBlockEx(gfx, sceneId, srcX, srcY, srcW, srcH, fn, user, 0);
}

extern "C" int32_t RsWorldMap_ForEachBlockEx(Gfx** gfx, int16_t sceneId, float srcX, float srcY, float srcW,
                                             float srcH, RsWorldMapBlockFn fn, void* user, uint32_t flags) {
    const LoadedMap* m = UsableMap(sceneId);
    if (m == nullptr || gfx == nullptr || fn == nullptr || srcW <= 0.0f || srcH <= 0.0f) {
        return 0;
    }
    const uint8_t* layer = m->Layer(LayerOf(*m));
    *gfx = LoadTlut(*gfx, *m, flags);
    const int32_t n = VisitBlocks(*m, (int32_t)std::floor(srcX), (int32_t)std::floor(srcY),
                                  (int32_t)std::ceil(srcX + srcW), (int32_t)std::ceil(srcY + srcH),
                                  [&](const Block& b) {
                                      *gfx = LoadBlock(*gfx, *m, layer, b);
                                      fn(gfx, b.bx, b.by, b.bw, b.bh, b.lx, b.ly, user);
                                  });
    Gfx* p = *gfx;
    gDPPipeSync(p++);
    gDPSetTextureLUT(p++, G_TT_NONE);
    *gfx = p;
    return n;
}

// --- the walls as geometry (#309) -----------------------------------------------------------------------
namespace {

// Liang-Barsky: segment (x0, y0)-(x1, y1) clipped to [rx0, rx1] x [ry0, ry1] in place. False if none is left.
bool ClipSegment(float& x0, float& y0, float& x1, float& y1, float rx0, float ry0, float rx1, float ry1) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    float t0 = 0.0f;
    float t1 = 1.0f;
    const float p[4] = { -dx, dx, -dy, dy };
    const float q[4] = { x0 - rx0, rx1 - x0, y0 - ry0, ry1 - y0 };
    for (int32_t i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) {
                return false; // parallel to this edge and outside it
            }
            continue;
        }
        const float t = q[i] / p[i];
        if (p[i] < 0.0f) {
            t0 = std::max(t0, t);
        } else {
            t1 = std::min(t1, t);
        }
        if (t0 > t1) {
            return false;
        }
    }
    x1 = x0 + dx * t1;
    y1 = y0 + dy * t1;
    x0 = x0 + dx * t0;
    y0 = y0 + dy * t0;
    return true;
}

// Layer k's joins (LoadedMap::EdgeTopo), built on first use.
const LoadedMap::EdgeTopo& TopoOf(const LoadedMap& m, int32_t k) {
    if ((int32_t)m.topo.size() < (int32_t)m.info.layers) {
        m.topo.resize(m.info.layers);
    }
    LoadedMap::EdgeTopo& t = m.topo[k];
    if (t.built) {
        return t;
    }
    t.built = true;
    const int32_t n = m.EdgeCount(k);
    std::vector<int32_t> s((size_t)n * 4);
    for (int32_t i = 0; i < n; i++) {
        uint16_t e[4];
        m.Edge(k, i, e);
        for (int32_t c = 0; c < 4; c++) {
            s[(size_t)i * 4 + c] = e[c];
        }
    }
    t.butt.assign((size_t)n * 2, 0);
    std::map<uint32_t, std::vector<std::pair<uint16_t, uint8_t>>> ends;
    for (int32_t i = 0; i < n; i++) {
        for (uint8_t end = 0; end < 2; end++) {
            const uint32_t key = ((uint32_t)s[(size_t)i * 4 + 2 * end] << 16) | (uint32_t)s[(size_t)i * 4 + 2 * end + 1];
            ends[key].push_back({ (uint16_t)i, end });
        }
    }
    int32_t armSum = 0;
    for (const auto& [key, list] : ends) {
        const int32_t px = (int32_t)(key >> 16);
        const int32_t py = (int32_t)(key & 0xFFFF);
        bool through = false; // a segment runs through the point (not ending there)
        for (int32_t j = 0; j < n && !through; j++) {
            const int32_t* e = &s[(size_t)j * 4];
            const int32_t dx = e[2] - e[0], dy = e[3] - e[1], wx = px - e[0], wy = py - e[1];
            const int32_t dot = dx * wx + dy * wy;
            through = dx * wy - dy * wx == 0 && dot > 0 && dot < dx * dx + dy * dy;
        }
        if (list.size() == 1 && !through) {
            continue; // a free end: square cap
        }
        for (const auto& e : list) {
            t.butt[(size_t)e.first * 2 + e.second] = 1;
        }
        if (through || list.size() < 2) {
            continue;
        }
        LoadedMap::EdgeJoin j{};
        j.x = (uint16_t)px;
        j.y = (uint16_t)py;
        j.n = (uint8_t)std::min<size_t>(list.size(), 8);
        for (int32_t a = 0; a < j.n; a++) {
            j.seg[a] = list[a].first;
            j.end[a] = list[a].second;
        }
        t.joins.push_back(j);
        t.vtxBound += 1 + 3 * j.n; // the point, two corners an arm, a mitre tip a gap
        armSum += 3 * j.n;         // its fan's triangles (hull - 2) are fewer than this
    }
    t.vtxBound += 4 * n;
    const int32_t tris = 2 * n + armSum;
    const int32_t primitives = n + (int32_t)t.joins.size();
    // Triangles go two a word; a vertex load per primitive at worst; the two matrix loads.
    t.wordBound = tris / 2 + 1 + primitives + 4;
    return t;
}

// Calls fn(x0, y0, x1, y1) for every segment of the current layer touching the rect, clipped to it.
template <typename F>
int32_t VisitEdges(const LoadedMap& m, float srcX, float srcY, float srcW, float srcH, F&& fn) {
    if (srcW <= 0.0f || srcH <= 0.0f) {
        return 0;
    }
    const int32_t k = LayerOf(m);
    const int32_t count = m.EdgeCount(k);
    int32_t n = 0;
    for (int32_t i = 0; i < count; i++) {
        uint16_t e[4];
        m.Edge(k, i, e);
        float x0 = e[0], y0 = e[1], x1 = e[2], y1 = e[3];
        if (ClipSegment(x0, y0, x1, y1, srcX, srcY, srcX + srcW, srcY + srcH) && (x0 != x1 || y0 != y1)) {
            fn(x0, y0, x1, y1);
            n++;
        }
    }
    return n;
}

} // namespace

extern "C" bool RsWorldMap_WallColour(int16_t sceneId, uint8_t rgb[3]) {
    const LoadedMap* m = UsableMap(sceneId);
    if (m == nullptr || rgb == nullptr) {
        return false;
    }
    // TLUT entry 2, big-endian RGBA5551; widen each 5-bit channel to 8 the way the RDP does.
    const uint8_t* t = m->Tlut() + 2 * 2;
    const uint16_t c = (uint16_t)((t[0] << 8) | t[1]);
    const uint8_t r5 = (c >> 11) & 31, g5 = (c >> 6) & 31, b5 = (c >> 1) & 31;
    rgb[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
    rgb[1] = (uint8_t)((g5 << 3) | (g5 >> 2));
    rgb[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    return true;
}

extern "C" int32_t RsWorldMap_ForEachEdge(int16_t sceneId, float srcX, float srcY, float srcW, float srcH,
                                          RsWorldMapEdgeFn fn, void* user) {
    const LoadedMap* m = UsableMap(sceneId);
    if (m == nullptr || fn == nullptr) {
        return 0;
    }
    return VisitEdges(*m, srcX, srcY, srcW, srcH, [&](float x0, float y0, float x1, float y1) { fn(x0, y0, x1, y1, user); });
}

extern "C" int32_t RsWorldMap_EdgeGfxWords(int16_t sceneId) {
    const LoadedMap* m = UsableMap(sceneId);
    if (m == nullptr) {
        return 0;
    }
    return TopoOf(*m, LayerOf(*m)).wordBound;
}

namespace {
RsWorldMapEdgeStats sEdgeStats{};
} // namespace

extern "C" RsWorldMapEdgeStats RsWorldMap_EdgeStats(void) {
    return sEdgeStats;
}

extern "C" int32_t RsWorldMap_EmitEdges(GraphicsContext* gfxCtx, Gfx** gfx, int16_t sceneId, float srcX, float srcY,
                                        float srcW, float srcH, float halfWidthPx) {
    return RsWorldMap_EmitEdgesEx(gfxCtx, gfx, sceneId, srcX, srcY, srcW, srcH, halfWidthPx, 0u);
}

extern "C" int32_t RsWorldMap_EmitEdgesEx(GraphicsContext* gfxCtx, Gfx** gfx, int16_t sceneId, float srcX, float srcY,
                                          float srcW, float srcH, float halfWidthPx, uint32_t flags) {
    sEdgeStats = {};
    const LoadedMap* m = UsableMap(sceneId);
    const float fix = m != nullptr ? m->edgeFix : 1.0f;

    // The same Matrix_* ops every call (WorldMap.h): the vertices are image pixels x fix under 1/fix.
    Matrix_Push();
    Matrix_Scale(1.0f / fix, 1.0f / fix, 1.0f, MTXMODE_APPLY);
    Mtx* fine = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);
    Matrix_Pop();
    Mtx* restored = Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__);

    // RS_WORLD_MAP_EDGES_SNAP: where a vertex lands on the render target, worked out from what the GPU will
    // actually multiply by. An Mtx is s15.16 fixed point, TRUNCATED from the float stack (guMtxF2L), and with
    // vertices up to ~18,500 units under a ~0.02 scale that truncation alone moves a vertex by up to a render
    // pixel, differently at every pan and zoom - enough to put both edges of a snapped 1 px line on pixel centres,
    // so it drew nothing (#309 look 2: walls vanishing until a pan). So read the emitted Mtx back (guMtxL2F), and
    // use the projection the rs menu loads, guOrtho(+-160, +-120) - itself truncated, 1/160 to 409/65536. Fast3D:
    // clip = ob . MV . P (row vectors), x widened by (4/3) / (W/H) about the centre, viewport 0..W x 0..H, so
    // rx = W/2 + clipX * 2H/3 and ry = H/2 - clipY * H/2.
    bool snap = false;
    bool odd = false;
    long widthPx = 0;
    float sx = 0.0f, sy = 0.0f, ox = 0.0f, oy = 0.0f;
    if ((flags & RS_WORLD_MAP_EDGES_SNAP) != 0 && m != nullptr && halfWidthPx > 0.0f) {
        float mv[4][4];
        guMtxL2F(mv, fine);
        const float W = (float)OTRGetGameRenderWidth();
        const float H = (float)OTRGetGameRenderHeight();
        const float px = (float)(int32_t)(2.0f / (float)SCREEN_WIDTH * 65536.0f) / 65536.0f;
        const float py = (float)(int32_t)(2.0f / (float)SCREEN_HEIGHT * 65536.0f) / 65536.0f;
        const float kx = px * 2.0f * H / 3.0f; // render px an N64 unit, x
        const float ky = py * H / 2.0f;        // and y
        // A vertex is (x fix, -y fix) for image pixel (x, y).
        sx = mv[0][0] * fix * kx;
        sy = mv[1][1] * fix * ky;
        const bool aligned = std::fabs(sx) > 1e-4f && std::fabs(sy) > 1e-4f &&
                             std::fabs(mv[1][0] * fix * kx) < 1e-3f * std::fabs(sx) &&
                             std::fabs(mv[0][1] * fix * ky) < 1e-3f * std::fabs(sy);
        if (aligned) {
            snap = true;
            ox = W * 0.5f + mv[3][0] * kx;
            oy = H * 0.5f - mv[3][1] * ky;
            widthPx = std::max(1L, std::lround(2.0f * halfWidthPx * std::fabs(sy)));
            odd = (widthPx & 1) != 0;
        }
    }
    // A line a whole number of pixels wide covers the same pixels everywhere when its centre sits on a pixel centre
    // (odd widths) or a pixel edge (even ones). Every endpoint moves there, so the lines and their joins move as one.
    auto snapTo = [&](float v, float o, float sc) {
        if (!snap) {
            return v;
        }
        const float r = o + v * sc;
        const float r2 = odd ? std::floor(r) + 0.5f : std::round(r);
        return (r2 - o) / sc;
    };
    if (m == nullptr || gfx == nullptr || halfWidthPx <= 0.0f || srcW <= 0.0f || srcH <= 0.0f) {
        return 0;
    }
    const int32_t k = LayerOf(*m);
    const int32_t n = m->EdgeCount(k);
    if (n == 0) {
        return 0;
    }
    const LoadedMap::EdgeTopo& topo = TopoOf(*m, k);
    sEdgeStats.snapped = snap;
    sEdgeStats.widthPx = snap ? (float)widthPx : 0.0f;

    std::vector<float> pt((size_t)n * 4);
    for (int32_t i = 0; i < n; i++) {
        uint16_t e[4];
        m->Edge(k, i, e);
        pt[(size_t)i * 4 + 0] = snapTo(e[0], ox, sx);
        pt[(size_t)i * 4 + 1] = snapTo(e[1], oy, sy);
        pt[(size_t)i * 4 + 2] = snapTo(e[2], ox, sx);
        pt[(size_t)i * 4 + 3] = snapTo(e[3], oy, sy);
    }
    const float hw = halfWidthPx;
    // Snapped, a cardinal line's half width is set per axis from the matrix, so both are exactly widthPx render
    // pixels whatever the two axes' scales; diagonals, caps and joins keep the caller's.
    const float hwX = snap ? (float)widthPx * 0.5f / std::fabs(sx) : hw; // a vertical line's
    const float hwY = snap ? (float)widthPx * 0.5f / std::fabs(sy) : hw; // a horizontal line's
    const float rx0 = srcX, ry0 = srcY, rx1 = srcX + srcW, ry1 = srcY + srcH;

    Vtx* vtx = (Vtx*)Graph_Alloc(gfxCtx, (size_t)topo.vtxBound * sizeof(Vtx));
    int32_t nv = 0;
    int32_t batch = 0;        // the current vertex load's first vertex
    std::vector<int32_t> pend; // its triangles, three absolute vertex indices each
    Gfx* p = *gfx;
    gSPMatrix(p++, fine, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    auto flush = [&]() {
        if (nv == batch) {
            return;
        }
        gSPVertex(p++, (uintptr_t)&vtx[batch], nv - batch, 0);
        size_t t = 0;
        for (; t + 6 <= pend.size(); t += 6) {
            gSP2Triangles(p++, pend[t] - batch, pend[t + 1] - batch, pend[t + 2] - batch, 0, pend[t + 3] - batch,
                          pend[t + 4] - batch, pend[t + 5] - batch, 0);
        }
        if (t < pend.size()) {
            gSP1Triangle(p++, pend[t] - batch, pend[t + 1] - batch, pend[t + 2] - batch, 0);
        }
        pend.clear();
        batch = nv;
    };
    auto reserve = [&](int32_t count) {
        if (nv - batch + count > 64) { // Fast3D's vertex buffer (MAX_VERTICES)
            flush();
        }
    };
    auto put = [&](float x, float y) {
        Vtx* v = &vtx[nv++];
        v->v.ob[0] = (int16_t)std::lround(x * fix);
        v->v.ob[1] = (int16_t)-std::lround(y * fix); // the geometry space is y-up
        v->v.ob[2] = 0;
        v->v.flag = 0;
        v->v.tc[0] = v->v.tc[1] = 0;
        v->v.cn[0] = v->v.cn[1] = v->v.cn[2] = v->v.cn[3] = 255;
    };

    // The segments: a quad each, clipped to the rect. An end square-caps (pushed out by the half width) only where
    // it is free; an end that meets another segment, or that the clip cut, stops flat at its point.
    for (int32_t i = 0; i < n; i++) {
        const float* e = &pt[(size_t)i * 4];
        float x0 = e[0], y0 = e[1], x1 = e[2], y1 = e[3];
        if (!ClipSegment(x0, y0, x1, y1, rx0, ry0, rx1, ry1) || (x0 == x1 && y0 == y1)) {
            continue;
        }
        const bool cap0 = x0 == e[0] && y0 == e[1] && topo.butt[(size_t)i * 2] == 0;
        const bool cap1 = x1 == e[2] && y1 == e[3] && topo.butt[(size_t)i * 2 + 1] == 0;
        const float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        const float dx = (x1 - x0) / len, dy = (y1 - y0) / len;
        const float side = x0 == x1 ? hwX : y0 == y1 ? hwY : hw; // the perpendicular half width
        const float ux = dx * hw;                                  // along, half a width long (the caps)
        const float uy = dy * hw;
        const float nx = -dy * side, ny = dx * side; // across
        const float ax = cap0 ? x0 - ux : x0, ay = cap0 ? y0 - uy : y0;
        const float bx = cap1 ? x1 + ux : x1, by = cap1 ? y1 + uy : y1;
        reserve(4);
        const int32_t b = nv;
        put(ax + nx, ay + ny);
        put(ax - nx, ay - ny);
        put(bx + nx, by + ny);
        put(bx - nx, by - ny);
        pend.insert(pend.end(), { b, b + 1, b + 3, b, b + 3, b + 2 });
        sEdgeStats.quads++;
    }

    // The joins: where segments end together, the flat ends leave a wedge open on the outside of each turn. Fill
    // the convex hull of the point, every arm's two corners and, for a turn of 90 degrees or less (the gap
    // between two arms 180-270 degrees), the mitre tip where their outer sides meet. Sharper turns stay bevelled:
    // a 45-degree turn's mitre would reach 2.6 half widths out.
    for (const LoadedMap::EdgeJoin& j : topo.joins) {
        const float px = snapTo(j.x, ox, sx), py = snapTo(j.y, oy, sy);
        if (px < rx0 || px > rx1 || py < ry0 || py > ry1) {
            continue;
        }
        struct Arm {
            float a, dx, dy;
        };
        Arm arms[8];
        int32_t c = 0;
        for (int32_t a = 0; a < j.n; a++) {
            const float* e = &pt[(size_t)j.seg[a] * 4];
            const float hx = j.end[a] == 0 ? e[0] : e[2], hy = j.end[a] == 0 ? e[1] : e[3];
            const float tx = j.end[a] == 0 ? e[2] : e[0], ty = j.end[a] == 0 ? e[3] : e[1];
            const float len = std::sqrt((tx - hx) * (tx - hx) + (ty - hy) * (ty - hy));
            if (len > 0.0f) {
                arms[c++] = { std::atan2(ty - hy, tx - hx), (tx - hx) / len, (ty - hy) / len };
            }
        }
        if (c < 2) {
            continue;
        }
        std::sort(arms, arms + c, [](const Arm& l, const Arm& r) { return l.a < r.a; });
        float hp[2 * 26];
        int32_t h = 0;
        auto add = [&](float x, float y) {
            hp[2 * h] = x;
            hp[2 * h + 1] = y;
            h++;
        };
        add(px, py);
        for (int32_t a = 0; a < c; a++) {
            add(px - arms[a].dy * hw, py + arms[a].dx * hw);
            add(px + arms[a].dy * hw, py - arms[a].dx * hw);
        }
        constexpr float kPi = 3.14159265f;
        for (int32_t a = 0; a < c; a++) {
            float gap = arms[(a + 1) % c].a - arms[a].a;
            if (a == c - 1) {
                gap += 2.0f * kPi;
            }
            if (gap > kPi + 1e-3f && gap <= 1.5f * kPi + 1e-3f) {
                const float mid = arms[a].a + gap * 0.5f;
                const float dist = hw / std::cos((gap - kPi) * 0.5f);
                add(px + std::cos(mid) * dist, py + std::sin(mid) * dist);
            }
        }
        // Andrew's monotone chain over the (at most 25) points.
        int32_t idx[26];
        for (int32_t a = 0; a < h; a++) {
            idx[a] = a;
        }
        std::sort(idx, idx + h, [&](int32_t l, int32_t r) {
            return hp[2 * l] < hp[2 * r] || (hp[2 * l] == hp[2 * r] && hp[2 * l + 1] < hp[2 * r + 1]);
        });
        auto cross = [&](int32_t o, int32_t a, int32_t b) {
            return (hp[2 * a] - hp[2 * o]) * (hp[2 * b + 1] - hp[2 * o + 1]) -
                   (hp[2 * a + 1] - hp[2 * o + 1]) * (hp[2 * b] - hp[2 * o]);
        };
        int32_t hull[52];
        int32_t hn = 0;
        for (int32_t a = 0; a < h; a++) {
            while (hn >= 2 && cross(hull[hn - 2], hull[hn - 1], idx[a]) <= 1e-9f) {
                hn--;
            }
            hull[hn++] = idx[a];
        }
        for (int32_t a = h - 2, lower = hn + 1; a >= 0; a--) {
            while (hn >= lower && cross(hull[hn - 2], hull[hn - 1], idx[a]) <= 1e-9f) {
                hn--;
            }
            hull[hn++] = idx[a];
        }
        hn--; // the last point repeats the first
        if (hn < 3) {
            continue;
        }
        reserve(hn);
        const int32_t b = nv;
        for (int32_t a = 0; a < hn; a++) {
            put(hp[2 * hull[a]], hp[2 * hull[a] + 1]);
        }
        for (int32_t a = 1; a + 1 < hn; a++) {
            pend.insert(pend.end(), { b, b + a, b + a + 1 });
        }
        sEdgeStats.joins++;
    }
    flush();
    gSPMatrix(p++, restored, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    *gfx = p;
    return sEdgeStats.quads;
}

extern "C" int8_t RsWorldMap_LinkStorey(void) {
    return gPlayState != nullptr && UsableMap(gPlayState->sceneNum) != nullptr ? sLinkStorey : (int8_t)-1;
}

extern "C" RsWorldMapStoreyMode RsWorldMap_StoreyMode(void) {
    const int32_t v = CVarGetInteger(CVAR_RS_WORLD_MAP_STOREY, RS_WORLD_MAP_STOREY_DEFAULT);
    return v >= 0 && v < RS_WORLD_MAP_STOREY_MODE_COUNT ? (RsWorldMapStoreyMode)v : RS_WORLD_MAP_STOREY_DEFAULT;
}

extern "C" void RsWorldMap_SetStoreyMode(RsWorldMapStoreyMode mode) {
    CVarSetInteger(CVAR_RS_WORLD_MAP_STOREY, (int32_t)mode);
    CVarSave();
}

extern "C" const char* RsWorldMap_StoreyModeName(RsWorldMapStoreyMode mode) {
    switch (mode) {
        case RS_WORLD_MAP_STOREY_ALL:
            return "all";
        case RS_WORLD_MAP_STOREY_CURRENT:
            return "current";
        case RS_WORLD_MAP_STOREY_BELOW:
            return "below";
        default:
            return "ground";
    }
}

extern "C" void RsWorldMap_SetDebugView(int32_t view) {
    sDebugView = view;
}

extern "C" int32_t RsWorldMap_DebugView(void) {
    return sDebugView;
}

extern "C" void RsWorldMap_DebugStats(int32_t* blocks, int32_t* words) {
    if (blocks != nullptr) {
        *blocks = sDebugBlocks;
    }
    if (words != nullptr) {
        *words = sDebugWords;
    }
}

// --- the debug view ------------------------------------------------------------------------------------
// Drawn the way a consumer will draw it: Vtx through RsWorldMap_DrawRect into a heap list, under our own
// 320x240 ortho at OnPlayDrawEnd, as RsMenu does. Not #303's or #306's look; it exists so a screenshot
// proves the image, the layers and Link's pixel.
namespace {

std::vector<Gfx>& DebugDl() {
    static std::vector<Gfx> dl; // the interpreter walks it after the hook returns
    return dl;
}

void PushQuad(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, float x, float y, float w, float h, uint8_t r, uint8_t g,
              uint8_t b) {
    Vtx* v = (Vtx*)Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
    const float xs[4] = { x, x + w, x, x + w };
    const float ys[4] = { y, y, y - h, y - h };
    for (int32_t c = 0; c < 4; c++) {
        v[c].v.ob[0] = (int16_t)std::lround(xs[c]);
        v[c].v.ob[1] = (int16_t)std::lround(ys[c]);
        v[c].v.ob[2] = 0;
        v[c].v.flag = 0;
        v[c].v.tc[0] = v[c].v.tc[1] = 0;
        v[c].v.cn[0] = v[c].v.cn[1] = v[c].v.cn[2] = v[c].v.cn[3] = 255;
    }
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF));
    dl.push_back(gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE));
    dl.push_back(gsDPSetPrimColor(0, 0, r, g, b, 255));
    dl.push_back(gsSPVertex(v, 4, 0));
    dl.push_back(gsSP1Quadrangle(0, 2, 3, 1, 0));
}

} // namespace

// File-static, not in the anonymous namespace: OPEN_DISPS redeclares FrameInterpolation_RecordOpenChild at block
// scope, and inside an anonymous namespace MSVC gives that C++ linkage (RsMenu.cpp says the same).
static void OnPlayDrawEndWorldMap() {
    PlayState* play = gPlayState;
    sDebugBlocks = 0;
    sDebugWords = 0;
    if (sDebugView == 0 || play == nullptr) {
        return;
    }
    RsWorldMapInfo info;
    if (!RsWorldMap_Info(play->sceneNum, &info)) {
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
    guOrtho(ortho, -(f32)(SCREEN_WIDTH / 2), (f32)(SCREEN_WIDTH / 2), -(f32)(SCREEN_HEIGHT / 2),
            (f32)(SCREEN_HEIGHT / 2), -1.0f, 1.0f, 1.0f);
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPViewport(vp));
    dl.push_back(gsSPMatrix(ortho, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
    dl.push_back(gsSPLoadGeometryMode(G_SHADING_SMOOTH));
    dl.push_back(gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE |
                                      G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2));

    float lx = 0.0f, ly = 0.0f;
    RsWorldMap_WorldToPixel(play->sceneNum, GET_PLAYER(play)->actor.world.pos.x, GET_PLAYER(play)->actor.world.pos.z,
                            &lx, &ly);
    // Source and destination rects. `whole`: the image fitted into a 200-unit box in the top-right
    // corner. `link`: a 96-pixel window centred on Link, at 2 units a pixel, same corner.
    float srcX, srcY, srcW, srcH, dstW, dstH;
    if (sDebugView == 1) {
        srcX = srcY = 0.0f;
        srcW = info.widthPx;
        srcH = info.heightPx;
        const float s = 200.0f / std::max(srcW, srcH);
        dstW = srcW * s;
        dstH = srcH * s;
    } else {
        srcW = srcH = 96.0f;
        srcX = lx - srcW / 2;
        srcY = ly - srcH / 2;
        dstW = dstH = 192.0f;
    }
    const float dstX = SCREEN_WIDTH / 2.0f - 8.0f - dstW;
    const float dstY = SCREEN_HEIGHT / 2.0f - 8.0f;
    PushQuad(gfxCtx, dl, dstX - 2, dstY + 2, dstW + 4, dstH + 4, 200, 160, 40); // a frame, so the edge reads

    FrameInterpolation_RecordOpenChild(&sDebugView, 0);
    Matrix_Push();
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    dl.push_back(gsSPMatrix(Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__),
                            G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
    dl.push_back(gsDPSetCombineMode(G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM));
    dl.push_back(gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
    const size_t before = dl.size();
    sDebugBlocks = RsWorldMap_DrawRect(gfxCtx, dl, play->sceneNum, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH);
    sDebugWords = (int32_t)(dl.size() - before);
    // Link's pixel: a red dot on the map, the same mapping the map used.
    const float sx = dstW / srcW;
    const float sy = dstH / srcH;
    const float dotX = dstX + (lx - srcX) * sx;
    const float dotY = dstY - (ly - srcY) * sy;
    const float dot = sDebugView == 1 ? 3.0f : 4.0f;
    if (lx >= srcX && ly >= srcY && lx <= srcX + srcW && ly <= srcY + srcH) {
        PushQuad(gfxCtx, dl, dotX - dot / 2, dotY + dot / 2, dot, dot, 255, 40, 40);
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

void RegisterWorldMapHooks() {
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateWorldMap);
    COND_HOOK(OnPlayDrawEnd, true, OnPlayDrawEndWorldMap);
}

RegisterShipInitFunc worldMapHooksInitFunc(RegisterWorldMapHooks);

} // namespace
