// Boat health: the store of each owned boat's damage, hits and heals, the boat hearts and the `boat hp|damage|capsize|heart`
// console (sturdy-bassoon#284, RsBoatHealth.h).

#include "soh/frame_interpolation.h" // before OPEN_DISPS: the Tempor Tantrum note's link rule for draws in C++

#include "RsBoatHealth.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>

#include "RsBoatDock.h"
#include "RsBoatFit.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/actors/RsActors.h"
#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "macros.h"
#include "variables.h"

extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

bool KindValid(int32_t kind) {
    return kind >= 0 && kind < RS_BOAT_KIND_COUNT;
}

void Mark(const char* fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    RsAgent_Marker(line);
}

// --- the store ------------------------------------------------------------------------------------
//
// Each owned kind's damage - what it is missing from its most - and whether it is wrecked, rather than its HP: a better
// hull or keel adds to the most and the HP alike, and what was missing stays missing (the owner, #284: an upgrade does
// not mend). Wrecked is kept apart so a better tier cannot float a sunk boat. A project-owned global, as the fit-out's.
struct Hull {
    int32_t damage;
    bool wrecked;
};
Hull sHull[RS_BOAT_KIND_COUNT] = {};

// Sets the owned kind's store to `hp`, clamped to 0..its most.
void Store(int32_t kind, int32_t hp) {
    const int32_t max = RsBoatHealth_Max(kind);
    hp = CLAMP(hp, 0, max);
    sHull[kind].wrecked = hp == 0 && max > 0;
    sHull[kind].damage = sHull[kind].wrecked ? 0 : max - hp;
}

// Why a hit on the boat would not land, or nullptr: what RsBoatHealth_Hit and the console both report.
const char* IgnoreReason(const RsBoat* boat, int32_t source) {
    if (boat->sinkTicks > 0) {
        return "sinking";
    }
    if (RsBoatHealth_Max(boat->kind) <= 0) {
        return "no_hp";
    }
    if (source != RS_BOAT_HIT_CONSOLE && !RsBoat_LinkAboard(boat, gPlayState)) {
        return "empty"; // the owner (#284): a boat with nobody aboard is never damaged
    }
    return nullptr;
}

// Every live boat, OLDEST FIRST, as the boat console numbers them (RsBoatConsole.cpp's Boats).
std::vector<RsBoat*> Boats() {
    std::vector<RsBoat*> boats;
    if (gPlayState == nullptr) {
        return boats;
    }
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_RS_BOAT && a->update != nullptr) {
            boats.insert(boats.begin(), reinterpret_cast<RsBoat*>(a));
        }
    }
    return boats;
}

RsBoat* OwnedBoat(int32_t kind) {
    for (RsBoat* boat : Boats()) {
        if (boat->owned && boat->kind == kind) {
            return boat;
        }
    }
    return nullptr;
}

// The boat Link is aboard, or nullptr: the newest, should he somehow be on two.
RsBoat* LinkBoat() {
    RsBoat* found = nullptr;
    for (RsBoat* boat : Boats()) {
        if (boat->sinkTicks == 0 && RsBoat_LinkAboard(boat, gPlayState)) {
            found = boat;
        }
    }
    return found;
}

// Writes a boat's HP where it lives: the store for the player's own, the actor for a fixture (its damage too).
void Write(RsBoat* boat, int32_t hp) {
    if (boat->owned) {
        Store(boat->kind, hp);
    } else {
        boat->damage = RsBoatHealth_Max(boat->kind) - CLAMP(hp, 0, RsBoatHealth_Max(boat->kind));
    }
}

void MarkHp(const char* event, const RsBoat* boat, const char* fields) {
    Mark("rs_boat event=%s kind=%s owned=%d %s hp=%d max=%d x=%d z=%d", event, RsBoat_KindName(boat->kind),
         boat->owned, fields, (int)RsBoatHealth_Of(boat), (int)RsBoatHealth_Max(boat->kind),
         (int)lroundf(boat->dyna.actor.world.pos.x), (int)lroundf(boat->dyna.actor.world.pos.z));
}

// 0 HP, then the hull goes down (RsBoat_Capsize). The one way a boat capsizes.
void Capsize(RsBoat* boat) {
    Write(boat, 0);
    RsBoat_Capsize(boat, gPlayState);
}

