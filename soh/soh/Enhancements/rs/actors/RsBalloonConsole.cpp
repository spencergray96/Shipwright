#include "RsBalloonConsole.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/debug/Console.h>

#include "RsBalloon.h"
#include "RsBoatHud.h"
#include "RsVehicleMinimap.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

// Hyrule Field's balloon (sturdy-bassoon#274): the issue's first test ground, large, open and vanilla, so no scene
// work. On by default while the prototype lives on its branch, so a human trying the branch finds one there.
#define CVAR_RS_BALLOON_FIELD CVAR_ENHANCEMENT("RsBalloonField")

namespace {

using ConsoleSink::Addf;

// The subcommand grammar, once: the usage line and the console's help text both print it.
#define RS_BALLOON_GRAMMAR                                                                                        \
    "list | spawn [<x> <z> [<yaw> [<height>]]] | board [<idx>] | controls on|off [<idx>] | "                        \
    "drive <throttle> <turn> <lift> <ticks> | tune [default] | tune <knob> <value> | cam [on|off|default] | "        \
    "cam height|dist|ease <n> | abandon [sink|hang|home] | field [on|off] | clear"

const char* kUsage = "op=usage result=error usage=\"balloon " RS_BALLOON_GRAMMAR "\"";

// How far in front of Link `spawn` and Hyrule Field's balloon go: clear of him, its half width and some.
constexpr float kSpawnAhead = 200.0f;

// The live balloons, OLDEST FIRST (RsBoatConsole's Boats(), for its reason: a new actor goes at the head of its list).
std::vector<RsBalloon*> Balloons() {
    std::vector<RsBalloon*> balloons;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_RS_BALLOON && a->update != nullptr) {
            balloons.insert(balloons.begin(), reinterpret_cast<RsBalloon*>(a));
        }
    }
    return balloons;
}

bool ParseFloat(const std::string& s, float* out) {
    char* end = nullptr;
    *out = strtof(s.c_str(), &end);
    return end != s.c_str() && *end == '\0' && std::isfinite(*out);
}

bool ParseInt(const std::string& s, long* out) {
    char* end = nullptr;
    *out = strtol(s.c_str(), &end, 0);
    return end != s.c_str() && *end == '\0';
}

// Spawns one at (x, z) on the ground there, or `height` over it; room -1 so a room change keeps it. Looks for the
// ground from a little over Link, so a spot under a bridge he stands on is the bridge.
RsBalloon* SpawnAt(float x, float z, int16_t yaw, float height) {
    const float from = GET_PLAYER(gPlayState)->actor.world.pos.y + 200.0f;
    const float ground = RsBalloon_GroundAt(gPlayState, nullptr, x, z, yaw, from, nullptr);
    if (ground <= BGCHECK_Y_MIN) {
        return nullptr;
    }
    // The params are unused.
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_RS_BALLOON, x, ground + height, z, 0, yaw, 0, 0);
    if (a != nullptr) {
        a->room = -1;
    }
    return reinterpret_cast<RsBalloon*>(a);
}

