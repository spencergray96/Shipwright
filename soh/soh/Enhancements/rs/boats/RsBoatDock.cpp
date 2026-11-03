// Boat docks: the store of where each owned boat is, the docks in the loaded scene, the pier's operations and the
// `boat where|docks|stow|takeout|putaway|fetch` console (sturdy-bassoon#282, RsBoatDock.h).

#include "RsBoatDock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <spdlog/spdlog.h>

#include "RsBoatFit.h"
#include "RsSceneWater.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/actors/RsActors.h"
#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/Enhancements/rs/interactions/Interaction.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Notification/Notification.h"
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/rs/actors/RsInteraction.h"

extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

// --- the docks' rows ------------------------------------------------------------------------------
//
// One row per dock interaction, keyed by the interaction id the grid tool minted (interactions.json). Every placed
// prop naming the id is a pier with these numbers; a dock with a different radius is a second interaction. The id is
// spelled as a number here, never RS_INTERACTION(n): the grid tool reads that spelling, in InteractionTable.cpp only,
// as "this interaction has code".
struct DockDef {
    int32_t interaction;
    float radius; // a boat this near the kind's mooring, in x/z, is docked here
    float edge;   // from the pier's trigger (its mesh centre) to the water's edge along its facing
};

// Interaction 1, "Pier: boat dock" (InteractionTable.cpp has its dialogue). 500, the owner's radius (2026-11-02): about
// two skiff lengths of slack around the mooring. The edge is half a tile: the pier stands on the shore's last tile.
const DockDef kDocks[] = {
    { 1, 500.0f, 20.0f },
};

// Clear water between the pier's edge and the hull's side at the mooring.
constexpr float kMooringGap = 20.0f;

// The fetch fee per kind (RsBoatKindId order: skiff, sloop, raft). Placeholders, like the upgrades' 5 and 10.
constexpr int32_t kFee[RS_BOAT_KIND_COUNT] = { RS_BOAT_DOCK_FEE_SKIFF, RS_BOAT_DOCK_FEE_SLOOP, RS_BOAT_DOCK_FEE_RAFT };

// --- the store ------------------------------------------------------------------------------------
//
// A project-owned global, not a gSaveContext member, for RsBoatFit.cpp's reasons. `x`, `z` and `yaw` follow the boat's
// actor every tick while its scene is loaded; `docked` is worked out there too, and kept for when it is not.
struct Place {
    bool out;
    int16_t scene; // SceneID; saved by its enum name, which survives the grid tool renumbering its rows
    float x;
    float z;
    int16_t yaw;
    bool docked;
    bool stranded; // its scene loaded and there was no water to put it on: abandoned until fetched (not saved)
    bool refused;  // its scene loaded and the DynaPoly pools could not take it (not saved)
};

Place sPlace[RS_BOAT_KIND_COUNT] = {};

bool KindValid(int32_t kind) {
    return kind >= 0 && kind < RS_BOAT_KIND_COUNT;
}

// "SCENE_LAKE_HYLIA" for each SceneID, generated from the scene table as util.cpp's is (the X macro).
#define DEFINE_SCENE(_0, _1, enumName, _3, _4, _5) #enumName,
const char* const kSceneEnumNames[] = {
#include "tables/scene_table.h"
};
#undef DEFINE_SCENE
static_assert(std::size(kSceneEnumNames) == SCENE_ID_MAX, "kSceneEnumNames covers exactly the scene table");

const char* SceneEnumName(int32_t scene) {
    return scene >= 0 && scene < SCENE_ID_MAX ? kSceneEnumNames[scene] : "?";
}

int32_t SceneByEnumName(const std::string& name) {
    for (int32_t i = 0; i < SCENE_ID_MAX; i++) {
        if (name == kSceneEnumNames[i]) {
            return i;
        }
    }
    return -1;
}

// --- the loaded scene -------------------------------------------------------------------------------

struct Dock {
    Actor* pier;
    const DockDef* def;
};

const DockDef* DockDefOf(const Actor* a) {
    if (a == nullptr || a->id != ACTOR_RS_INTERACTION || a->update == nullptr) {
        return nullptr;
    }
    const int32_t id = reinterpret_cast<const RsInteraction*>(a)->interactionId;
    for (const DockDef& d : kDocks) {
        if (d.interaction == id) {
            return &d;
        }
    }
    return nullptr;
}

// The docks in the loaded scene, earliest-spawned first (Actor_AddToCategory puts a new actor at the head).
std::vector<Dock> Docks() {
    std::vector<Dock> docks;
    if (gPlayState == nullptr) {
        return docks;
    }
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].head; a != nullptr; a = a->next) {
        if (const DockDef* def = DockDefOf(a)) {
            docks.insert(docks.begin(), Dock{ a, def });
        }
    }
    return docks;
}

