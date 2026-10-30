#include "RsBoatConsole.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/debug/Console.h>

#include "RsBoat.h"
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

// Lake Hylia's boat (sturdy-bassoon#256): the issue's first test water, a large vanilla waterbox, so no scene
// work. On by default while the prototype lives on its branch, so a human trying the branch finds a boat there.
#define CVAR_RS_BOAT_LAKE_HYLIA CVAR_ENHANCEMENT("RsBoatLakeHylia")
// The soft-sprite table's size, read by z_play.c at each scene load (sturdy-bassoon#268); unset is its default.
#define CVAR_RS_EFFECT_SLOTS CVAR_ENHANCEMENT("RsEffectSlots")

namespace {

using ConsoleSink::Addf;

// The subcommand grammar, once: the usage line and the console's help text both print it.
#define RS_BOAT_GRAMMAR                                                                                         \
    "list | spawn [<x> <z> [<yaw>]] | probe <x> <z> | water <x0> <z0> <x1> <z1> <y> | water clear | "            \
    "board [<idx>] | helm on|off [<idx>] | drive <throttle> <turn> <ticks> | lakehylia [on|off] | "             \
    "lakelevel [full|drained] | cam [on|off|default] | cam height|dist|ease <n> | hud [on|off] | "             \
    "climb [hop|hands] | kind [skiff|sloop|raft] | tune [default] | tune <knob> <value> | wake [default|reset] | "  \
    "wake slots [<n>|default] | "                                                                                   \
    "wake <knob> <value> | contact [<idx>] | still [on|off] | clear"

const char* kUsage = "op=usage result=error usage=\"boat " RS_BOAT_GRAMMAR "\"";

// Where Lake Hylia's boat waits: in the deepest part of the lake, the pond the drained lake keeps (x -1750..0,
// z 5500..7000, up to 400 deep), so it is on open water at both levels, bow to the pond's north bank. Read off
// `boat probe` in the #256 runs. The lake's main waterbox is NOT water when drained: it is lowered under the
// lakebed, and a boat put down at the first spot tried (-1080, 4450) sat on it, under the map.
constexpr float kLakeHyliaX = -875.0f;
constexpr float kLakeHyliaZ = 6000.0f;
constexpr int16_t kLakeHyliaYaw = (int16_t)0x8000;

// The test waterbox's corners and surface must stay inside a WaterBox's s16 fields, lengths included.
constexpr float kWaterLimit = 16000.0f;

// The live boats, OLDEST FIRST. Actor_AddToCategory puts a new actor at the head of its list, so walking the
// list gives newest first and every spawn would renumber the others. A killed boat (update cleared, still
// listed until the end of the tick) is skipped, so `clear` then `list` reads empty at once.
std::vector<RsBoat*> Boats() {
    std::vector<RsBoat*> boats;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_RS_BOAT && a->update != nullptr) {
            boats.insert(boats.begin(), reinterpret_cast<RsBoat*>(a));
        }
    }
    return boats;
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

// The kind `boat kind` last picked (RsBoatKindId), which Lake Hylia's boat and `spawn` put down.
s32 CurrentKind() {
    return CLAMP(CVarGetInteger(CVAR_RS_BOAT_KIND, RS_BOAT_KIND_SKIFF), 0, RS_BOAT_KIND_COUNT - 1);
}

// Spawns one at (x, z) on the water's surface there, room -1 so a room change in a multi-room scene keeps it.
RsBoat* SpawnAt(float x, float z, int16_t yaw) {
    float y = GET_PLAYER(gPlayState)->actor.world.pos.y;
    if (!RsBoat_SurfaceAt(gPlayState, x, z, &y, nullptr)) {
        return nullptr;
    }
    // The params are the kind.
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_RS_BOAT, x, y, z, 0, yaw, 0, (s16)CurrentKind());
    if (a != nullptr) {
        a->room = -1;
    }
    return reinterpret_cast<RsBoat*>(a);
}

