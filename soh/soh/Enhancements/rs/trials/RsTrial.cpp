#include "RsTrial.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <imgui.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/actors/RsActors.h"
#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/Enhancements/rs/actors/RsBoatConsole.h"
#include "soh/ShipInit.hpp"
// OPEN_DISPS declares these at block scope; seen here first, they keep C linkage in a C++ file (EquipFlight.cpp's way).
#include "soh/frame_interpolation.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"

extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

// --- the course -----------------------------------------------------------------------------------
//
// The world export's frame (tools/grid-scene-tool README, "World export"): the matrix's origin tile's south-west
// corner is (0, 0), RS east is +X and RS north is -Z, 40 units a tile. A tile's centre is half a tile in from both.
constexpr int kOriginX = 3040;
constexpr int kOriginY = 2848;
constexpr float kTile = 40.0f;

float TileX(int rsX) {
    return (rsX - kOriginX) * kTile + kTile / 2;
}
float TileZ(int rsY) {
    return (kOriginY - rsY) * kTile - kTile / 2;
}

enum Rank { RANK_SWORDFISH, RANK_SHARK, RANK_MARLIN, RANK_COUNT };
const char* const kRankNames[RANK_COUNT] = { "swordfish", "shark", "marlin" };
// RS's target times in seconds (the trial's wiki page), scaled by RS_TRIAL_TARGET_SCALE for a run's target.
const int kRankTargetSeconds[RANK_COUNT] = { 108, 171, 270 };

struct Crate {
    int16_t rsX, rsY;
    uint8_t rank; // the first rank that needs it; every later rank does too
};

// The cache's crate spots, loc 59245 + index (the multiloc parents that become 59244 "Lost supplies"), at their
// placed tiles. Swordfish's 14 are on the wiki's Swordfish route; splitting the other 22 at 59269 gives the 11 that
// make Shark's inferred 25 (docs/notes/2026-10-31-tempor-tantrum-research.md, "The crate ids follow the ranks").
constexpr int kCrateCount = 36;
constexpr int kFirstCrateLoc = 59245;
const Crate kCrates[kCrateCount] = {
    { 2994, 2891, RANK_SWORDFISH }, { 2978, 2866, RANK_SWORDFISH }, { 2981, 2848, RANK_SWORDFISH },
    { 2990, 2808, RANK_SWORDFISH }, { 3012, 2768, RANK_SWORDFISH }, { 3057, 2792, RANK_SWORDFISH },
    { 3065, 2811, RANK_SWORDFISH }, { 3077, 2825, RANK_SWORDFISH }, { 3078, 2863, RANK_SWORDFISH },
    { 3053, 2920, RANK_SWORDFISH }, { 2978, 2828, RANK_SWORDFISH }, { 3001, 2788, RANK_SWORDFISH },
    { 3082, 2875, RANK_SWORDFISH }, { 3084, 2896, RANK_SWORDFISH }, { 3018, 2889, RANK_SHARK },
    { 3001, 2866, RANK_SHARK },     { 3004, 2834, RANK_SHARK },     { 3006, 2819, RANK_SHARK },
    { 3028, 2789, RANK_SHARK },     { 3077, 2776, RANK_SHARK },     { 3082, 2801, RANK_SHARK },
    { 3093, 2835, RANK_SHARK },     { 3093, 2882, RANK_SHARK },     { 3059, 2904, RANK_SHARK },
    { 3079, 2906, RANK_SHARK },     { 2963, 2882, RANK_MARLIN },    { 2965, 2865, RANK_MARLIN },
    { 2957, 2829, RANK_MARLIN },    { 2953, 2809, RANK_MARLIN },    { 2966, 2795, RANK_MARLIN },
    { 2987, 2777, RANK_MARLIN },    { 3037, 2762, RANK_MARLIN },    { 3030, 2815, RANK_MARLIN },
    { 3095, 2775, RANK_MARLIN },    { 3119, 2855, RANK_MARLIN },    { 3060, 2883, RANK_MARLIN },
};

