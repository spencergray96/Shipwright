#include "SwimStamina.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/stairs/Stairs.h"
#include "soh/Enhancements/rs/warps/WarpDef.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
// z_player.c's, undeclared in any header: the damage path the void-out uses, without DamageMult on top.
s32 Player_InflictDamageModified(PlayState* play, s32 damage, u8 modified);
}

// See SwimStamina.h for the rules. This file is the budget, the drowning and where he goes back to.

namespace {

// --- drown points ---------------------------------------------------------------------------------
//
// Where a drowned Link is put back when he has no refuge. A point is an RS SURFACE TILE (y north), the
// frame the music zones use, because it never moves when a world is re-baked or grows; its scene's
// anchor turns it into OoT units (the world export's include, below). He lands on the tile's centre,
// on whatever floor a ray finds there, facing `yaw`.
//
// `unlockFlag` is a world flag (WorldFlagIds.h) that has to be set for the point to be used, or -1 for
// always. Among the unlocked points in Link's scene the nearest wins. Port Sarim is the default and is
// always unlocked: walkable from the start, so drowning never takes Link anywhere he could not walk to.
// A new point unlocks only once it is as easy to reach (the owner, 2026-10-25).
struct DrownPoint {
    const char* name; // snake_case, for markers
    int16_t sceneId;
    int32_t rsX;
    int32_t rsY;
    int16_t yaw;
    int32_t unlockFlag;
};

constexpr int32_t kAlwaysUnlocked = -1;

const DrownPoint kDrownPoints[] = {
    // Port Sarim's street, south of Betty's, east of the docks.
    { "port_sarim", SCENE_F2P_OVERWORLD, 3029, 3240, 0, kAlwaysUnlocked },
};
constexpr int32_t kDrownPointCount = (int32_t)(sizeof(kDrownPoints) / sizeof(kDrownPoints[0]));

// Each world scene's anchor: the RS tile whose NORTH-west corner is OoT (0, 0), as the world export
// writes it for the zone director (the `directorRs` pair).
struct SceneAnchor {
    int16_t sceneId;
    int32_t rsX;
    int32_t rsY;
    int32_t unitsPerTile;
};

#define RS_WORLD_SCENE(scene_, originRsX_, originRsY_, directorRsX_, directorRsY_, unitsPerTile_) \
    { scene_, directorRsX_, directorRsY_, unitsPerTile_ },
#define RS_WORLD_CHUNK(scene_, squareX_, squareY_, title_, nodeTitle_, nodeRsX_, nodeRsY_, nodeX_, nodeZ_)
const SceneAnchor kAnchors[] = {
#include "soh/custom/scenes/grid_tool/f2p_overworld/f2p_overworld_world.inc"
};
#undef RS_WORLD_SCENE
#undef RS_WORLD_CHUNK

const SceneAnchor* AnchorOf(int16_t sceneId) {
    for (const SceneAnchor& a : kAnchors) {
        if (a.sceneId == sceneId && a.unitsPerTile > 0) {
            return &a;
        }
    }
    return nullptr;
}

// --- state ------------------------------------------------------------------------------------------

// DIVE: the budget ran out under water in an A-dive; he finishes it and drowns at the surface.
enum Phase { PHASE_IDLE, PHASE_DIVE, PHASE_SINKING, PHASE_MOVING };

// The longest a dive may hold the drown off, in ticks. A dive is a few seconds down and back up; this
// only stops something that keeps him under (a held item, a grab) from holding it off for ever.
constexpr int32_t kDiveGraceMaxTicks = 200;

struct Refuge {
    bool set = false;
    Actor* actor = nullptr; // NULL: x/y/z/yaw are a world position
    int16_t actorId = 0;    // with the pointer, so a recycled actor slot is not mistaken for the boat
    Vec3f pos = { 0.0f, 0.0f, 0.0f };
    int16_t yaw = 0;
};

struct State {
    Phase phase = PHASE_IDLE;
    int32_t budget = RS_SWIM_STAMINA_DEFAULT_SECONDS * 20;
    int32_t sinkTicks = 0;
    bool ownsClock = false;
    Refuge refuge;
    int32_t drowns = 0;
    char lastRespawn[48] = "none";
    // Last tick's reading, for `swim status`.
    bool swimming = false;
    bool grounded = false;
    float depth = 0.0f; // Player's yDistToWater: how far his feet are below the surface
    const char* holdReason = "none";
};

State sState;

void Marker(const char* fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    AgentTest_WriteMarker(line);
}

// STANDING counts as land, wading included: on a floor with his head above the surface, boots or not (the
// owner, 2026-10-26). "Head above" is Player's own line, ageProperties->unk_2C (56 units deep adult, 32
// young): deeper than that it muffles the audio and runs its underwater timer, and without boots he
// swims. Player's IN_WATER is no use here: in iron boots it stays set walking out until the water is only
// unk_24 deep (36 adult), so it would drain him in a band where he is standing with his head out.
bool IsGrounded(Player* player) {
    if (!(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        return false;
    }
    return !(player->stateFlags1 & PLAYER_STATE1_IN_WATER) ||
           player->actor.yDistToWater < player->ageProperties->unk_2C;
}

// In the water and not standing: swimming, diving, or sunk to the bottom in iron boots with his head under
// (the owner, 2026-10-26: boots that force a sink keep the budget draining). Player sets IN_WATER on
// entering deep water even in iron boots on the floor (z_player.c, func_8083D36C).
bool IsSwimming(Player* player) {
    return (player->stateFlags1 & PLAYER_STATE1_IN_WATER) && !IsGrounded(player);
}

// Under water in an A-dive: Player's DIVING flag (set by the A press, kept after it) while UNDERWATER (cleared
// back at the surface). Not in iron boots, which sink him without a dive and never bring him back up.
bool InDive(Player* player) {
    return (player->stateFlags2 & PLAYER_STATE2_DIVING) && (player->stateFlags2 & PLAYER_STATE2_UNDERWATER) &&
           player->currentBoots != PLAYER_BOOTS_IRON;
}

// The main timer belongs to somebody else: running, and not as the swim clock.
bool TimerHeldElsewhere() {
    return gSaveContext.timerState != TIMER_STATE_OFF && !sState.ownsClock;
}

// Whether `actor` is still in the actor lists, as the same kind of actor. A refuge's pointer is never
// followed until this says so: the boat may have been killed since he left the helm.
bool ActorAlive(PlayState* play, Actor* actor, int16_t id) {
    for (int32_t cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = play->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            if (a == actor && a->id == id) {
                return true;
            }
        }
    }
    return false;
}

void ClearRefuge(const char* cause) {
    if (!sState.refuge.set) {
        return;
    }
    sState.refuge = Refuge();
    Marker("rs_swim event=refuge_cleared cause=%s", cause);
}

// The refuge as a world position, or false when there is none any more.
bool RefugeWorld(PlayState* play, Vec3f* pos, int16_t* yaw) {
    const Refuge& r = sState.refuge;
    if (!r.set) {
        return false;
    }
    if (r.actor == nullptr) {
        *pos = r.pos;
        *yaw = r.yaw;
        return true;
    }
    if (!ActorAlive(play, r.actor, r.actorId)) {
        ClearRefuge("actor_gone");
        return false;
    }
    const float s = Math_SinS(r.actor->shape.rot.y);
    const float c = Math_CosS(r.actor->shape.rot.y);
    pos->x = r.actor->world.pos.x + r.pos.x * c + r.pos.z * s;
    pos->y = r.actor->world.pos.y + r.pos.y;
    pos->z = r.actor->world.pos.z - r.pos.x * s + r.pos.z * c;
    *yaw = r.actor->shape.rot.y + r.yaw;
    return true;
}

// A drown point in OoT units, on the floor a ray finds under its tile. False when its scene is not the
// one loaded, or nothing is under it.
bool PlacePoint(PlayState* play, const DrownPoint& p, Vec3f* out, bool* wet) {
    const SceneAnchor* a = AnchorOf(p.sceneId);
    if (a == nullptr || play->sceneNum != p.sceneId) {
        return false;
    }
    Vec3f probe;
    probe.x = ((float)(p.rsX - a->rsX) + 0.5f) * (float)a->unitsPerTile;
    probe.z = ((float)(a->rsY - p.rsY) + 0.5f) * (float)a->unitsPerTile;
    probe.y = 10000.0f;
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    const f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (floorY <= BGCHECK_Y_MIN + 1.0f) {
        return false;
    }
    out->x = probe.x;
    out->y = floorY;
    out->z = probe.z;
    if (wet != nullptr) {
        f32 surface = 0.0f;
        WaterBox* box = nullptr;
        *wet = WaterBox_GetSurface1(play, &play->colCtx, probe.x, probe.z, &surface, &box) && surface > floorY;
    }
    return true;
}

bool PointUnlocked(const DrownPoint& p) {
    return p.unlockFlag == kAlwaysUnlocked || Flags_GetWorldFlag(p.unlockFlag) != 0;
}

// The nearest unlocked point in this scene, or -1.
int32_t NearestPoint(PlayState* play, Vec3f from, Vec3f* pos) {
    int32_t best = -1;
    float bestDist = 0.0f;
    for (int32_t i = 0; i < kDrownPointCount; i++) {
        Vec3f p;
        if (!PointUnlocked(kDrownPoints[i]) || !PlacePoint(play, kDrownPoints[i], &p, nullptr)) {
            continue;
        }
        const float d = Math_Vec3f_DistXZ(&from, &p);
        if (best < 0 || d < bestDist) {
            best = i;
            bestDist = d;
            *pos = p;
        }
    }
    return best;
}

void BeginSinking(Player* player, const char* cause) {
    sState.phase = PHASE_SINKING;
    sState.sinkTicks = 0;
    sState.budget = 0;
    sState.drowns++;
    Player_PlaySfx(&player->actor, player->ageProperties->unk_92 + NA_SE_VO_LI_DAMAGE_S);
    Marker("rs_swim event=drown cause=%s x=%.0f y=%.0f z=%.0f", cause, player->actor.world.pos.x,
           player->actor.world.pos.y, player->actor.world.pos.z);
}

// The sink is over: put him somewhere dry.
void Respawn(PlayState* play, Player* player) {
    Vec3f pos;
    int16_t yaw = 0;
    bool found = false;
    char name[48];
    if (RefugeWorld(play, &pos, &yaw)) {
        found = true;
        std::snprintf(name, sizeof(name), "refuge");
    } else {
        const int32_t point = NearestPoint(play, player->actor.world.pos, &pos);
        if (point >= 0) {
            found = true;
            yaw = kDrownPoints[point].yaw;
            std::snprintf(name, sizeof(name), "point:%s", kDrownPoints[point].name);
        }
    }
    if (found) {
        RsWarpMoveDest dest = {};
        dest.fromTile = 0;
        dest.toTile = 0;
        dest.x = pos.x;
        dest.y = pos.y;
        dest.z = pos.z;
        dest.yaw = yaw;
        dest.room = play->roomCtx.curRoom.num;
        dest.entrance = RS_WARP_HERE;
        dest.toMap = 0;
        const int32_t result = RsStair_BeginWarpMove(&dest, "swim");
        if (result == RS_STAIR_OK) {
            sState.phase = PHASE_MOVING;
            std::snprintf(sState.lastRespawn, sizeof(sState.lastRespawn), "%s", name);
            Marker("rs_swim event=respawn to=%s x=%.0f y=%.0f z=%.0f yaw=%d", name, pos.x, pos.y, pos.z, yaw);
            return;
        }
        Marker("rs_swim event=respawn_refused to=%s result=%s", name, RsStair_ResultName(result));
    }
    // Vanilla's void-out: the scene's respawn point and its own damage, as En_Stream's whirlpool does it.
    player->stateFlags2 |= PLAYER_STATE2_FORCED_VOID_OUT;
    sState.phase = PHASE_IDLE;
    sState.budget = RsSwim_MaxTicks();
    std::snprintf(sState.lastRespawn, sizeof(sState.lastRespawn), "voidout");
    Marker("rs_swim event=respawn to=voidout");
}

// Whether the swim clock should show: swimming, or in the air with part of the budget spent - a hop
// out of the water onto a deck, so the clock does not vanish and replay its preview on the way.
bool WantsClock() {
    return RsSwim_Enabled() && sState.phase == PHASE_IDLE &&
           (sState.swimming || (!sState.grounded && sState.budget < RsSwim_MaxTicks()));
}

void OnPlayerUpdateSwim() {
    PlayState* play = gPlayState;
    if (play == nullptr || GET_PLAYER(play) == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(play);
    const int32_t max = RsSwim_MaxTicks();
    sState.swimming = IsSwimming(player);
    sState.grounded = IsGrounded(player);
    sState.depth = player->actor.yDistToWater;
    sState.holdReason = "none";

    // The refuge is spent by standing on anything but the boat it names.
    if (sState.refuge.set && sState.grounded) {
        DynaPolyActor* floor = player->actor.floorBgId != BGCHECK_SCENE
                                   ? DynaPoly_GetActor(&play->colCtx, player->actor.floorBgId)
                                   : nullptr;
        if (sState.refuge.actor == nullptr || floor == nullptr || &floor->actor != sState.refuge.actor) {
            ClearRefuge("land");
        }
    }

    if (!RsSwim_Enabled()) {
        sState.phase = PHASE_IDLE;
        sState.budget = max;
        return;
    }
    if (sState.budget > max) {
        sState.budget = max;
    }

    switch (sState.phase) {
        case PHASE_IDLE:
            if (sState.grounded) {
                sState.budget = max;
                return;
            }
            if (!sState.swimming) {
                return; // in the air: hold
            }
            if (play->msgCtx.msgMode != MSGMODE_NONE) {
                sState.holdReason = "message";
                return;
            }
            if (Player_InCsMode(play) || RsStair_IsMoving()) {
                sState.holdReason = "cutscene";
                return;
            }
            if (TimerHeldElsewhere()) {
                sState.holdReason = "timer";
                return;
            }
            if (--sState.budget <= 0) {
                if (InDive(player)) {
                    // Out under water mid-dive: let the dive finish (the owner, 2026-10-26).
                    sState.budget = 0;
                    sState.sinkTicks = 0;
                    sState.phase = PHASE_DIVE;
                    Marker("rs_swim event=drown_deferred reason=dive");
                } else {
                    BeginSinking(player, "budget");
                }
            }
            return;
        case PHASE_DIVE:
            if (sState.grounded) {
                // Standing on something, however he got there: that refills it, as always.
                sState.phase = PHASE_IDLE;
                sState.budget = max;
                Marker("rs_swim event=drown_cancelled reason=grounded");
                return;
            }
            if (InDive(player) && ++sState.sinkTicks < kDiveGraceMaxTicks) {
                return;
            }
            // Back at the surface (or out of time): he has already been under, so no second sink - straight
            // to wherever he goes back to.
            sState.drowns++;
            Player_PlaySfx(&player->actor, player->ageProperties->unk_92 + NA_SE_VO_LI_DAMAGE_S);
            Marker("rs_swim event=drown cause=after_dive x=%.0f y=%.0f z=%.0f", player->actor.world.pos.x,
                   player->actor.world.pos.y, player->actor.world.pos.z);
            Respawn(play, player);
            return;
        case PHASE_SINKING:
            // En_Stream's pull: velocity eased down, from after Player's own update, so his buoyancy
            // loses. He can still steer, which is fine for a second and a half.
            Math_SmoothStepToF(&player->actor.velocity.y, -3.0f, 0.7f, 3.0f, 0.0f);
            player->actor.world.pos.y -= 2.0f;
            if (++sState.sinkTicks >= RS_SWIM_SINK_TICKS) {
                Respawn(play, player);
            }
            return;
        case PHASE_MOVING:
            if (RsStair_IsMoving()) {
                return;
            }
            // Landed. The void-out's damage, which vanilla charges on a respawn and the move does not - and
            // charged the way vanilla charges it (Player's unk_A86): once Player is out of every blocking
            // cutscene state, which can outlast the move by a tick, and then once, whatever it returns.
            // Player refuses damage in such a state, and its return value is not "it landed". Unmodified, as
            // vanilla's is: VoidDamageMult applies and DamageMult does not.
            if (Player_InBlockingCsMode(play, player)) {
                return;
            }
            Player_InflictDamageModified(play, -16 * (1 << CVarGetInteger(CVAR_ENHANCEMENT("VoidDamageMult"), 0)),
                                         false);
            sState.phase = PHASE_IDLE;
            sState.budget = max;
            Marker("rs_swim event=landed health=%d", gSaveContext.health);
            return;
    }
}

// The swim clock's seconds, every tick it is ours. The engine never counts it down (VB_SHOW_SWIM_CLOCK).
// Ownership lapses the moment the main timer is anything but a hazard clock - a minigame's took it over,
// or it stopped - so it is never mistaken for a vanilla hazard clock that starts later.
void OnInterfaceUpdateSwim() {
    if (gSaveContext.timerState < TIMER_STATE_ENV_HAZARD_INIT ||
        gSaveContext.timerState > TIMER_STATE_ENV_HAZARD_TICK) {
        sState.ownsClock = false;
    }
    if (sState.ownsClock) {
        gSaveContext.timerSeconds = RsSwim_ClockSeconds();
    }
}

void OnSceneInitSwim(int16_t sceneNum) {
    ClearRefuge("scene");
    // A void-out or a scene change mid-sink: start dry and full.
    sState.phase = PHASE_IDLE;
    sState.budget = RsSwim_MaxTicks();
}

void RegisterSwimHooks() {
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateSwim);
    COND_HOOK(OnSceneInit, true, OnSceneInitSwim);
    COND_HOOK(OnInterfaceUpdate, true, OnInterfaceUpdateSwim);
    // Asked where the clock would start (default false) and every tick it shows (default true).
    COND_VB_SHOULD(VB_SHOW_SWIM_CLOCK, true, {
        *should = WantsClock();
        sState.ownsClock = *should;
        if (*should) {
            gSaveContext.timerSeconds = RsSwim_ClockSeconds();
        }
    });
}

RegisterShipInitFunc swimHooksInitFunc(RegisterSwimHooks);

} // namespace

