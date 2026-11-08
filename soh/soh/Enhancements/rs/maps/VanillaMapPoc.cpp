// sturdy-bassoon#302 - THROWAWAY proof of concept, never merged (scratch/302-vanilla-maps).
//
// Draws vanilla's two pause map pages from mod code, outside kaleido:
//   - a dungeon floor map (two CI4 48x85 halves sharing one 16-entry TLUT) with the palette-swap fog,
//     built from gMapData exactly as Map_SetFloorPalettesData does, into the mod's own u8[32];
//   - the Hyrule backdrop (one CI8 216x128 + 256-entry TLUT) in vanilla's 15 strips via gDPLoadMultiTile,
//     or in one load, to see what Fast3D does with each.
// The TLUT modes exist to show the pointer-keyed palette cache: `inplace` mutates one buffer (stale),
// `intern` keeps one never-rewritten buffer per distinct palette, `delete` mutates in place and drops the
// texture's cache entries on every change.
//
//   vmap off | status
//   vmap dungeon <mapIndex> <floor>     floor 0..7 as gMapData indexes it (0 = top)
//   vmap rooms all|none|visited|<hex>   which rooms count as visited
//   vmap hasmap 0|1                     the Dungeon Map item: entry 15 black (unvisited structure)
//   vmap current <room>|-1              pulse that room's palette entry (40 steps)
//   vmap tlut inplace|intern|delete
//   vmap load block|multi               the CI4 halves via LoadTextureBlock_4b or LoadMultiTile_4b
//   vmap hyrule strips|whole|block|multi1

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/frame_interpolation.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
extern MapData* gMapData;
void Gfx_TextureCacheDelete(const uint8_t* addr);
}

#include "textures/map_48x85_static/map_48x85_static.h"
#include "textures/icon_item_field_static/icon_item_field_static.h"

