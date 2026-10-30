/*
 * Area ambience (sturdy-bassoon#275, #276). RsAmbience.h says what this is and what it is not.
 */

#include "RsAmbience.h"
#include "RsAmbienceMixer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>
#include <spdlog/spdlog.h>

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
// code_800EC960.c, not in a header: the engine's own pan for a view-space position, 0 (left) .. 127 (right).
s8 Audio_ComputeSoundPanSigned(f32 x, f32 z, u8 token);
}

#define CVAR_RS_AMBIENCE CVAR_ENHANCEMENT("RsAmbience")
// How many OoT-sound emitters may ask for their sound at once: the nearest N to Link. CapToNearest says why.
#define CVAR_RS_AMBIENCE_MAX CVAR_ENHANCEMENT("RsAmbienceMax")

// A library entry's `oot` when it plays files through the mixer (GridToolSoundLibrary.inc).
#define RS_GEN_SOUND_FILE (-1)

namespace {

constexpr float kUnitsPerTile = 40.0f;
// How far above the floor an emitter sits: about Link's ear height.
constexpr float kAboveFloor = 40.0f;
// The env bank has 3 channels (gChannelsPerBank). 2 leaves one for the game's own env sounds - a
// door, a chest, an RS fire - which this feature must not crowd out. `max` goes no higher than the 3.
constexpr int32_t kDefaultMax = 2;
constexpr int32_t kMaxMax = 3;
// File emitters the mixer may play at once, nearest first. Half its voice pool, so a fading voice never
// blocks a new one.
constexpr int32_t kMixerVoices = 8;
// A scene's emitters at most. The volumes live in a fixed array (sVol) because the engine keeps a pointer to
// one for as long as its sound plays; a scene past this keeps its first kMaxEmitters and logs the rest.
constexpr int32_t kMaxEmitters = 512;
// An emitter's `sound` when it plays files through the mixer instead of an OoT sfx.
constexpr int16_t kFile = RS_GEN_SOUND_FILE;
// pi/2, spelled out: M_PI needs a header this file does not include (RsMenu.cpp has the same note).
constexpr float kQuarterTurn = 1.57079633f;

// --- where emitters come from ----------------------------------------------------------------------

/*
 * THE SOUND LIBRARY and THE PLACEMENTS (#276). The grid tool exports both: the library (tools/sfx/sounds.json)
 * as GridToolSoundLibrary.inc, and every exported scene's sound placements as `<slug>_sounds.inc`, through the
 * aggregate GridToolSounds.inc (the grid tool README, "Sounds"). A placement names a library entry and may
 * override its radius and gain (negative: the library's). Collected by functions rather than array
 * initialisers, so a library or an aggregate with no rows yet compiles.
 */
struct SoundDef {
    const char* id;
    int16_t sound;     // an En_River_Sound param, or kFile
    const char* files; // kFile: rs-sfx ids joined by '|'
    int16_t minTicks;  // kFile: 0, 0 loops the first file; otherwise one plays every minTicks..maxTicks
    int16_t maxTicks;
    float radius; // RS tiles
    float gain;   // 0..1
};

struct PlacedSound {
    int16_t scene;
    const char* name;
    const char* sound; // a SoundDef id
    float x, y, z;     // scene units, y the floor
    float radius, gain;
};

void CollectLibrary(std::vector<SoundDef>& defs) {
#define RS_GEN_SOUND_DEF(id, oot, files, minTicks, maxTicks, radius, gain) \
    defs.push_back({ (id), (int16_t)(oot), (files), (minTicks), (maxTicks), (radius), (gain) });
#include "soh/custom/scenes/grid_tool/generated/GridToolSoundLibrary.inc"
#undef RS_GEN_SOUND_DEF
}

void CollectPlaced(std::vector<PlacedSound>& rows) {
#define RS_GEN_SOUND(scene, name, sound, x, y, z, radius, gain) \
    rows.push_back({ (int16_t)(scene), (name), (sound), (x), (y), (z), (radius), (gain) });
#include "soh/custom/scenes/grid_tool/generated/GridToolSounds.inc"
#undef RS_GEN_SOUND
}

/*
 * THE BUILT-IN TABLE: Phase A's hand-written emitters (#275), for the F2P overworld only, placed by RS tile.
 * A few of RS's real ambient emitters on the Lumbridge square (50,50), plane 0, from the cache's placements;
 * the two file rows are stand-ins that prove the mixer. They go when the owner has placed the same sounds
 * in the world map with the grid tool (#276 P2); until then a scene's exported placements and these sit
 * side by side.
 * The two fountains went first (2026-10-30), at the owner's say: fountain-879's kind carries the "fountain"
 * sound (#276 P3), so a fountain placed in the Lumbridge chunk plays it, and keeping these would have played
 * each fountain twice. Until fountains are placed there, Lumbridge's fountains are silent.
 */
// Where the overworld sits in RS's frame: f2p_overworld_world.inc's RS_WORLD_SCENE row (the RS tile whose
// NORTH-west corner is OoT (0, 0)). Copied, not included, because these rows are going.
constexpr int32_t kOriginRsX = 3136;
constexpr int32_t kOriginRsY = 3327;
// Above anything the overworld's terrain reaches, so the floor raycast lands on the top surface.
constexpr float kRaycastFromY = 4000.0f;

struct BuiltIn {
    const char* name;
    int32_t rsX;
    int32_t rsY;
    const char* sound; // a SoundDef id
    float radius;      // negative: the library's
};

const BuiltIn kBuiltIn[] = {
    { "cooking_range", 3212, 3215, "fire", -1.0f },        // loc 114 Cooking range (kitchen), r=3
    { "fireplace", 3214, 3249, "fire", 5.0f },             // loc 24969 Fireplace, r=5, sound 2198
    { "furnace", 3226, 3256, "fire", 10.0f },              // loc 24009 Furnace, r=10, sound 2209
    { "spit_roast", 3245, 3246, "fire", -1.0f },           // loc 5608 Spit roast, r=3, sound 2220
    { "river_lum", 3243, 3229, "river", -1.0f },           // loc 16392 by the River Lum, r=20, sound 2168
    { "river_lum_north", 3235, 3262, "river", -1.0f },     // loc 16392, r=20, sound 2168
    { "birds_fountain", 3224, 3213, "birds", -1.0f },      // stand-in: RS birdsong by the south fountain
    { "waystone_test", 3232, 3216, "waystone-hum", -1.0f }, // stand-in: proves a file loop
};

// --- the current scene's emitters ------------------------------------------------------------------

struct Emitter {
    std::string name;
    const char* from; // "export" or "builtin"
    int16_t sound;
    std::string files;
    int16_t minTicks, maxTicks;
    Vec3f pos;          // where it stands, ear height; a built-in row's y comes from a raycast at spawn
    bool raycast;
    float baseRadius;   // the placement's or the library's, before `rsambience` tuning
    float baseGain;
    float radius, gain; // live
    // live state
    Actor* actor = nullptr;
    bool active = false;
    bool filesOk = false;
    uint32_t trigger = 0; // one-shots: the mixer starts a file when this changes
    int32_t countdown = 0;
    int32_t pick = 0;
};

std::vector<Emitter> sEmitters;
// The volume each emitter asks for this tick (gain x the radius fade). The engine keeps this POINTER with
// the sound's entry and reads it on every audio update, so it must outlive the request and never move: a
// fixed array, indexed like sEmitters.
f32 sVol[kMaxEmitters] = {};
bool sSpawnPending = false;
// `rsambience radius|gain` values by emitter name: kept across scene reloads in one session, never saved. A
// value the owner settles on by ear goes into the library or the placement.
std::unordered_map<std::string, float> sTunedRadius;
std::unordered_map<std::string, float> sTunedGain;

// Tunable ranges, in RS tiles and as a multiplier. Above 1 the gain would be louder than vanilla.
constexpr float kRadiusMin = 0.5f;
constexpr float kRadiusMax = 64.0f;
constexpr float kGainMax = 1.0f;

bool Enabled() {
    return CVarGetInteger(CVAR_RS_AMBIENCE, 1) != 0;
}

int32_t MaxActive() {
    const int32_t n = CVarGetInteger(CVAR_RS_AMBIENCE_MAX, kDefaultMax);
    return n < 0 ? 0 : (n > kMaxMax ? kMaxMax : n);
}

bool IsFile(const Emitter& e) {
    return e.sound == kFile;
}

const SoundDef* FindDef(const std::vector<SoundDef>& defs, const char* id) {
    for (const SoundDef& d : defs) {
        if (std::strcmp(d.id, id) == 0) {
            return &d;
        }
    }
    return nullptr;
}

void Push(const SoundDef& def, const std::string& name, const char* from, Vec3f pos, bool raycast, float radius,
          float gain) {
    if ((int32_t)sEmitters.size() >= kMaxEmitters) {
        SPDLOG_WARN("[rs-ambience] more than {} emitters in the scene: {} and later are left out", kMaxEmitters, name);
        return;
    }
    Emitter e;
    e.name = name;
    e.from = from;
    e.sound = def.sound;
    e.files = def.files != nullptr ? def.files : "";
    e.minTicks = def.minTicks;
    e.maxTicks = def.maxTicks;
    e.pos = pos;
    e.raycast = raycast;
    e.baseRadius = radius >= 0.0f ? radius : def.radius;
    e.baseGain = gain >= 0.0f ? gain : def.gain;
    auto r = sTunedRadius.find(name);
    auto g = sTunedGain.find(name);
    e.radius = r != sTunedRadius.end() ? r->second : e.baseRadius;
    e.gain = g != sTunedGain.end() ? g->second : e.baseGain;
    sEmitters.push_back(e);
}

// The scene's emitters: its exported placements, then (in the F2P overworld) the built-in rows. A placement
// naming a library entry the library lacks is logged and left out; the grid tool's export notes it too.
void BuildEmitters(int16_t sceneNum) {
    sEmitters.clear();
    std::vector<SoundDef> defs;
    std::vector<PlacedSound> placed;
    CollectLibrary(defs);
    CollectPlaced(placed);
    for (const PlacedSound& p : placed) {
        if (p.scene != sceneNum) {
            continue;
        }
        const SoundDef* def = FindDef(defs, p.sound);
        if (def == nullptr) {
            SPDLOG_WARN("[rs-ambience] {} plays \"{}\", which the sound library does not have", p.name, p.sound);
            continue;
        }
        Push(*def, p.name, "export", { p.x, p.y + kAboveFloor, p.z }, false, p.radius, p.gain);
    }
    if (sceneNum == SCENE_F2P_OVERWORLD) {
        for (const BuiltIn& b : kBuiltIn) {
            const SoundDef* def = FindDef(defs, b.sound);
            if (def == nullptr) {
                SPDLOG_WARN("[rs-ambience] built-in {} plays \"{}\", which the sound library does not have", b.name,
                            b.sound);
                continue;
            }
            // The inverse of ZoneDirector.cpp's WorldToRs: z grows as RS y falls; +0.5 is the tile's middle.
            const Vec3f pos = { ((float)(b.rsX - kOriginRsX) + 0.5f) * kUnitsPerTile, kRaycastFromY,
                                ((float)(kOriginRsY - b.rsY) + 0.5f) * kUnitsPerTile };
            Push(*def, b.name, "builtin", pos, true, b.radius, -1.0f);
        }
    }
}

// --- the OoT sounds ----------------------------------------------------------------------------------

// Every En_River_Sound param that is a plain looping sfx, from the actor's own soundEffects table
// (z_en_river_sound.c), with the name `rsambience status` prints. The params left out (0, 4, 5, 11, 12, 13, 19)
// are not an sfx at all - a river that follows a path, Saria's song, the fairy fountain's music - and an
// emitter naming one stays silent and reads sound=unsupported. The grid tool's OOT_SOUNDS is the same list.
struct OotSound {
    int16_t param;
    u16 sfx;
    const char* name;
};
const OotSound kOotSounds[] = {
    { RS_SMALL_WATERFALL, NA_SE_EV_WATER_WALL - SFX_FLAG, "small_waterfall" },
    { RS_LAVA_BUBBLES_1, NA_SE_EV_MAGMA_LEVEL - SFX_FLAG, "lava_bubbles_1" },
    { RS_LARGE_WATERFALL, NA_SE_EV_WATER_WALL_BIG - SFX_FLAG, "large_waterfall" },
    { RS_LAVA_BUBBLES_2, NA_SE_EV_MAGMA_LEVEL_M - SFX_FLAG, "lava_bubbles_2" },
    { RS_LAVA_BUBBLES_3, NA_SE_EV_MAGMA_LEVEL_L - SFX_FLAG, "lava_bubbles_3" },
    { RS_DRIPPING_WATER, NA_SE_EV_WATERDROP - SFX_FLAG, "dripping_water" },
    { RS_FOUNTAIN_WATER, NA_SE_EV_FOUNTAIN - SFX_FLAG, "fountain" },
    { RS_MARKET_CROWD, NA_SE_EV_CROWD - SFX_FLAG, "market_crowd" },
    { RS_SANDSTORM, NA_SE_EV_SAND_STORM - SFX_FLAG, "sandstorm" },
    { RS_LAKESIDE_LAB_TANK, NA_SE_EV_WATER_BUBBLE - SFX_FLAG, "water_bubble" },
    { RS_CHAMBER_OF_SAGES_1, NA_SE_EV_KENJA_ENVIROMENT_0 - SFX_FLAG, "chamber_of_sages_1" },
    { RS_CHAMBER_OF_SAGES_2, NA_SE_EV_KENJA_ENVIROMENT_1 - SFX_FLAG, "chamber_of_sages_2" },
    { RS_RUMBLING, NA_SE_EV_EARTHQUAKE - SFX_FLAG, "rumbling" },
    { RS_TORCH_CRACKLING, NA_SE_EV_TORCH - SFX_FLAG, "torch" },
    { RS_COW_MOOING, NA_SE_EV_COW_CRY_LV - SFX_FLAG, "cow" },
};

const OotSound* FindOotSound(int16_t param) {
    for (const OotSound& s : kOotSounds) {
        if (s.param == param) {
            return &s;
        }
    }
    return nullptr;
}

// 0 for a param that is not a plain sfx: EmitterDraw then requests nothing.
u16 SfxFor(int16_t sound) {
    const OotSound* s = FindOotSound(sound);
    return s != nullptr ? s->sfx : 0;
}

const char* SoundName(int16_t sound) {
    const OotSound* s = FindOotSound(sound);
    return s != nullptr ? s->name : "unsupported";
}

std::vector<std::string> FileIds(const Emitter& e) {
    std::vector<std::string> ids;
    std::string current;
    for (char c : e.files) {
        if (c == '|') {
            ids.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        ids.push_back(current);
    }
    return ids;
}

// --- the actors --------------------------------------------------------------------------------------

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
    for (const Emitter& e : sEmitters) {
        n += IsLive(play, e.actor) ? 1 : 0;
    }
    return n;
}

void SpawnAll(PlayState* play) {
    for (Emitter& e : sEmitters) {
        if (IsLive(play, e.actor)) {
            continue;
        }
        if (IsFile(e)) {
            // Decoded once and kept; a file missing from every archive is logged and the emitter stays silent.
            e.filesOk = true;
            for (const std::string& id : FileIds(e)) {
                e.filesOk = RsAmbienceMixer_Load(id.c_str()) && e.filesOk;
            }
        }
        Vec3f pos = e.pos;
        if (e.raycast) {
            CollisionPoly* poly = nullptr;
            const f32 floorY = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
            pos.y = (floorY > BGCHECK_Y_MIN ? floorY : 0.0f) + kAboveFloor;
        }
        // The actor only carries the position: CapToNearest replaces its draw with EmitterDraw (an OoT sound)
        // or none (a file, which the mixer plays). So every emitter spawns with the plain fountain param, whose
        // Init and Update do nothing, never with a param whose actor behaviour is special (a river on a path,
        // Saria's song).
        e.actor = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_RIVER_SOUND, pos.x, pos.y, pos.z, 0, 0, 0,
                              RS_FOUNTAIN_WATER);
        if (e.actor != nullptr) {
            // Room -1: a room change kills every actor of the room it leaves, and an emitter belongs
            // to the scene, not to whichever room Link was in when it spawned.
            e.actor->room = -1;
        }
    }
}