// The start: the skiff's centre a few tiles south of the Barracudas' boat (RS 3036, 2926), bow to the south (+Z).
const int kStartRsX = 3036;
const int kStartRsY = 2922;
const int16_t kStartYaw = 0;

// The area: the twelve buoys' bounding box, RS x 2947-3131, y 2753-2935, at its tiles' outer edges.
const float kAreaX0 = (2947 - kOriginX) * kTile;
const float kAreaX1 = (3131 + 1 - kOriginX) * kTile;
const float kAreaZ0 = (kOriginY - (2935 + 1)) * kTile;
const float kAreaZ1 = (kOriginY - 2753) * kTile;

// The skiff's hull in its own space (RsBoat.c's sRsBoatKinds: stern -112, bow +112, gunwale ~48 at most).
constexpr float kHullHalfBeam = 50.0f;
constexpr float kHullStern = -112.0f;
constexpr float kHullBow = 112.0f;

// --- a run ------------------------------------------------------------------------------------------

enum State { STATE_IDLE, STATE_RUNNING, STATE_FINISHED, STATE_FAILED };
const char* const kStateNames[] = { "idle", "running", "finished", "failed" };

struct Run {
    State state = STATE_IDLE;
    int rank = -1;
    int32_t ticks = 0;     // the clock, from the first tick the skiff moves
    bool moving = false;   // the skiff has moved since the start
    uint64_t collected = 0; // a bit per crate
    int32_t outTicks = 0;  // ticks Link has spent outside the area in a row
};
Run sRun;
bool sPending = false;    // flood and spawn on the next player update in the scene
int32_t sHelmTicks = 0;   // ticks left in which a placement puts Link back at the tiller and takes the helm

bool InTrialScene() {
    return gPlayState != nullptr && gPlayState->sceneNum == SCENE_TEMPOR_TANTRUM;
}

bool CrateLive(int n) {
    return sRun.state != STATE_IDLE && sRun.rank >= 0 && kCrates[n].rank <= sRun.rank;
}

bool CrateCollected(int n) {
    return (sRun.collected >> n) & 1;
}

int RankTotal(int rank) {
    int total = 0;
    for (int n = 0; n < kCrateCount; n++) {
        total += kCrates[n].rank <= rank ? 1 : 0;
    }
    return total;
}

int CollectedCount() {
    int count = 0;
    for (int n = 0; n < kCrateCount; n++) {
        count += CrateLive(n) && CrateCollected(n) ? 1 : 0;
    }
    return count;
}

int32_t TargetTicks(int rank) {
    return rank < 0 ? 0 : (int32_t)lroundf(kRankTargetSeconds[rank] * RS_TRIAL_TARGET_SCALE * 20.0f);
}

bool Inside(float x, float z) {
    return x >= kAreaX0 && x <= kAreaX1 && z >= kAreaZ0 && z <= kAreaZ1;
}

// The trial's skiff: the oldest live skiff in the scene.
RsBoat* TrialBoat() {
    if (gPlayState == nullptr) {
        return nullptr;
    }
    RsBoat* found = nullptr;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_RS_BOAT && a->update != nullptr &&
            reinterpret_cast<RsBoat*>(a)->kind == RS_BOAT_KIND_SKIFF) {
            found = reinterpret_cast<RsBoat*>(a); // the list is newest first, so the last one seen is the oldest
        }
    }
    return found;
}

void Mark(const char* fmt, ...) {
    char line[240];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    RsAgent_Marker(line);
}