int32_t List(std::vector<std::string>& lines) {
    const Player* player = GET_PLAYER(gPlayState);
    const std::vector<RsBoat*> boats = Boats();
    for (size_t idx = 0; idx < boats.size(); idx++) {
        const RsBoat* boat = boats[idx];
        const Actor* a = &boat->dyna.actor;
        Vec3f helm;
        RsBoat_HelmPos(boat, &helm);
        Addf(lines,
             "boat idx=%d kind=%s x=%d y=%d z=%d yaw=%d speed=%d top=%d throttle=%d tiller=%d rudder=%d helm=%d on_water=%d "
             "surface=%d blocked=%s blocks=%d touch=%s touches=%d lift=%d floor_low=%d link_on=%d helm_dist=%d "
             "cam=%d map_arrow=%s wake_rings=%d wake_held=%d",
             (int)idx, RsBoat_KindName(boat->kind), (int)lroundf(a->world.pos.x), (int)lroundf(a->world.pos.y),
             (int)lroundf(a->world.pos.z), (int)(u16)a->shape.rot.y, (int)lroundf(boat->speed * 100.0f),
             (int)lroundf(RsBoat_TopSpeed(boat) * 100.0f),
             (int)lroundf(boat->throttle * 100.0f), (int)lroundf(boat->tiller * 100.0f),
             (int)lroundf(boat->rudder * 100.0f), boat->helm,
             boat->onWater, (int)lroundf(boat->surfaceY), RsBoat_BlockName(boat->blocked), boat->blockCount,
             RsBoat_BlockName(boat->touch), boat->touchCount, (int)lroundf(boat->lift * 100.0f),
             (int)lroundf(boat->floorLow * 100.0f), RsBoat_LinkOnDeck(boat, player) ? 1 : 0,
             (int)lroundf(Math_Vec3f_DistXZ(&helm, (Vec3f*)&player->actor.world.pos)),
             boat->cam.subCamId != SUBCAM_NONE ? 1 : 0, RsVehicleMinimap_ArrowName(RsVehicleMinimap_Arrow(a)),
             boat->wakeRings, boat->wakeHeld);
    }
    Addf(lines,
         "op=list result=ok count=%d link_x=%d link_y=%d link_z=%d link_yaw=%d link_floor_bg=%d "
         "link_input_disabled=%d link_swimming=%d link_can_take_helm=%d tiller_in_reach=%d link_focus_boat=%d "
         "a_label_check=%d a_label=%d link_ledge=%d/%d/%d link_lin=%d drive_ticks=%d hud=%s",
         (int)boats.size(), (int)lroundf(player->actor.world.pos.x), (int)lroundf(player->actor.world.pos.y),
         (int)lroundf(player->actor.world.pos.z), (int)(u16)player->actor.shape.rot.y, (int)player->actor.floorBgId,
         (player->stateFlags1 & PLAYER_STATE1_INPUT_DISABLED) ? 1 : 0,
         (player->stateFlags1 & PLAYER_STATE1_IN_WATER) ? 1 : 0, RsSteer_LinkCanTake(player) ? 1 : 0,
         RsBoat_TillerInReach(gPlayState) ? 1 : 0,
         (player->focusActor != nullptr && player->focusActor->id == ACTOR_RS_BOAT) ? 1 : 0,
         gPlayState->interfaceCtx.unk_1F0 == DO_ACTION_CHECK ? 1 : 0, (int)gPlayState->interfaceCtx.unk_1F0,
         (int)player->ledgeClimbType, (int)player->ledgeClimbDelayTimer, (int)lroundf(player->yDistToLedge),
         (int)lroundf(player->linearVelocity * 100.0f), gRsBoatDrive.ticks,
         RsBoatHud_ShownName(RsBoatHud_Shown()));
    return 0;
}

int32_t Spawn(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const Player* player = GET_PLAYER(gPlayState);
    float x;
    float z;
    int16_t yaw = player->actor.shape.rot.y;

    if (args.size() == 1) {
        x = player->actor.world.pos.x + Math_SinS(yaw) * 220.0f;
        z = player->actor.world.pos.z + Math_CosS(yaw) * 220.0f;
    } else if (args.size() == 3 || args.size() == 4) {
        long y = yaw;
        if (!ParseFloat(args[1], &x) || !ParseFloat(args[2], &z) || (args.size() == 4 && !ParseInt(args[3], &y))) {
            Addf(lines, "op=spawn result=error error=bad_value");
            return 1;
        }
        yaw = (int16_t)y;
    } else {
        lines.push_back(kUsage);
        return 1;
    }
    RsBoat* boat = SpawnAt(x, z, yaw);
    if (boat == nullptr) {
        Addf(lines, "op=spawn result=error error=no_water x=%d z=%d", (int)lroundf(x), (int)lroundf(z));
        return 1;
    }
    const Actor* a = &boat->dyna.actor;
    // The newest boat is the last index (Boats() is oldest first).
    Addf(lines, "op=spawn result=ok idx=%d x=%d y=%d z=%d yaw=%d", (int)Boats().size() - 1,
         (int)lroundf(a->world.pos.x), (int)lroundf(a->world.pos.y), (int)lroundf(a->world.pos.z),
         (int)(u16)a->shape.rot.y);
    return 0;
}