// Where a boat of the kind lies at the pier: out along the pier's facing past its edge, the hull's side clear of it,
// bow to the right of the facing so it lies along the shore.
void Mooring(const Actor* pier, const DockDef* def, int32_t kind, float* x, float* z, int16_t* yaw) {
    const int16_t facing = pier->home.rot.y;
    const float out = def->edge + kMooringGap + RsBoat_KindHalfBeam(kind);
    *x = pier->world.pos.x + Math_SinS(facing) * out;
    *z = pier->world.pos.z + Math_CosS(facing) * out;
    *yaw = (int16_t)(facing - 0x4000);
}

float DistXZ(float ax, float az, float bx, float bz) {
    return sqrtf((ax - bx) * (ax - bx) + (az - bz) * (az - bz));
}

// The dock a boat of the kind at (x, z) belongs to: inside the radius of its mooring, the nearest mooring on an
// overlap, the earliest pier on a tie. -1 for none (abandoned).
int32_t DockAt(const std::vector<Dock>& docks, int32_t kind, float x, float z) {
    int32_t best = -1;
    float bestDist = 0.0f;
    for (int32_t i = 0; i < (int32_t)docks.size(); i++) {
        float mx;
        float mz;
        int16_t myaw;
        Mooring(docks[i].pier, docks[i].def, kind, &mx, &mz, &myaw);
        const float d = DistXZ(x, z, mx, mz);
        if (d <= docks[i].def->radius && (best < 0 || d < bestDist)) {
            best = i;
            bestDist = d;
        }
    }
    return best;
}

int32_t DockIndexOf(const std::vector<Dock>& docks, const Actor* pier) {
    for (int32_t i = 0; i < (int32_t)docks.size(); i++) {
        if (docks[i].pier == pier) {
            return i;
        }
    }
    return -1;
}

// Every live boat, owned or a fixture.
std::vector<RsBoat*> LiveBoats() {
    std::vector<RsBoat*> boats;
    if (gPlayState == nullptr) {
        return boats;
    }
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_RS_BOAT && a->update != nullptr) {
            boats.push_back(reinterpret_cast<RsBoat*>(a));
        }
    }
    return boats;
}

// The owned boat of the kind in the loaded scene, or nullptr.
RsBoat* OwnedBoat(int32_t kind) {
    for (RsBoat* boat : LiveBoats()) {
        if (boat->owned && boat->kind == kind) {
            return boat;
        }
    }
    return nullptr;
}

// The dock the kind's owned boat belongs to in the loaded scene, or -1 (not out here, or abandoned).
int32_t MooredDock(const std::vector<Dock>& docks, int32_t kind) {
    const RsBoat* boat = OwnedBoat(kind);
    if (boat == nullptr) {
        return -1;
    }
    return DockAt(docks, kind, boat->dyna.actor.world.pos.x, boat->dyna.actor.world.pos.z);
}

// --- the DynaPoly guard -----------------------------------------------------------------------------
//
// What the scene's dyna pools hold now, from every registered actor's header: a killed boat still counts until it is
// deleted, so a swap is judged against the worst case and can never overflow on the tick both are registered. With
// `boatsAtMost` (the guard's reckoning) a boat counts at its kind's most collision, not its header now: the header is
// the kind's, and grows by a box when a facility is fitted (`boat fit`, a build) with no spawn to guard it. Without
// it, what the pools hold (`boat where`).
void DynaUsed(int32_t* polys, int32_t* vtx, bool boatsAtMost) {
    *polys = 0;
    *vtx = 0;
    const DynaCollisionContext* dyna = &gPlayState->colCtx.dyna;
    for (int32_t i = 0; i < BG_ACTOR_MAX; i++) {
        if (!(dyna->bgActorFlags[i] & 1) || dyna->bgActors[i].colHeader == nullptr) {
            continue;
        }
        const Actor* actor = dyna->bgActors[i].actor;
        if (boatsAtMost && actor != nullptr && actor->id == ACTOR_RS_BOAT) {
            int32_t boatPolys;
            int32_t boatVtx;
            RsBoat_KindCollisionMax(reinterpret_cast<const RsBoat*>(actor)->kind, &boatPolys, &boatVtx);
            *polys += boatPolys;
            *vtx += boatVtx;
        } else {
            *polys += (int32_t)dyna->bgActors[i].colHeader->numPolygons;
            *vtx += (int32_t)dyna->bgActors[i].colHeader->numVertices;
        }
    }
}