int32_t List(std::vector<std::string>& lines) {
    const Player* player = GET_PLAYER(gPlayState);
    const std::vector<RsBalloon*> balloons = Balloons();
    for (size_t idx = 0; idx < balloons.size(); idx++) {
        const RsBalloon* b = balloons[idx];
        const Actor* a = &b->dyna.actor;
        Vec3f stand;
        RsBalloon_StandPos(b, &stand);
        Addf(lines,
             "balloon idx=%d x=%d y=%d z=%d yaw=%d alt=%d vy=%d speed=%d throttle=%d lift=%d tiller=%d controls=%d "
             "landed=%d on_water=%d ground=%d height=%d ceiling=%d blocked=%s blocks=%d link_on=%d away=%d "
             "abandoned=%d stand_dist=%d cam=%d map_arrow=%s",
             (int)idx, (int)lroundf(a->world.pos.x), (int)lroundf(a->world.pos.y), (int)lroundf(a->world.pos.z),
             (int)(u16)a->shape.rot.y, (int)lroundf(b->alt * 100.0f), (int)lroundf(b->vy * 100.0f),
             (int)lroundf(b->speed * 100.0f), (int)lroundf(b->throttle * 100.0f), (int)lroundf(b->lift * 100.0f),
             (int)lroundf(b->tiller * 100.0f), b->controls, b->landed, b->onWater, (int)lroundf(b->groundY),
             (int)lroundf(b->alt - b->groundY),
             (int)lroundf(b->launchY + RsBalloon_Tune(RS_BALLOON_TUNE_CEILING)), RsBalloon_BlockName(b->blocked),
             b->blockCount, RsBalloon_LinkAboard(b, player) ? 1 : 0, b->awayTicks, b->abandoned,
             (int)lroundf(Math_Vec3f_DistXZ(&stand, (Vec3f*)&player->actor.world.pos)),
             b->cam.subCamId != SUBCAM_NONE ? 1 : 0, RsVehicleMinimap_ArrowName(RsVehicleMinimap_Arrow(a)));
    }
    Addf(lines,
         "op=list result=ok count=%d link_x=%d link_y=%d link_z=%d link_yaw=%d link_floor_bg=%d link_ground=%d "
         "link_input_disabled=%d link_can_take=%d controls_in_reach=%d a_label=%d link_ledge=%d/%d/%d "
         "link_health=%d drive_ticks=%d abandon=%s hud=%s",
         (int)balloons.size(), (int)lroundf(player->actor.world.pos.x), (int)lroundf(player->actor.world.pos.y),
         (int)lroundf(player->actor.world.pos.z), (int)(u16)player->actor.shape.rot.y, (int)player->actor.floorBgId,
         (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 1 : 0,
         (player->stateFlags1 & PLAYER_STATE1_INPUT_DISABLED) ? 1 : 0, RsSteer_LinkCanTake(player) ? 1 : 0,
         RsBalloon_ControlsInReach(gPlayState) ? 1 : 0, (int)gPlayState->interfaceCtx.unk_1F0,
         (int)player->ledgeClimbType, (int)player->ledgeClimbDelayTimer, (int)lroundf(player->yDistToLedge),
         (int)gSaveContext.health, gRsBalloonDrive.ticks,
         RsBalloon_AbandonName(RsBalloon_AbandonMode()),
         RsBoatHud_ShownName(RsBoatHud_Shown()));
    return 0;
}

int32_t Spawn(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const Player* player = GET_PLAYER(gPlayState);
    float x;
    float z;
    float height = 0.0f;
    int16_t yaw = player->actor.shape.rot.y;

    if (args.size() == 1) {
        x = player->actor.world.pos.x + Math_SinS(yaw) * kSpawnAhead;
        z = player->actor.world.pos.z + Math_CosS(yaw) * kSpawnAhead;
    } else if (args.size() >= 3 && args.size() <= 5) {
        long y = yaw;
        if (!ParseFloat(args[1], &x) || !ParseFloat(args[2], &z) || (args.size() >= 4 && !ParseInt(args[3], &y)) ||
            (args.size() == 5 && (!ParseFloat(args[4], &height) || height < 0.0f || height > 20000.0f))) {
            Addf(lines, "op=spawn result=error error=bad_value");
            return 1;
        }
        yaw = (int16_t)y;
    } else {
        lines.push_back(kUsage);
        return 1;
    }
    RsBalloon* balloon = SpawnAt(x, z, yaw, height);
    if (balloon == nullptr) {
        Addf(lines, "op=spawn result=error error=no_ground x=%d z=%d", (int)lroundf(x), (int)lroundf(z));
        return 1;
    }
    const Actor* a = &balloon->dyna.actor;
    Addf(lines, "op=spawn result=ok idx=%d x=%d y=%d z=%d yaw=%d landed=%d", (int)Balloons().size() - 1,
         (int)lroundf(a->world.pos.x), (int)lroundf(a->world.pos.y), (int)lroundf(a->world.pos.z),
         (int)(u16)a->shape.rot.y, balloon->landed);
    return 0;
}

// The optional trailing balloon index, default 0 (the oldest). Writes the error line itself.
RsBalloon* PickBalloon(const std::vector<std::string>& args, size_t at, const char* op,
                       std::vector<std::string>& lines) {
    long idx = 0;
    if (args.size() > at && !ParseInt(args[at], &idx)) {
        Addf(lines, "op=%s result=error error=bad_value", op);
        return nullptr;
    }
    const std::vector<RsBalloon*> balloons = Balloons();
    if (idx < 0 || idx >= (long)balloons.size()) {
        Addf(lines, "op=%s result=error error=no_balloon idx=%ld count=%d", op, idx, (int)balloons.size());
        return nullptr;
    }
    return balloons[(size_t)idx];
}

int32_t Board(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    RsBalloon* balloon = PickBalloon(args, 1, "board", lines);
    if (balloon == nullptr) {
        return 1;
    }
    Player* player = GET_PLAYER(gPlayState);
    // A little over the floor: the next tick's floor check lands him on it.
    RsBalloon_StandAtControls(balloon, player, 5.0f);
    player->actor.velocity.y = 0.0f;
    Addf(lines, "op=board result=ok x=%d y=%d z=%d", (int)lroundf(player->actor.world.pos.x),
         (int)lroundf(player->actor.world.pos.y), (int)lroundf(player->actor.world.pos.z));
    return 0;
}

int32_t Controls(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() < 2 || (args[1] != "on" && args[1] != "off")) {
        lines.push_back(kUsage);
        return 1;
    }
    RsBalloon* balloon = PickBalloon(args, 2, "controls", lines);
    if (balloon == nullptr) {
        return 1;
    }
    if (!RsBalloon_SetControls(balloon, gPlayState, args[1] == "on" ? 1 : 0)) {
        Addf(lines, "op=controls result=error error=not_aboard");
        return 1;
    }
    Addf(lines, "op=controls result=ok controls=%d", balloon->controls);
    return 0;
}