// The optional trailing boat index, default 0 (the oldest). Writes the error line itself.
RsBoat* PickBoat(const std::vector<std::string>& args, size_t at, const char* op, std::vector<std::string>& lines) {
    long idx = 0;
    if (args.size() > at && !ParseInt(args[at], &idx)) {
        Addf(lines, "op=%s result=error error=bad_value", op);
        return nullptr;
    }
    const std::vector<RsBoat*> boats = Boats();
    if (idx < 0 || idx >= (long)boats.size()) {
        Addf(lines, "op=%s result=error error=no_boat idx=%ld count=%d", op, idx, (int)boats.size());
        return nullptr;
    }
    return boats[(size_t)idx];
}

// What a boat would read at (x, z): the waterbox surface, and the floor under it. Every boat's collision
// counts, so probe away from them. For finding a shore or a spawn spot.
int32_t Probe(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    float x;
    float z;
    if (args.size() != 3 || !ParseFloat(args[1], &x) || !ParseFloat(args[2], &z)) {
        Addf(lines, "op=probe result=error error=bad_value usage=\"probe <x> <z>\"");
        return 1;
    }
    float surface = GET_PLAYER(gPlayState)->actor.world.pos.y;
    const bool water = RsBoat_SurfaceAt(gPlayState, x, z, &surface, nullptr) != 0;
    Vec3f from = { x, surface + RS_BOAT_DECK_Y + RS_BOAT_HEADROOM, z }; // where the boat's own shore test starts
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    const float floorY = BgCheck_EntityRaycastFloor3(&gPlayState->colCtx, &poly, &bgId, &from);
    Addf(lines, "op=probe result=ok x=%d z=%d water=%d surface=%d floor=%d depth=%d", (int)lroundf(x),
         (int)lroundf(z), water ? 1 : 0, water ? (int)lroundf(surface) : 0, (int)lroundf(floorY),
         water ? (int)lroundf(surface - floorY) : 0);
    return 0;
}

int32_t Board(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    RsBoat* boat = PickBoat(args, 1, "board", lines);
    if (boat == nullptr) {
        return 1;
    }
    Player* player = GET_PLAYER(gPlayState);
    // A little above the deck: the next tick's floor check lands him on it.
    RsBoat_StandAtHelm(boat, player, 5.0f);
    player->actor.velocity.y = 0.0f;
    Addf(lines, "op=board result=ok x=%d y=%d z=%d", (int)lroundf(player->actor.world.pos.x),
         (int)lroundf(player->actor.world.pos.y), (int)lroundf(player->actor.world.pos.z));
    return 0;
}

int32_t Helm(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() < 2 || (args[1] != "on" && args[1] != "off")) {
        lines.push_back(kUsage);
        return 1;
    }
    RsBoat* boat = PickBoat(args, 2, "helm", lines);
    if (boat == nullptr) {
        return 1;
    }
    if (!RsBoat_SetHelm(boat, gPlayState, args[1] == "on" ? 1 : 0)) {
        Addf(lines, "op=helm result=error error=not_aboard");
        return 1;
    }
    Addf(lines, "op=helm result=ok helm=%d", boat->helm);
    return 0;
}

int32_t Drive(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    float throttle;
    float turn;
    long ticks;
    if (args.size() != 4 || !ParseFloat(args[1], &throttle) || !ParseFloat(args[2], &turn) ||
        !ParseInt(args[3], &ticks) || throttle < -1.0f || throttle > 1.0f || turn < -1.0f || turn > 1.0f ||
        ticks < 0 || ticks > 1200) {
        Addf(lines, "op=drive result=error error=bad_value "
                    "usage=\"drive <throttle -1..1> <turn -1..1> <ticks 0..1200>\"");
        return 1;
    }
    gRsBoatDrive.throttle = throttle;
    gRsBoatDrive.turn = turn;
    gRsBoatDrive.ticks = (int32_t)ticks;
    Addf(lines, "op=drive result=ok throttle=%d turn=%d ticks=%ld", (int)lroundf(throttle * 100.0f),
         (int)lroundf(turn * 100.0f), ticks);
    return 0;
}

int32_t LakeHylia(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] != "on" && args[1] != "off") {
            lines.push_back(kUsage);
            return 1;
        }
        CVarSetInteger(CVAR_RS_BOAT_LAKE_HYLIA, args[1] == "on" ? 1 : 0);
        CVarSave();
    }
    Addf(lines, "op=lakehylia result=ok setting=%s", CVarGetInteger(CVAR_RS_BOAT_LAKE_HYLIA, 1) ? "on" : "off");
    return 0;
}

