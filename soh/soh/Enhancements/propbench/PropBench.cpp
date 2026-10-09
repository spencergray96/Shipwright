// THROWAWAY (sturdy-bassoon#117 questions 2 and 3). Not for merging.
//
// Rs_PropBench: one actor per prop, drawing the same crate geometry by two resolution paths, so the
// per-frame cost of an actor-drawn archive prop can be read off a sweep of N:
//
//   mode 0 nodraw       resident, no draw function work (the per-actor floor)
//   mode 1 comp_crate   the compiled-in crate's material + triangles, by pointer (20 tris)
//   mode 2 arch_crate   the archive crate's top-level list, resolved ONCE at init; nested 0x27/0x24/0x25 per frame
//   mode 3 comp_barrel  the compiled-in stacked barrels (372 tris, two materials)
//   mode 4 arch_plant   mode 2 + 6 planted G_DL_OTR_FILEPATH calls to an empty archive list
//   mode 5 comp_plant   mode 1 + 6 planted G_DL calls to an empty compiled list
//   mode 6 trigger      draws nothing; a talk trigger that opens a direct-text line ("search")
//   mode 7 pickup       draws the archive crate; checking it opens a line, closing it kills the actor
//   mode 11 flipbook    sturdy-bassoon#187: one of N bake-registered frame lists, picked by the clock
//   mode 12 split       sturdy-bassoon#187: a bake-registered base list plus an interpreted one beside it
//
// sturdy-bassoon#205 (THROWAWAY): `propbench at <mode + 256> ...` draws that copy in the translucent
// pass (POLY_XLU_DISP) instead of the opaque one; `propbench room opa|xlu <path>` puts an archive list in
// the room's own draw, in the opaque pass where a map's prop lists go (before the room's geometry) or
// in its translucent pass; `propbench alpha ...` sets the replay's alpha curves.
//
// `propbench spawn <mode> <n> [dist]` kills any bench actors and spawns n in a wall in front of Link.
// `propbench clear`, `propbench status`.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <fast/StaticMeshCache.h>

#include "soh/ActorDB.h"
#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/rs/actors/RsActors.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/frame_interpolation.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;

// The compiled-in props in rs_props_p2_host (hand-merged, already in the tree).
extern Gfx mat_rs_crate_355_rs_crate_355_vcol_tex22_layerOpaque[];
extern Gfx rs_crate_355_rs_crate_355_mesh_layer_Opaque_tri_0[];
extern Gfx rs_stacked_barrels_4641_opaque_dl[];
}

using ConsoleSink::Addf;