int32_t Drive(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    float throttle;
    float turn;
    float lift;
    long ticks;
    if (args.size() != 5 || !ParseFloat(args[1], &throttle) || !ParseFloat(args[2], &turn) ||
        !ParseFloat(args[3], &lift) || !ParseInt(args[4], &ticks) || throttle < -1.0f || throttle > 1.0f ||
        turn < -1.0f || turn > 1.0f || lift < -1.0f || lift > 1.0f || ticks < 0 || ticks > 1200) {
        Addf(lines, "op=drive result=error error=bad_value "
                    "usage=\"drive <throttle -1..1> <turn -1..1> <lift -1..1> <ticks 0..1200>\"");
        return 1;
    }
    gRsBalloonDrive.throttle = throttle;
    gRsBalloonDrive.turn = turn;
    gRsBalloonDrive.lift = lift;
    gRsBalloonDrive.ticks = (int32_t)ticks;
    Addf(lines, "op=drive result=ok throttle=%d turn=%d lift=%d ticks=%ld", (int)lroundf(throttle * 100.0f),
         (int)lroundf(turn * 100.0f), (int)lroundf(lift * 100.0f), ticks);
    return 0;
}

// The handling knobs (RsBalloon.h's RsBalloonTuneKnob), saved: a balloon reads them every tick.
void AddTune(std::vector<std::string>& lines, const char* op) {
    std::string line = std::string("op=") + op + " result=ok";
    for (int k = 0; k < RS_BALLOON_TUNE_COUNT; k++) {
        char part[64];
        snprintf(part, sizeof(part), " %s=%g", gRsBalloonTune[k].name, (double)RsBalloon_Tune(k));
        line += part;
    }
    lines.push_back(line);
}

int32_t Tune(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2 && args[1] == "default") {
        for (int k = 0; k < RS_BALLOON_TUNE_COUNT; k++) {
            CVarClear(gRsBalloonTune[k].cvar);
        }
        CVarSave();
    } else if (args.size() == 3) {
        int knob = -1;
        for (int k = 0; k < RS_BALLOON_TUNE_COUNT; k++) {
            if (args[1] == gRsBalloonTune[k].name) {
                knob = k;
            }
        }
        float value = 0.0f;
        if (knob < 0 || !ParseFloat(args[2], &value) || value < gRsBalloonTune[knob].min ||
            value > gRsBalloonTune[knob].max) {
            std::string ranges;
            for (int k = 0; k < RS_BALLOON_TUNE_COUNT; k++) {
                char part[64];
                snprintf(part, sizeof(part), " %s=%g..%g", gRsBalloonTune[k].name, (double)gRsBalloonTune[k].min,
                         (double)gRsBalloonTune[k].max);
                ranges += part;
            }
            lines.push_back("op=tune result=error error=bad_value" + ranges);
            return 1;
        }
        CVarSetFloat(gRsBalloonTune[knob].cvar, value);
        CVarSave();
    } else if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    AddTune(lines, "tune");
    return 0;
}