// Lake Hylia's water level is one event flag, EVENTCHKINF_RAISED_LAKE_HYLIA_WATER, which the Water Temple's
// ending sets. Adult Link without it gets the drained lake (z_scene_table.c, Bg_Spot06_Objects); a child always
// gets the full one. The flag is read when the scene loads, so a change shows on the next entry. It lives in the
// save, so `agenttest save <n>` keeps it.
int32_t LakeLevel(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "full") {
            Flags_SetEventChkInf(EVENTCHKINF_RAISED_LAKE_HYLIA_WATER);
        } else if (args[1] == "drained") {
            Flags_UnsetEventChkInf(EVENTCHKINF_RAISED_LAKE_HYLIA_WATER);
        } else {
            lines.push_back(kUsage);
            return 1;
        }
    }
    Addf(lines, "op=lakelevel result=ok raised=%d adult=%d in_lake=%d",
         Flags_GetEventChkInf(EVENTCHKINF_RAISED_LAKE_HYLIA_WATER) ? 1 : 0, LINK_IS_ADULT ? 1 : 0,
         gPlayState->sceneNum == SCENE_LAKE_HYLIA ? 1 : 0);
    return 0;
}

// The steering camera's settings, saved: on/off, and the eye's height over the deck, its distance behind Link and
// how fast it eases. A boat at the helm reads them every tick, so a change shows at once.
int32_t Cam(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    struct Knob {
        const char* name;
        const char* cvar;
        int32_t def;
        int32_t min;
        int32_t max;
    };
    const Knob knobs[] = {
        { "height", CVAR_RS_BOAT_CAM_HEIGHT, RS_BOAT_CAM_HEIGHT_DEFAULT, RS_BOAT_CAM_HEIGHT_MIN,
          RS_BOAT_CAM_HEIGHT_MAX },
        { "dist", CVAR_RS_BOAT_CAM_DIST, RS_BOAT_CAM_DIST_DEFAULT, RS_BOAT_CAM_DIST_MIN, RS_BOAT_CAM_DIST_MAX },
        { "ease", CVAR_RS_BOAT_CAM_EASE, RS_BOAT_CAM_EASE_DEFAULT, RS_BOAT_CAM_EASE_MIN, RS_BOAT_CAM_EASE_MAX },
    };
    if (args.size() == 2 && (args[1] == "on" || args[1] == "off")) {
        CVarSetInteger(CVAR_RS_BOAT_CAM, args[1] == "on" ? 1 : 0);
        CVarSave();
    } else if (args.size() == 2 && args[1] == "default") {
        CVarClear(CVAR_RS_BOAT_CAM);
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
                 RS_BOAT_CAM_HEIGHT_MIN, RS_BOAT_CAM_HEIGHT_MAX, RS_BOAT_CAM_DIST_MIN, RS_BOAT_CAM_DIST_MAX,
                 RS_BOAT_CAM_EASE_MIN, RS_BOAT_CAM_EASE_MAX);
            return 1;
        }
        CVarSetInteger(knob->cvar, (int32_t)value);
        CVarSave();
    } else if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    Addf(lines, "op=cam result=ok cam=%s height=%d dist=%d ease=%d",
         CVarGetInteger(CVAR_RS_BOAT_CAM, 1) ? "on" : "off",
         CVarGetInteger(CVAR_RS_BOAT_CAM_HEIGHT, RS_BOAT_CAM_HEIGHT_DEFAULT),
         CVarGetInteger(CVAR_RS_BOAT_CAM_DIST, RS_BOAT_CAM_DIST_DEFAULT),
         CVarGetInteger(CVAR_RS_BOAT_CAM_EASE, RS_BOAT_CAM_EASE_DEFAULT));
    return 0;
}

int32_t Hud(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] != "on" && args[1] != "off") {
            lines.push_back(kUsage);
            return 1;
        }
        CVarSetInteger(CVAR_RS_BOAT_HUD, args[1] == "on" ? 1 : 0);
        CVarSave();
    } else if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    Addf(lines, "op=hud result=ok hud=%s shown=%s", CVarGetInteger(CVAR_RS_BOAT_HUD, 1) ? "on" : "off",
         RsBoatHud_ShownName(RsBoatHud_Shown()));
    return 0;
}

// Holds every boat level and at its float height, no bob and no rock (not saved: a test aid, #262), so a line test
// or a capture against the hull meets it where its boat-space numbers say.
int32_t Still(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] != "on" && args[1] != "off") {
            lines.push_back(kUsage);
            return 1;
        }
        gRsBoatStill = args[1] == "on" ? 1 : 0;
    } else if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    Addf(lines, "op=still result=ok still=%s", gRsBoatStill ? "on" : "off");
    return 0;
}