extern "C" int32_t RsSwim_Enabled(void) {
    return CVarGetInteger(CVAR_RS_SWIM_STAMINA, 0) != 0 ? 1 : 0;
}

extern "C" void RsSwim_SetEnabled(int32_t on) {
    CVarSetInteger(CVAR_RS_SWIM_STAMINA, on != 0 ? 1 : 0);
    CVarSave();
    sState.budget = RsSwim_MaxTicks();
    sState.phase = PHASE_IDLE;
}

extern "C" int32_t RsSwim_GetLengthSeconds(void) {
    const int32_t s = CVarGetInteger(CVAR_RS_SWIM_STAMINA_SECONDS, 0);
    if (s <= 0) {
        return RS_SWIM_STAMINA_DEFAULT_SECONDS;
    }
    return s > RS_SWIM_STAMINA_MAX_SECONDS ? RS_SWIM_STAMINA_MAX_SECONDS : s;
}

extern "C" void RsSwim_SetLengthSeconds(int32_t seconds) {
    if (seconds < 1) {
        seconds = 1;
    } else if (seconds > RS_SWIM_STAMINA_MAX_SECONDS) {
        seconds = RS_SWIM_STAMINA_MAX_SECONDS;
    }
    CVarSetInteger(CVAR_RS_SWIM_STAMINA_SECONDS, seconds);
    CVarSave();
    sState.budget = RsSwim_MaxTicks();
}