// --- the boat hearts ------------------------------------------------------------------------------------
//
// Per scene, in world units: in code for now, as the scenes' water is (RsSceneWater.h; the owner, 2026-11-02 and #284).
// Each is back on every load of its scene. Its height is the water's under it, read once the scene is flooded.
struct HeartSpot {
    int16_t scene;
    float x;
    float z;
};

const HeartSpot kBoatHearts[] = {
    // Lake Hylia's pond, where its boat waits (-875, 6000; RsBoatConsole.cpp): water at both lake levels. One dead
    // ahead of the boat's bow, one off to its side.
    { SCENE_LAKE_HYLIA, -875.0f, 5600.0f },
    { SCENE_LAKE_HYLIA, -1400.0f, 6500.0f },
    // Boat Docks 282: open water south of the middle island, and in the far south-east channel.
    { SCENE_BOAT_DOCKS_282, 0.0f, 0.0f },
    { SCENE_BOAT_DOCKS_282, -900.0f, -1000.0f },
};

struct Heart {
    float x;
    float z;
    float y;    // the water's surface under it, once found
    bool found; // the water under it was found: before that it is neither drawn nor collected
    bool live;
};

std::vector<Heart> sHearts;
bool sHeartsPending = false;

constexpr float kHeartRise = 22.0f;  // its centre over the surface: it floats, like an item float, never on the water
constexpr float kHeartBob = 4.0f;    // up and down, units
// Vanilla's 3D heart is 16 over its actor's 0.02 (EnItem00), 0.32 in all: drawn with no actor, a boat's is the whole
// scale, nearly twice as big. (30, the 16 alone and more, drew a heart wider than the skiff.)
constexpr float kHeartScale = 0.6f;

void OnSceneInitHearts(int16_t sceneNum) {
    sHearts.clear();
    sHeartsPending = true;
}

void OnPlayerUpdateHearts() {
    if (gPlayState == nullptr) {
        return;
    }
    if (sHeartsPending) {
        sHeartsPending = false;
        for (const HeartSpot& h : kBoatHearts) {
            if (h.scene == gPlayState->sceneNum) {
                sHearts.push_back(Heart{ h.x, h.z, 0.0f, false, true });
            }
        }
    }
    // The scene's water is put in on the first player update (RsSceneWater_Apply, from the docks' arrival), which may
    // come after this hook's: a heart looks for its water until it is there.
    for (Heart& h : sHearts) {
        if (!h.found) {
            float y = GET_PLAYER(gPlayState)->actor.world.pos.y;
            h.found = RsBoat_SurfaceAt(gPlayState, h.x, h.z, &y, nullptr) != 0;
            h.y = y;
        }
    }
    // Sailed over, with Link aboard: Link swimming into one does nothing. Collected even at full HP, as vanilla's are.
    for (RsBoat* boat : Boats()) {
        if (boat->sinkTicks > 0 || !RsBoat_LinkAboard(boat, gPlayState)) {
            continue;
        }
        for (size_t i = 0; i < sHearts.size(); i++) {
            Heart& h = sHearts[i];
            if (!h.live || !h.found || !RsBoat_HullContains(boat, h.x, h.z, RS_BOAT_HEART_MARGIN)) {
                continue;
            }
            h.live = false;
            Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            const int32_t gained = RsBoatHealth_Heal(boat, RS_BOAT_HEART_HP, RS_BOAT_HEAL_HEART);
            char fields[64];
            std::snprintf(fields, sizeof(fields), "n=%d gained=%d", (int)i, (int)gained);
            MarkHp("heart", boat, fields);
        }
    }
}

} // namespace

// The hearts, at file scope: OPEN_DISPS in an anonymous namespace does not link (the Tempor Tantrum note). Vanilla's 3D
// recovery heart (GetItem_Draw's GID_HEART, as EnItem00 draws an overworld heart), bigger, spinning and bobbing. Each is
// keyed for interpolation, so one collected does not slide the rest (the Tempor Tantrum note's crates).
static void RsBoatHealth_DrawHearts() {
    if (gPlayState == nullptr || sHearts.empty() || gPlayState->pauseCtx.state != 0) {
        return;
    }
    const float t = (float)gPlayState->gameplayFrames;
    for (size_t i = 0; i < sHearts.size(); i++) {
        const Heart& h = sHearts[i];
        if (!h.live || !h.found) {
            continue;
        }
        // Keyed on the list and the index, which stay put: a heart's own address moves when `heart here` grows the list.
        FrameInterpolation_RecordOpenChild(&sHearts, (int)i);
        Matrix_Translate(h.x, h.y + kHeartRise + sinf(t * 0.15f + (float)i) * kHeartBob, h.z, MTXMODE_NEW);
        Matrix_RotateY(BINANG_TO_RAD((s16)(gPlayState->gameplayFrames * 0x3C0)), MTXMODE_APPLY);
        Matrix_Scale(kHeartScale, kHeartScale, kHeartScale, MTXMODE_APPLY);
        GetItem_Draw(gPlayState, GID_HEART);
        FrameInterpolation_RecordCloseChild();
    }
}