void KillAll(PlayState* play) {
    for (Emitter& e : sEmitters) {
        if (IsLive(play, e.actor)) {
            Actor_Kill(e.actor);
        }
        e.actor = nullptr;
    }
}

// Plays one emitter's sound. Replaces En_River_Sound's own draw, which plays at the vanilla volume:
// this one passes the emitter's own volume, so the radius and gain apply. The actor is kept only as the
// carrier of projectedPos, the camera-relative position the engine pans and attenuates by.
void EmitterDraw(Actor* actor, PlayState*) {
    for (size_t i = 0; i < sEmitters.size(); i++) {
        if (sEmitters[i].actor == actor) {
            const u16 sfx = SfxFor(sEmitters[i].sound);
            if (sfx != 0) {
                Audio_PlaySoundGeneral(sfx, &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &sVol[i],
                                       &gSfxDefaultReverb);
            }
            return;
        }
    }
}

/*
 * THE RADIUS (the owner's first listen, 2026-10-27). All three sounds use the engine's longest range
 * (sfxParams & 3 == 0 in sEnvBankParams): full volume to 100 units, then a fade that reaches silence
 * only at 10,000 units, 250 tiles - a fountain is still at half volume 50 tiles away. So each emitter has
 * its own radius: full volume within one tile of the emitter, falling linearly to nothing at the radius,
 * by XZ distance from Link the way RS measures it in tiles. Out of range, the emitter does not ask at all.
 *
 * THE CAP (found on the first run, 2026-10-27). Left alone, every emitter asks for its sound every tick
 * and the env bank's 3 channels go by priority = distance^2 + (255 - importance)^2 * 76^2. NA_SE_EV_TORCH
 * has importance 0x10 against 0x30 for the water sounds, a gap worth about 9,000 units of distance: a
 * fountain across Lumbridge outranks a fire at Link's feet, and the fires never played at all. So only
 * the nearest N emitters in range keep their draw, which is what asks for the sound. The radius alone
 * does not make the cap redundant: where two radii overlap, the wall is back.
 *
 * FILE EMITTERS (2026-10-28) take none of that: they go to the mixer, which has its own voices. They share the
 * radius and gain, are capped separately at the nearest kMixerVoices, and are panned with the engine's own
 * Audio_ComputeSoundPanSigned on the same view-space position the engine would use (projectedPos), so the
 * two kinds sit in the same place left to right. What they do not get is the engine's distance curve on top
 * of the fade, so one gain is not quite the same loudness in both kinds: tune each by ear.
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

// Picks the nearest `max` emitters of one kind (file or not) that are in range.
void PickNearest(const std::vector<float>& dist, bool fileRows, int32_t max) {
    for (int32_t k = 0; k < max; k++) {
        int32_t best = -1;
        for (size_t i = 0; i < sEmitters.size(); i++) {
            if (IsFile(sEmitters[i]) == fileRows && dist[i] >= 0.0f && !sEmitters[i].active &&
                (best < 0 || dist[i] < dist[best])) {
                best = (int32_t)i;
            }
        }
        if (best < 0) {
            break;
        }
        sEmitters[best].active = true;
    }
}

// Tells the mixer what the active file emitters should play this tick: loops by gain and pan, one-shots by a
// trigger that moves when the emitter's countdown runs out.
void SubmitFiles(PlayState* play, const std::vector<bool>& wasActive) {
    std::vector<RsAmbienceVoice> voices;
    std::vector<std::string> picked(sEmitters.size());
    for (size_t i = 0; i < sEmitters.size(); i++) {
        Emitter& e = sEmitters[i];
        if (!IsFile(e) || !e.active || !e.filesOk) {
            continue;
        }
        const std::vector<std::string> ids = FileIds(e);
        if (ids.empty()) {
            continue;
        }
        const bool loop = e.maxTicks <= 0;
        if (!loop) {
            if (!wasActive[i]) {
                // Coming into range: the first play lands somewhere in the first interval, not at once.
                e.countdown = (int32_t)(Rand_ZeroOne() * e.minTicks);
            }
            if (--e.countdown <= 0) {
                e.trigger++;
                e.pick = (int32_t)(Rand_ZeroOne() * ids.size()) % (int32_t)ids.size();
                e.countdown = e.minTicks + (int32_t)(Rand_ZeroOne() * (e.maxTicks - e.minTicks));
            }
        }
        Vec3f pos = e.actor->world.pos;
        Vec3f view;
        f32 w;
        SkinMatrix_Vec3fMtxFMultXYZW(&play->viewProjectionMtxF, &pos, &view, &w);
        const float pan = (float)Audio_ComputeSoundPanSigned(view.x, view.z, 4) / 127.0f;
        picked[i] = loop ? ids[0] : ids[e.pick];
        RsAmbienceVoice v;
        v.key = (int32_t)i;
        v.id = picked[i].c_str();
        v.loop = loop;
        v.trigger = e.trigger;
        v.left = sVol[i] * cosf(pan * kQuarterTurn);
        v.right = sVol[i] * sinf(pan * kQuarterTurn);
        voices.push_back(v);
    }
    RsAmbienceMixer_Submit(voices.data(), (int32_t)voices.size());
}

void CapToNearest(PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == nullptr) {
        return;
    }
    std::vector<float> dist(sEmitters.size(), -1.0f);
    std::vector<bool> wasActive(sEmitters.size(), false);
    for (size_t i = 0; i < sEmitters.size(); i++) {
        Emitter& e = sEmitters[i];
        wasActive[i] = e.active;
        e.active = false;
        sVol[i] = 0.0f;
        if (IsLive(play, e.actor)) {
            const float d = Math_Vec3f_DistXZ(&e.actor->world.pos, &player->actor.world.pos);
            sVol[i] = e.gain * Fade(d, e.radius);
            if (sVol[i] > 0.0f) {
                dist[i] = d;
            }
        }
    }
    PickNearest(dist, false, MaxActive());
    PickNearest(dist, true, kMixerVoices);
    for (Emitter& e : sEmitters) {
        if (IsLive(play, e.actor)) {
            e.actor->draw = (e.active && !IsFile(e)) ? EmitterDraw : nullptr;
        }
    }
    SubmitFiles(play, wasActive);
}

// Spawning waits for Link's first tick rather than OnSceneSpawnActors, which fires only when the
// scene has setup actors, and needs the collision the floor raycast reads.
void OnPlayerUpdate() {
    if (gPlayState == nullptr || sEmitters.empty()) {
        sSpawnPending = false;
        RsAmbienceMixer_Submit(nullptr, 0);
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
    COND_HOOK(OnSceneInit, true, [](int16_t sceneNum) {
        // The previous scene's actors went with it; its emitters go too.
        BuildEmitters(sceneNum);
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

bool MixerHeard(const RsAmbienceMixerStats& mixer, int32_t key) {
    for (int32_t i = 0; i < mixer.audibleKeyCount; i++) {
        if (mixer.audibleKeys[i] == key) {
            return true;
        }
    }
    return false;
}

int32_t Status(const std::string& op, std::vector<std::string>& lines) {
    PlayState* play = gPlayState;
    int32_t requested = 0, playing = 0;
    EnvCounts(&requested, &playing);
    const int32_t channels = gChannelsPerBank[gSfxChannelLayout][BANK_ENV];
    if (play == nullptr) {
        lines.push_back("op=" + op + " enabled=" + std::to_string(Enabled() ? 1 : 0) +
                        " scene=none here=0 emitters=0 spawned=0");
        return 0;
    }
    const RsAmbienceMixerStats mixer = RsAmbienceMixer_GetStats();
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "op=%s enabled=%d scene=0x%02X here=%d emitters=%d spawned=%d max=%d env_requested=%d "
                  "env_playing=%d env_channels=%d mixer_voices=%d mixer_audible=%d mixer_loaded=%d "
                  "mixer_batches=%llu",
                  op.c_str(), Enabled() ? 1 : 0, play->sceneNum, sEmitters.empty() ? 0 : 1, (int32_t)sEmitters.size(),
                  SpawnedCount(play), MaxActive(), requested, playing, channels, mixer.voices, mixer.audible,
                  mixer.loaded, (unsigned long long)mixer.batches);
    lines.push_back(buf);
    Player* player = GET_PLAYER(play);
    for (size_t i = 0; i < sEmitters.size(); i++) {
        const Emitter& e = sEmitters[i];
        const bool live = IsLive(play, e.actor);
        bool req = false, playingNow = false;
        Vec3f pos = e.pos;
        if (live) {
            pos = e.actor->world.pos;
            if (IsFile(e)) {
                // The mixer's view: submitted this tick, and heard in the last batch it mixed.
                req = e.active && e.filesOk;
                playingNow = MixerHeard(mixer, (int32_t)i);
            } else {
                EmitterState(e.actor, &req, &playingNow);
            }
        }
        const float dist = (live && player != nullptr) ? Math_Vec3f_DistXZ(&pos, &player->actor.world.pos) : -1.0f;
        const std::string sound = IsFile(e) ? "file:" + e.files : SoundName(e.sound);
        std::snprintf(buf, sizeof(buf),
                      "emitter=%s sound=%s from=%s pos=%.0f,%.0f,%.0f dist=%.0f spawned=%d active=%d requested=%d "
                      "playing=%d radius=%.1f gain=%.2f vol=%.2f",
                      e.name.c_str(), sound.c_str(), e.from, pos.x, (live || !e.raycast) ? pos.y : 0.0f, pos.z, dist,
                      live ? 1 : 0, (live && e.active) ? 1 : 0, req ? 1 : 0, playingNow ? 1 : 0, e.radius, e.gain,
                      (live && e.active) ? sVol[i] : 0.0f);
        std::string line = buf;
        if (IsFile(e)) {
            line += live ? (e.filesOk ? " files=ok" : " files=missing") : " files=unknown";
            line += " plays=" + std::to_string(e.trigger);
        }
        lines.push_back(line);
    }
    return 0;
}

int32_t Switch(bool on, std::vector<std::string>& lines) {
    CVarSetInteger(CVAR_RS_AMBIENCE, on ? 1 : 0);
    CVarSave();
    PlayState* play = gPlayState;
    if (play != nullptr) {
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
    if (n < 0 || n > kMaxMax) {
        lines.push_back("op=max result=error error=range max_allowed=" + std::to_string(kMaxMax));
        return 1;
    }
    CVarSetInteger(CVAR_RS_AMBIENCE_MAX, n);
    CVarSave();
    lines.push_back("op=max result=ok max=" + std::to_string(MaxActive()));
    return 0;
}

// `radius|gain <emitter|all> [<value>|default]`: one line per emitter of the scene it names, a read without a
// value. The value is checked before anything is written, so a refusal changes nothing.
int32_t Tune(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string& op = args[0];
    const bool isRadius = op == "radius";
    const float lo = isRadius ? kRadiusMin : 0.0f;
    const float hi = isRadius ? kRadiusMax : kGainMax;
    int32_t only = -1;
    if (args.size() >= 2 && args[1] != "all") {
        for (size_t i = 0; i < sEmitters.size(); i++) {
            if (args[1] == sEmitters[i].name) {
                only = (int32_t)i;
            }
        }
    }
    if (args.size() < 2 || (args[1] != "all" && only < 0) || sEmitters.empty()) {
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
    auto& tuned = isRadius ? sTunedRadius : sTunedGain;
    for (size_t i = 0; i < sEmitters.size(); i++) {
        if (only >= 0 && (int32_t)i != only) {
            continue;
        }
        Emitter& e = sEmitters[i];
        float& slot = isRadius ? e.radius : e.gain;
        if (write) {
            if (toDefault) {
                tuned.erase(e.name);
                slot = isRadius ? e.baseRadius : e.baseGain;
            } else {
                tuned[e.name] = value;
                slot = value;
            }
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "op=%s result=ok emitter=%s %s=%.2f", op.c_str(), e.name.c_str(), op.c_str(),
                      slot);
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
    "Area ambience (sturdy-bassoon#275, #276): status | on | off | max [<n>|default] | "
    "radius|gain <emitter|all> [<value>|default]. The current scene's sound emitters - the grid "
    "tool's exported placements, plus Lumbridge's built-in rows in the F2P overworld - playing OoT "
    "sounds through the engine and external files (rs/sfx/, npm run fetch:rs-sfx) through the "
    "ambience mixer. status lists each emitter with its distance, radius, gain and whether its "
    "sound is playing, plus the env sound budget in use anywhere and the mixer's voices; on|off "
    "switch them live for an A/B by ear; max is how many of the nearest OoT-sound emitters in "
    "range may sound at once (0..3, default 2; file emitters have their own 8); radius (RS tiles) "
    "and gain (0..1) tune one emitter or all of them by ear, unsaved.",
    { { "status|on|off|max|radius|gain", Ship::ArgumentType::TEXT, true },
      { "n|emitter", Ship::ArgumentType::TEXT, true },
      { "value", Ship::ArgumentType::TEXT, true } });

} // namespace