// How Link gets from the deck onto a rail (saved): the hands-on climb a prop and a covered bow get (the default, the
// owner's pick, #262), or the boat's hop. A boat reads it every tick.
int32_t Climb(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] != "hop" && args[1] != "hands") {
            lines.push_back(kUsage);
            return 1;
        }
        CVarSetInteger(CVAR_RS_BOAT_INTERIOR_CLIMB, args[1] == "hands" ? 1 : 0);
        CVarSave();
    } else if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    Addf(lines, "op=climb result=ok climb=%s", CVarGetInteger(CVAR_RS_BOAT_INTERIOR_CLIMB, RS_BOAT_INTERIOR_CLIMB_DEFAULT) ? "hands" : "hop");
    return 0;
}

// A table of saved float knobs (RsBoatTuneInfo): the handling's (`tune`) and the wake's (`wake`). A boat reads them
// every tick, so a change shows at once. `read` is the table's clamped getter.
struct KnobTable {
    const char* op;
    const RsBoatTuneInfo* knobs;
    int count;
    f32 (*read)(s32 knob);
};

std::string KnobValues(const KnobTable& table) {
    std::string line;
    for (int k = 0; k < table.count; k++) {
        char part[64];
        snprintf(part, sizeof(part), " %s=%g", table.knobs[k].name, (double)table.read(k));
        line += part;
    }
    return line;
}

// `<op>` reads the knobs, `<op> default` clears them, `<op> <knob> <value>` sets one. `extra` is appended to the
// result line (the wake's pool figures).
int32_t KnobCommand(const KnobTable& table, const std::vector<std::string>& args, std::vector<std::string>& lines,
                    const std::string& extra = "") {
    if (args.size() == 2 && args[1] == "default") {
        for (int k = 0; k < table.count; k++) {
            CVarClear(table.knobs[k].cvar);
        }
        CVarSave();
    } else if (args.size() == 3) {
        int knob = -1;
        for (int k = 0; k < table.count; k++) {
            if (args[1] == table.knobs[k].name) {
                knob = k;
            }
        }
        float value = 0.0f;
        if (knob < 0 || !ParseFloat(args[2], &value) || value < table.knobs[knob].min ||
            value > table.knobs[knob].max) {
            std::string ranges;
            for (int k = 0; k < table.count; k++) {
                char part[64];
                snprintf(part, sizeof(part), " %s=%g..%g", table.knobs[k].name, (double)table.knobs[k].min,
                         (double)table.knobs[k].max);
                ranges += part;
            }
            lines.push_back(std::string("op=") + table.op + " result=error error=bad_value" + ranges);
            return 1;
        }
        CVarSetFloat(table.knobs[knob].cvar, value);
        CVarSave();
    } else if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    lines.push_back(std::string("op=") + table.op + " result=ok" + KnobValues(table) + extra);
    return 0;
}

const KnobTable kTune = { "tune", gRsBoatTune, RS_BOAT_TUNE_COUNT, RsBoat_Tune };
const KnobTable kWake = { "wake", gRsBoatWake, RS_BOAT_WAKE_COUNT, RsBoat_WakeTune };

// The soft-sprite pool as it stands (RsBoat_EffectPool), for the `wake` line: `pool_used` of `pool_size` slots,
// `ripples` of them water rings (anyone's), `wake` of them the wake's own, EffectSs_FindSlot's counts since the game
// started (`yielded` by a wake effect, `evicted` from any other, `refused`), every boat's own counts summed (`rings`
// spawned, `held` for want of room) and `types=<EFFECT_SS_* id>:<count>,...` (effect_ss_table.h).
std::string PoolCensus() {
    RsBoatEffectPool pool = {};
    int rings = 0;
    int held = 0;
    if (gPlayState != nullptr) {
        RsBoat_EffectPool(&pool);
        for (const RsBoat* boat : Boats()) {
            rings += boat->wakeRings;
            held += boat->wakeHeld;
        }
    }
    char line[256];
    snprintf(line, sizeof(line),
             " pool_size=%d pool_used=%d pool_peak=%d ripples=%d wake=%d yielded=%d evicted=%d refused=%d rings=%d "
             "held=%d types=",
             (int)pool.size, (int)pool.used, (int)pool.peak, (int)pool.ripples, (int)pool.wake, (int)pool.yielded,
             (int)pool.evicted, (int)pool.refused, rings, held);
    int count[256] = { 0 }; // EffectSs.type is a u8
    for (s32 i = 0; i < pool.size; i++) {
        const EffectSs* e = RsBoat_EffectSlot(i);
        if (e != nullptr) {
            count[e->type]++;
        }
    }
    std::string types;
    for (int t = 0; t < 256; t++) {
        if (count[t] > 0) {
            types += (types.empty() ? "" : ",") + std::to_string(t) + ":" + std::to_string(count[t]);
        }
    }
    return line + types;
}