namespace {

void RegisterRsBoatHealth() {
    static bool hooked = false;
    if (hooked) {
        return;
    }
    hooked = true;
    COND_HOOK(OnSceneInit, true, OnSceneInitHearts);
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateHearts);
    COND_HOOK(OnPlayDrawEnd, true, RsBoatHealth_DrawHearts);
}

RegisterShipInitFunc rsBoatHealthInitFunc(RegisterRsBoatHealth);

} // namespace

// --- the C API ----------------------------------------------------------------------------------------

extern "C" const char* RsBoatHealth_HitSourceName(int32_t source) {
    static const char* const kNames[RS_BOAT_HIT_SOURCE_COUNT] = { "console", "hazard", "rapids", "lightning", "enemy" };
    return source >= 0 && source < RS_BOAT_HIT_SOURCE_COUNT ? kNames[source] : "?";
}

extern "C" int32_t RsBoatHealth_HitSourceByName(const char* name) {
    for (int32_t s = 0; s < RS_BOAT_HIT_SOURCE_COUNT; s++) {
        if (std::strcmp(name, RsBoatHealth_HitSourceName(s)) == 0) {
            return s;
        }
    }
    return -1;
}

extern "C" const char* RsBoatHealth_HealSourceName(int32_t source) {
    static const char* const kNames[RS_BOAT_HEAL_SOURCE_COUNT] = { "console", "heart", "pier" };
    return source >= 0 && source < RS_BOAT_HEAL_SOURCE_COUNT ? kNames[source] : "?";
}

extern "C" int32_t RsBoatHealth_Max(int32_t kind) {
    if (!KindValid(kind)) {
        return 0;
    }
    RsBoatFitStats fit;
    RsBoatFit_Stats(kind, &fit);
    return MAX(fit.hitpoints, 0);
}

extern "C" int32_t RsBoatHealth_Armour(int32_t kind) {
    if (!KindValid(kind)) {
        return 0;
    }
    RsBoatFitStats fit;
    RsBoatFit_Stats(kind, &fit);
    return MAX(fit.armour, 0);
}

extern "C" int32_t RsBoatHealth_KindHp(int32_t kind) {
    if (!KindValid(kind)) {
        return 0;
    }
    // Owned or not: `boat own <kind> 0` then 1 is the same boat back, damage and all (an owned boat is never lost).
    if (sHull[kind].wrecked) {
        return 0;
    }
    const int32_t max = RsBoatHealth_Max(kind);
    return CLAMP(max - sHull[kind].damage, 0, max);
}

extern "C" int32_t RsBoatHealth_KindDamaged(int32_t kind) {
    return KindValid(kind) && RsBoatFit_Owned(kind) && RsBoatHealth_KindHp(kind) < RsBoatHealth_Max(kind) ? 1 : 0;
}

extern "C" int32_t RsBoatHealth_KindWrecked(int32_t kind) {
    return KindValid(kind) && RsBoatFit_Owned(kind) && sHull[kind].wrecked ? 1 : 0;
}

extern "C" int32_t RsBoatHealth_SetKindHp(int32_t kind, int32_t hp, int32_t source) {
    if (!KindValid(kind) || !RsBoatFit_Owned(kind)) {
        return 0;
    }
    const int32_t before = RsBoatHealth_KindHp(kind);
    Store(kind, hp);
    hp = RsBoatHealth_KindHp(kind);
    Mark("rs_boat event=hp_set kind=%s source=%s gained=%d hp=%d max=%d wrecked=%d", RsBoat_KindName(kind),
         RsBoatHealth_HealSourceName(source), (int)(hp - before), (int)hp, (int)RsBoatHealth_Max(kind),
         (int)RsBoatHealth_KindWrecked(kind));
    if (RsBoatHealth_KindWrecked(kind)) {
        // Wrecked is always stowed: in the loaded scene the boat goes down (and is stowed as it does); anywhere else it
        // is stowed now, so no pier fetches a wreck from another scene.
        RsBoat* boat = gPlayState != nullptr ? OwnedBoat(kind) : nullptr;
        if (boat != nullptr) {
            RsBoat_Capsize(boat, gPlayState);
        } else {
            RsBoatDock_Stow(kind);
        }
    }
    return 1;
}

