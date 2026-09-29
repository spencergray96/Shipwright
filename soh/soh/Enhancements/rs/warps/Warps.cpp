#include "Warps.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/stairs/Stairs.h"
#include "soh/ShipInit.hpp"
#include <spdlog/spdlog.h>

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

// See Warps.h for what this file owns: the scan, the detector and the latch.

namespace {

// --- tuning -------------------------------------------------------------------------------------

// A deliberate push on the stick, of the 60 Player reads - #151's threshold, for the same job:
// under it the stick is "let go", which lifts a latch.
constexpr float kStickHeld = 15.0f;

// How far beyond his landing Link must be from a latched tile to be clear of it. Measured from the
// tile's centre, where the landing is one tile-width out, so being put down on the landing never
// counts - #151's "10 beyond the landing".
constexpr float kClearBeyond = 10.0f;

// How near a tile's floor Link's feet must be to count as on it - and, for a latched tile, how far
// above or below it he must be to have left its storey. Under half a storey (a storey is 80 at 40 a
// tile), #147's talk gate: a tile a storey below him, seen through a hole or from a deck's edge, is
// not one he is on, even while it is the floor under his centre.
constexpr float kOnTileY = 30.0f;

// A PUSH tile fires only while Link moves within this of "into" it - the direction opposite its
// landing: 45 degrees either side. And moving at all: standing on it is not pushing.
constexpr s16 kPushCone = 0x2000;
constexpr float kPushSpeed = 1.0f;

// A tile's polygons are its floor, and a warp tile is flat: normal.y over 0.7 (the engine stores the
// normal as s16 over +-0x7FFF), and every vertex within a unit of the same height.
constexpr s16 kFloorNormalY = static_cast<s16>(0.7f * 0x7FFF);
constexpr float kFlatY = 1.0f;

// Where a landing's floor may be relative to its tile's: a step, not a storey.
constexpr float kLandingFloorY = 10.0f;

// The height of the line tested from a tile to its landing for a wall in between: Player's own wall
// probe sits at feet + 26 (ladder ADR).
constexpr float kWallProbeY = 26.0f;

// Why a tile did not fire, in the order they are asked.
enum class Why { Ok, Inert, Moving, Busy, Airborne, Disarmed, LandingOnly, Aim, Count };

const char* const kWhyNames[] = { "ok", "inert", "moving", "busy", "airborne", "disarmed", "landing_only", "aim" };
static_assert(sizeof(kWhyNames) / sizeof(kWhyNames[0]) == static_cast<size_t>(Why::Count), "one name per Why");

uint32_t WhyBit(Why why) {
    return 1u << static_cast<uint32_t>(why);
}

const float kDirX[] = { 0.0f, 1.0f, 0.0f, -1.0f };
const float kDirZ[] = { 1.0f, 0.0f, -1.0f, 0.0f };

// --- registry -----------------------------------------------------------------------------------

std::vector<const RsWarpSceneDef*> sScenes;

bool IsToken(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    for (const char* c = name; *c != '\0'; c++) {
        if (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r' || *c == '%' || *c == '#' || *c == '"') {
            return false;
        }
    }
    return true;
}

const RsWarpTileDef* FindTileDef(const RsWarpSceneDef& def, int32_t id) {
    for (int32_t i = 0; i < def.tileCount; i++) {
        if (def.tiles[i].id == id) {
            return &def.tiles[i];
        }
    }
    return nullptr;
}

bool IsHere(const RsWarpDest& dest) {
    return dest.entrance == RS_WARP_HERE;
}

bool IsEntrance(int32_t entrance) {
    return entrance >= 0 && entrance < ENTR_MAX;
}

// The scene an entrance loads. Only for IsEntrance.
int16_t EntranceScene(int32_t entrance) {
    return gEntranceTable[entrance].scene;
}

// The row a destination in another scene names in THAT scene's table, or null: no table for that
// scene, or no such tile in it. Only for a destination that is not IsHere, with a valid entrance.
const RsWarpTileDef* OtherSceneRow(const RsWarpDest& dest) {
    const RsWarpSceneDef* otherScene = RsWarp_GetSceneDef(EntranceScene(dest.entrance));
    return otherScene != nullptr ? FindTileDef(*otherScene, dest.tile) : nullptr;
}

// "2" for a tile here, "1@0x63E" for tile 1 through entrance 0x63E - for console lines.
std::string DestToken(const RsWarpDest& dest) {
    if (IsHere(dest)) {
        return std::to_string(dest.tile);
    }
    char token[32];
    std::snprintf(token, sizeof(token), "%d@0x%X", dest.tile, dest.entrance);
    return token;
}

// --- per-scene state ----------------------------------------------------------------------------

struct TileState {
    bool present = false;              // found in the collision
    const RsWarpTileDef* def = nullptr; // its row in the table
    const char* bad = nullptr;         // why it is inert for this visit
    int32_t polys = 0;
    int32_t dir = -1;
    bool mixedDir = false;
    bool notFloor = false;
    float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f, minY = 0.0f, maxY = 0.0f;
    float area = 0.0f;
    Vec3f centre = {};
    float width = 0.0f;
    Vec3f landing = {};
    s16 yaw = 0;
    bool mustLeave = false;
    bool latched = false;
    int32_t fires = 0;
    std::array<int32_t, RS_WARP_MAX_DESTS> picks = {};
};

struct State {
    bool scanned = false;
    int16_t sceneNum = -1;
    const RsWarpSceneDef* def = nullptr;
    std::array<TileState, RS_WARP_TILE_ID_MAX + 1> tiles = {};
    int32_t present = 0;
    int32_t ok = 0;
    int32_t bad = 0;
    bool arrived = false;     // the first grounded tick in the scene has happened
    int32_t onTile = 0;       // the tile under him last tick: a CONTACT lasts while this holds
    uint32_t refusedMask = 0; // the reasons already reported this contact, by WhyBit
    int32_t pendingLatch = 0;
    std::string last;
};

State sState;

void Marker(const char* fmt, ...) {
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    AgentTest_WriteMarker(line);
}

// A detector event, also kept for `warps status` - without its `rs_warp ` prefix, so the answer never
// reads as an event to a grep for `rs_warp tile=<n> event=`.
void Event(const char* fmt, ...) {
    static const char kPrefix[] = "rs_warp ";
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    AgentTest_WriteMarker(line);
    sState.last = std::strncmp(line, kPrefix, sizeof(kPrefix) - 1) == 0 ? line + sizeof(kPrefix) - 1 : line;
}

int32_t TileIdOf(const CollisionHeader* col, const CollisionPoly* poly) {
    const SurfaceType& type = col->surfaceTypeList[poly->type];
    return RS_WARP_TILE_ID(type.data[0], type.data[1]);
}

int32_t Degrees(s16 angle) {
    return static_cast<int32_t>(static_cast<float>(angle) * (360.0f / 65536.0f));
}

// --- the scan -----------------------------------------------------------------------------------

// Grows the tile's bounds by one vertex. The first polygon seeds them (Scan), so this only widens.
void Extend(TileState& t, const Vec3s& v) {
    const float x = v.x;
    const float y = v.y;
    const float z = v.z;
    t.minX = std::fmin(t.minX, x);
    t.maxX = std::fmax(t.maxX, x);
    t.minY = std::fmin(t.minY, y);
    t.maxY = std::fmax(t.maxY, y);
    t.minZ = std::fmin(t.minZ, z);
    t.maxZ = std::fmax(t.maxZ, z);
}

// Works out where the tile is and where its landing is, and checks both. Leaves `bad` set on the
// first thing wrong.
void Measure(PlayState* play, TileState& t) {
    t.centre = { (t.minX + t.maxX) * 0.5f, t.maxY, (t.minZ + t.maxZ) * 0.5f };
    if (t.notFloor) {
        t.bad = "not_floor";
        return;
    }
    if (t.mixedDir) {
        t.bad = "mixed_dir";
        return;
    }
    // One landing direction for the whole tile, read off the first polygon.
    t.width = (t.dir == RS_WARP_DIR_POS_Z || t.dir == RS_WARP_DIR_NEG_Z) ? (t.maxZ - t.minZ) : (t.maxX - t.minX);
    t.landing = { t.centre.x + kDirX[t.dir] * t.width, t.centre.y, t.centre.z + kDirZ[t.dir] * t.width };
    t.yaw = static_cast<s16>(t.dir << 14);
    if (t.maxY - t.minY > kFlatY) {
        t.bad = "not_flat";
        return;
    }
    // Two regions with one id - two tiles painted with the same number - cover less than their joint
    // bounding box, so the area says so without walking the mesh.
    if (t.area < 0.98f * (t.maxX - t.minX) * (t.maxZ - t.minZ)) {
        t.bad = "split";
        return;
    }
    // Something to stand on at the landing, on the tile's own storey, that is not itself a warp tile.
    // From one tile-width up: a storey is two, so this is under the floor above at any grid scale.
    Vec3f probe = { t.landing.x, t.landing.y + t.width, t.landing.z };
    CollisionPoly* floor = nullptr;
    s32 floorBg = BGCHECK_SCENE;
    const f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &floor, &floorBg, &probe);
    if (floor == nullptr || std::fabs(floorY - t.centre.y) > kLandingFloorY) {
        t.bad = "no_landing";
        return;
    }
    if (floorBg == BGCHECK_SCENE && TileIdOf(play->colCtx.colHeader, floor) != 0) {
        t.bad = "landing_on_tile";
        return;
    }
    // And no wall between the tile and its landing: an edge wall on the way-out edge would put Link
    // down on the far side of it.
    Vec3f from = { t.centre.x, t.centre.y + kWallProbeY, t.centre.z };
    Vec3f to = { t.landing.x, t.landing.y + kWallProbeY, t.landing.z };
    Vec3f hit = {};
    CollisionPoly* wall = nullptr;
    s32 wallBg = BGCHECK_SCENE;
    if (BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &wall, true, false, false, false, &wallBg)) {
        t.bad = "landing_blocked";
        return;
    }
    t.landing.y = floorY;
}