// Moves the skiff to (x, z) heading `yaw`, stopped, and stands Link at its helm. The hull's previous position moves
// with it, so the engine does not read the jump as the platform carrying whoever stands on it.
void PlaceBoat(RsBoat* boat, float x, float z, int16_t yaw) {
    Actor* a = &boat->dyna.actor;
    // Let go first, as B would, so the jump is not a Link knocked off the tiller mid-steer.
    if (boat->helm) {
        RsBoat_SetHelm(boat, gPlayState, 0);
    }
    a->world.pos.x = a->prevPos.x = a->home.pos.x = x;
    a->world.pos.z = a->prevPos.z = a->home.pos.z = z;
    a->world.rot.y = a->shape.rot.y = yaw;
    boat->speed = 0.0f;
    boat->tiller = 0.0f;
    boat->yawVel = 0;
    Player* player = GET_PLAYER(gPlayState);
    RsBoat_StandAtHelm(boat, player, 5.0f);
    player->actor.velocity.y = 0.0f;
    // The jump can leave Link off the tiller, which lets go of the helm: OnPlayerUpdateTrial puts him back and takes
    // it again, for up to two seconds.
    sHelmTicks = 40;
}

void StartRun(int rank) {
    sRun = Run();
    sRun.state = STATE_RUNNING;
    sRun.rank = rank;
    Mark("rs_trial event=start rank=%s total=%d target_ticks=%d", kRankNames[rank], RankTotal(rank), TargetTicks(rank));
}

// --- the scene load -----------------------------------------------------------------------------------

void OnSceneInitTrial(int16_t sceneNum) {
    sRun = Run();
    sPending = sceneNum == SCENE_TEMPOR_TANTRUM;
    sHelmTicks = 0;
}

void OnPlayerUpdateTrial() {
    if (gPlayState == nullptr) {
        return;
    }
    // After a placement: Link back at the tiller if the jump left him off it (in the water beside the hull, or
    // elsewhere on deck), then the helm once he is standing on the deck.
    if (sHelmTicks > 0) {
        sHelmTicks--;
        RsBoat* boat = TrialBoat();
        Player* player = GET_PLAYER(gPlayState);
        // The whole window, even while the helm reads as held: a jump made at the helm only lets go of it on the
        // boat's next update, and the platform can still carry Link off by the jump a tick later.
        if (boat != nullptr && !boat->helm) {
            Vec3f helm;
            RsBoat_HelmPos(boat, &helm);
            if ((player->stateFlags1 & PLAYER_STATE1_IN_WATER) ||
                Math_Vec3f_DistXZ(&helm, &player->actor.world.pos) > 40.0f) {
                RsBoat_StandAtHelm(boat, player, 5.0f);
                player->actor.velocity.y = 0.0f;
            } else if (RsBoat_LinkOnDeck(boat, player)) {
                RsBoat_SetHelm(boat, gPlayState, 1);
            }
        }
    }
    if (!sPending || !InTrialScene()) {
        return;
    }
    sPending = false;
    // The whole world, its nine squares at their outer edges.
    RsBoatConsole_AddWater(-96 * kTile, -96 * kTile, 96 * kTile, 96 * kTile, RS_TRIAL_WATER_Y);
    Actor* boat = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_RS_BOAT, TileX(kStartRsX), RS_TRIAL_WATER_Y,
                              TileZ(kStartRsY), 0, kStartYaw, 0, RS_BOAT_KIND_SKIFF);
    Actor* manager =
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_RS_TRIAL, 0.0f, RS_TRIAL_WATER_Y, 0.0f, 0, 0, 0, 0);
    if (manager != nullptr) {
        manager->room = -1;
    }
    if (boat != nullptr) {
        boat->room = -1;
        PlaceBoat(reinterpret_cast<RsBoat*>(boat), boat->world.pos.x, boat->world.pos.z, kStartYaw);
    }
}

void RegisterRsTrial() {
    COND_HOOK(OnSceneInit, true, OnSceneInitTrial);
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateTrial);
}

RegisterShipInitFunc rsTrialInitFunc(RegisterRsTrial);