extern "C" int32_t RsBoatHealth_RepairFee(int32_t kind) {
    return KindValid(kind) ? (RsBoatHealth_Max(kind) - RsBoatHealth_KindHp(kind)) * RS_BOAT_REPAIR_RUPEES_PER_HP : 0;
}

extern "C" int32_t RsBoatHealth_Of(const RsBoat* boat) {
    if (boat == nullptr) {
        return 0;
    }
    if (boat->owned) {
        return RsBoatHealth_KindHp(boat->kind);
    }
    return MAX(RsBoatHealth_Max(boat->kind) - boat->damage, 0);
}

extern "C" RsBoat* RsBoatHealth_LinkBoat(void) {
    return LinkBoat();
}

extern "C" void RsBoatHealth_InitBoat(RsBoat* boat) {
    boat->damage = 0;
}

extern "C" int32_t RsBoatHealth_Hit(RsBoat* boat, int32_t hit, int32_t source) {
    if (boat == nullptr || gPlayState == nullptr) {
        return -1;
    }
    const char* ignored = IgnoreReason(boat, source);
    if (ignored != nullptr) {
        char fields[96];
        std::snprintf(fields, sizeof(fields), "source=%s hit=%d reason=%s", RsBoatHealth_HitSourceName(source),
                      (int)hit, ignored);
        MarkHp("hit_ignored", boat, fields);
        return -1;
    }
    const int32_t armour = RsBoatHealth_Armour(boat->kind);
    const int32_t dealt = MAX(0, hit - armour);
    const int32_t hp = MAX(0, RsBoatHealth_Of(boat) - dealt);
    Write(boat, hp);
    char fields[96];
    std::snprintf(fields, sizeof(fields), "source=%s hit=%d armour=%d dealt=%d", RsBoatHealth_HitSourceName(source),
                  (int)hit, (int)armour, (int)dealt);
    MarkHp("hit", boat, fields);
    if (dealt > 0) {
        Audio_PlayActorSound2(&boat->dyna.actor, NA_SE_EV_WOOD_BOUND);
    }
    if (hp == 0) {
        Capsize(boat);
    }
    return dealt;
}

extern "C" int32_t RsBoatHealth_Heal(RsBoat* boat, int32_t amount, int32_t source) {
    if (boat == nullptr || boat->sinkTicks > 0 || amount <= 0) {
        return 0;
    }
    const int32_t before = RsBoatHealth_Of(boat);
    Write(boat, before + amount);
    const int32_t gained = RsBoatHealth_Of(boat) - before;
    char fields[64];
    std::snprintf(fields, sizeof(fields), "source=%s gained=%d", RsBoatHealth_HealSourceName(source), (int)gained);
    MarkHp("heal", boat, fields);
    return gained;
}

// --- the save ---------------------------------------------------------------------------------------
//
// Inside each kind's object of `rsBoats` (RsBoatFit.cpp), version 1, new keys with no save break, each written only when
// it is not 0; a file without them reads as undamaged:
//   "damage": 43    what the boat is missing from its most
//   "wrecked": 1    it capsized and has not been mended

extern "C" void RsBoatHealth_SaveKind(int32_t kind) {
    if (!KindValid(kind)) {
        return;
    }
    if (sHull[kind].damage > 0) {
        SaveManager::Instance->SaveData("damage", sHull[kind].damage);
    }
    if (sHull[kind].wrecked) {
        SaveManager::Instance->SaveData("wrecked", 1);
    }
}