extern "C" void RsSwim_ClearLengthSeconds(void) {
    CVarClear(CVAR_RS_SWIM_STAMINA_SECONDS);
    CVarSave();
    sState.budget = RsSwim_MaxTicks();
}

extern "C" int32_t RsSwim_LengthOverridden(void) {
    return CVarGetInteger(CVAR_RS_SWIM_STAMINA_SECONDS, 0) > 0 ? 1 : 0;
}

extern "C" int32_t RsSwim_MaxTicks(void) {
    return RsSwim_GetLengthSeconds() * 20;
}

extern "C" void RsSwim_SetBudgetTicks(int32_t ticks) {
    const int32_t max = RsSwim_MaxTicks();
    sState.budget = ticks < 1 ? 1 : (ticks > max ? max : ticks);
}

extern "C" int32_t RsSwim_ClockSeconds(void) {
    return (sState.budget + 19) / 20;
}

extern "C" void RsSwim_SetRefuge(Actor* actor, float x, float y, float z, int16_t yaw) {
    sState.refuge.set = true;
    sState.refuge.actor = actor;
    sState.refuge.actorId = actor != nullptr ? actor->id : 0;
    sState.refuge.pos = { x, y, z };
    sState.refuge.yaw = yaw;
    Marker("rs_swim event=refuge_set on=%s x=%.0f y=%.0f z=%.0f yaw=%d", actor != nullptr ? "actor" : "world", x,
           y, z, yaw);
}