// The kind Lake Hylia's boat and `boat spawn` put down (saved). In Lake Hylia, with its boat on, every boat is
// killed and the lake's put back at its spot as the new kind at once, so a kind can be tried without leaving;
// elsewhere it shows on the next spawn.
int32_t Kind(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int respawned = 0;
    if (args.size() == 2) {
        const s32 kind = RsBoat_KindByName(args[1].c_str());
        if (kind < 0) {
            std::string kinds;
            for (s32 k = 0; k < RS_BOAT_KIND_COUNT; k++) {
                kinds += (k > 0 ? "," : "") + std::string(RsBoat_KindName(k));
            }
            lines.push_back("op=kind result=error error=bad_value kinds=" + kinds);
            return 1;
        }
        CVarSetInteger(CVAR_RS_BOAT_KIND, kind);
        CVarSave();
        if (gPlayState != nullptr && gPlayState->sceneNum == SCENE_LAKE_HYLIA &&
            CVarGetInteger(CVAR_RS_BOAT_LAKE_HYLIA, 1) != 0) {
            for (RsBoat* boat : Boats()) {
                Actor_Kill(&boat->dyna.actor);
            }
            respawned = SpawnAt(kLakeHyliaX, kLakeHyliaZ, kLakeHyliaYaw) != nullptr ? 1 : 0;
        }
    } else if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    const s32 kind = CurrentKind();
    Addf(lines, "op=kind result=ok kind=%s polys=%d respawned=%d", RsBoat_KindName(kind), RsBoat_KindPolyCount(kind),
         respawned);
    return 0;
}

int32_t Contact(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    RsBoat* boat = PickBoat(args, 1, "contact", lines);
    if (boat == nullptr) {
        return 1;
    }
    char points[900];
    const s32 deepest = RsBoat_ContactReport(boat, gPlayState, points, (s32)sizeof(points));
    Addf(lines, "op=contact result=ok deepest=%s %s", RsBoat_BlockName(deepest), points);
    return 0;
}

int32_t Clear(std::vector<std::string>& lines) {
    const std::vector<RsBoat*> boats = Boats();
    for (RsBoat* boat : boats) {
        Actor_Kill(&boat->dyna.actor);
    }
    Addf(lines, "op=clear result=ok killed=%d", (int)boats.size());
    return 0;
}

// --- the test waterbox ---------------------------------------------------------------------------
//
// `water <x0> <z0> <x1> <z1> <y>` points the collision context at a COPY of the scene's collision header whose
// waterbox list is the scene's plus one box. The scene's own header (a compiled-in scene's static struct, or a
// vanilla scene's cached resource) is never written, so nothing outlives the visit: the next scene load builds
// its context from its own header and the copy is simply no longer pointed at.
//
// The box is in every room and has camera index 0, which the camera reads as "no setting of its own" and gives
// the swimming camera (Camera_GetWaterBoxDataIdx).
constexpr u32 kMaxWaterBoxes = 64;
constexpr u32 kWaterBoxAllRooms = 0x3Fu << 13; // properties bits 13-18, read by WaterBox_GetSurfaceImpl
WaterBox sTestBoxes[kMaxWaterBoxes];
CollisionHeader sTestHeader;
CollisionHeader* sSceneHeader = nullptr; // the header the copy was made from

bool TestWaterLive() {
    return gPlayState != nullptr && gPlayState->colCtx.colHeader == &sTestHeader;
}

void RestoreWater() {
    if (TestWaterLive()) {
        gPlayState->colCtx.colHeader = sSceneHeader;
    }
    sSceneHeader = nullptr;
    gRsBoatTestWater.active = 0;
}