void Scan(PlayState* play) {
    sState = State();
    sState.scanned = true;
    sState.sceneNum = play->sceneNum;
    sState.def = RsWarp_GetSceneDef(play->sceneNum);

    // A scene with no route table has no warp tiles, whatever its collision says. The engine never
    // reads these bits, but nothing proves every vanilla scene's data leaves them at zero - so vanilla
    // scenes, and any other scene nobody routed, are never looked at.
    if (sState.def == nullptr) {
        return;
    }
    const CollisionHeader* col = play->colCtx.colHeader;
    if (col != nullptr && col->polyList != nullptr && col->surfaceTypeList != nullptr && col->vtxList != nullptr) {
        for (u32 i = 0; i < col->numPolygons; i++) {
            const CollisionPoly& poly = col->polyList[i];
            const int32_t id = TileIdOf(col, &poly);
            if (id == 0) {
                continue;
            }
            TileState& t = sState.tiles[id];
            const int32_t dir = RS_WARP_TILE_DIR(col->surfaceTypeList[poly.type].data[0]);
            const Vec3s& a = col->vtxList[COLPOLY_VTX_INDEX(poly.flags_vIA)];
            const Vec3s& b = col->vtxList[COLPOLY_VTX_INDEX(poly.flags_vIB)];
            const Vec3s& c = col->vtxList[COLPOLY_VTX_INDEX(poly.vIC)];
            if (t.polys == 0) {
                t.dir = dir;
                t.minX = t.maxX = a.x;
                t.minY = t.maxY = a.y;
                t.minZ = t.maxZ = a.z;
            } else if (t.dir != dir) {
                t.mixedDir = true;
            }
            Extend(t, a);
            Extend(t, b);
            Extend(t, c);
            if (poly.normal.y < kFloorNormalY) {
                t.notFloor = true;
            }
            const float cross = static_cast<float>(b.x - a.x) * static_cast<float>(c.z - a.z) -
                                static_cast<float>(b.z - a.z) * static_cast<float>(c.x - a.x);
            t.area += 0.5f * std::fabs(cross);
            t.polys++;
            t.present = true;
        }
    }

    for (int32_t id = 1; id <= RS_WARP_TILE_ID_MAX; id++) {
        TileState& t = sState.tiles[id];
        if (t.present) {
            sState.present++;
            Measure(play, t);
        }
    }

    // The table against the collision. A tile only the table knows has nowhere to be; a tile only the
    // collision knows has nothing to do.
    if (sState.def != nullptr) {
        for (int32_t i = 0; i < sState.def->tileCount; i++) {
            const RsWarpTileDef& row = sState.def->tiles[i];
            TileState& t = sState.tiles[row.id];
            t.def = &row;
            if (!t.present) {
                t.bad = "missing";
            } else if (t.bad == nullptr && row.room >= play->numRooms) {
                t.bad = "bad_room";
            }
        }
    }
    for (int32_t id = 1; id <= RS_WARP_TILE_ID_MAX; id++) {
        TileState& t = sState.tiles[id];
        if (t.present && t.def == nullptr && t.bad == nullptr) {
            t.bad = "unrouted";
        }
    }
    // A tile that sends Link to ANOTHER scene (#148) needs that scene's table to have the tile - else
    // there is no room to load him into and no way back. Whether its collision has the tile too is
    // only knowable there, once it loads: `arrival_failed`, and the scene's spawn instead.
    for (int32_t id = 1; id <= RS_WARP_TILE_ID_MAX; id++) {
        TileState& t = sState.tiles[id];
        if (t.bad != nullptr || t.def == nullptr) {
            continue;
        }
        for (int32_t k = 0; k < t.def->destCount; k++) {
            if (!IsHere(t.def->dests[k]) && OtherSceneRow(t.def->dests[k]) == nullptr) {
                t.bad = "dest_unrouted";
                break;
            }
        }
    }
    // A tile that sends Link to a broken one is broken too - otherwise it would fire and have nowhere
    // to put him. Repeated until nothing changes, so a chain of them all goes inert.
    for (bool changed = true; changed;) {
        changed = false;
        for (int32_t id = 1; id <= RS_WARP_TILE_ID_MAX; id++) {
            TileState& t = sState.tiles[id];
            if (t.bad != nullptr || t.def == nullptr) {
                continue;
            }
            for (int32_t k = 0; k < t.def->destCount; k++) {
                if (IsHere(t.def->dests[k]) && sState.tiles[t.def->dests[k].tile].bad != nullptr) {
                    t.bad = "bad_dest";
                    changed = true;
                    break;
                }
            }
        }
    }

    if (sState.present == 0 && sState.def == nullptr) {
        return; // nothing here, and nothing to say
    }
    for (int32_t id = 1; id <= RS_WARP_TILE_ID_MAX; id++) {
        const TileState& t = sState.tiles[id];
        if (!t.present && t.def == nullptr) {
            continue;
        }
        if (t.bad != nullptr) {
            sState.bad++;
            // Loud, never fatal: a broken tile is scene data that disagrees with its table, which a
            // run and a human should both see - and the rest of the scene's tiles still work.
            SPDLOG_ERROR("RsWarps: scene 0x{:X} tile {} is inert: {}", play->sceneNum, id, t.bad);
            Marker("rs_warp tile=%d event=bad_tile reason=%s", id, t.bad);
        } else {
            sState.ok++;
        }
    }
    Marker("rs_warp event=loaded scene=0x%X table=%s tiles=%d ok=%d bad=%d", play->sceneNum,
           sState.def != nullptr ? sState.def->name : "none", sState.ok + sState.bad, sState.ok, sState.bad);
}