// The steering camera's settings, saved, as `boat cam`.
int32_t Cam(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    struct Knob {
        const char* name;
        const char* cvar;
        int32_t def;
        int32_t min;
        int32_t max;
    };
    const Knob knobs[] = {
        { "height", CVAR_RS_BALLOON_CAM_HEIGHT, RS_BALLOON_CAM_HEIGHT_DEFAULT, RS_BALLOON_CAM_HEIGHT_MIN,
          RS_BALLOON_CAM_HEIGHT_MAX },
        { "dist", CVAR_RS_BALLOON_CAM_DIST, RS_BALLOON_CAM_DIST_DEFAULT, RS_BALLOON_CAM_DIST_MIN,
          RS_BALLOON_CAM_DIST_MAX },
        { "ease", CVAR_RS_BALLOON_CAM_EASE, RS_BALLOON_CAM_EASE_DEFAULT, RS_BALLOON_CAM_EASE_MIN,
          RS_BALLOON_CAM_EASE_MAX },
    };
    if (args.size() == 2 && (args[1] == "on" || args[1] == "off")) {
        CVarSetInteger(CVAR_RS_BALLOON_CAM, args[1] == "on" ? 1 : 0);
        CVarSave();
    } else if (args.size() == 2 && args[1] == "default") {
        CVarClear(CVAR_RS_BALLOON_CAM);
        for (const Knob& k : knobs) {
            CVarClear(k.cvar);
        }
        CVarSave();
    } else if (args.size() == 3) {
        const Knob* knob = nullptr;
        for (const Knob& k : knobs) {
            if (args[1] == k.name) {
                knob = &k;
            }
        }
        long value = 0;
        if (knob == nullptr || !ParseInt(args[2], &value) || value < knob->min || value > knob->max) {
            Addf(lines, "op=cam result=error error=bad_value height=%d..%d dist=%d..%d ease=%d..%d",
                 RS_BALLOON_CAM_HEIGHT_MIN, RS_BALLOON_CAM_HEIGHT_MAX, RS_BALLOON_CAM_DIST_MIN,
                 RS_BALLOON_CAM_DIST_MAX, RS_BALLOON_CAM_EASE_MIN, RS_BALLOON_CAM_EASE_MAX);
            return 1;
        }
        CVarSetInteger(knob->cvar, (int32_t)value);
        CVarSave();
    } else if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    Addf(lines, "op=cam result=ok cam=%s height=%d dist=%d ease=%d",
         CVarGetInteger(CVAR_RS_BALLOON_CAM, 1) ? "on" : "off",
         CVarGetInteger(CVAR_RS_BALLOON_CAM_HEIGHT, RS_BALLOON_CAM_HEIGHT_DEFAULT),
         CVarGetInteger(CVAR_RS_BALLOON_CAM_DIST, RS_BALLOON_CAM_DIST_DEFAULT),
         CVarGetInteger(CVAR_RS_BALLOON_CAM_EASE, RS_BALLOON_CAM_EASE_DEFAULT));
    return 0;
}

int32_t Abandon(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        int mode = -1;
        for (int m = 0; m < RS_BALLOON_ABANDON_COUNT; m++) {
            if (args[1] == RsBalloon_AbandonName(m)) {
                mode = m;
            }
        }
        if (mode < 0) {
            Addf(lines, "op=abandon result=error error=bad_value modes=sink|hang|home");
            return 1;
        }
        CVarSetInteger(CVAR_RS_BALLOON_ABANDON, mode);
        CVarSave();
    } else if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    Addf(lines, "op=abandon result=ok abandon=%s",
         RsBalloon_AbandonName(RsBalloon_AbandonMode()));
    return 0;
}