// --- drawing -------------------------------------------------------------------------------------------
//
// A crate is a placeholder box with a pole over it, so it can be seen across the course. RsBoat.c's placeholder
// boxes' way: per-face normals, PRIMITIVE x SHADE, the colour per face group.
Vtx sCrateVtx[24];
Vtx sPoleVtx[24];
bool sBuilt = false;

void SetVtx(Vtx* v, s16 x, s16 y, s16 z, s8 nx, s8 ny, s8 nz) {
    v->n.ob[0] = x;
    v->n.ob[1] = y;
    v->n.ob[2] = z;
    v->n.flag = 0;
    v->n.tc[0] = 0;
    v->n.tc[1] = 0;
    v->n.n[0] = nx;
    v->n.n[1] = ny;
    v->n.n[2] = nz;
    v->n.a = 255;
}

// Top, bottom, +x, -x, +z, -z, four vertices each round its edge (RsBoat_BuildBox's order).
void BuildBox(Vtx* out, s16 x0, s16 y0, s16 z0, s16 x1, s16 y1, s16 z1) {
    SetVtx(&out[0], x0, y1, z0, 0, 127, 0);
    SetVtx(&out[1], x1, y1, z0, 0, 127, 0);
    SetVtx(&out[2], x1, y1, z1, 0, 127, 0);
    SetVtx(&out[3], x0, y1, z1, 0, 127, 0);
    SetVtx(&out[4], x0, y0, z0, 0, -127, 0);
    SetVtx(&out[5], x1, y0, z0, 0, -127, 0);
    SetVtx(&out[6], x1, y0, z1, 0, -127, 0);
    SetVtx(&out[7], x0, y0, z1, 0, -127, 0);
    SetVtx(&out[8], x1, y0, z0, 127, 0, 0);
    SetVtx(&out[9], x1, y1, z0, 127, 0, 0);
    SetVtx(&out[10], x1, y1, z1, 127, 0, 0);
    SetVtx(&out[11], x1, y0, z1, 127, 0, 0);
    SetVtx(&out[12], x0, y0, z0, -127, 0, 0);
    SetVtx(&out[13], x0, y1, z0, -127, 0, 0);
    SetVtx(&out[14], x0, y1, z1, -127, 0, 0);
    SetVtx(&out[15], x0, y0, z1, -127, 0, 0);
    SetVtx(&out[16], x0, y0, z1, 0, 0, 127);
    SetVtx(&out[17], x1, y0, z1, 0, 0, 127);
    SetVtx(&out[18], x1, y1, z1, 0, 0, 127);
    SetVtx(&out[19], x0, y1, z1, 0, 0, 127);
    SetVtx(&out[20], x0, y0, z0, 0, 0, -127);
    SetVtx(&out[21], x1, y0, z0, 0, 0, -127);
    SetVtx(&out[22], x1, y1, z0, 0, 0, -127);
    SetVtx(&out[23], x0, y1, z0, 0, 0, -127);
}

// --- the HUD -------------------------------------------------------------------------------------------
//
// RsBoatHud.cpp's arrangement: an ImGui window over the game, centred under the hearts' row, only in the trial's scene.
const char* const kWindowKey = "Barracuda Trial##RS";
const ImVec4 kWhite = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
const ImVec4 kGreen = ImVec4(0.35f, 1.0f, 0.35f, 1.0f);
const ImVec4 kRed = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);

void FormatTicks(char* out, size_t size, int32_t ticks) {
    const int32_t tenths = ticks / 2; // 20 ticks a second
    snprintf(out, size, "%d:%02d.%d", (int)(tenths / 600), (int)((tenths / 10) % 60), (int)(tenths % 10));
}

class RsTrialHudWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }

    void Draw() override {
        if (!InTrialScene() || gPlayState->pauseCtx.state != 0) {
            return;
        }
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.16f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("RsTrialHud", nullptr, flags);
        ImGui::SetWindowFontScale(1.4f);
        if (sRun.state == STATE_IDLE) {
            ImGui::TextColored(kWhite, "Tempor Tantrum: start a run from the console (trial start swordfish)");
        } else {
            char time[16];
            char target[16];
            FormatTicks(time, sizeof(time), sRun.ticks);
            FormatTicks(target, sizeof(target), TargetTicks(sRun.rank));
            ImGui::TextColored(kWhite, "%s   Supplies %d/%d   %s  (target %s)", kRankNames[sRun.rank], CollectedCount(),
                               RankTotal(sRun.rank), time, target);
            if (sRun.state == STATE_RUNNING && CollectedCount() == RankTotal(sRun.rank)) {
                ImGui::TextColored(kGreen, "Every crate collected: back to the start");
            }
            if (sRun.state == STATE_RUNNING && sRun.outTicks > 0) {
                ImGui::TextColored(kRed, "Outside the trial area: %d s to come back",
                                   (int)((RS_TRIAL_OUT_TICKS - sRun.outTicks + 19) / 20));
            }
            if (sRun.state == STATE_FINISHED) {
                ImGui::TextColored(sRun.ticks <= TargetTicks(sRun.rank) ? kGreen : kWhite, "Finished in %s%s", time,
                                   sRun.ticks <= TargetTicks(sRun.rank) ? ": rank reached" : "");
            }
            if (sRun.state == STATE_FAILED) {
                ImGui::TextColored(kRed, "Failed: left the trial area");
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(2);
    }
};

void RegisterRsTrialHud() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui->GetGuiWindow(kWindowKey) != nullptr) {
        return;
    }
    gui->AddGuiWindow(std::make_shared<RsTrialHudWindow>("", true, std::string(kWindowKey)));
}

RegisterShipInitFunc rsTrialHudInitFunc(RegisterRsTrialHud);

} // namespace

// At file scope, not in the anonymous namespace: OPEN_DISPS declares FrameInterpolation_* at block scope, which would
// otherwise name functions of that namespace and fail to link.
static void DrawBox(GraphicsContext* gfxCtx, Vtx* vtx, u8 r, u8 g, u8 b, u8 topR, u8 topG, u8 topB) {
    OPEN_DISPS(gfxCtx);
    gSPVertex(POLY_OPA_DISP++, (uintptr_t)vtx, 24, 0);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, topR, topG, topB, 255);
    gSP2Triangles(POLY_OPA_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, 255);
    gSP2Triangles(POLY_OPA_DISP++, 4, 5, 6, 0, 4, 6, 7, 0);
    gSP2Triangles(POLY_OPA_DISP++, 8, 9, 10, 0, 8, 10, 11, 0);
    gSP2Triangles(POLY_OPA_DISP++, 12, 13, 14, 0, 12, 14, 15, 0);
    gSP2Triangles(POLY_OPA_DISP++, 16, 17, 18, 0, 16, 18, 19, 0);
    gSP2Triangles(POLY_OPA_DISP++, 20, 21, 22, 0, 20, 22, 23, 0);
    CLOSE_DISPS(gfxCtx);
}

// --- the actor -------------------------------------------------------------------------------------------

extern "C" void RsTrial_Init(Actor* thisx, PlayState* play) {
    if (!sBuilt) {
        BuildBox(sCrateVtx, -15, -12, -15, 15, 18, 15);
        BuildBox(sPoleVtx, -2, 18, -2, 2, 160, 2);
        sBuilt = true;
    }
}

extern "C" void RsTrial_Destroy(Actor* thisx, PlayState* play) {
}

