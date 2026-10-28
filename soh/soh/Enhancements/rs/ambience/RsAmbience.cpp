/*
 * Area ambience, Phase A (sturdy-bassoon#275). RsAmbience.h says what this is and what it is not.
 */

#include "RsAmbience.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_River_Sound/z_en_river_sound.h"
extern PlayState* gPlayState;
}

#define CVAR_RS_AMBIENCE CVAR_ENHANCEMENT("RsAmbience")
// How many emitters may ask for their sound at once: the nearest N to Link. CapToNearest says why.
#define CVAR_RS_AMBIENCE_MAX CVAR_ENHANCEMENT("RsAmbienceMax")

namespace {

// Where the overworld sits in RS's frame: f2p_overworld_world.inc's RS_WORLD_SCENE row, the zone
// director's anchor (the RS tile whose NORTH-west corner is OoT (0, 0)) and 40 units a tile. Copied,
// not included, because this is an experiment; a world re-export that moves the origin moves every
// row, and the `pos=` on the status line is where to see it.
constexpr int32_t kOriginRsX = 3136;
constexpr int32_t kOriginRsY = 3327;
constexpr float kUnitsPerTile = 40.0f;
// Above anything the overworld's terrain reaches, so the floor raycast lands on the top surface.
constexpr float kRaycastFromY = 4000.0f;
// How far above the floor the emitter sits: about Link's ear height.
constexpr float kAboveFloor = 40.0f;
// The env bank has 3 channels (gChannelsPerBank). 2 leaves one for the game's own env sounds - a
// door, a chest, an RS fire - which this feature must not crowd out.
constexpr int32_t kDefaultMax = 2;

struct Emitter {
    const char* name;
    int32_t rsX;
    int32_t rsY;
    int16_t sound; // an En_River_Sound param (z_en_river_sound.h)
    // How far the sound reaches, in RS tiles from Link (XZ). Seeded from the loc's own RS radius.
    float radius;
    // Loudness at the emitter, 0..1, multiplied into the engine's own distance curve. Water sits lower
    // than fire: the owner's first listen (2026-10-27) found the water the more offensive of the two.
    float gain;
};

/*
 * THE TABLE. A few of RS's real ambient emitters on the Lumbridge square (50,50), plane 0, from the
 * cache's placements. Each row: the RS loc it stands for, its RS radius (tiles) and sound, and why
 * this OoT sound. Left out on purpose: the area-hum locs (16433/16438, sound 2184 - wind and birds,
 * which the actor's table has no match for), the rocks (5604) and the castle's grandfather clock.
 */
// Radius and gain are the last two fields: the radius is RS's own, the gain a first guess for the ear.
const Emitter kEmitters[] = {
    { "fountain_south", 3221, 3210, RS_FOUNTAIN_WATER, 7.0f, 0.5f },   // loc 879 Fountain, r=7, sound 2150
    { "fountain_north", 3221, 3226, RS_FOUNTAIN_WATER, 7.0f, 0.5f },   // loc 879 Fountain, r=7, sound 2150
    { "cooking_range", 3212, 3215, RS_TORCH_CRACKLING, 3.0f, 0.7f },   // loc 114 Cooking range (kitchen), r=3
    { "fireplace", 3214, 3249, RS_TORCH_CRACKLING, 5.0f, 0.7f },       // loc 24969 Fireplace, r=5, sound 2198
    { "furnace", 3226, 3256, RS_TORCH_CRACKLING, 10.0f, 0.7f },        // loc 24009 Furnace, r=10, sound 2209
    { "spit_roast", 3245, 3246, RS_TORCH_CRACKLING, 3.0f, 0.7f },      // loc 5608 Spit roast, r=3, sound 2220
    { "river_lum", 3243, 3229, RS_SMALL_WATERFALL, 20.0f, 0.4f },      // loc 16392 by the River Lum, r=20, sound 2168
    { "river_lum_north", 3235, 3262, RS_SMALL_WATERFALL, 20.0f, 0.4f }, // loc 16392, r=20, sound 2168
};
constexpr int32_t kEmitterCount = (int32_t)(sizeof(kEmitters) / sizeof(kEmitters[0]));

// The live actors, one per row; null when not spawned. Cleared on every scene init, because the
// scene's actors go with it. Checked against the actor list before every use (IsLive), since
// something other than this file could kill one.
Actor* sActors[kEmitterCount] = {};
// Whether each row is in range and among the nearest N this tick, and so allowed to ask for its sound.
bool sActive[kEmitterCount] = {};
// Each row's live radius and gain: the table's, until `rsambience radius|gain` tunes them. Not saved -
// a value the owner settles on by ear goes into the table.
float sRadius[kEmitterCount];
float sGain[kEmitterCount];
bool sTuningSeeded = false;
// The volume each row asks for this tick (gain x the radius fade). The engine keeps this POINTER with
// the sound's entry and reads it on every audio update, so it must outlive the request: static storage.
f32 sVol[kEmitterCount] = {};
bool sSpawnPending = false;

// Tunable ranges, in RS tiles and as a multiplier. Above 1 the gain would be louder than vanilla.
constexpr float kRadiusMin = 0.5f;
constexpr float kRadiusMax = 64.0f;
constexpr float kGainMax = 1.0f;

void SeedTuning() {
    if (sTuningSeeded) {
        return;
    }
    for (int32_t i = 0; i < kEmitterCount; i++) {
        sRadius[i] = kEmitters[i].radius;
        sGain[i] = kEmitters[i].gain;
    }
    sTuningSeeded = true;
}

bool Enabled() {
    return CVarGetInteger(CVAR_RS_AMBIENCE, 1) != 0;
}

int32_t MaxActive() {
    const int32_t n = CVarGetInteger(CVAR_RS_AMBIENCE_MAX, kDefaultMax);
    return n < 0 ? 0 : (n > kEmitterCount ? kEmitterCount : n);
}

bool InTableScene(PlayState* play) {
    return play != nullptr && play->sceneNum == SCENE_F2P_OVERWORLD;
}

// The sfx En_River_Sound's own draw plays for each param (its soundEffects table).
u16 SfxFor(int16_t sound) {
    switch (sound) {
        case RS_FOUNTAIN_WATER:
            return NA_SE_EV_FOUNTAIN - SFX_FLAG;
        case RS_TORCH_CRACKLING:
            return NA_SE_EV_TORCH - SFX_FLAG;
        case RS_SMALL_WATERFALL:
        default:
            return NA_SE_EV_WATER_WALL - SFX_FLAG;
    }
}

const char* SoundName(int16_t sound) {
    switch (sound) {
        case RS_FOUNTAIN_WATER:
            return "fountain";
        case RS_TORCH_CRACKLING:
            return "torch";
        case RS_SMALL_WATERFALL:
            return "small_waterfall";
        default:
            return "other";
    }
}

Vec3f TileCentre(const Emitter& e) {
    // The inverse of ZoneDirector.cpp's WorldToRs: rsY = originY - floor(z / 40), so z grows as RS y
    // falls. +0.5 puts it in the middle of the tile.
    Vec3f pos;
    pos.x = ((float)(e.rsX - kOriginRsX) + 0.5f) * kUnitsPerTile;
    pos.y = kRaycastFromY;
    pos.z = ((float)(kOriginRsY - e.rsY) + 0.5f) * kUnitsPerTile;
    return pos;
}

bool IsLive(PlayState* play, Actor* actor) {
    if (actor == nullptr) {
        return false;
    }
    for (Actor* a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        if (a == actor) {
            return a->update != nullptr;
        }
    }
    return false;
}

int32_t SpawnedCount(PlayState* play) {
    int32_t n = 0;
    for (int32_t i = 0; i < kEmitterCount; i++) {
        n += IsLive(play, sActors[i]) ? 1 : 0;
    }
    return n;
}

void SpawnAll(PlayState* play) {
    for (int32_t i = 0; i < kEmitterCount; i++) {
        if (IsLive(play, sActors[i])) {
            continue;
        }
        Vec3f pos = TileCentre(kEmitters[i]);
        CollisionPoly* poly = nullptr;
        const f32 floorY = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
        pos.y = (floorY > BGCHECK_Y_MIN ? floorY : 0.0f) + kAboveFloor;
        sActors[i] = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_RIVER_SOUND, pos.x, pos.y, pos.z, 0, 0, 0,
                                 kEmitters[i].sound);
        if (sActors[i] != nullptr) {
            // Room -1: a room change kills every actor of the room it leaves, and an emitter belongs
            // to the scene, not to whichever room Link was in when it spawned.
            sActors[i]->room = -1;
        }
    }
}