// --- the detector -------------------------------------------------------------------------------

int32_t TileUnder(PlayState* play, Player* player) {
    const CollisionPoly* poly = player->actor.floorPoly;
    if (poly == nullptr || player->actor.floorBgId != BGCHECK_SCENE || play->colCtx.colHeader == nullptr) {
        return 0;
    }
    // The floor under his centre, but only near his feet: jumping off a deck, the ground a storey
    // below is under him too, and he is not on it.
    if (std::fabs(player->actor.world.pos.y - player->actor.floorHeight) > kOnTileY) {
        return 0;
    }
    return TileIdOf(play->colCtx.colHeader, poly);
}

float XZDist(const Vec3f& a, const Vec3f& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

void Latch(int32_t id, const char* reason, float stick) {
    TileState& t = sState.tiles[id];
    if (t.latched) {
        return;
    }
    t.latched = true;
    if (sState.onTile == id) {
        sState.refusedMask &= ~WhyBit(Why::Disarmed); // refused by the latch now: a new thing to say
    }
    Marker("rs_warp tile=%d event=latch reason=%s stick=%d", id, reason, static_cast<int>(stick));
}

void Rearm(int32_t id, const char* reason) {
    TileState& t = sState.tiles[id];
    if (!t.latched) {
        return;
    }
    t.latched = false;
    // Let go while standing ON it - he ran back onto the tile under the held stick, then stopped - is
    // not an arming: a STEP tile would fire the moment the stick came to rest, which is the very
    // return the latch exists to stop. He has to step off it first, as on arriving anywhere.
    // (Found in the #154 run: the latch lifted `released` with Link still on tile 1.)
    if (sState.onTile == id) {
        t.mustLeave = true;
        sState.refusedMask &= ~WhyBit(Why::Disarmed); // still `disarmed`, now for this reason
    }
    Marker("rs_warp tile=%d event=rearm reason=%s on_tile=%d", id, reason, sState.onTile == id ? 1 : 0);
}

// The direction Link is moving, against "into" the tile: 0 is dead into it. Player keeps his heading
// in world.rot.y and his speed along it in linearVelocity (negative for a Z-targeted back-step).
s16 AimOff(const TileState& t, Player* player) {
    const s16 into = static_cast<s16>(t.yaw + 0x8000);
    return static_cast<s16>(player->actor.world.rot.y - into);
}

Why Evaluate(PlayState* play, Player* player, const TileState& t) {
    if (t.bad != nullptr) {
        return Why::Inert;
    }
    // Written by the controller's own OnPlayerUpdate hook, and hooks on one event run in no set order
    // (MODDING_HOOKS.md) - so at a move's end this can read it a tick late. Harmless: a move begins
    // inside Fire, on this hook's own tick, and a move ends with him on a landing, never on a tile.
    if (RsStair_IsMoving()) {
        return Why::Moving;
    }
    if (Message_GetState(&play->msgCtx) != TEXT_STATE_NONE || Player_InCsMode(play)) {
        return Why::Busy;
    }
    if (!(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        return Why::Airborne;
    }
    if (t.latched || t.mustLeave) {
        return Why::Disarmed;
    }
    if (t.def->destCount == 0) {
        return Why::LandingOnly;
    }
    if (t.def->entry == RS_WARP_ENTRY_PUSH &&
        (player->linearVelocity < kPushSpeed || std::abs(static_cast<int32_t>(AimOff(t, player))) > kPushCone)) {
        return Why::Aim;
    }
    return Why::Ok;
}

void Fire(Player* player, int32_t id) {
    TileState& t = sState.tiles[id];
    const int32_t choices = t.def->destCount;
    // Picked now, every time - not shuffled once when the scene loads. The game's RNG is seeded from
    // the clock in Play_Init (z_play.c), so no visit repeats another's sequence either.
    int32_t pick = 0;
    if (choices > 1) {
        pick = static_cast<int32_t>(Rand_ZeroOne() * static_cast<float>(choices));
        pick = pick >= choices ? choices - 1 : pick;
    }
    const RsWarpDest& to = t.def->dests[pick];

    // To another scene, the line says where: an in-place line is unchanged.
    char sceneFields[64] = "";
    if (!IsHere(to)) {
        std::snprintf(sceneFields, sizeof(sceneFields), " entrance=0x%X scene_to=0x%X", to.entrance,
                      EntranceScene(to.entrance));
    }
    Event("rs_warp tile=%d event=fired to=%d pick=%d choices=%d entry=%s pos=%.1f,%.1f,%.1f floor_y=%.1f "
          "move_yaw=%d aim=%d speed=%.1f%s",
          id, to.tile, pick, choices, RsWarp_EntryName(t.def->entry), player->actor.world.pos.x,
          player->actor.world.pos.y, player->actor.world.pos.z, player->actor.floorHeight, player->actor.world.rot.y,
          Degrees(AimOff(t, player)), player->linearVelocity, sceneFields);

    RsWarpMoveDest dest = {};
    dest.fromTile = id;
    dest.toTile = to.tile;
    if (IsHere(to)) {
        const TileState& d = sState.tiles[to.tile];
        dest.x = d.landing.x;
        dest.y = d.landing.y;
        dest.z = d.landing.z;
        dest.yaw = d.yaw;
        dest.room = d.def->room;
        dest.entrance = RS_WARP_HERE;
    } else {
        // The landing is in the other scene's collision; its scan works it out when it loads. The
        // room is its table's - the scan made sure the row is there (`dest_unrouted`).
        const RsWarpTileDef* row = OtherSceneRow(to);
        dest.room = row != nullptr ? row->room : 0;
        dest.entrance = to.entrance;
    }
    // Disarmed until he steps off it, whatever the controller says: after a move he is frozen on it
    // for the fade, and after a refused one (`move_refused`) he is still standing on it - which must
    // not fire again next tick. The scan's checks leave the controller nothing to refuse today.
    t.mustLeave = true;
    if (RsStair_BeginWarpMove(&dest, "warp") == RS_STAIR_OK) {
        t.fires++;
        t.picks[pick]++;
        // In place, the latch is owed here. To another scene this state is gone by the time he lands;
        // the arrival there owes it instead (OnSceneInitWarps).
        if (IsHere(to)) {
            sState.pendingLatch = to.tile;
        }
    }
    // So the `moving` refusal that follows on this same tile is reported.
    sState.refusedMask = 0;
}

void OnPlayerUpdateWarps() {
    PlayState* play = gPlayState;
    if (play == nullptr || GET_PLAYER(play) == nullptr) {
        return;
    }
    if (!sState.scanned || sState.sceneNum != play->sceneNum) {
        Scan(play);
    }
    if (sState.present == 0) {
        return;
    }
    Player* player = GET_PLAYER(play);
    const int32_t tile = TileUnder(play, player);

    const bool grounded = (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) != 0;
    if (tile != sState.onTile) {
        sState.onTile = tile;
        sState.refusedMask = 0; // a new contact
    }
    // Arriving in the scene on a tile - a spawn, a void-out - is not stepping onto it. Counted on his
    // first GROUNDED tick, not his first tick: he may spawn in the air, or the first update may not
    // have read the floor yet, and the tile he lands on is the one he arrived on.
    if (!sState.arrived && grounded) {
        sState.arrived = true;
        if (tile != 0) {
            sState.tiles[tile].mustLeave = true;
        }
    }
    // STEPPING OFF a tile is standing on some other floor - GROUNDED on it, not just a different
    // polygon under his centre. At a ledge his centre can cross the edge for a tick with nothing
    // under him but the ground a storey below, then settle back: counting that as stepping off
    // re-armed the covered hole at the deck's edge in the #154 run, and it fired under a stick that
    // had only just been let go.
    if (grounded && sState.arrived) {
        for (int32_t id = 1; id <= RS_WARP_TILE_ID_MAX; id++) {
            if (id != tile) {
                sState.tiles[id].mustLeave = false;
            }
        }
    }

    // The stick exactly as Player reads it, magnitude only: whether it is held at all.
    f32 stick = 0.0f;
    s16 stickAngle = 0;
    func_80077D10(&stick, &stickAngle, &play->state.input[0]);

    // The latch the last warp move owes, once it is over: only if he is still pushing.
    if (sState.pendingLatch != 0 && !RsStair_IsMoving()) {
        if (stick >= kStickHeld) {
            Latch(sState.pendingLatch, "move", stick);
        }
        sState.pendingLatch = 0;
    }
    for (int32_t id = 1; id <= RS_WARP_TILE_ID_MAX; id++) {
        TileState& t = sState.tiles[id];
        if (!t.latched) {
            continue;
        }
        if (stick < kStickHeld) {
            Rearm(id, "released");
        } else if (XZDist(player->actor.world.pos, t.centre) > t.width + kClearBeyond) {
            Rearm(id, "clear");
        } else if (std::fabs(player->actor.world.pos.y - t.centre.y) > kOnTileY) {
            Rearm(id, "left");
        }
    }

    if (tile == 0) {
        return;
    }
    TileState& t = sState.tiles[tile];
    const Why why = Evaluate(play, player, t);
    if (why == Why::Ok) {
        Fire(player, tile);
        return;
    }
    const int32_t whyIndex = static_cast<int32_t>(why);
    if (sState.refusedMask & WhyBit(why)) {
        return; // said already, this contact
    }
    sState.refusedMask |= WhyBit(why);
    // THE marker that proves a negative was challenged: Link was ON the tile, and this is why it did
    // not fire - as opposed to him never reaching it.
    Event("rs_warp tile=%d event=refused reason=%s entry=%s pos=%.1f,%.1f,%.1f move_yaw=%d aim=%d speed=%.1f stick=%d "
          "latched=%d must_leave=%d",
          tile, kWhyNames[whyIndex], t.def != nullptr ? RsWarp_EntryName(t.def->entry) : "none",
          player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z, player->actor.world.rot.y,
          t.bad == nullptr ? Degrees(AimOff(t, player)) : 0, player->linearVelocity, static_cast<int>(stick),
          t.latched ? 1 : 0, t.mustLeave ? 1 : 0);
}

// A scene change forgets everything: the next Player update scans the new scene. The move controller
// aborts its own move (Stairs.cpp).
//
// Unless a step warp is bringing Link here from another scene (#148). Then the scan runs NOW: the
// scene's init has allocated its collision, and Player_Init has not run yet - so the destination
// tile's landing can go into the respawn slot Player_Init is about to stand him on. And the tile he
// arrives beside owes the latch, as after a move in place: latched if the stick is still held when
// the move ends, so a stick held through the load does not walk him straight back through it.
void OnSceneInitWarps(int16_t sceneNum) {
    sState = State();
    PlayState* play = gPlayState;
    int32_t tile = 0;
    int32_t fromTile = 0;
    if (play == nullptr || !RsStair_SceneArrival(sceneNum, &tile, &fromTile)) {
        return;
    }
    Scan(play);
    const TileState* t = tile >= 1 && tile <= RS_WARP_TILE_ID_MAX ? &sState.tiles[tile] : nullptr;
    if (t != nullptr && t->present && t->bad == nullptr) {
        // A tile that is not bad has a row, and the scan checked its room against this scene's.
        RsStair_PlaceSceneArrival(t->landing.x, t->landing.y, t->landing.z, t->yaw, t->def->room, 1);
        sState.pendingLatch = tile;
        return;
    }
    // Routed there but broken in this scene's collision - an authoring mistake that only the load
    // could show. Loud, and the scene's own spawn rather than wherever the slot pointed.
    const char* why = t == nullptr ? "bad_id" : (t->bad != nullptr ? t->bad : "missing");
    SPDLOG_ERROR("RsWarps: arriving in scene 0x{:X} at tile {}: {}; using the spawn", sceneNum, tile, why);
    Marker("rs_warp tile=%d event=arrival_failed to=%d scene=0x%X reason=%s", fromTile, tile, sceneNum, why);
    if (play->linkActorEntry != nullptr) {
        const ActorEntry* spawn = play->linkActorEntry;
        const int32_t room = play->setupEntranceList != nullptr ? play->setupEntranceList[play->curSpawn].room : 0;
        RsStair_PlaceSceneArrival(spawn->pos.x, spawn->pos.y, spawn->pos.z, spawn->rot.y, room, 0);
    }
}

void RegisterWarpsHooks() {
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateWarps);
    COND_HOOK(OnSceneInit, true, OnSceneInitWarps);
}

RegisterShipInitFunc warpsHooksInitFunc(RegisterWarpsHooks);

} // namespace