int32_t Water(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2 && args[1] == "clear") {
        const bool had = TestWaterLive();
        RestoreWater();
        Addf(lines, "op=water result=ok cleared=%d", had ? 1 : 0);
        return 0;
    }
    float x0;
    float z0;
    float x1;
    float z1;
    float y;
    if (args.size() != 6 || !ParseFloat(args[1], &x0) || !ParseFloat(args[2], &z0) || !ParseFloat(args[3], &x1) ||
        !ParseFloat(args[4], &z1) || !ParseFloat(args[5], &y) || fabsf(x0) > kWaterLimit ||
        fabsf(z0) > kWaterLimit || fabsf(x1) > kWaterLimit || fabsf(z1) > kWaterLimit || fabsf(y) > kWaterLimit) {
        Addf(lines, "op=water result=error error=bad_value limit=%d usage=\"water <x0> <z0> <x1> <z1> <y> | "
                    "water clear\"",
             (int)kWaterLimit);
        return 1;
    }
    // A second `water` replaces the first box rather than stacking on it.
    CollisionHeader* scene = TestWaterLive() ? sSceneHeader : gPlayState->colCtx.colHeader;
    // A header with no waterboxes may point its list at segment 0 rather than null (WaterBox_GetSurfaceImpl's
    // test), so the count alone says how many there are.
    const u32 count = scene->numWaterBoxes;
    if (count + 1 > kMaxWaterBoxes) {
        Addf(lines, "op=water result=error error=too_many have=%u max=%u", count, kMaxWaterBoxes - 1);
        return 1;
    }
    for (u32 i = 0; i < count; i++) {
        sTestBoxes[i] = scene->waterBoxes[i];
    }
    WaterBox* box = &sTestBoxes[count];
    box->xMin = (s16)lroundf(fminf(x0, x1));
    box->zMin = (s16)lroundf(fminf(z0, z1));
    box->xLength = (s16)lroundf(fabsf(x1 - x0));
    box->zLength = (s16)lroundf(fabsf(z1 - z0));
    box->ySurface = (s16)lroundf(y);
    box->properties = kWaterBoxAllRooms;

    sTestHeader = *scene;
    sTestHeader.waterBoxes = sTestBoxes;
    sTestHeader.numWaterBoxes = count + 1;
    sSceneHeader = scene;
    gPlayState->colCtx.colHeader = &sTestHeader;

    gRsBoatTestWater.x0 = (float)box->xMin;
    gRsBoatTestWater.z0 = (float)box->zMin;
    gRsBoatTestWater.x1 = (float)(box->xMin + box->xLength);
    gRsBoatTestWater.z1 = (float)(box->zMin + box->zLength);
    gRsBoatTestWater.y = (float)box->ySurface;
    gRsBoatTestWater.active = 1;
    Addf(lines, "op=water result=ok x0=%d z0=%d x1=%d z1=%d y=%d boxes=%u", box->xMin, box->zMin,
         box->xMin + box->xLength, box->zMin + box->zLength, box->ySurface, count + 1);
    return 0;
}

// --- Lake Hylia's boat --------------------------------------------------------------------------
//
// Spawned on the first player update after the scene loads rather than in OnSceneInit, so the actor context
// and the waterboxes are certainly up. Once per scene load: `boat clear` leaves the lake empty until the next.
bool sLakeHyliaPending = false;

void OnSceneInitBoat(int16_t sceneNum) {
    // The new scene's collision context is its own; the test water's copy is no longer pointed at.
    RestoreWater();
    sLakeHyliaPending = sceneNum == SCENE_LAKE_HYLIA && CVarGetInteger(CVAR_RS_BOAT_LAKE_HYLIA, 1) != 0;
}

void OnPlayerUpdateBoat() {
    if (gPlayState == nullptr) {
        return;
    }
    RsBoat_SmoothLinkOnDeck(gPlayState);
    if (!sLakeHyliaPending) {
        return;
    }
    sLakeHyliaPending = false;
    if (gPlayState->sceneNum == SCENE_LAKE_HYLIA) {
        SpawnAt(kLakeHyliaX, kLakeHyliaZ, kLakeHyliaYaw);
    }
}

void RegisterRsBoat() {
    COND_HOOK(OnSceneInit, true, OnSceneInitBoat);
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateBoat);
}

RegisterShipInitFunc rsBoatInitFunc(RegisterRsBoat);

} // namespace