bool DynaFits(int32_t kind) {
    int32_t needPolys;
    int32_t needVtx;
    int32_t usedPolys;
    int32_t usedVtx;
    RsBoat_KindCollisionMax(kind, &needPolys, &needVtx);
    DynaUsed(&usedPolys, &usedVtx, true);
    const DynaCollisionContext* dyna = &gPlayState->colCtx.dyna;
    const int32_t polyRoom = MIN(dyna->polyListMax, dyna->polyNodesMax);
    return usedPolys + needPolys <= polyRoom && usedVtx + needVtx <= dyna->vtxListMax;
}

void MarkRefused(int32_t kind, const char* reason, float x, float z) {
    char line[200];
    int32_t usedPolys;
    int32_t usedVtx;
    int32_t needPolys;
    int32_t needVtx;
    DynaUsed(&usedPolys, &usedVtx, true);
    RsBoat_KindCollisionMax(kind, &needPolys, &needVtx);
    std::snprintf(line, sizeof(line),
                  "rs_boat event=refused kind=%s reason=%s x=%d z=%d dyna_polys=%d/%d need=%d dyna_vtx=%d/%d",
                  RsBoat_KindName(kind), reason, (int)lroundf(x), (int)lroundf(z), usedPolys,
                  gPlayState->colCtx.dyna.polyListMax, needPolys, usedVtx, gPlayState->colCtx.dyna.vtxListMax);
    RsAgent_Marker(line);
}

// Spawns the kind's own boat at (x, z) on the water there. A spawn the pools cannot take is refused before it is made.
RsBoat* SpawnOwned(int32_t kind, float x, float z, int16_t yaw, int32_t* result) {
    float y = GET_PLAYER(gPlayState)->actor.world.pos.y;
    if (!RsBoat_SurfaceAt(gPlayState, x, z, &y, nullptr)) {
        *result = RS_BOAT_DOCK_NO_WATER;
        return nullptr;
    }
    if (!DynaFits(kind)) {
        MarkRefused(kind, "dyna_pool", x, z);
        *result = RS_BOAT_DOCK_DYNA_POOL;
        return nullptr;
    }
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_RS_BOAT, x, y, z, 0, yaw, 0,
                           RS_BOAT_PARAMS(kind, 1));
    if (a == nullptr) {
        *result = RS_BOAT_DOCK_DYNA_POOL; // Actor_Spawn refuses only for want of room
        return nullptr;
    }
    a->room = -1; // a room change in a multi-room scene keeps it, as Lake Hylia's
    *result = RS_BOAT_DOCK_OK;
    return reinterpret_cast<RsBoat*>(a);
}

void Stow(int32_t kind) {
    if (RsBoat* boat = OwnedBoat(kind)) {
        Actor_Kill(&boat->dyna.actor);
    }
    sPlace[kind] = {};
}

// --- arriving in a scene, and every tick after ---------------------------------------------------------

bool sArrivalPending = false;

void OnSceneInitDock(int16_t sceneNum) {
    sArrivalPending = true;
}

// The first player update after a scene loads (the actor context and the scene's own actors are up, as for Lake
// Hylia's boat): the scene's water, then each owned boat that is out here.
void Arrive() {
    RsSceneWater_Apply(gPlayState);
    for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
        Place& p = sPlace[kind];
        p.stranded = false;
        p.refused = false;
        if (!p.out || p.scene != gPlayState->sceneNum || !RsBoatFit_Owned(kind)) {
            continue;
        }
        int32_t result = RS_BOAT_DOCK_OK;
        SpawnOwned(kind, p.x, p.z, p.yaw, &result);
        p.stranded = result == RS_BOAT_DOCK_NO_WATER;
        p.refused = result == RS_BOAT_DOCK_DYNA_POOL;
        if (p.stranded) {
            // The owner deferred what to do with one (2026-11-02): it stays where the save has it, abandoned and
            // fetchable, so nothing is lost. Abandoned from every scene, so not docked, whatever it was saved as.
            p.docked = false;
            char line[160];
            std::snprintf(line, sizeof(line), "rs_boat event=stranded kind=%s x=%d z=%d", RsBoat_KindName(kind),
                          (int)lroundf(p.x), (int)lroundf(p.z));
            RsAgent_Marker(line);
        }
    }
}