// --- registry -----------------------------------------------------------------------------------

extern "C" const char* RsWarp_ProblemName(int32_t problem) {
    switch (problem) {
        case RS_WARP_PROBLEM_NONE:
            return "none";
        case RS_WARP_PROBLEM_NULL_DEF:
            return "null_def";
        case RS_WARP_PROBLEM_BAD_NAME:
            return "bad_name";
        case RS_WARP_PROBLEM_NO_TILES:
            return "no_tiles";
        case RS_WARP_PROBLEM_BAD_ID:
            return "bad_id";
        case RS_WARP_PROBLEM_DUPLICATE:
            return "duplicate";
        case RS_WARP_PROBLEM_BAD_ENTRY:
            return "bad_entry";
        case RS_WARP_PROBLEM_BAD_ROOM:
            return "bad_room";
        case RS_WARP_PROBLEM_DEST_COUNT:
            return "dest_count";
        case RS_WARP_PROBLEM_BAD_DEST:
            return "bad_dest";
        case RS_WARP_PROBLEM_SELF_DEST:
            return "self_dest";
        case RS_WARP_PROBLEM_SCENE_TAKEN:
            return "scene_taken";
        case RS_WARP_PROBLEM_BAD_ENTRANCE:
            return "bad_entrance";
        case RS_WARP_PROBLEM_ENTRANCE_HERE:
            return "entrance_here";
        default:
            return "unknown";
    }
}