namespace {

enum PbMode {
    PB_NODRAW = 0,
    PB_COMP_CRATE,
    PB_ARCH_CRATE,
    PB_COMP_BARREL,
    PB_ARCH_PLANT,
    PB_COMP_PLANT,
    PB_TRIGGER,
    PB_PICKUP,
    PB_TEXFLOOD,
    PB_ARCH_BAKED,
    PB_NULLDRAW,
    PB_FLIPBOOK,
    PB_SPLIT,
    PB_MODE_COUNT
};
const char* const kModeNames[PB_MODE_COUNT] = { "nodraw",     "comp_crate", "arch_crate", "comp_barrel", "arch_plant",
                                                "comp_plant", "trigger",    "pickup",     "texflood",    "arch_baked",
                                                "nulldraw",   "flipbook",   "split" };

// mode 8: the planted texture-cache overflow. One actor draws the compiled crate K times, each copy
// binding its own 64x64 CI4 texture from this pool by raw pointer, so a frame uses K distinct cache
// entries (keyed by address). Synthetic stripes, nothing RS-derived. Built once into a static list.
constexpr s32 kFloodMax = 1400;
constexpr s32 kFloodTexBytes = 64 * 64 / 2;
u8 sFloodPool[kFloodMax][kFloodTexBytes];
Gfx sFlood[1 + kFloodMax * 6 + 1];
s32 sFloodCount = 0;

// The archive list arch modes draw, resolved at each actor's init. `propbench path <p>` changes it
// (step 2 draws the generated archive's local-space kind list instead).
std::string sArchPath = "__OTR__objects/rs_props/rs_crate_355/rs_crate_355";
char kArchEmptyPath[] = "objects/propbench/empty"; // in mods/propbench.o2r

// sturdy-bassoon#187: the flipbook's frame lists and their lengths in RS client cycles (20 ms), and
// the split route's interpreted list. `propbench flip` and `propbench split` set them.
constexpr s32 kFlipMax = 128;
std::vector<std::string> sFlipPaths;
std::vector<s32> sFlipCycles;
s32 sFlipPinned = -1; // a frame index to hold every flipbook at, or -1 to run
std::string sSplitPath;
u32 sFlipSwitches = 0; // frame changes drawn, for the status line

typedef struct {
    Actor actor;
    Gfx* flip[kFlipMax];
    s32 flipCount;
    s32 flipPhase; // this copy's start, in cycles: RS starts each placement at a random frame
    s32 flipLast;
    Gfx* splitDl;
    Gfx* archDl; // resolved once at init
    s32 mode;
    s32 xlu; // sturdy-bassoon#205: draw into POLY_XLU_DISP
    s32 talking;
    s32 checks;
} PropBench;

Gfx sCompCrate[] = {
    gsSPDisplayList(mat_rs_crate_355_rs_crate_355_vcol_tex22_layerOpaque),
    gsSPDisplayList(rs_crate_355_rs_crate_355_mesh_layer_Opaque_tri_0),
    gsSPEndDisplayList(),
};
Gfx sCompEmpty[] = { gsSPEndDisplayList() };
Gfx sCompPlant[] = {
    gsSPDisplayList(sCompEmpty), gsSPDisplayList(sCompEmpty), gsSPDisplayList(sCompEmpty),
    gsSPDisplayList(sCompEmpty), gsSPDisplayList(sCompEmpty), gsSPDisplayList(sCompEmpty),
    gsSPEndDisplayList(),
};
Gfx sArchPlant[] = {
    gsSPDisplayListOTRFilePath(kArchEmptyPath), gsSPDisplayListOTRFilePath(kArchEmptyPath),
    gsSPDisplayListOTRFilePath(kArchEmptyPath), gsSPDisplayListOTRFilePath(kArchEmptyPath),
    gsSPDisplayListOTRFilePath(kArchEmptyPath), gsSPDisplayListOTRFilePath(kArchEmptyPath),
    gsSPEndDisplayList(),
};

s32 sBenchId = -1;
Gfx* sRoomOpa = nullptr; // sturdy-bassoon#205: drawn by z_room.c's room draw
Gfx* sRoomXlu = nullptr;
s32 sNullDrawId = -1;
s32 sLastMode = -1;
s32 sLastCount = 0;
s32 sInits = 0;
s32 sArchNull = 0;

void PbMarker(const char* fmt, ...) {
    char line[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(line, sizeof(line), fmt, va);
    va_end(va);
    AgentTest_WriteMarker(line);
}

bool UsesArch(s32 mode) {
    return mode == PB_ARCH_CRATE || mode == PB_ARCH_PLANT || mode == PB_PICKUP || mode == PB_ARCH_BAKED || mode == PB_SPLIT;
}

// The flipbook's frame at `cycles` RS client cycles into its loop.
s32 FlipFrameAt(s32 count, s32 cycles) {
    s32 total = 0;
    for (s32 i = 0; i < count; i++) {
        total += sFlipCycles[i];
    }
    if (total <= 0) {
        return 0;
    }
    s32 t = cycles % total;
    for (s32 i = 0; i < count; i++) {
        if (t < sFlipCycles[i]) {
            return i;
        }
        t -= sFlipCycles[i];
    }
    return count - 1;
}

void PropBench_Init(Actor* thisx, PlayState* play) {
    PropBench* pb = (PropBench*)thisx;
    pb->mode = thisx->params & 0xFF;
    pb->xlu = (thisx->params >> 8) & 1;
    pb->archDl = nullptr;
    pb->talking = 0;
    pb->checks = 0;
    pb->flipCount = 0;
    pb->flipLast = -1;
    pb->splitDl = nullptr;
    if (pb->mode == PB_FLIPBOOK) {
        s32 total = 0;
        for (size_t i = 0; i < sFlipPaths.size() && i < (size_t)kFlipMax; i++) {
            Gfx* dl = ResourceMgr_LoadGfxByName(sFlipPaths[i].c_str());
            if (dl == nullptr) {
                sArchNull++;
                continue;
            }
            Fast::StaticBakeRegister(dl);
            pb->flip[pb->flipCount++] = dl;
            total += sFlipCycles[i];
        }
        pb->flipPhase = total > 0 ? (s32)(Rand_ZeroOne() * total) : 0;
    }
    if (pb->mode == PB_SPLIT && !sSplitPath.empty()) {
        pb->splitDl = ResourceMgr_LoadGfxByName(sSplitPath.c_str()); // never registered: interpreted
        if (pb->splitDl == nullptr) {
            sArchNull++;
        }
    }
    if (UsesArch(pb->mode)) {
        pb->archDl = ResourceMgr_LoadGfxByName(sArchPath.c_str());
        if (pb->archDl == nullptr) {
            sArchNull++;
        } else if (pb->mode == PB_ARCH_BAKED || pb->mode == PB_SPLIT) {
            // Offered to the bake the way ArchiveProps offers a room's lists: by the resolved pointer.
            // Registering twice is harmless; the first submission records it, under this actor's
            // matrix, and every later one replays.
            Fast::StaticBakeRegister(pb->archDl);
        }
    }
    Actor_SetScale(thisx, 1.0f);
    ActorShape_Init(&thisx->shape, 0.0f, NULL, 0.0f);
    if (pb->mode == PB_TRIGGER || pb->mode == PB_PICKUP) {
        thisx->flags |= ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY;
        thisx->targetMode = 0;
        thisx->textId = RS_TEXT_DIRECT;
    }
    sInits++;
}

void PropBench_Destroy(Actor* thisx, PlayState* play) {
}

void PropBench_Update(Actor* thisx, PlayState* play) {
    PropBench* pb = (PropBench*)thisx;
    if (pb->mode != PB_TRIGGER && pb->mode != PB_PICKUP) {
        return;
    }
    Actor_SetFocus(thisx, 20.0f);
    if (pb->talking) {
        u8 state = Message_GetState(&play->msgCtx);
        if (state == TEXT_STATE_NONE || (state == TEXT_STATE_DONE && Message_ShouldAdvance(play))) {
            pb->talking = 0;
            PbMarker("propbench event=close mode=%s checks=%d", kModeNames[pb->mode], pb->checks);
            if (pb->mode == PB_PICKUP) {
                PbMarker("propbench event=kill mode=pickup x=%.0f z=%.0f", thisx->world.pos.x, thisx->world.pos.z);
                Actor_Kill(thisx);
            }
        }
        return;
    }
    if (Actor_ProcessTalkRequest(thisx, play)) {
        pb->talking = 1;
        pb->checks++;
        PbMarker("propbench event=open mode=%s checks=%d x=%.0f z=%.0f", kModeNames[pb->mode], pb->checks,
                 thisx->world.pos.x, thisx->world.pos.z);
        return;
    }
    // The line is set on every tick the offer stands, so the box shows this actor's text whichever
    // trigger Link picks; one direct slot is enough while only one box can be open.
    RsText_SetDirect(pb->mode == PB_PICKUP ? "You pick up the crate." : "You search the crate. It is empty.");
    Actor_OfferTalk(thisx, play, 60.0f);
}

} // namespace

// Outside the anonymous namespace: OPEN_DISPS declares FrameInterpolation_RecordOpenChild at block
// scope, which C++ binds to the innermost enclosing namespace - inside an anonymous one that is a
// different, C++-linkage function, and the link fails.
static void PropBench_Draw(Actor* thisx, PlayState* play) {
    PropBench* pb = (PropBench*)thisx;
    if (pb->mode == PB_NODRAW || pb->mode == PB_TRIGGER) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    // sturdy-bassoon#205: a translucent-pass copy writes its matrix and list into POLY_XLU_DISP.
    Gfx** disp = pb->xlu ? &POLY_XLU_DISP : &POLY_OPA_DISP;
    if (pb->xlu) {
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    } else {
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
    }
    switch (pb->mode) {
        case PB_COMP_CRATE:
        case PB_COMP_PLANT:
        case PB_TEXFLOOD:
            // The compiled crate's vertices were translated by (100, 0, -60) when it was merged.
            Matrix_Translate(-100.0f, 0.0f, 60.0f, MTXMODE_APPLY);
            break;
        case PB_COMP_BARREL:
            Matrix_Translate(40.0f, 0.0f, 140.0f, MTXMODE_APPLY);
            break;
        default:
            break;
    }
    gSPMatrix((*disp)++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    switch (pb->mode) {
        case PB_COMP_CRATE:
            gSPDisplayList(POLY_OPA_DISP++, sCompCrate);
            break;
        case PB_COMP_PLANT:
            gSPDisplayList(POLY_OPA_DISP++, sCompCrate);
            gSPDisplayList(POLY_OPA_DISP++, sCompPlant);
            break;
        case PB_COMP_BARREL:
            gSPDisplayList(POLY_OPA_DISP++, rs_stacked_barrels_4641_opaque_dl);
            break;
        case PB_ARCH_CRATE:
        case PB_PICKUP:
        case PB_ARCH_BAKED:
            if (pb->archDl != nullptr) {
                gSPDisplayList((*disp)++, pb->archDl);
            }
            break;
        case PB_ARCH_PLANT:
            if (pb->archDl != nullptr) {
                gSPDisplayList(POLY_OPA_DISP++, pb->archDl);
            }
            gSPDisplayList(POLY_OPA_DISP++, sArchPlant);
            break;
        case PB_FLIPBOOK:
            if (pb->flipCount > 0) {
                // The game ticks at 20 Hz and RS's clock at 50: 2.5 cycles a tick, so a frame of 5
                // cycles holds for two ticks. Stepped, as RS draws it.
                s32 k = sFlipPinned >= 0 ? sFlipPinned % pb->flipCount
                                         : FlipFrameAt(pb->flipCount, (s32)(play->gameplayFrames * 5 / 2) + pb->flipPhase);
                if (k != pb->flipLast) {
                    sFlipSwitches++;
                    pb->flipLast = k;
                }
                gSPDisplayList(POLY_OPA_DISP++, pb->flip[k]);
            }
            break;
        case PB_SPLIT:
            if (pb->archDl != nullptr) {
                gSPDisplayList(POLY_OPA_DISP++, pb->archDl);
            }
            if (pb->splitDl != nullptr) {
                gSPDisplayList(POLY_OPA_DISP++, pb->splitDl);
            }
            break;
        case PB_TEXFLOOD:
            if (sFloodCount > 0) {
                gSPDisplayList(POLY_OPA_DISP++, sFlood);
            }
            break;
        default:
            break;
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

namespace {

bool sAdded = false;

void RegisterPropBench() {
    if (sAdded) {
        return;
    }
    ActorDBInit init = {
        "Rs_PropBench",
        "THROWAWAY #117 prop bench",
        -1,
        ACTORCAT_PROP,
        0u,
        OBJECT_GAMEPLAY_KEEP,
        sizeof(PropBench),
        (ActorFunc)PropBench_Init,
        (ActorFunc)PropBench_Destroy,
        (ActorFunc)PropBench_Update,
        (ActorFunc)PropBench_Draw,
        nullptr,
    };
    ActorDB::Instance->AddEntry(init);
    sBenchId = ActorDB::Instance->RetrieveId("Rs_PropBench");
    // mode 10: the same actor with NO draw function, so Actor_DrawAll skips its light and matrix
    // setup - the floor of an interaction-only trigger.
    ActorDBInit nul = init;
    nul.name = "Rs_PropBenchNull";
    nul.desc = "THROWAWAY #117 prop bench, no draw";
    nul.draw = nullptr;
    ActorDB::Instance->AddEntry(nul);
    sNullDrawId = ActorDB::Instance->RetrieveId("Rs_PropBenchNull");
    sAdded = true;
}

RegisterShipInitFunc propBenchInit(RegisterPropBench);

s32 CountBench(PlayState* play) {
    s32 n = 0;
    for (Actor* a = play->actorCtx.actorLists[ACTORCAT_PROP].head; a != nullptr; a = a->next) {
        if ((a->id == sBenchId || a->id == sNullDrawId) && a->update != nullptr) {
            n++;
        }
    }
    return n;
}

void KillBench(PlayState* play) {
    for (Actor* a = play->actorCtx.actorLists[ACTORCAT_PROP].head; a != nullptr; a = a->next) {
        if (a->id == sBenchId || a->id == sNullDrawId) {
            Actor_Kill(a);
        }
    }
}

int32_t Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        Addf(lines, "propbench error=no_play");
        return 1;
    }
    const std::string sub = args.empty() ? "status" : args[0];
    if (sub == "status") {
        Gfx* empty = ResourceMgr_LoadGfxByName("__OTR__objects/propbench/empty");
        Gfx* crate = ResourceMgr_LoadGfxByName(sArchPath.c_str());
        bool alt = Ship::Context::GetRawInstance()->GetResourceManager()->IsAltAssetsEnabled();
        Addf(lines, "propbench status id=%d resident=%d last_mode=%s last_n=%d inits=%d arch_null=%d empty_dl=%d crate_dl=%d alt=%d",
             sBenchId, CountBench(play), sLastMode >= 0 ? kModeNames[sLastMode] : "none", sLastCount, sInits, sArchNull,
             empty != nullptr, crate != nullptr, alt ? 1 : 0);
        PbMarker("%s", lines.back().c_str());
        return 0;
    }
    if (sub == "flood" && args.size() >= 2) {
        s32 k = atoi(args[1].c_str());
        if (k < 0 || k > kFloodMax) {
            Addf(lines, "propbench error=bad_flood max=%d", kFloodMax);
            return 1;
        }
        Gfx* g = sFlood;
        gSPDisplayList(g++, mat_rs_crate_355_rs_crate_355_vcol_tex22_layerOpaque);
        for (s32 i = 0; i < k; i++) {
            memset(sFloodPool[i], ((i * 37) & 0xFF) | 0x11, kFloodTexBytes);
            sFloodPool[i][0] = (u8)i;
            sFloodPool[i][1] = (u8)(i >> 8);
            // The compiled material's own load, pointed at this copy's texture.
            gDPSetTextureImage(g++, G_IM_FMT_CI, G_IM_SIZ_16b, 1, sFloodPool[i]);
            gDPSetTile(g++, G_IM_FMT_CI, G_IM_SIZ_16b, 0, 0, 7, 0, G_TX_WRAP | G_TX_NOMIRROR, 0, 0,
                       G_TX_WRAP | G_TX_NOMIRROR, 0, 0);
            gDPLoadBlock(g++, 7, 0, 0, 1023, 512);
            gDPSetTile(g++, G_IM_FMT_CI, G_IM_SIZ_4b, 4, 0, 0, 0, G_TX_WRAP | G_TX_NOMIRROR, 6, 0,
                       G_TX_WRAP | G_TX_NOMIRROR, 6, 0);
            gDPSetTileSize(g++, 0, 0, 0, 252, 252);
            gSPDisplayList(g++, rs_crate_355_rs_crate_355_mesh_layer_Opaque_tri_0);
        }
        gSPEndDisplayList(g++);
        sFloodCount = k;
        Addf(lines, "propbench flood k=%d words=%d", k, (int)(g - sFlood));
        PbMarker("%s", lines.back().c_str());
        return 0;
    }
    // sturdy-bassoon#187 routes.
    if (sub == "flip" && args.size() >= 3) {
        // flip <path pattern with two %d> <cycles,cycles,...>: frame k's list is the pattern with k.
        sFlipPaths.clear();
        sFlipCycles.clear();
        const std::string& cyc = args[2];
        size_t at = 0;
        s32 k = 0;
        while (at <= cyc.size() && k < kFlipMax) {
            size_t comma = cyc.find(',', at);
            std::string one = cyc.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            char path[256];
            snprintf(path, sizeof(path), args[1].c_str(), k, k);
            sFlipPaths.push_back(std::string("__OTR__") + path);
            sFlipCycles.push_back(atoi(one.c_str()));
            k++;
            if (comma == std::string::npos) {
                break;
            }
            at = comma + 1;
        }
        s32 resolved = 0;
        for (const std::string& fp : sFlipPaths) {
            resolved += ResourceMgr_LoadGfxByName(fp.c_str()) != nullptr ? 1 : 0;
        }
        Addf(lines, "propbench flip frames=%d resolved=%d", (int)sFlipPaths.size(), resolved);
        PbMarker("%s", lines.back().c_str());
        return 0;
    }
    if (sub == "flippin" && args.size() >= 2) {
        sFlipPinned = atoi(args[1].c_str());
        Addf(lines, "propbench flippin=%d", sFlipPinned);
        PbMarker("%s", lines.back().c_str());
        return 0;
    }
    if (sub == "split" && args.size() >= 3) {
        sArchPath = "__OTR__" + args[1];
        sSplitPath = "__OTR__" + args[2];
        Addf(lines, "propbench split base=%s resolves=%d slow=%s resolves=%d", args[1].c_str(),
             ResourceMgr_LoadGfxByName(sArchPath.c_str()) != nullptr, args[2].c_str(),
             ResourceMgr_LoadGfxByName(sSplitPath.c_str()) != nullptr);
        PbMarker("%s", lines.back().c_str());
        return 0;
    }
    // sturdy-bassoon#187 A1: the prototype's scroll, clock and record timer are gone. The scroll and the
    // clock are the built `staticbake scroll` and `staticbake clock` now.
    if (sub == "scroll" || sub == "clock" || sub == "recordtime") {
        Addf(lines, "propbench error=moved use=staticbake_scroll|staticbake_clock flip_switches=%u", sFlipSwitches);
        PbMarker("%s", lines.back().c_str());
        return 1;
    }
    // sturdy-bassoon#205: alpha curves, and lists in the room's own passes.
    if (sub == "alpha" && args.size() >= 2 && args[1] == "clear") {
        Fast::StaticBakeClearAlphaCurves();
        Addf(lines, "propbench alpha cleared=1");
        PbMarker("%s", lines.back().c_str());
        return 0;
    }
    if (sub == "alpha" && (args.size() < 2 || args[1] == "status")) {
        const Fast::StaticBakeAlphaStats st = Fast::StaticBakeGetAlphaStats();
        Addf(lines, "propbench alpha interp_vertices=%u recorded_vertices=%u clock=%.3f", st.interpVertices,
             st.recordedVertices, Fast::StaticBakeClockSeconds());
        PbMarker("%s", lines.back().c_str());
        return 0;
    }
    if (sub == "alpha" && args.size() >= 5) {
        // alpha <curve 0-7> <loop seconds> <end,end,...> <delta,delta,...>: ends are fractions of the
        // loop, deltas RS transparency steps (0-255), one per frame.
        std::vector<float> ends, deltas;
        for (int w = 0; w < 2; w++) {
            const std::string& list = args[3 + w];
            size_t at = 0;
            while (at <= list.size()) {
                size_t comma = list.find(',', at);
                (w == 0 ? ends : deltas)
                    .push_back((float)atof(list.substr(at, comma == std::string::npos ? std::string::npos : comma - at).c_str()));
                if (comma == std::string::npos) {
                    break;
                }
                at = comma + 1;
            }
        }
        for (float& d : deltas) {
            d = d / 255.0f;
        }
        const bool ok = ends.size() == deltas.size() &&
                        Fast::StaticBakeSetAlphaCurve(atoi(args[1].c_str()), (float)atof(args[2].c_str()), (int)ends.size(),
                                                      ends.data(), deltas.data());
        Addf(lines, "propbench alpha curve=%s loop=%s frames=%d ok=%d", args[1].c_str(), args[2].c_str(), (int)ends.size(), ok);
        PbMarker("%s", lines.back().c_str());
        return ok ? 0 : 1;
    }
    if (sub == "room" && args.size() >= 2) {
        // room opa|xlu <path> | room clear: a list in the room's opaque pass (where a map's prop lists go,
        // before its geometry) or its translucent pass. Registered with the bake unless its path ends in
        // _interp.
        if (args[1] == "clear") {
            sRoomOpa = sRoomXlu = nullptr;
            Addf(lines, "propbench room cleared=1");
            PbMarker("%s", lines.back().c_str());
            return 0;
        }
        if (args.size() < 3 || (args[1] != "opa" && args[1] != "xlu")) {
            Addf(lines, "propbench error=bad_room usage=room_opa|xlu_<path>|clear");
            return 1;
        }
        const std::string p = "__OTR__" + args[2];
        Gfx* dl = ResourceMgr_LoadGfxByName(p.c_str());
        const bool interp = p.size() > 7 && p.compare(p.size() - 7, 7, "_interp") == 0;
        if (dl != nullptr && !interp) {
            Fast::StaticBakeRegister(dl);
        }
        (args[1] == "opa" ? sRoomOpa : sRoomXlu) = dl;
        Addf(lines, "propbench room pass=%s path=%s resolves=%d registered=%d", args[1].c_str(), args[2].c_str(),
             dl != nullptr, dl != nullptr && !interp);
        PbMarker("%s", lines.back().c_str());
        return dl != nullptr ? 0 : 1;
    }
    if (sub == "path" && args.size() >= 2) {
        sArchPath = "__OTR__" + args[1];
        Gfx* dl = ResourceMgr_LoadGfxByName(sArchPath.c_str());
        Addf(lines, "propbench path=%s resolves=%d", args[1].c_str(), dl != nullptr);
        PbMarker("%s", lines.back().c_str());
        return 0;
    }
    if (sub == "at" && args.size() >= 5) {
        const s32 raw = atoi(args[1].c_str());
        s32 mode = raw & 0xFF;
        f32 x = (f32)atof(args[2].c_str()), y = (f32)atof(args[3].c_str()), z = (f32)atof(args[4].c_str());
        s16 ry = args.size() >= 6 ? (s16)strtol(args[5].c_str(), nullptr, 0) : 0;
        if (mode < 0 || mode >= PB_MODE_COUNT) {
            Addf(lines, "propbench error=bad_mode");
            return 1;
        }
        Actor* a = Actor_Spawn(&play->actorCtx, play, (s16)(mode == PB_NULLDRAW ? sNullDrawId : sBenchId), x, y, z, 0, ry, 0, (s16)raw);
        Addf(lines, "propbench at mode=%s xlu=%d x=%.0f y=%.0f z=%.0f ry=%d ok=%d", kModeNames[mode], (raw >> 8) & 1, x, y,
             z, (int)ry, a != nullptr);
        PbMarker("%s", lines.back().c_str());
        return a != nullptr ? 0 : 1;
    }
    if (sub == "clear") {
        KillBench(play);
        sLastMode = -1;
        sLastCount = 0;
        Addf(lines, "propbench cleared");
        PbMarker("propbench cleared");
        return 0;
    }
    if (sub == "spawn" && args.size() >= 3) {
        const s32 raw = atoi(args[1].c_str()); // sturdy-bassoon#205: + 256 draws in the translucent pass
        s32 mode = raw & 0xFF;
        s32 n = atoi(args[2].c_str());
        f32 dist = args.size() >= 4 ? (f32)atof(args[3].c_str()) : 500.0f;
        s32 cols = args.size() >= 5 ? atoi(args[4].c_str()) : 25;
        f32 spacing = args.size() >= 6 ? (f32)atof(args[5].c_str()) : 34.0f;
        if (mode < 0 || mode >= PB_MODE_COUNT || n < 0 || n > 1500 || cols < 1) {
            Addf(lines, "propbench error=bad_args");
            return 1;
        }
        KillBench(play);
        Player* player = GET_PLAYER(play);
        s16 yaw = player->actor.shape.rot.y;
        f32 fx = Math_SinS(yaw), fz = Math_CosS(yaw);
        f32 rx = fz, rz = -fx;
        Vec3f base = player->actor.world.pos;
        s32 spawned = 0;
        for (s32 i = 0; i < n; i++) {
            s32 row = i / cols;
            s32 col = i % cols;
            f32 lat = (col - (cols - 1) * 0.5f) * spacing;
            f32 x = base.x + fx * dist + rx * lat;
            f32 z = base.z + fz * dist + rz * lat;
            f32 y = base.y + row * spacing;
            if (Actor_Spawn(&play->actorCtx, play, (s16)(mode == PB_NULLDRAW ? sNullDrawId : sBenchId), x, y, z, 0, (s16)(yaw + 0x8000), 0, (s16)raw) != nullptr) {
                spawned++;
            }
        }
        sLastMode = mode;
        sLastCount = n;
        Addf(lines, "propbench spawned mode=%s n=%d spawned=%d dist=%.0f cols=%d spacing=%.0f yaw=%d", kModeNames[mode], n,
             spawned, dist, cols, spacing, (int)yaw);
        PbMarker("%s", lines.back().c_str());
        return 0;
    }
    Addf(lines, "propbench usage: status | clear | spawn <mode 0-7> <n> [dist] [cols] [spacing]");
    return 1;
}

const ConsoleSink::Command propBenchCommand("propbench", Run, "THROWAWAY #117 prop bench: status | clear | spawn <mode> <n> [dist] [cols] [spacing]",
                                            { { "sub", Ship::ArgumentType::TEXT, true },
                                              { "a", Ship::ArgumentType::TEXT, true },
                                              { "b", Ship::ArgumentType::TEXT, true },
                                              { "c", Ship::ArgumentType::TEXT, true },
                                              { "d", Ship::ArgumentType::TEXT, true },
                                              { "e", Ship::ArgumentType::TEXT, true } });

} // namespace

// sturdy-bassoon#205: the room draw's lists (z_room.c, func_80095AB4). pass 0 opaque, 1 translucent.
extern "C" Gfx* PropBench_RoomList(s32 pass) {
    return pass == 0 ? sRoomOpa : sRoomXlu;
}