extern "C" void RsBoatHealth_LoadKind(int32_t kind) {
    if (!KindValid(kind)) {
        return;
    }
    int32_t damage = 0;
    int32_t wrecked = 0;
    SaveManager::Instance->LoadData("damage", damage, 0);
    SaveManager::Instance->LoadData("wrecked", wrecked, 0);
    if (damage < 0) {
        SPDLOG_ERROR("RsBoatHealth: save file's {} damage {} is negative; reading it as 0", RsBoat_KindName(kind), damage);
        damage = 0;
    }
    // Past the most is kept as it is (a newer save's better hull in an older build): RsBoatHealth_KindHp clamps it.
    sHull[kind] = Hull{ damage, wrecked != 0 };
}

extern "C" void RsBoatHealth_Reset(void) {
    for (Hull& h : sHull) {
        h = {};
    }
}

// --- the console ----------------------------------------------------------------------------------------

namespace {

bool ParseInt(const std::string& s, long* out) {
    char* end = nullptr;
    *out = std::strtol(s.c_str(), &end, 10);
    return end != s.c_str() && *end == '\0';
}

// The boat a verb acts on: the one Link is aboard, else boat idx (oldest first), else the only boat.
RsBoat* BoatArg(long idx, bool idxGiven, const char* op, std::vector<std::string>& lines, long* outIdx) {
    const std::vector<RsBoat*> boats = Boats();
    RsBoat* boat = nullptr;
    if (idxGiven) {
        boat = idx >= 0 && idx < (long)boats.size() ? boats[(size_t)idx] : nullptr;
    } else if ((boat = LinkBoat()) == nullptr && boats.size() == 1) {
        boat = boats[0];
    }
    if (boat == nullptr) {
        Addf(lines, "op=%s result=error error=no_boat boats=%d", op, (int)boats.size());
        return nullptr;
    }
    *outIdx = 0;
    for (size_t i = 0; i < boats.size(); i++) {
        if (boats[i] == boat) {
            *outIdx = (long)i;
        }
    }
    return boat;
}

int32_t Hp(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 3) {
        const int32_t kind = RsBoat_KindByName(args[1].c_str());
        long hp = 0;
        if (kind < 0) {
            Addf(lines, "op=hp result=error error=bad_kind kinds=skiff,sloop,raft");
            return 1;
        }
        if (args[2] == "full") {
            hp = RsBoatHealth_Max(kind);
        } else if (!ParseInt(args[2], &hp)) {
            Addf(lines, "op=hp result=error error=bad_value");
            return 1;
        }
        if (!RsBoatHealth_SetKindHp(kind, (int32_t)hp, RS_BOAT_HEAL_CONSOLE)) {
            Addf(lines, "op=hp result=error error=not_owned kind=%s", RsBoat_KindName(kind));
            return 1;
        }
        Addf(lines, "op=hp result=ok kind=%s hp=%d max=%d", RsBoat_KindName(kind), (int)RsBoatHealth_KindHp(kind),
             (int)RsBoatHealth_Max(kind));
        return 0;
    }
    if (args.size() != 1) {
        Addf(lines, "op=usage result=error usage=\"boat " RS_BOAT_HEALTH_GRAMMAR "\"");
        return 1;
    }
    Addf(lines, "op=hp result=ok");
    for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
        Addf(lines, "hp kind=%s owned=%d hp=%d max=%d armour=%d wrecked=%d fee=%d", RsBoat_KindName(kind),
             (int)RsBoatFit_Owned(kind), (int)RsBoatHealth_KindHp(kind), (int)RsBoatHealth_Max(kind),
             (int)RsBoatHealth_Armour(kind), (int)RsBoatHealth_KindWrecked(kind), (int)RsBoatHealth_RepairFee(kind));
    }
    const std::vector<RsBoat*> boats = Boats();
    for (size_t i = 0; i < boats.size(); i++) {
        const RsBoat* boat = boats[i];
        Addf(lines, "boat idx=%d kind=%s owned=%d hp=%d max=%d aboard=%d sinking=%d", (int)i,
             RsBoat_KindName(boat->kind), boat->owned, (int)RsBoatHealth_Of(boat), (int)RsBoatHealth_Max(boat->kind),
             (int)RsBoat_LinkAboard(boat, gPlayState), boat->sinkTicks);
    }
    return 0;
}