extern "C" int32_t RsWarp_SceneDefProblem(const RsWarpSceneDef* def, int32_t* where) {
    int32_t unused = -1;
    int32_t* at = where != nullptr ? where : &unused;
    *at = -1;
    if (def == nullptr) {
        return RS_WARP_PROBLEM_NULL_DEF;
    }
    if (!IsToken(def->name)) {
        return RS_WARP_PROBLEM_BAD_NAME;
    }
    if (def->tileCount < 1 || def->tiles == nullptr) {
        return RS_WARP_PROBLEM_NO_TILES;
    }
    for (int32_t i = 0; i < def->tileCount; i++) {
        const RsWarpTileDef& tile = def->tiles[i];
        *at = i;
        if (tile.id < 1 || tile.id > RS_WARP_TILE_ID_MAX) {
            return RS_WARP_PROBLEM_BAD_ID;
        }
        for (int32_t j = 0; j < i; j++) {
            if (def->tiles[j].id == tile.id) {
                return RS_WARP_PROBLEM_DUPLICATE;
            }
        }
        if (tile.entry < 0 || tile.entry >= RS_WARP_ENTRY_COUNT) {
            return RS_WARP_PROBLEM_BAD_ENTRY;
        }
        if (tile.room < 0 || tile.room > 255) {
            return RS_WARP_PROBLEM_BAD_ROOM;
        }
        if (tile.destCount < 0 || tile.destCount > RS_WARP_MAX_DESTS ||
            (tile.destCount > 0 && tile.dests == nullptr)) {
            return RS_WARP_PROBLEM_DEST_COUNT;
        }
    }
    // Destinations last: they name other rows, which are only known good now.
    for (int32_t i = 0; i < def->tileCount; i++) {
        const RsWarpTileDef& tile = def->tiles[i];
        *at = i;
        for (int32_t k = 0; k < tile.destCount; k++) {
            const RsWarpDest& dest = tile.dests[k];
            if (IsHere(dest)) {
                if (dest.tile == tile.id) {
                    return RS_WARP_PROBLEM_SELF_DEST;
                }
                if (FindTileDef(*def, dest.tile) == nullptr) {
                    return RS_WARP_PROBLEM_BAD_DEST;
                }
                continue;
            }
            // Another scene's tile. Only its shape is checkable here: that scene's table may not be
            // registered yet, so whether it has the tile is the scan's `dest_unrouted`.
            if (!IsEntrance(dest.entrance)) {
                return RS_WARP_PROBLEM_BAD_ENTRANCE;
            }
            if (EntranceScene(dest.entrance) == def->sceneId) {
                return RS_WARP_PROBLEM_ENTRANCE_HERE;
            }
            if (dest.tile < 1 || dest.tile > RS_WARP_TILE_ID_MAX) {
                return RS_WARP_PROBLEM_BAD_DEST;
            }
        }
    }
    *at = -1;
    const RsWarpSceneDef* existing = RsWarp_GetSceneDef(def->sceneId);
    if (existing != nullptr && existing != def) {
        return RS_WARP_PROBLEM_SCENE_TAKEN;
    }
    return RS_WARP_PROBLEM_NONE;
}