namespace {

// Copied from z_kaleido_scope_PAL.c (file-static there).
const char* sDungeonMapTexs[] = {
#include "VanillaMapPocTexs.inc"
};
constexpr int32_t kTexCount = (int32_t)(sizeof(sDungeonMapTexs) / sizeof(sDungeonMapTexs[0]));

struct Poc {
    int32_t view = 0; // 0 off, 1 dungeon, 2 hyrule
    int32_t mapIndex = 0;
    int32_t floor = 5;
    int32_t roomsMode = 0; // 0 all, 1 none, 2 visited, 3 hex
    uint32_t roomsHex = 0;
    bool hasMap = true;
    int32_t current = -1;
    int32_t tlut = 1; // 0 inplace, 1 intern, 2 delete
    int32_t load = 0; // 0 block, 1 multi
    int32_t hyrule = 0; // 0 strips, 1 whole, 2 block, 3 multi1
    // last frame
    const uint8_t* palPtr = nullptr;
    uint8_t lastPal[32] = {};
    int32_t interned = 0;
    int32_t deletes = 0;
    int32_t texIndex = -1;
} s;

alignas(8) uint8_t sInPlace[32];
alignas(8) uint8_t sInterned[256][32];

uint32_t Rooms() {
    switch (s.roomsMode) {
        case 0: return 0xFFFFFFFFu;
        case 1: return 0;
        case 2: return gSaveContext.sceneFlags[s.mapIndex].rooms;
        default: return s.roomsHex;
    }
}

void PutEntry(uint8_t* pal, int32_t entry, uint16_t rgba16) {
    pal[entry * 2] = (uint8_t)(rgba16 >> 8);
    pal[entry * 2 + 1] = (uint8_t)(rgba16 & 0xFF);
}

// Map_SetFloorPalettesData + Map_SetPaletteData, into a buffer of our own.
void BuildPalette(uint8_t* pal, uint32_t frame) {
    std::memset(pal, 0, 32);
    if (s.hasMap) {
        PutEntry(pal, 15, 0x0001);
    }
    const uint32_t rooms = Rooms();
    for (int32_t i = 0; i < gMapData->maxPaletteCount[s.mapIndex]; i++) {
        const int16_t room = gMapData->paletteRoom[s.mapIndex][s.floor][i];
        if (room != 0xFF && (rooms & (1u << room))) {
            PutEntry(pal, gMapData->roomPalette[s.mapIndex][room], 0x02BF);
        }
    }
    if (s.current >= 0 && (rooms & (1u << s.current))) {
        // vanilla's two pulse colours (5-bit), 20 ticks each way
        const int32_t step = (int32_t)(frame % 40);
        const int32_t t = step < 20 ? step : 40 - step;
        const int32_t r = 0, g = 10 + (25 - 10) * t / 20, b = 31 + (17 - 31) * t / 20;
        PutEntry(pal, gMapData->roomPalette[s.mapIndex][s.current], (uint16_t)((r << 11) | (g << 6) | (b << 1) | 1));
    }
}

const uint8_t* PalettePointer(uint8_t* want) {
    const bool changed = std::memcmp(want, s.lastPal, 32) != 0;
    std::memcpy(s.lastPal, want, 32);
    if (s.tlut == 1) {
        for (int32_t i = 0; i < s.interned; i++) {
            if (std::memcmp(sInterned[i], want, 32) == 0) {
                return sInterned[i];
            }
        }
        if (s.interned == 256) {
            s.interned = 0; // a real page would clear the cache here; the POC never gets near it
        }
        std::memcpy(sInterned[s.interned], want, 32);
        return sInterned[s.interned++];
    }
    std::memcpy(sInPlace, want, 32);
    if (s.tlut == 2 && changed && s.texIndex >= 0) {
        Gfx_TextureCacheDelete((const uint8_t*)sDungeonMapTexs[s.texIndex]);
        Gfx_TextureCacheDelete((const uint8_t*)sDungeonMapTexs[s.texIndex + 1]);
        s.deletes++;
    }
    return sInPlace;
}

void Quad(Vtx* v, float x, float y, float w, float h, int32_t sw, int32_t th) {
    const float xs[4] = { x, x + w, x, x + w };
    const float ys[4] = { y, y, y - h, y - h };
    const int32_t ss[4] = { 0, sw << 5, 0, sw << 5 };
    const int32_t ts[4] = { 0, 0, th << 5, th << 5 };
    for (int32_t c = 0; c < 4; c++) {
        v[c].v.ob[0] = (int16_t)xs[c];
        v[c].v.ob[1] = (int16_t)ys[c];
        v[c].v.ob[2] = 0;
        v[c].v.flag = 0;
        v[c].v.tc[0] = (int16_t)ss[c];
        v[c].v.tc[1] = (int16_t)ts[c];
        v[c].v.cn[0] = v[c].v.cn[1] = v[c].v.cn[2] = v[c].v.cn[3] = 255;
    }
}

} // namespace