void KillAll(PlayState* play) {
    for (int32_t i = 0; i < kEmitterCount; i++) {
        if (IsLive(play, sActors[i])) {
            Actor_Kill(sActors[i]);
        }
        sActors[i] = nullptr;
    }
}

// Plays one emitter's sound. Replaces En_River_Sound's own draw, which plays at the vanilla volume:
// this one passes the row's own volume, so the radius and gain apply. The actor is kept only as the
// carrier of projectedPos, the camera-relative position the engine pans and attenuates by.
void EmitterDraw(Actor* actor, PlayState*) {
    for (int32_t i = 0; i < kEmitterCount; i++) {
        if (sActors[i] == actor) {
            Audio_PlaySoundGeneral(SfxFor(kEmitters[i].sound), &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &sVol[i], &gSfxDefaultReverb);
            return;
        }
    }
}

/*
 * THE RADIUS (the owner's first listen, 2026-10-27). All three sounds use the engine's longest range
 * (sfxParams & 3 == 0 in sEnvBankParams): full volume to 100 units, then a fade that reaches silence
 * only at 10,000 units, 250 tiles - a fountain is still at half volume 50 tiles away. So each row has
 * its own radius: full volume within one tile of the emitter, falling linearly to nothing at the radius,
 * by XZ distance from Link the way RS measures it in tiles. Out of range, the row does not ask at all.
 *
 * THE CAP (found on the first run, 2026-10-27). Left alone, every emitter asks for its sound every tick
 * and the env bank's 3 channels go by priority = distance^2 + (255 - importance)^2 * 76^2. NA_SE_EV_TORCH
 * has importance 0x10 against 0x30 for the water sounds, a gap worth about 9,000 units of distance: a
 * fountain across Lumbridge outranks a fire at Link's feet, and the fires never played at all. So only
 * the nearest N emitters in range keep their draw, which is what asks for the sound. The radius alone
 * does not make the cap redundant: where two radii overlap, the wall is back.
 */