extern "C" int32_t RsWarp_RegisterScene(const RsWarpSceneDef* def) {
    int32_t where = -1;
    const int32_t problem = RsWarp_SceneDefProblem(def, &where);
    if (problem != RS_WARP_PROBLEM_NONE) {
        // BUG CLASS, as RsStair_Register is: the kind and the row, never the prose.
        char scene[16];
        if (def != nullptr) {
            std::snprintf(scene, sizeof(scene), "0x%X", def->sceneId);
        } else {
            std::snprintf(scene, sizeof(scene), "none");
        }
        char line[160];
        std::snprintf(line, sizeof(line), "rs_warp event=refused problem=%s scene=%s row=%d",
                      RsWarp_ProblemName(problem), scene, where);
        AgentTest_WriteMarker(line);
        SPDLOG_ERROR("RsWarps: register scene 0x{:X}: {} at row {}", def != nullptr ? def->sceneId : -1,
                     RsWarp_ProblemName(problem), where);
        assert(false && "step warp table failed validation");
        return problem;
    }
    if (RsWarp_GetSceneDef(def->sceneId) == def) {
        return 0; // the same pointer again: a ShipInit re-run
    }
    sScenes.push_back(def);
    return 0;
}

extern "C" const RsWarpSceneDef* RsWarp_GetSceneDef(int32_t sceneId) {
    for (const RsWarpSceneDef* def : sScenes) {
        if (def->sceneId == sceneId) {
            return def;
        }
    }
    return nullptr;
}