// Follows every owned boat in the scene, and drops one whose kind is no longer owned (`boat own <kind> 0`), wherever
// it is: one out in another scene too, so owning the kind again starts it stowed.
void Track() {
    for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
        if (sPlace[kind].out && !RsBoatFit_Owned(kind)) {
            Stow(kind);
        }
    }
    const std::vector<Dock> docks = Docks();
    for (RsBoat* boat : LiveBoats()) {
        if (!boat->owned) {
            continue;
        }
        if (!RsBoatFit_Owned(boat->kind)) {
            Stow(boat->kind);
            continue;
        }
        Place& p = sPlace[boat->kind];
        const Actor* a = &boat->dyna.actor;
        p.out = true;
        p.scene = gPlayState->sceneNum;
        p.x = a->world.pos.x;
        p.z = a->world.pos.z;
        p.yaw = a->shape.rot.y;
        p.docked = DockAt(docks, boat->kind, p.x, p.z) >= 0;
        p.stranded = false;
    }
}

void OnPlayerUpdateDock() {
    if (gPlayState == nullptr) {
        return;
    }
    if (sArrivalPending) {
        sArrivalPending = false;
        Arrive();
    }
    Track();
}

void RegisterRsBoatDock() {
    static bool hooked = false;
    if (hooked) {
        return;
    }
    hooked = true;
    COND_HOOK(OnSceneInit, true, OnSceneInitDock);
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateDock);
}

RegisterShipInitFunc rsBoatDockInitFunc(RegisterRsBoatDock);

// --- the operations ----------------------------------------------------------------------------------

// The kinds the last pier operation put away to make room, for what the pier says (RsBoatDock_DialogueAction).
std::vector<int32_t> sMadeRoom;

// The kinds in sMadeRoom as `raft+sloop`, or `none`.
std::string MadeRoomList() {
    std::string list;
    for (int32_t k : sMadeRoom) {
        list += (list.empty() ? "" : "+") + std::string(RsBoat_KindName(k));
    }
    return list.empty() ? "none" : list;
}

void MarkOp(int32_t op, int32_t kind, int32_t result, int32_t dock, int32_t fee) {
    char line[260];
    const Place& p = sPlace[KindValid(kind) ? kind : 0];
    std::snprintf(line, sizeof(line),
                  "rs_boat event=%s kind=%s result=%s dock=%d fee=%d rupees=%d state=%s x=%d z=%d yaw=%d made_room=%s",
                  RsBoatDock_OpName(op), RsBoat_KindName(kind), RsBoatDock_ResultName(result), (int)dock, (int)fee,
                  (int)Quest_RupeesAfterPending(), RsBoatDock_StateName(RsBoatDock_State(kind)), (int)lroundf(p.x),
                  (int)lroundf(p.z), (int)(u16)p.yaw, MadeRoomList().c_str());
    RsAgent_Marker(line);
}

// Takes the kind to the pier's mooring, stowing any other of the player's boats lying on it (the owner, 2026-11-02:
// only what is physically in the way, wherever it belongs; one of theirs elsewhere in the pier's water stays). Only a
// boat that is not the player's blocks it. Everything is checked before anything changes.
int32_t PlaceAtPier(int32_t kind, const std::vector<Dock>& docks, int32_t dock, int32_t fee) {
    float mx;
    float mz;
    int16_t myaw;
    Mooring(docks[dock].pier, docks[dock].def, kind, &mx, &mz, &myaw);
    float y = 0.0f;
    if (!RsBoat_SurfaceAt(gPlayState, mx, mz, &y, nullptr)) {
        return RS_BOAT_DOCK_NO_WATER;
    }

    RsBoat* own = OwnedBoat(kind);
    // What is on the mooring: within the two hulls' half lengths of it. The kind's own boat is moved, not in the way.
    std::vector<RsBoat*> stowing;
    for (RsBoat* other : LiveBoats()) {
        if (other == own) {
            continue;
        }
        const Actor* a = &other->dyna.actor;
        if (DistXZ(a->world.pos.x, a->world.pos.z, mx, mz) <
            RsBoat_KindHalfLength(kind) + RsBoat_KindHalfLength(other->kind)) {
            if (!other->owned) {
                return RS_BOAT_DOCK_MOORING_BLOCKED;
            }
            stowing.push_back(other);
        }
    }
    if (!DynaFits(kind)) {
        MarkRefused(kind, "dyna_pool", mx, mz);
        return RS_BOAT_DOCK_DYNA_POOL;
    }
    if (fee > 0 && Quest_CheckChargeRupees(fee) != QUEST_OK) {
        return RS_BOAT_DOCK_CANT_AFFORD;
    }

    // The new boat first, so a spawn that fails all the same (Actor_Spawn short of room) has changed nothing: the old
    // one is still registered, which the pool check above already allowed for.
    int32_t result = RS_BOAT_DOCK_OK;
    if (SpawnOwned(kind, mx, mz, myaw, &result) == nullptr) {
        return result;
    }
    if (fee > 0) {
        Quest_ChargeRupees(fee);
    }
    for (RsBoat* boat : stowing) {
        sMadeRoom.push_back(boat->kind);
        Stow(boat->kind);
    }
    if (own != nullptr) {
        Actor_Kill(&own->dyna.actor);
    }
    Place& p = sPlace[kind];
    p = {};
    p.out = true;
    p.scene = gPlayState->sceneNum;
    p.x = mx;
    p.z = mz;
    p.yaw = myaw;
    p.docked = true;
    return result;
}

} // namespace