int32_t RsBoatConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string& sub = args[0];
    if (sub == "lakehylia") {
        return LakeHylia(args, lines);
    }
    // Settings, not scene state: they work from the title screen too.
    if (sub == "cam") {
        return Cam(args, lines);
    }
    if (sub == "hud") {
        return Hud(args, lines);
    }
    if (sub == "climb") {
        return Climb(args, lines);
    }
    if (sub == "still") {
        return Still(args, lines);
    }
    if (sub == "tune") {
        return KnobCommand(kTune, args, lines);
    }
    if (sub == "wake") {
        // `wake reset` zeroes the pool's peak, so a run can read one moment's demand; not a knob.
        if (args.size() == 2 && args[1] == "reset") {
            RsBoat_EffectPoolResetPeak();
            lines.push_back("op=wake result=ok reset=1" + PoolCensus());
            return 0;
        }
        // `wake slots [<n>|default]`: the soft-sprite table's size, which z_play.c reads at each scene load (saved;
        // shows on the next load). Not a wake knob, but the lever the wake's share of it pulls. 85 is the game's size
        // and 512 z_play.c's PLAY_EFFECT_SS_SLOTS_MAX, which it clamps to anyway.
        if (args.size() >= 2 && args[1] == "slots") {
            long slots = 0;
            if (args.size() == 3 && args[2] == "default") {
                CVarClear(CVAR_RS_EFFECT_SLOTS);
                CVarSave();
            } else if (args.size() == 3 && ParseInt(args[2], &slots) && slots >= 0x55 && slots <= 512) {
                CVarSetInteger(CVAR_RS_EFFECT_SLOTS, (int32_t)slots);
                CVarSave();
            } else if (args.size() != 2) {
                Addf(lines, "op=wake result=error error=bad_value slots=85..512|default");
                return 1;
            }
            Addf(lines, "op=wake result=ok slots=%d set=%d", (int)CVarGetInteger(CVAR_RS_EFFECT_SLOTS, 0),
                 CVarGetInteger(CVAR_RS_EFFECT_SLOTS, 0) != 0 ? 1 : 0);
            lines.back() += PoolCensus();
            return 0;
        }
        return KnobCommand(kWake, args, lines, PoolCensus());
    }
    if (sub == "kind") {
        return Kind(args, lines);
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=boat result=error error=no_scene");
        return 1;
    }
    if (sub == "list" && args.size() == 1) {
        return List(lines);
    }
    if (sub == "spawn") {
        return Spawn(args, lines);
    }
    if (sub == "water") {
        return Water(args, lines);
    }
    if (sub == "probe") {
        return Probe(args, lines);
    }
    if (sub == "board") {
        return Board(args, lines);
    }
    if (sub == "helm") {
        return Helm(args, lines);
    }
    if (sub == "drive") {
        return Drive(args, lines);
    }
    if (sub == "contact") {
        return Contact(args, lines);
    }
    if (sub == "clear" && args.size() == 1) {
        return Clear(lines);
    }
    if (sub == "lakelevel") {
        return LakeLevel(args, lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `boat` console command -------------------------------------------------

namespace {

const ConsoleSink::Command rsBoatCommand(
    "boat", RsBoatConsole_Run,
    "The sailing prototype's boats (sturdy-bassoon#256): " RS_BOAT_GRAMMAR ". spawn puts one on the water in "
    "front of Link (or at x z); probe reads the water and floor at a point; water adds a test waterbox to this "
    "scene until it reloads; board stands Link at a boat's tiller; helm takes or lets go of it as A and B do; "
    "drive steers the boat at the helm for <ticks> ticks without the pad; lakehylia switches Lake Hylia's own "
    "boat (saved); lakelevel fills or drains Lake Hylia (its event flag, in the save; shows on the next entry); "
    "cam sets the steering camera (height over the deck, distance behind, ease in percent a tick; saved); "
    "hud switches the steering label; kind picks the hull Lake Hylia's boat and spawn use (saved; puts Lake "
    "Hylia's boat back as that kind); tune sets the handling: fwd (top speed for every kind; 0 for each kind's own), "
    "rev (astern, a share of top speed, for every kind; 0 for each kind's own), accel, drag, brake, turnstill and turnmove (binang a tick), steps (0: the "
    "stick's throttle is smooth), upgrade (1: each kind's upgraded top speed); saved; wake sets the trail a moving "
    "boat leaves: on, spacing (units between rings), size (times the hull's own), grow (how much a ring grows at top "
    "speed), lines (1 or 2), bow (the bow wave), foam (share of top speed for a stern splash; 0: none), reserve "
    "(effect slots kept free for everything else); saved; it also reports the effect pool; contact lists what the "
    "hull's outline meets where it is; still holds every boat level, with no bob or rock (a test aid, not saved).",
    { { "list|spawn|probe|water|board|helm|drive|lakehylia|lakelevel|cam|hud|kind|tune|wake|contact|still|clear",
        Ship::ArgumentType::TEXT },
      { "arg1", Ship::ArgumentType::TEXT, true },
      { "arg2", Ship::ArgumentType::TEXT, true },
      { "arg3", Ship::ArgumentType::TEXT, true },
      { "arg4", Ship::ArgumentType::TEXT, true },
      { "arg5", Ship::ArgumentType::TEXT, true } });

} // namespace