extern "C" void RsTrial_Update(Actor* thisx, PlayState* play) {
    if (sRun.state != STATE_RUNNING) {
        return;
    }
    RsBoat* boat = TrialBoat();
    Player* player = GET_PLAYER(play);

    if (boat != nullptr && !sRun.moving && fabsf(boat->speed) > 0.01f) {
        sRun.moving = true;
    }
    if (sRun.moving) {
        sRun.ticks++;
    }

    // Pickups: a live crate inside the hull's rectangle, grown by the margin, in the hull's own frame.
    if (boat != nullptr) {
        const Actor* a = &boat->dyna.actor;
        const f32 s = Math_SinS(a->shape.rot.y);
        const f32 c = Math_CosS(a->shape.rot.y);
        for (int n = 0; n < kCrateCount; n++) {
            if (!CrateLive(n) || CrateCollected(n)) {
                continue;
            }
            const f32 dx = TileX(kCrates[n].rsX) - a->world.pos.x;
            const f32 dz = TileZ(kCrates[n].rsY) - a->world.pos.z;
            const f32 along = dx * s + dz * c;
            const f32 across = dx * c - dz * s;
            if (fabsf(across) <= kHullHalfBeam + RS_TRIAL_PICKUP_MARGIN && along >= kHullStern - RS_TRIAL_PICKUP_MARGIN &&
                along <= kHullBow + RS_TRIAL_PICKUP_MARGIN) {
                sRun.collected |= 1ull << n;
                Sfx_PlaySfxCentered(NA_SE_SY_GET_RUPY);
                Mark("rs_trial event=crate n=%d loc=%d collected=%d total=%d ticks=%d speed=%d", n, kFirstCrateLoc + n,
                     CollectedCount(), RankTotal(sRun.rank), sRun.ticks, (int)lroundf(boat->speed * 100.0f));
            }
        }
    }

    // The area: Link, wherever he is (on the deck, or swimming).
    if (Inside(player->actor.world.pos.x, player->actor.world.pos.z)) {
        if (sRun.outTicks > 0) {
            Mark("rs_trial event=in ticks=%d out_ticks=%d", sRun.ticks, sRun.outTicks);
        }
        sRun.outTicks = 0;
    } else {
        if (sRun.outTicks == 0) {
            Mark("rs_trial event=out ticks=%d x=%d z=%d", sRun.ticks, (int)lroundf(player->actor.world.pos.x),
                 (int)lroundf(player->actor.world.pos.z));
        }
        if (++sRun.outTicks >= RS_TRIAL_OUT_TICKS) {
            sRun.state = STATE_FAILED;
            Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
            Mark("rs_trial event=fail reason=out_of_area ticks=%d collected=%d total=%d", sRun.ticks, CollectedCount(),
                 RankTotal(sRun.rank));
            return;
        }
    }

    // The finish: every crate, and the skiff back at the start.
    if (boat != nullptr && CollectedCount() == RankTotal(sRun.rank)) {
        const f32 dx = boat->dyna.actor.world.pos.x - TileX(kStartRsX);
        const f32 dz = boat->dyna.actor.world.pos.z - TileZ(kStartRsY);
        if (dx * dx + dz * dz <= RS_TRIAL_FINISH_RADIUS * RS_TRIAL_FINISH_RADIUS) {
            sRun.state = STATE_FINISHED;
            Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
            Mark("rs_trial event=finish rank=%s ticks=%d target_ticks=%d reached=%d collected=%d", kRankNames[sRun.rank],
                 sRun.ticks, TargetTicks(sRun.rank), sRun.ticks <= TargetTicks(sRun.rank) ? 1 : 0, CollectedCount());
        }
    }
}