float Fade(float dist, float radiusTiles) {
    const float full = kUnitsPerTile;
    const float edge = radiusTiles * kUnitsPerTile;
    if (dist >= edge) {
        return 0.0f;
    }
    if (dist <= full || edge <= full) {
        return 1.0f;
    }
    return 1.0f - (dist - full) / (edge - full);
}

void CapToNearest(PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == nullptr) {
        return;
    }
    SeedTuning();
    float dist[kEmitterCount];
    for (int32_t i = 0; i < kEmitterCount; i++) {
        sActive[i] = false;
        sVol[i] = 0.0f;
        dist[i] = -1.0f;
        if (IsLive(play, sActors[i])) {
            const float d = Math_Vec3f_DistXZ(&sActors[i]->world.pos, &player->actor.world.pos);
            sVol[i] = sGain[i] * Fade(d, sRadius[i]);
            if (sVol[i] > 0.0f) {
                dist[i] = d;
            }
        }
    }
    const int32_t max = MaxActive();
    for (int32_t k = 0; k < max; k++) {
        int32_t best = -1;
        for (int32_t i = 0; i < kEmitterCount; i++) {
            if (dist[i] >= 0.0f && !sActive[i] && (best < 0 || dist[i] < dist[best])) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        sActive[best] = true;
    }
    for (int32_t i = 0; i < kEmitterCount; i++) {
        if (IsLive(play, sActors[i])) {
            sActors[i]->draw = sActive[i] ? EmitterDraw : nullptr;
        }
    }
}

// Spawning waits for Link's first tick rather than OnSceneSpawnActors, which fires only when the
// scene has setup actors, and needs the collision the floor raycast reads.
void OnPlayerUpdate() {
    if (!InTableScene(gPlayState)) {
        sSpawnPending = false;
        return;
    }
    if (sSpawnPending) {
        sSpawnPending = false;
        if (Enabled()) {
            SpawnAll(gPlayState);
        }
    }
    CapToNearest(gPlayState);
}

void RegisterRsAmbience() {
    // Unconditional, with the CVar read inside: `rsambience on|off` writes it from the console, and a
    // console write never re-runs ShipInit (soh-add-console-command's gotcha).
    COND_HOOK(OnSceneInit, true, [](int16_t) {
        for (int32_t i = 0; i < kEmitterCount; i++) {
            sActors[i] = nullptr;
            sActive[i] = false;
        }
        sSpawnPending = true;
    });
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdate);
}