static Gfx* Panel(Gfx* p, GraphicsContext* gfxCtx, float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b) {
    Vtx* v = (Vtx*)Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
    Quad(v, x, y, w, h, 0, 0);
    gDPPipeSync(p++);
    gSPTexture(p++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(p++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(p++, 0, 0, r, g, b, 255);
    gSPVertex(p++, (uintptr_t)v, 4, 0);
    gSP1Quadrangle(p++, 0, 2, 3, 1, 0);
    return p;
}

static Gfx* DrawDungeon(Gfx* p, PlayState* play) {
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    const int32_t idx = gMapData->dgnMinimapTexIndexBase[s.mapIndex] + gMapData->floorTexIndexOffset[s.mapIndex][s.floor];
    if (idx < 0 || idx + 1 >= kTexCount) {
        return p;
    }
    s.texIndex = idx;
    uint8_t want[32];
    BuildPalette(want, (uint32_t)play->gameplayFrames);
    s.palPtr = PalettePointer(want);

    // 2x: each half 96x170, side by side, centred
    const float sc = 2.0f, w = 48 * sc, h = 85 * sc, x0 = -w, y0 = h / 2;
    p = Panel(p, gfxCtx, x0 - 4, y0 + 4, 2 * w + 8, h + 8, 200, 160, 40);
    p = Panel(p, gfxCtx, x0, y0, 2 * w, h, 40, 44, 52);

    Vtx* v = (Vtx*)Graph_Alloc(gfxCtx, 8 * sizeof(Vtx));
    Quad(v, x0, y0, w, h, 48, 85);
    Quad(v + 4, x0 + w, y0, w, h, 48, 85);

    gDPPipeSync(p++);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(p++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPSetPrimColor(p++, 0, 0, 255, 255, 255, 255);
    gDPSetTextureFilter(p++, G_TF_POINT);
    gDPLoadTLUT_pal16(p++, 0, s.palPtr);
    gDPSetTextureLUT(p++, G_TT_RGBA16);
    gSPVertex(p++, (uintptr_t)v, 8, 0);
    for (int32_t half = 0; half < 2; half++) {
        const char* tex = sDungeonMapTexs[idx + half];
        if (s.load == 0) {
            gDPLoadTextureBlock_4b(p++, tex, G_IM_FMT_CI, 48, 85, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                   G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        } else {
            gDPLoadMultiTile_4b(p++, tex, 0, G_TX_RENDERTILE, G_IM_FMT_CI, 48, 85, 0, 0, 47, 84, 0,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
        }
        gSP1Quadrangle(p++, half * 4 + 0, half * 4 + 2, half * 4 + 3, half * 4 + 1, 0);
    }
    gDPPipeSync(p++);
    gDPSetTextureLUT(p++, G_TT_NONE);
    gDPSetTextureFilter(p++, G_TF_BILERP);
    return p;
}

static Gfx* DrawHyrule(Gfx* p, PlayState* play) {
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    const float x0 = -108, y0 = 64; // 1:1 in N64 units, as kaleido places it (-108..108, 58..-70)
    p = Panel(p, gfxCtx, x0 - 4, y0 + 4, 216 + 8, 128 + 8, 200, 160, 40);

    gDPPipeSync(p++);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, s.hyrule == 3 ? 1 : G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(p++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPSetPrimColor(p++, 0, 0, 255, 255, 255, 255);
    gDPSetTextureFilter(p++, G_TF_POINT);
    gDPLoadTLUT_pal256(p++, gWorldMapImageTLUT);
    gDPSetTextureLUT(p++, G_TT_RGBA16);

    if (s.hyrule == 0 || s.hyrule == 3) {
        // vanilla: 14 strips of 9 rows and one of 2, 216 x 9 = 1,944 texel bytes each
        const int32_t rtile = s.hyrule == 3 ? 1 : G_TX_RENDERTILE;
        for (int32_t t = 0; t < 15; t++) {
            const int32_t rows = t < 14 ? 9 : 2;
            Vtx* v = (Vtx*)Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
            Quad(v, x0, y0 - t * 9, 216, (float)rows, 216, rows);
            gSPVertex(p++, (uintptr_t)v, 4, 0);
            gDPLoadMultiTile(p++, gWorldMapImageTex, 0, rtile, G_IM_FMT_CI, G_IM_SIZ_8b, 216, 128, 0, t * 9, 216 - 1,
                             t * 9 + rows - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                             G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gDPSetTileSize(p++, rtile, 0, 0, (216 - 1) << G_TEXTURE_IMAGE_FRAC, (rows - 1) << G_TEXTURE_IMAGE_FRAC);
            gSP1Quadrangle(p++, 0, 2, 3, 1, 0);
        }
    } else {
        Vtx* v = (Vtx*)Graph_Alloc(gfxCtx, 4 * sizeof(Vtx));
        Quad(v, x0, y0, 216, 128, 216, 128);
        gSPVertex(p++, (uintptr_t)v, 4, 0);
        if (s.hyrule == 1) {
            gDPLoadTextureTile(p++, gWorldMapImageTex, G_IM_FMT_CI, G_IM_SIZ_8b, 216, 128, 0, 0, 215, 127, 0,
                               G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                               G_TX_NOLOD, G_TX_NOLOD);
        } else {
            gDPLoadTextureBlock(p++, gWorldMapImageTex, G_IM_FMT_CI, G_IM_SIZ_8b, 216, 128, 0,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
        }
        gSP1Quadrangle(p++, 0, 2, 3, 1, 0);
    }
    gDPPipeSync(p++);
    gDPSetTextureLUT(p++, G_TT_NONE);
    gDPSetTextureFilter(p++, G_TF_BILERP);
    return p;
}

static void OnPlayDrawEndVanillaMapPoc() {
    PlayState* play = gPlayState;
    if (s.view == 0 || play == nullptr || gMapData == nullptr) {
        return;
    }
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx* p = OVERLAY_DISP;

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
    gDPPipeSync(p++);
    gSPViewport(p++, vp);
    gSPMatrix(p++, ortho, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPLoadGeometryMode(p++, G_SHADING_SMOOTH);
    gDPSetOtherMode(p++,
                    G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2);

    FrameInterpolation_RecordOpenChild(&s, 0);
    Matrix_Push();
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    gSPMatrix(p++, Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    p = s.view == 1 ? DrawDungeon(p, play) : DrawHyrule(p, play);
    Matrix_Pop();
    FrameInterpolation_RecordCloseChild();

    OVERLAY_DISP = p;
    CLOSE_DISPS(gfxCtx);
}

namespace {

std::string Status() {
    char pal[200];
    int32_t n = 0;
    for (int32_t i = 0; i < 16; i++) {
        n += std::snprintf(pal + n, sizeof(pal) - n, "%s%02X%02X", i ? "," : "", s.lastPal[i * 2], s.lastPal[i * 2 + 1]);
    }
    static const char* tl[] = { "inplace", "intern", "delete" };
    static const char* hy[] = { "strips", "whole", "block", "multi1" };
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "op=status view=%s map=%d floor=%d tex=%d rooms=0x%08X visited=0x%08X hasmap=%d current=%d tlut=%s "
                  "load=%s hyrule=%s pal_ptr=%p interned=%d deletes=%d pal=%s",
                  s.view == 0 ? "off" : s.view == 1 ? "dungeon" : "hyrule", s.mapIndex, s.floor, s.texIndex, Rooms(),
                  gSaveContext.sceneFlags[s.mapIndex].rooms, s.hasMap ? 1 : 0, s.current, tl[s.tlut],
                  s.load ? "multi" : "block", hy[s.hyrule], (const void*)s.palPtr, s.interned, s.deletes, pal);
    return buf;
}

int32_t Run(const std::vector<std::string>& a, std::vector<std::string>& lines) {
    const std::string sub = a.empty() ? "status" : a[0];
    const std::string arg = a.size() > 1 ? a[1] : "";
    if (sub == "off") {
        s.view = 0;
    } else if (sub == "dungeon") {
        s.view = 1;
        if (a.size() > 2) {
            s.mapIndex = std::atoi(a[1].c_str()) % 10;
            s.floor = std::atoi(a[2].c_str()) & 7;
        }
    } else if (sub == "rooms") {
        if (arg == "all") s.roomsMode = 0;
        else if (arg == "none") s.roomsMode = 1;
        else if (arg == "visited") s.roomsMode = 2;
        else { s.roomsMode = 3; s.roomsHex = (uint32_t)std::strtoul(arg.c_str(), nullptr, 16); }
    } else if (sub == "hasmap") {
        s.hasMap = arg != "0";
    } else if (sub == "current") {
        s.current = std::atoi(arg.c_str());
    } else if (sub == "tlut") {
        s.tlut = arg == "inplace" ? 0 : arg == "delete" ? 2 : 1;
    } else if (sub == "load") {
        s.load = arg == "multi" ? 1 : 0;
    } else if (sub == "hyrule") {
        s.view = 2;
        s.hyrule = arg == "whole" ? 1 : arg == "block" ? 2 : arg == "multi1" ? 3 : 0;
    } else if (sub != "status") {
        lines.push_back("usage: vmap off|status|dungeon <map> <floor>|rooms all|none|visited|<hex>|hasmap 0|1|"
                        "current <room>|tlut inplace|intern|delete|load block|multi|hyrule strips|whole|block|multi1");
        return 1;
    }
    lines.push_back(Status());
    return 0;
}

const ConsoleSink::Command vmapCommand("vmap", Run, "sturdy-bassoon#302 POC: vanilla map pages drawn from mod code.",
                                       { { "sub", Ship::ArgumentType::TEXT, true },
                                         { "arg", Ship::ArgumentType::TEXT, true },
                                         { "arg", Ship::ArgumentType::TEXT, true } });

void RegisterVanillaMapPoc() {
    COND_HOOK(OnPlayDrawEnd, true, OnPlayDrawEndVanillaMapPoc);
}

RegisterShipInitFunc vanillaMapPocInit(RegisterVanillaMapPoc);

} // namespace