extern "C" void RsTrial_Draw(Actor* thisx, PlayState* play) {
    if (sRun.state == STATE_IDLE) {
        return;
    }
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL_25Opa(gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, 0, SHADE, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, SHADE, 0, 0, 0, 0,
                      PRIMITIVE);
    CLOSE_DISPS(gfxCtx);

    for (int n = 0; n < kCrateCount; n++) {
        if (!CrateLive(n) || CrateCollected(n)) {
            continue;
        }
        // A slow bob, out of step from crate to crate.
        const f32 bob = 3.0f * Math_SinS((s16)(play->gameplayFrames * 600 + n * 7000));
        Matrix_Translate(TileX(kCrates[n].rsX), RS_TRIAL_WATER_Y + bob, TileZ(kCrates[n].rsY), MTXMODE_NEW);
        Matrix_RotateY((f32)BINANG_TO_RAD((s16)(n * 5000)), MTXMODE_APPLY);
        OPEN_DISPS(gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        CLOSE_DISPS(gfxCtx);
        DrawBox(gfxCtx, sCrateVtx, 120, 78, 40, 170, 124, 70);
        DrawBox(gfxCtx, sPoleVtx, 255, 210, 40, 255, 230, 90);
    }

    OPEN_DISPS(gfxCtx);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    CLOSE_DISPS(gfxCtx);
}

// --- the console -------------------------------------------------------------------------------------------

namespace {

int32_t Usage(std::vector<std::string>& lines) {
    lines.push_back("op=usage result=error usage=\"trial status | start <swordfish|shark|marlin> | reset | crates | "
                    "goto <0-35>\"");
    return 1;
}

int32_t Status(std::vector<std::string>& lines) {
    const RsBoat* boat = TrialBoat();
    const Player* player = gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
    const float lx = player != nullptr ? player->actor.world.pos.x : 0.0f;
    const float lz = player != nullptr ? player->actor.world.pos.z : 0.0f;
    Addf(lines,
         "op=status result=ok here=%d state=%s rank=%s ticks=%d moving=%d collected=%d total=%d out_ticks=%d "
         "target_ticks=%d boat=%d boat_x=%d boat_z=%d link_x=%d link_z=%d inside=%d",
         InTrialScene() ? 1 : 0, kStateNames[sRun.state], sRun.rank >= 0 ? kRankNames[sRun.rank] : "none", sRun.ticks,
         sRun.moving ? 1 : 0, CollectedCount(), sRun.rank >= 0 ? RankTotal(sRun.rank) : 0, sRun.outTicks,
         TargetTicks(sRun.rank), boat != nullptr ? 1 : 0, boat != nullptr ? (int)lroundf(boat->dyna.actor.world.pos.x) : 0,
         boat != nullptr ? (int)lroundf(boat->dyna.actor.world.pos.z) : 0, (int)lroundf(lx), (int)lroundf(lz),
         Inside(lx, lz) ? 1 : 0);
    return 0;
}

int32_t Crates(std::vector<std::string>& lines) {
    for (int n = 0; n < kCrateCount; n++) {
        Addf(lines, "crate n=%d loc=%d rank=%s x=%d z=%d live=%d collected=%d", n, kFirstCrateLoc + n,
             kRankNames[kCrates[n].rank], (int)TileX(kCrates[n].rsX), (int)TileZ(kCrates[n].rsY), CrateLive(n) ? 1 : 0,
             CrateCollected(n) ? 1 : 0);
    }
    Addf(lines, "op=crates result=ok count=%d", kCrateCount);
    return 0;
}

// Whether a straight run from (x, z) to the crate meets a wall of the scene's own (the land): the hull's centre line
// and both sides, at the gunwale's height over the water. A boat's collision in the way does not count.
bool ApproachClear(float x, float z, float cx, float cz, int16_t yaw) {
    const float side[] = { 0.0f, kHullHalfBeam, -kHullHalfBeam };
    const f32 c = Math_CosS(yaw);
    const f32 s = Math_SinS(yaw);
    for (float off : side) {
        Vec3f from = { x + off * c, RS_TRIAL_WATER_Y + 20.0f, z - off * s };
        Vec3f to = { cx + off * c, RS_TRIAL_WATER_Y + 20.0f, cz - off * s };
        Vec3f hit;
        CollisionPoly* poly = nullptr;
        s32 bgId = BGCHECK_SCENE;
        if (BgCheck_EntityLineTest1(&gPlayState->colCtx, &from, &to, &hit, &poly, 1, 0, 0, 1, &bgId) &&
            bgId == BGCHECK_SCENE) {
            return false;
        }
    }
    return true;
}

int32_t Goto(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    char* end = nullptr;
    const long n = args.size() == 2 ? strtol(args[1].c_str(), &end, 10) : -1;
    if (args.size() != 2 || end == args[1].c_str() || *end != '\0' || n < 0 || n >= kCrateCount) {
        return Usage(lines);
    }
    RsBoat* boat = TrialBoat();
    if (boat == nullptr) {
        Addf(lines, "op=goto result=error error=no_boat");
        return 1;
    }
    const float cx = TileX(kCrates[n].rsX);
    const float cz = TileZ(kCrates[n].rsY);
    // The bow two tiles short of the pickup's edge, tried from each of eight headings until one is clear of land.
    const float back = kHullBow + RS_TRIAL_PICKUP_MARGIN + 2 * kTile;
    for (int i = 0; i < 8; i++) {
        const int16_t yaw = (int16_t)(i * 0x2000);
        const float x = cx - back * Math_SinS(yaw);
        const float z = cz - back * Math_CosS(yaw);
        if (!ApproachClear(x, z, cx, cz, yaw)) {
            continue;
        }
        PlaceBoat(boat, x, z, yaw);
        Addf(lines, "op=goto result=ok n=%ld loc=%ld x=%d z=%d yaw=%d", n, kFirstCrateLoc + n, (int)lroundf(x),
             (int)lroundf(z), (int)(u16)yaw);
        return 0;
    }
    Addf(lines, "op=goto result=error error=no_approach n=%ld", n);
    return 1;
}

} // namespace

int32_t RsTrialConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        return Usage(lines);
    }
    const std::string& sub = args[0];
    if (sub == "status" && args.size() == 1) {
        return Status(lines);
    }
    if (sub == "crates" && args.size() == 1) {
        return Crates(lines);
    }
    if (sub != "start" && sub != "reset" && sub != "goto") {
        return Usage(lines);
    }
    if (!InTrialScene()) {
        Addf(lines, "op=%s result=error error=not_here", sub.c_str());
        return 1;
    }
    if (sub == "reset" && args.size() == 1) {
        sRun = Run();
        Addf(lines, "op=reset result=ok");
        return 0;
    }
    if (sub == "goto") {
        return Goto(args, lines);
    }
    int rank = -1;
    for (int r = 0; r < RANK_COUNT && args.size() == 2; r++) {
        if (args[1] == kRankNames[r]) {
            rank = r;
        }
    }
    if (rank < 0) {
        return Usage(lines);
    }
    RsBoat* boat = TrialBoat();
    if (boat == nullptr) {
        Addf(lines, "op=start result=error error=no_boat");
        return 1;
    }
    PlaceBoat(boat, TileX(kStartRsX), TileZ(kStartRsY), kStartYaw);
    StartRun(rank);
    Addf(lines, "op=start result=ok rank=%s total=%d target_ticks=%d", kRankNames[rank], RankTotal(rank),
         TargetTicks(rank));
    return 0;
}

namespace {

const ConsoleSink::Command rsTrialCommand(
    "trial", RsTrialConsole_Run,
    "The Tempor Tantrum Barracuda Trial (sturdy-bassoon#289, crude cut): status | start <swordfish|shark|marlin> | "
    "reset | crates | goto <0-35>. start puts the skiff back at the start and runs the clock until every crate of the "
    "rank is collected and the skiff is back; goto puts the skiff two tiles short of a crate heading at it (a test "
    "aid). Only in the trial's scene (tempor_tantrum).",
    { { "status|start|reset|crates|goto", Ship::ArgumentType::TEXT }, { "arg", Ship::ArgumentType::TEXT, true } });

} // namespace