// --- the C API ----------------------------------------------------------------------------------------

extern "C" int32_t RsBoatDock_SpawnFits(int32_t kind) {
    return gPlayState != nullptr && DynaFits(kind) ? 1 : 0;
}

extern "C" int32_t RsBoatDock_State(int32_t kind) {
    if (!KindValid(kind) || !RsBoatFit_Owned(kind)) {
        return -1;
    }
    const Place& p = sPlace[kind];
    if (!p.out) {
        return RS_BOAT_DOCK_STOWED;
    }
    return p.docked && !p.stranded ? RS_BOAT_DOCK_DOCKED : RS_BOAT_DOCK_ABANDONED;
}

extern "C" Actor* RsBoatDock_TalkPier(void) {
    if (gPlayState == nullptr) {
        return nullptr;
    }
    Actor* talk = GET_PLAYER(gPlayState)->talkActor;
    return DockDefOf(talk) != nullptr ? talk : nullptr;
}

extern "C" int32_t RsBoatDock_DialogueAction(int32_t op, int32_t kind) {
    const int32_t result = RsBoatDock_Run(op, kind, RsBoatDock_TalkPier());
    // What the pier says about it (Interaction.h): after the option's reply when it put another boat away to make room,
    // in its place for a refusal. Placeholder prose, as the menu's.
    std::string words;
    int32_t instead = 1;
    switch (result) {
        case RS_BOAT_DOCK_OK:
            instead = 0;
            if (!sMadeRoom.empty()) {
                std::string list;
                for (size_t i = 0; i < sMadeRoom.size(); i++) {
                    list += (i == 0 ? "" : " and ") + std::string(RsBoat_KindName(sMadeRoom[i]));
                }
                words = "I've put your " + list + " away to make room.";
            }
            break;
        case RS_BOAT_DOCK_ALREADY_HERE:
            words = "Your " + std::string(RsBoat_KindName(kind)) + "'s already here.";
            break;
        case RS_BOAT_DOCK_MOORING_BLOCKED:
            words = "Something's in the way at the end of the pier.";
            break;
        case RS_BOAT_DOCK_DYNA_POOL:
            words = "There's no room for another boat here.";
            break;
        case RS_BOAT_DOCK_NO_WATER:
            words = "There's no water at the end of the pier.";
            break;
        case RS_BOAT_DOCK_CANT_AFFORD:
            words = "You can't afford that.";
            break;
        default:
            // A gate let through a pick it should have hidden: a bug, so a toast naming it rather than prose.
            Notification::Emit({ .prefix = RsBoat_KindName(kind),
                                 .message = RsBoatDock_ResultName(result),
                                 .messageColor = ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                                 .remainingTime = 6.0f });
            break;
    }
    if (!words.empty()) {
        RsInteraction_ActionSays(words.c_str(), instead);
        char line[200];
        std::snprintf(line, sizeof(line), "rs_boat event=pier_says kind=%s result=%s instead=%d text=\"%s\"",
                      RsBoat_KindName(kind), RsBoatDock_ResultName(result), (int)instead, words.c_str());
        RsAgent_Marker(line);
    }
    return result;
}

extern "C" int32_t RsBoatDock_StateIs(int32_t kind, int32_t state) {
    const int32_t now = RsBoatDock_State(kind);
    if (now < 0) {
        return 0;
    }
    if (state == RS_BOAT_DOCK_HERE) {
        const Actor* pier = RsBoatDock_TalkPier();
        if (pier == nullptr || now != RS_BOAT_DOCK_DOCKED) {
            return 0;
        }
        const std::vector<Dock> docks = Docks();
        const int32_t dock = MooredDock(docks, kind);
        return dock >= 0 && docks[dock].pier == pier ? 1 : 0;
    }
    return now == state ? 1 : 0;
}