RegisterShipInitFunc rsAmbienceInitFunc(RegisterRsAmbience);

// --- the console ---------------------------------------------------------------------------------

// The env bank's entry list: every sound asking to play (requested) and those holding a channel
// (playing). The list is a linked list threaded through gSoundBanks[bank] from the head entry 0,
// ended by 0xFF (code_800F7260.c); bounded here so a corrupt link cannot spin the console.
void EnvCounts(int32_t* requested, int32_t* playing) {
    *requested = 0;
    *playing = 0;
    uint8_t index = gSoundBanks[BANK_ENV][0].next;
    for (int32_t guard = 0; index != 0xFF && guard < 64; guard++) {
        (*requested)++;
        if (gSoundBanks[BANK_ENV][index].state >= SFX_STATE_PLAYING_REFRESH) {
            (*playing)++;
        }
        index = gSoundBanks[BANK_ENV][index].next;
    }
}

// This actor's own entry, matched the way Audio_ProcessSoundRequest matches a re-request: by the
// position pointer it was requested with, which for Audio_PlayActorSound2 is &projectedPos.
void EmitterState(Actor* actor, bool* requested, bool* playing) {
    *requested = false;
    *playing = false;
    uint8_t index = gSoundBanks[BANK_ENV][0].next;
    for (int32_t guard = 0; index != 0xFF && guard < 64; guard++) {
        const SoundBankEntry& e = gSoundBanks[BANK_ENV][index];
        if (e.posX == &actor->projectedPos.x) {
            *requested = true;
            *playing = e.state >= SFX_STATE_PLAYING_REFRESH;
            return;
        }
        index = e.next;
    }
}

int32_t Status(const std::string& op, std::vector<std::string>& lines) {
    PlayState* play = gPlayState;
    int32_t requested = 0, playing = 0;
    EnvCounts(&requested, &playing);
    const int32_t channels = gChannelsPerBank[gSfxChannelLayout][BANK_ENV];
    if (play == nullptr) {
        lines.push_back("op=" + op + " enabled=" + std::to_string(Enabled() ? 1 : 0) + " scene=none here=0 spawned=0");
        return 0;
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "op=%s enabled=%d scene=0x%02X here=%d spawned=%d max=%d env_requested=%d env_playing=%d "
                  "env_channels=%d",
                  op.c_str(), Enabled() ? 1 : 0, play->sceneNum, InTableScene(play) ? 1 : 0, SpawnedCount(play),
                  MaxActive(), requested, playing, channels);
    lines.push_back(buf);
    if (!InTableScene(play)) {
        return 0;
    }
    SeedTuning();
    Player* player = GET_PLAYER(play);
    for (int32_t i = 0; i < kEmitterCount; i++) {
        const Emitter& e = kEmitters[i];
        Actor* actor = sActors[i];
        const bool live = IsLive(play, actor);
        bool req = false, playingNow = false;
        Vec3f pos = TileCentre(e);
        if (live) {
            EmitterState(actor, &req, &playingNow);
            pos = actor->world.pos;
        }
        const float dist = (live && player != nullptr) ? Math_Vec3f_DistXZ(&pos, &player->actor.world.pos) : -1.0f;
        std::snprintf(buf, sizeof(buf),
                      "emitter=%s sound=%s tile=%d,%d pos=%.0f,%.0f,%.0f dist=%.0f spawned=%d active=%d requested=%d "
                      "playing=%d radius=%.1f gain=%.2f vol=%.2f",
                      e.name, SoundName(e.sound), e.rsX, e.rsY, pos.x, live ? pos.y : 0.0f, pos.z, dist, live ? 1 : 0,
                      (live && sActive[i]) ? 1 : 0, req ? 1 : 0, playingNow ? 1 : 0, sRadius[i], sGain[i],
                      (live && sActive[i]) ? sVol[i] : 0.0f);
        lines.push_back(buf);
    }
    return 0;
}

int32_t Switch(bool on, std::vector<std::string>& lines) {
    CVarSetInteger(CVAR_RS_AMBIENCE, on ? 1 : 0);
    CVarSave();
    PlayState* play = gPlayState;
    if (InTableScene(play)) {
        if (on) {
            SpawnAll(play);
        } else {
            KillAll(play);
        }
    }
    // Killed actors leave the list on the next actor update, so `spawned` counts the live ones that
    // are not dying: an `off` reads 0 at once.
    lines.push_back(std::string("op=") + (on ? "on" : "off") + " result=ok enabled=" + (on ? "1" : "0") +
                    " spawned=" + std::to_string(play != nullptr ? SpawnedCount(play) : 0));
    return 0;
}