int32_t Field(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] != "on" && args[1] != "off") {
            lines.push_back(kUsage);
            return 1;
        }
        CVarSetInteger(CVAR_RS_BALLOON_FIELD, args[1] == "on" ? 1 : 0);
        CVarSave();
    } else if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    Addf(lines, "op=field result=ok setting=%s", CVarGetInteger(CVAR_RS_BALLOON_FIELD, 1) ? "on" : "off");
    return 0;
}

int32_t Clear(std::vector<std::string>& lines) {
    const std::vector<RsBalloon*> balloons = Balloons();
    for (RsBalloon* balloon : balloons) {
        Actor_Kill(&balloon->dyna.actor);
    }
    Addf(lines, "op=clear result=ok killed=%d", (int)balloons.size());
    return 0;
}

// --- Hyrule Field's balloon ---------------------------------------------------------------------
//
// In front of Link wherever he comes in, so every way into the field finds one. Spawned on the first player update
// after the scene loads rather than in OnSceneInit, so the actor context is certainly up. Once per scene load:
// `balloon clear` leaves the field empty until the next.
bool sFieldPending = false;

void OnSceneInitBalloon(int16_t sceneNum) {
    sFieldPending = sceneNum == SCENE_HYRULE_FIELD && CVarGetInteger(CVAR_RS_BALLOON_FIELD, 1) != 0;
}

void OnPlayerUpdateBalloon() {
    if (!sFieldPending || gPlayState == nullptr) {
        return;
    }
    sFieldPending = false;
    if (gPlayState->sceneNum == SCENE_HYRULE_FIELD) {
        const Player* player = GET_PLAYER(gPlayState);
        const int16_t yaw = player->actor.shape.rot.y;
        SpawnAt(player->actor.world.pos.x + Math_SinS(yaw) * kSpawnAhead,
                player->actor.world.pos.z + Math_CosS(yaw) * kSpawnAhead, yaw, 0.0f);
    }
}

void RegisterRsBalloon() {
    COND_HOOK(OnSceneInit, true, OnSceneInitBalloon);
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateBalloon);
}

RegisterShipInitFunc rsBalloonInitFunc(RegisterRsBalloon);

} // namespace

int32_t RsBalloonConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string& sub = args[0];
    // Settings, not scene state: they work from the title screen too.
    if (sub == "tune") {
        return Tune(args, lines);
    }
    if (sub == "cam") {
        return Cam(args, lines);
    }
    if (sub == "abandon") {
        return Abandon(args, lines);
    }
    if (sub == "field") {
        return Field(args, lines);
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=balloon result=error error=no_scene");
        return 1;
    }
    if (sub == "list" && args.size() == 1) {
        return List(lines);
    }
    if (sub == "spawn") {
        return Spawn(args, lines);
    }
    if (sub == "board") {
        return Board(args, lines);
    }
    if (sub == "controls") {
        return Controls(args, lines);
    }
    if (sub == "drive") {
        return Drive(args, lines);
    }
    if (sub == "clear" && args.size() == 1) {
        return Clear(lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `balloon` console command ----------------------------------------------

namespace {

const ConsoleSink::Command rsBalloonCommand(
    "balloon", RsBalloonConsole_Run,
    "The balloon prototype (sturdy-bassoon#274): " RS_BALLOON_GRAMMAR ". spawn puts one on the ground in front of "
    "Link (or at x z, or height over the ground there); board stands Link at its controls; controls takes or lets "
    "go of them as A and B do; drive flies the balloon at the controls for <ticks> ticks without the pad; tune sets "
    "the handling: climb, sink, lift (the inertia), drift, fwd, accel, drag, brake, turn, ceiling, abandonsink "
    "(saved); cam sets the steering camera (saved); abandon picks what a balloon left alone in the air does (sink, "
    "hang, home; saved); field switches Hyrule Field's own balloon (saved); clear kills every balloon.",
    { { "list|spawn|board|controls|drive|tune|cam|abandon|field|clear", Ship::ArgumentType::TEXT },
      { "arg1", Ship::ArgumentType::TEXT, true },
      { "arg2", Ship::ArgumentType::TEXT, true },
      { "arg3", Ship::ArgumentType::TEXT, true },
      { "arg4", Ship::ArgumentType::TEXT, true },
      { "arg5", Ship::ArgumentType::TEXT, true } });

} // namespace