extern "C" const char* RsBoatDock_StateName(int32_t state) {
    switch (state) {
        case RS_BOAT_DOCK_STOWED:
            return "stowed";
        case RS_BOAT_DOCK_DOCKED:
            return "docked";
        case RS_BOAT_DOCK_ABANDONED:
            return "abandoned";
        case RS_BOAT_DOCK_HERE:
            return "here";
        default:
            return "?";
    }
}

extern "C" const char* RsBoatDock_ResultName(int32_t result) {
    static const char* const kNames[RS_BOAT_DOCK_RESULT_COUNT] = {
        "ok",       "not_owned",       "no_pier",   "already_here", "not_here",   "is_abandoned",
        "not_abandoned", "mooring_blocked", "dyna_pool", "no_water",     "cant_afford",
    };
    return result >= 0 && result < RS_BOAT_DOCK_RESULT_COUNT ? kNames[result] : "?";
}

extern "C" const char* RsBoatDock_OpName(int32_t op) {
    switch (op) {
        case RS_BOAT_DOCK_OP_TAKE_OUT:
            return "takeout";
        case RS_BOAT_DOCK_OP_PUT_AWAY:
            return "putaway";
        case RS_BOAT_DOCK_OP_FETCH:
            return "fetch";
        default:
            return "?";
    }
}

extern "C" int32_t RsBoatDock_Fee(int32_t kind) {
    return KindValid(kind) ? kFee[kind] : 0;
}

extern "C" int32_t RsBoatDock_Run(int32_t op, int32_t kind, Actor* pier) {
    sMadeRoom.clear();
    int32_t result = RS_BOAT_DOCK_OK;
    int32_t dock = -1;
    int32_t fee = 0;
    if (!KindValid(kind) || !RsBoatFit_Owned(kind)) {
        result = RS_BOAT_DOCK_NOT_OWNED;
    } else if (gPlayState == nullptr || DockDefOf(pier) == nullptr) {
        result = RS_BOAT_DOCK_NO_PIER;
    } else {
        const std::vector<Dock> docks = Docks();
        dock = DockIndexOf(docks, pier);
        const bool here = dock >= 0 && MooredDock(docks, kind) == dock;
        // Out inside this pier's ring, though nearer a neighbour's mooring (overlapping docks): already here too, for
        // taking out or fetching. Putting away still wants it to be this pier's.
        bool nearby = here;
        if (dock >= 0 && !nearby) {
            if (const RsBoat* boat = OwnedBoat(kind)) {
                float mx;
                float mz;
                int16_t myaw;
                Mooring(docks[dock].pier, docks[dock].def, kind, &mx, &mz, &myaw);
                nearby = DistXZ(boat->dyna.actor.world.pos.x, boat->dyna.actor.world.pos.z, mx, mz) <=
                         docks[dock].def->radius;
            }
        }
        const int32_t state = RsBoatDock_State(kind);
        if (dock < 0) {
            result = RS_BOAT_DOCK_NO_PIER;
        } else if (op == RS_BOAT_DOCK_OP_PUT_AWAY) {
            if (!here) {
                result = RS_BOAT_DOCK_NOT_HERE;
            } else {
                Stow(kind);
            }
        } else if (nearby) {
            result = RS_BOAT_DOCK_ALREADY_HERE;
        } else if (op == RS_BOAT_DOCK_OP_TAKE_OUT && state == RS_BOAT_DOCK_ABANDONED) {
            result = RS_BOAT_DOCK_IS_ABANDONED;
        } else if (op == RS_BOAT_DOCK_OP_FETCH && state != RS_BOAT_DOCK_ABANDONED) {
            result = RS_BOAT_DOCK_NOT_ABANDONED;
        } else {
            fee = op == RS_BOAT_DOCK_OP_FETCH ? RsBoatDock_Fee(kind) : 0;
            result = PlaceAtPier(kind, docks, dock, fee);
            if (result != RS_BOAT_DOCK_OK) {
                fee = 0;
            }
        }
    }
    MarkOp(op, kind, result, dock, fee);
    return result;
}

// --- the save ---------------------------------------------------------------------------------------
//
// Inside each kind's object of `rsBoats` (RsBoatFit.cpp), version 1, new keys with no save break:
//   "out": 0 | 1, and when 1: "scene": "SCENE_BOAT_DOCKS_282", "x": -812, "z": 1204, "yaw": 16384, "docked": 1
// A file without them reads as stowed. A scene this build does not have (a map since deleted) reads as stowed, logged:
// an owned boat is never lost.