int32_t SetMax(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() < 2) {
        lines.push_back("op=max result=ok max=" + std::to_string(MaxActive()));
        return 0;
    }
    int32_t n = -1;
    if (args[1] == "default") {
        n = kDefaultMax;
    } else if (args[1].size() == 1 && args[1][0] >= '0' && args[1][0] <= '9') {
        n = args[1][0] - '0';
    }
    if (n < 0 || n > kEmitterCount) {
        lines.push_back("op=max result=error error=range max_allowed=" + std::to_string(kEmitterCount));
        return 1;
    }
    CVarSetInteger(CVAR_RS_AMBIENCE_MAX, n);
    CVarSave();
    lines.push_back("op=max result=ok max=" + std::to_string(MaxActive()));
    return 0;
}

// `radius|gain <emitter|all> [<value>|default]`: one line per row it names, a read without a value.
// The value is checked before anything is written, so a refusal changes nothing.
int32_t Tune(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string& op = args[0];
    const bool isRadius = op == "radius";
    const float lo = isRadius ? kRadiusMin : 0.0f;
    const float hi = isRadius ? kRadiusMax : kGainMax;
    int32_t only = -1;
    if (args.size() >= 2 && args[1] != "all") {
        for (int32_t i = 0; i < kEmitterCount; i++) {
            if (args[1] == kEmitters[i].name) {
                only = i;
            }
        }
    }
    if (args.size() < 2 || (args[1] != "all" && only < 0)) {
        lines.push_back("op=" + op + " result=error error=emitter");
        return 1;
    }
    const bool write = args.size() >= 3;
    const bool toDefault = write && args[2] == "default";
    float value = 0.0f;
    if (write && !toDefault) {
        char* end = nullptr;
        value = std::strtof(args[2].c_str(), &end);
        if (end == args[2].c_str() || *end != 0 || !std::isfinite(value) || value < lo || value > hi) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "op=%s result=error error=range min=%.1f max=%.1f", op.c_str(), lo, hi);
            lines.push_back(buf);
            return 1;
        }
    }
    SeedTuning();
    for (int32_t i = 0; i < kEmitterCount; i++) {
        if (only >= 0 && i != only) {
            continue;
        }
        float& slot = isRadius ? sRadius[i] : sGain[i];
        if (write) {
            slot = toDefault ? (isRadius ? kEmitters[i].radius : kEmitters[i].gain) : value;
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "op=%s result=ok emitter=%s %s=%.2f", op.c_str(), kEmitters[i].name,
                      op.c_str(), slot);
        lines.push_back(buf);
    }
    return 0;
}

const char* kUsage =
    "usage: rsambience [status] | on | off | max [<n>|default] | radius|gain <emitter|all> [<value>|default]";

} // namespace

int32_t RsAmbienceConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty() || args[0] == "status") {
        return Status("status", lines);
    }
    if (args[0] == "on") {
        return Switch(true, lines);
    }
    if (args[0] == "off") {
        return Switch(false, lines);
    }
    if (args[0] == "max") {
        return SetMax(args, lines);
    }
    if (args[0] == "radius" || args[0] == "gain") {
        return Tune(args, lines);
    }
    lines.push_back(kUsage);
    return 1;
}

namespace {

const ConsoleSink::Command rsAmbienceCommand(
    "rsambience", RsAmbienceConsole_Run,
    "Area ambience experiment (sturdy-bassoon#275): status | on | off | max [<n>|default] | "
    "radius|gain <emitter|all> [<value>|default]. A few of RS's ambient emitters around Lumbridge "
    "on vanilla En_River_Sound actors. status lists each emitter with its distance, radius, gain "
    "and whether its sound is playing, plus the env sound budget in use anywhere; on|off switch "
    "them live for an A/B by ear; max is how many of the nearest in range may sound at once "
    "(default 2); radius (RS tiles) and gain (0..1) tune one emitter or all of them by ear, "
    "unsaved.",
    { { "status|on|off|max|radius|gain", Ship::ArgumentType::TEXT, true },
      { "n|emitter", Ship::ArgumentType::TEXT, true },
      { "value", Ship::ArgumentType::TEXT, true } });

} // namespace
