// RsSprites.cpp - RS interface art by token (sturdy-bassoon#297). The contract is RsSprites.h.

#include "RsSprites.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include <fast/resource/type/Texture.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "macros.h"
}

namespace {

constexpr int32_t kTokenMax = 48;
constexpr int32_t kPathLen = 96;
// A grid's dimensions are probed tile by tile; the packer's grids are 2 x 2 today. A bound so a malformed archive
// cannot make a probe loop long.
constexpr int32_t kGridMax = 8;

struct Tile {
    alignas(8) char path[kPathLen];
    int32_t x0, y0, w, h; // its place in the whole picture, texels
};

struct Entry {
    bool found = false;
    RsSpriteInfo info{};
    std::vector<Tile> tiles;
};

std::map<std::string, std::unique_ptr<Entry>>& Cache() {
    static std::map<std::string, std::unique_ptr<Entry>> cache;
    return cache;
}

bool TokenClean(const char* token) {
    if (token == nullptr || token[0] == '\0' || std::strlen(token) > (size_t)kTokenMax) {
        return false;
    }
    for (const char* c = token; *c != '\0'; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '/')) {
            return false;
        }
    }
    return true;
}

// The texture at an archive path and its size, or false. LoadResourceProcess strips "__OTR__" itself.
bool Probe(const char* path, int32_t* w, int32_t* h) {
    auto res = Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(path);
    auto tex = std::dynamic_pointer_cast<Fast::Texture>(res);
    if (tex == nullptr) {
        return false;
    }
    *w = tex->Width;
    *h = tex->Height;
    return true;
}

Entry* Resolve(const char* token) {
    auto& cache = Cache();
    auto it = cache.find(token);
    if (it != cache.end()) {
        return it->second.get();
    }
    auto e = std::make_unique<Entry>();
    Tile t{};
    std::snprintf(t.path, kPathLen, "__OTR__rs/sprites/%s", token);
    if (Probe(t.path, &t.w, &t.h)) {
        e->tiles.push_back(t);
        e->info = { t.w, t.h, 1, 1 };
        e->found = true;
    } else {
        // A grid: <token>/<col>_<row>. Columns along row 0 give the widths, rows along column 0 the heights.
        int32_t cols = 0, rows = 0, widths[kGridMax] = {}, heights[kGridMax] = {};
        char path[kPathLen];
        for (int32_t c = 0; c < kGridMax; c++) {
            int32_t w, h;
            std::snprintf(path, kPathLen, "__OTR__rs/sprites/%s/%d_0", token, c);
            if (!Probe(path, &w, &h)) {
                break;
            }
            widths[cols++] = w;
            if (c == 0) {
                heights[rows++] = h;
            }
        }
        for (int32_t r = 1; cols > 0 && r < kGridMax; r++) {
            int32_t w, h;
            std::snprintf(path, kPathLen, "__OTR__rs/sprites/%s/0_%d", token, r);
            if (!Probe(path, &w, &h)) {
                break;
            }
            heights[rows++] = h;
        }
        bool whole = cols > 0;
        int32_t y0 = 0;
        for (int32_t r = 0; whole && r < rows; r++) {
            int32_t x0 = 0;
            for (int32_t c = 0; c < cols; c++) {
                Tile g{};
                std::snprintf(g.path, kPathLen, "__OTR__rs/sprites/%s/%d_%d", token, c, r);
                // Every tile must be there, at the size its row and column say: a hole would draw garbage.
                if (!Probe(g.path, &g.w, &g.h) || g.w != widths[c] || g.h != heights[r]) {
                    whole = false;
                    break;
                }
                g.x0 = x0;
                g.y0 = y0;
                e->tiles.push_back(g);
                x0 += g.w;
            }
            y0 += heights[r];
        }
        if (whole) {
            int32_t width = 0;
            for (int32_t c = 0; c < cols; c++) {
                width += widths[c];
            }
            e->info = { width, y0, cols, rows };
            e->found = true;
        } else {
            e->tiles.clear();
        }
    }
    Entry* out = e.get();
    cache.emplace(token, std::move(e));
    return out;
}