extern "C" void RsBoatDock_SaveKind(int32_t kind) {
    const Place& p = sPlace[kind];
    SaveManager::Instance->SaveData("out", p.out ? 1 : 0);
    if (!p.out) {
        return;
    }
    SaveManager::Instance->SaveData("scene", std::string(SceneEnumName(p.scene)));
    SaveManager::Instance->SaveData("x", (int32_t)lroundf(p.x));
    SaveManager::Instance->SaveData("z", (int32_t)lroundf(p.z));
    SaveManager::Instance->SaveData("yaw", (int32_t)(u16)p.yaw);
    SaveManager::Instance->SaveData("docked", p.docked ? 1 : 0);
}

extern "C" void RsBoatDock_LoadKind(int32_t kind) {
    Place& p = sPlace[kind];
    p = {};
    int32_t out = 0;
    SaveManager::Instance->LoadData("out", out, 0);
    if (out == 0) {
        return;
    }
    std::string sceneName;
    int32_t x = 0;
    int32_t z = 0;
    int32_t yaw = 0;
    int32_t docked = 0;
    SaveManager::Instance->LoadData("scene", sceneName, std::string());
    SaveManager::Instance->LoadData("x", x, 0);
    SaveManager::Instance->LoadData("z", z, 0);
    SaveManager::Instance->LoadData("yaw", yaw, 0);
    SaveManager::Instance->LoadData("docked", docked, 0);
    const int32_t scene = SceneByEnumName(sceneName);
    if (scene < 0) {
        SPDLOG_ERROR("RsBoatDock: save file's {} is out in scene '{}', which this build does not have; stowed",
                     RsBoat_KindName(kind), sceneName);
        return;
    }
    p.out = true;
    p.scene = (int16_t)scene;
    p.x = (float)x;
    p.z = (float)z;
    p.yaw = (int16_t)yaw;
    p.docked = docked != 0;
}

extern "C" void RsBoatDock_Reset(void) {
    for (Place& p : sPlace) {
        p = {};
    }
}

// --- the console ----------------------------------------------------------------------------------------

namespace {

int32_t KindArg(const std::vector<std::string>& args, size_t at, const char* op, std::vector<std::string>& lines) {
    const int32_t kind = args.size() > at ? RsBoat_KindByName(args[at].c_str()) : -1;
    if (kind < 0) {
        Addf(lines, "op=%s result=error error=bad_kind kinds=skiff,sloop,raft", op);
    }
    return kind;
}

// The pier a console verb acts at: the dock index given, else the pier Link is talking to, else dock 0.
Actor* PierArg(const std::vector<std::string>& args, size_t at, const char* op, std::vector<std::string>& lines) {
    const std::vector<Dock> docks = Docks();
    long idx = 0;
    if (args.size() > at) {
        char* end = nullptr;
        idx = strtol(args[at].c_str(), &end, 10);
        if (end == args[at].c_str() || *end != '\0') {
            Addf(lines, "op=%s result=error error=bad_value", op);
            return nullptr;
        }
    } else if (Actor* talk = RsBoatDock_TalkPier()) {
        return talk;
    }
    if (idx < 0 || idx >= (long)docks.size()) {
        Addf(lines, "op=%s result=error error=no_dock idx=%ld docks=%d", op, idx, (int)docks.size());
        return nullptr;
    }
    return docks[(size_t)idx].pier;
}

int32_t Where(std::vector<std::string>& lines) {
    const std::vector<Dock> docks = Docks();
    const Player* player = gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
    for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
        const Place& p = sPlace[kind];
        const RsBoat* boat = gPlayState != nullptr ? OwnedBoat(kind) : nullptr;
        const int32_t dock = boat != nullptr ? MooredDock(docks, kind) : -1;
        const int32_t here = gPlayState != nullptr && p.out && p.scene == gPlayState->sceneNum ? 1 : 0;
        const int32_t dist =
            here && player != nullptr ? (int32_t)lroundf(DistXZ(p.x, p.z, player->actor.world.pos.x,
                                                                player->actor.world.pos.z))
                                      : -1;
        Addf(lines,
             "boat_where kind=%s owned=%d state=%s out=%d scene=%s in_scene=%d spawned=%d x=%d z=%d yaw=%d dock=%d "
             "dist=%d stranded=%d refused=%d fee=%d",
             RsBoat_KindName(kind), RsBoatFit_Owned(kind), RsBoatDock_StateName(RsBoatDock_State(kind)), p.out ? 1 : 0,
             p.out ? SceneEnumName(p.scene) : "-", here, boat != nullptr ? 1 : 0, (int)lroundf(p.x), (int)lroundf(p.z),
             (int)(u16)p.yaw, (int)dock, (int)dist, p.stranded ? 1 : 0, p.refused ? 1 : 0, (int)RsBoatDock_Fee(kind));
    }
    int32_t polys = 0;
    int32_t vtx = 0;
    if (gPlayState != nullptr) {
        DynaUsed(&polys, &vtx, false);
    }
    Addf(lines, "op=where result=ok scene=%s docks=%d boats=%d dyna_polys=%d/%d dyna_vtx=%d/%d rupees=%d",
         gPlayState != nullptr ? SceneEnumName(gPlayState->sceneNum) : "-", (int)docks.size(),
         (int)LiveBoats().size(), (int)polys, gPlayState != nullptr ? gPlayState->colCtx.dyna.polyListMax : 0, (int)vtx,
         gPlayState != nullptr ? gPlayState->colCtx.dyna.vtxListMax : 0, (int)Quest_RupeesAfterPending());
    return 0;
}