// --- C++ surface --------------------------------------------------------------------------------

std::vector<const RsWarpSceneDef*> RsWarp_ListScenes() {
    return sScenes;
}

int32_t RsWarp_TileUnderPlayer() {
    if (gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr) {
        return 0;
    }
    return TileUnder(gPlayState, GET_PLAYER(gPlayState));
}

RsWarpSceneReport RsWarp_Report() {
    RsWarpSceneReport report;
    report.scanned = sState.scanned;
    report.sceneNum = sState.sceneNum;
    report.def = sState.def;
    report.ok = sState.ok;
    report.bad = sState.bad;
    report.onTile = sState.onTile;
    report.pendingLatch = sState.pendingLatch;
    report.last = sState.last;
    for (int32_t id = 1; id <= RS_WARP_TILE_ID_MAX; id++) {
        const TileState& t = sState.tiles[id];
        if (!t.present && t.def == nullptr) {
            continue;
        }
        RsWarpTileReport r;
        r.id = id;
        r.present = t.present;
        r.routed = t.def != nullptr;
        r.bad = t.bad;
        r.entry = t.def != nullptr ? t.def->entry : -1;
        r.room = t.def != nullptr ? t.def->room : -1;
        if (t.def == nullptr || t.def->destCount == 0) {
            r.dests = "-";
        } else {
            for (int32_t k = 0; k < t.def->destCount; k++) {
                const std::string token = DestToken(t.def->dests[k]);
                r.dests += (k > 0 ? "," : "") + token;
                r.picks += (k > 0 ? "," : "") + token + ":" + std::to_string(t.picks[k]);
            }
        }
        if (r.picks.empty()) {
            r.picks = "-";
        }
        r.cx = t.centre.x;
        r.cz = t.centre.z;
        r.y = t.centre.y;
        r.dir = t.dir;
        r.width = t.width;
        r.lx = t.landing.x;
        r.ly = t.landing.y;
        r.lz = t.landing.z;
        r.yaw = t.yaw;
        r.mustLeave = t.mustLeave;
        r.latched = t.latched;
        r.fires = t.fires;
        report.tiles.push_back(r);
    }
    return report;
}

const char* RsWarp_DirName(int32_t dir) {
    static const char* const kDirNames[] = { "+z", "+x", "-z", "-x" };
    return dir >= 0 && dir < 4 ? kDirNames[dir] : "none";
}

const char* RsWarp_EntryName(int32_t entry) {
    switch (entry) {
        case RS_WARP_ENTRY_STEP:
            return "step";
        case RS_WARP_ENTRY_PUSH:
            return "push";
        default:
            return "none";
    }
}