// Words one tile's quad writes: the load (about 7 commands, more with the hardware workaround's split), the vertices,
// two triangles and a sync. Generous on purpose: it sizes a buffer.
constexpr int32_t kWordsPerTile = 24;

} // namespace

extern "C" int32_t RsSprites_Info(const char* token, RsSpriteInfo* out) {
    if (out != nullptr) {
        *out = RsSpriteInfo{};
    }
    if (!TokenClean(token)) {
        return 0;
    }
    Entry* e = Resolve(token);
    if (!e->found) {
        return 0;
    }
    if (out != nullptr) {
        *out = e->info;
    }
    return 1;
}

extern "C" const char* RsSprites_Path(const char* token) {
    if (!TokenClean(token)) {
        return nullptr;
    }
    Entry* e = Resolve(token);
    return e->found && e->tiles.size() == 1 ? e->tiles[0].path : nullptr;
}

extern "C" int32_t RsSprites_GfxWords(const char* token) {
    if (!TokenClean(token)) {
        return 0;
    }
    Entry* e = Resolve(token);
    return e->found ? (int32_t)e->tiles.size() * kWordsPerTile : 0;
}

extern "C" int32_t RsSprites_EmitRect(GraphicsContext* gfxCtx, Gfx** gfx, const char* token, float x, float y, float w,
                                      float h) {
    if (gfx == nullptr || !TokenClean(token)) {
        return 0;
    }
    Entry* e = Resolve(token);
    if (!e->found || e->info.width <= 0 || e->info.height <= 0) {
        return 0;
    }
    const float sx = w / (float)e->info.width;
    const float sy = h / (float)e->info.height;
    Gfx* p = *gfx;
    for (const Tile& t : e->tiles) {
        gDPPipeSync(p++);
        // A TILE load, as RsItemPickup's: LoadBlock caps at 4096 texels. The whole tile, clamped, so the bilinear
        // filter at its edge reads its own last texel and not the next tile's first (the seam would show).
        gDPLoadTextureTile(p++, (u8*)t.path, G_IM_FMT_RGBA, G_IM_SIZ_32b, t.w, t.h, 0, 0, t.w - 1, t.h - 1, 0,
                           G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                           G_TX_NOLOD, G_TX_NOLOD);
        Vtx* v = (Vtx*)Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
        const float left = x + t.x0 * sx, right = x + (t.x0 + t.w) * sx;
        const float top = y - t.y0 * sy, bottom = y - (t.y0 + t.h) * sy;
        const float xs[4] = { left, right, left, right };
        const float ys[4] = { top, top, bottom, bottom };
        const int16_t ss[4] = { 0, (int16_t)(t.w << 5), 0, (int16_t)(t.w << 5) };
        const int16_t ts[4] = { 0, 0, (int16_t)(t.h << 5), (int16_t)(t.h << 5) };
        for (int32_t c = 0; c < 4; c++) {
            v[c].v.ob[0] = (int16_t)(xs[c] < 0 ? xs[c] - 0.5f : xs[c] + 0.5f);
            v[c].v.ob[1] = (int16_t)(ys[c] < 0 ? ys[c] - 0.5f : ys[c] + 0.5f);
            v[c].v.ob[2] = 0;
            v[c].v.flag = 0;
            v[c].v.tc[0] = ss[c];
            v[c].v.tc[1] = ts[c];
            v[c].v.cn[0] = v[c].v.cn[1] = v[c].v.cn[2] = v[c].v.cn[3] = 255;
        }
        gSPVertex(p++, (uintptr_t)v, 4, 0);
        gSP2Triangles(p++, 0, 2, 3, 0, 0, 3, 1, 0);
    }
    *gfx = p;
    return (int32_t)e->tiles.size();
}

int32_t RsSprites_EmitRect(GraphicsContext* gfxCtx, std::vector<Gfx>& dl, const char* token, float x, float y, float w,
                           float h) {
    const int32_t words = RsSprites_GfxWords(token);
    if (words == 0) {
        return 0;
    }
    const size_t at = dl.size();
    dl.resize(at + (size_t)words);
    Gfx* p = &dl[at];
    const int32_t n = RsSprites_EmitRect(gfxCtx, &p, token, x, y, w, h);
    dl.resize((size_t)(p - dl.data()));
    return n;
}