int32_t DocksList(std::vector<std::string>& lines) {
    const std::vector<Dock> docks = Docks();
    for (int32_t i = 0; i < (int32_t)docks.size(); i++) {
        const Actor* pier = docks[i].pier;
        // Every one of the player's boats docked here: one on the mooring, any others elsewhere in its water.
        std::string docked;
        for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
            if (MooredDock(docks, kind) == i) {
                docked += (docked.empty() ? "" : "+") + std::string(RsBoat_KindName(kind));
            }
        }
        if (docked.empty()) {
            docked = "none";
        }
        float mx;
        float mz;
        int16_t myaw;
        Mooring(pier, docks[i].def, RS_BOAT_KIND_SKIFF, &mx, &mz, &myaw);
        Addf(lines,
             "dock idx=%d interaction=%d x=%d y=%d z=%d facing=%d radius=%d skiff_mooring_x=%d skiff_mooring_z=%d "
             "mooring_yaw=%d docked=%s",
             (int)i, (int)docks[i].def->interaction, (int)lroundf(pier->world.pos.x), (int)lroundf(pier->world.pos.y),
             (int)lroundf(pier->world.pos.z), (int)(u16)pier->home.rot.y, (int)lroundf(docks[i].def->radius),
             (int)lroundf(mx), (int)lroundf(mz), (int)(u16)myaw, docked.c_str());
    }
    Addf(lines, "op=docks result=ok count=%d", (int)docks.size());
    return 0;
}

} // namespace

int32_t RsBoatDock_ConsoleRun(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string& sub = args[0];
    if (sub != "where" && sub != "docks" && sub != "stow" && sub != "takeout" && sub != "putaway" && sub != "fetch") {
        return -1;
    }
    if (sub == "where" && args.size() == 1) {
        return Where(lines);
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=%s result=error error=no_scene", sub.c_str());
        return 1;
    }
    if (sub == "docks" && args.size() == 1) {
        return DocksList(lines);
    }
    // A test aid with no rules: the kind back to stowed, wherever it is.
    if (sub == "stow" && args.size() == 2) {
        int32_t stowed = 0;
        for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
            if (args[1] == "all" || args[1] == RsBoat_KindName(kind)) {
                Stow(kind);
                stowed++;
            }
        }
        if (stowed == 0) {
            Addf(lines, "op=stow result=error error=bad_kind kinds=skiff,sloop,raft,all");
            return 1;
        }
        Addf(lines, "op=stow result=ok stowed=%d", (int)stowed);
        return 0;
    }
    if ((sub == "takeout" || sub == "putaway" || sub == "fetch") && (args.size() == 2 || args.size() == 3)) {
        const int32_t kind = KindArg(args, 1, sub.c_str(), lines);
        if (kind < 0) {
            return 1;
        }
        Actor* pier = PierArg(args, 2, sub.c_str(), lines);
        if (pier == nullptr) {
            return 1;
        }
        const int32_t op = sub == "takeout" ? RS_BOAT_DOCK_OP_TAKE_OUT
                           : sub == "putaway" ? RS_BOAT_DOCK_OP_PUT_AWAY
                                              : RS_BOAT_DOCK_OP_FETCH;
        const int32_t result = RsBoatDock_Run(op, kind, pier);
        Addf(lines, "op=%s result=%s kind=%s state=%s", sub.c_str(), result == RS_BOAT_DOCK_OK ? "ok" : "refused",
             RsBoat_KindName(kind), RsBoatDock_StateName(RsBoatDock_State(kind)));
        lines.back() += std::string(" reason=") + RsBoatDock_ResultName(result);
        return result == RS_BOAT_DOCK_OK ? 0 : 1;
    }
    Addf(lines, "op=usage result=error usage=\"boat " RS_BOAT_DOCK_GRAMMAR "\"");
    return 1;
}