extern "C" void RsSwim_ClearRefuge(void) {
    ClearRefuge("call");
}

RsSwimStatus RsSwim_GetStatus() {
    static const char* const kPhaseNames[] = { "idle", "dive", "sinking", "moving" };
    RsSwimStatus s;
    s.enabled = RsSwim_Enabled() != 0;
    s.phase = kPhaseNames[sState.phase];
    s.budget = sState.budget;
    s.max = RsSwim_MaxTicks();
    s.swimming = sState.swimming;
    s.grounded = sState.grounded;
    s.depth = sState.depth;
    s.holdReason = sState.holdReason;
    s.clockOwned = sState.ownsClock;
    s.refuge = sState.refuge.set;
    s.refugeOnActor = sState.refuge.set && sState.refuge.actor != nullptr;
    s.drowns = sState.drowns;
    s.lastRespawn = sState.lastRespawn;
    return s;
}

int32_t RsSwim_PointCount() {
    return kDrownPointCount;
}

RsSwimPointStatus RsSwim_GetPoint(int32_t index) {
    RsSwimPointStatus s = {};
    if (index < 0 || index >= kDrownPointCount) {
        return s;
    }
    const DrownPoint& p = kDrownPoints[index];
    s.name = p.name;
    s.sceneId = p.sceneId;
    s.rsX = p.rsX;
    s.rsY = p.rsY;
    s.unlocked = PointUnlocked(p);
    PlayState* play = gPlayState;
    s.inScene = play != nullptr && play->sceneNum == p.sceneId;
    if (s.inScene) {
        Vec3f pos;
        s.placed = PlacePoint(play, p, &pos, &s.wet);
        if (s.placed) {
            s.x = pos.x;
            s.y = pos.y;
            s.z = pos.z;
        }
    }
    return s;
}

int32_t RsSwim_DrownNow() {
    PlayState* play = gPlayState;
    if (!RsSwim_Enabled() || play == nullptr || GET_PLAYER(play) == nullptr || sState.phase != PHASE_IDLE) {
        return 0;
    }
    BeginSinking(GET_PLAYER(play), "console");
    return 1;
}