int32_t Damage(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    long hit = 0;
    if (args.size() < 2 || args.size() > 4 || !ParseInt(args[1], &hit) || hit < 0) {
        Addf(lines, "op=damage result=error error=bad_value");
        return 1;
    }
    int32_t source = RS_BOAT_HIT_CONSOLE;
    long idx = 0;
    bool idxGiven = false;
    for (size_t i = 2; i < args.size(); i++) {
        const int32_t s = RsBoatHealth_HitSourceByName(args[i].c_str());
        if (s >= 0) {
            source = s;
        } else if (ParseInt(args[i], &idx)) {
            idxGiven = true;
        } else {
            Addf(lines, "op=damage result=error error=bad_source sources=console,hazard,rapids,lightning,enemy");
            return 1;
        }
    }
    long at = 0;
    RsBoat* boat = BoatArg(idx, idxGiven, "damage", lines, &at);
    if (boat == nullptr) {
        return 1;
    }
    const char* ignored = IgnoreReason(boat, source); // read before the hit: one that lands may sink it
    const int32_t dealt = RsBoatHealth_Hit(boat, (int32_t)hit, source);
    if (dealt < 0) {
        Addf(lines, "op=damage result=ignored idx=%ld reason=%s", at, ignored);
        return 0;
    }
    Addf(lines, "op=damage result=ok idx=%ld source=%s dealt=%d hp=%d max=%d", at, RsBoatHealth_HitSourceName(source),
         (int)dealt, (int)RsBoatHealth_Of(boat), (int)RsBoatHealth_Max(boat->kind));
    return 0;
}

int32_t CapsizeVerb(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    long idx = 0;
    const bool idxGiven = args.size() == 2;
    if (args.size() > 2 || (idxGiven && !ParseInt(args[1], &idx))) {
        Addf(lines, "op=capsize result=error error=bad_value");
        return 1;
    }
    long at = 0;
    RsBoat* boat = BoatArg(idx, idxGiven, "capsize", lines, &at);
    if (boat == nullptr) {
        return 1;
    }
    if (boat->sinkTicks > 0) {
        Addf(lines, "op=capsize result=error error=sinking idx=%ld", at);
        return 1;
    }
    Capsize(boat);
    Addf(lines, "op=capsize result=ok idx=%ld kind=%s owned=%d", at, RsBoat_KindName(boat->kind), boat->owned);
    return 0;
}

int32_t HeartVerb(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() != 2) {
        Addf(lines, "op=heart result=error error=bad_value");
        return 1;
    }
    if (args[1] == "here") {
        const Player* player = GET_PLAYER(gPlayState);
        float y = player->actor.world.pos.y;
        if (!RsBoat_SurfaceAt(gPlayState, player->actor.world.pos.x, player->actor.world.pos.z, &y, nullptr)) {
            Addf(lines, "op=heart result=error error=no_water");
            return 1;
        }
        sHearts.push_back(Heart{ player->actor.world.pos.x, player->actor.world.pos.z, y, true, true });
        Addf(lines, "op=heart result=ok n=%d x=%d z=%d y=%d", (int)sHearts.size() - 1,
             (int)lroundf(player->actor.world.pos.x), (int)lroundf(player->actor.world.pos.z), (int)lroundf(y));
        return 0;
    }
    if (args[1] == "list") {
        for (size_t i = 0; i < sHearts.size(); i++) {
            Addf(lines, "heart n=%d x=%d z=%d y=%d found=%d live=%d", (int)i, (int)lroundf(sHearts[i].x),
                 (int)lroundf(sHearts[i].z), (int)lroundf(sHearts[i].y), sHearts[i].found ? 1 : 0,
                 sHearts[i].live ? 1 : 0);
        }
        Addf(lines, "op=heart result=ok count=%d", (int)sHearts.size());
        return 0;
    }
    if (args[1] == "clear") {
        sHearts.clear();
        Addf(lines, "op=heart result=ok count=0");
        return 0;
    }
    Addf(lines, "op=heart result=error error=bad_value");
    return 1;
}

} // namespace

int32_t RsBoatHealth_ConsoleRun(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string& sub = args[0];
    if (sub != "hp" && sub != "damage" && sub != "capsize" && sub != "heart") {
        return -1;
    }
    // `hp` reads and sets the save, so it works from the title screen too; the rest need a scene.
    if (sub == "hp") {
        return Hp(args, lines);
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=%s result=error error=no_scene", sub.c_str());
        return 1;
    }
    if (sub == "damage") {
        return Damage(args, lines);
    }
    if (sub == "capsize") {
        return CapsizeVerb(args, lines);
    }
    return HeartVerb(args, lines);
}
