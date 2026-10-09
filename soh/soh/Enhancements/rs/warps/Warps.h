#ifndef SOH_RS_WARPS_H
#define SOH_RS_WARPS_H

#include <stdint.h>
#include "WarpDef.h"

// ============================================================================================
//  STEP WARPS: step onto a tile, be moved in place  (sturdy-bassoon#154)
// ============================================================================================
//
// Vanilla detects a grotto exit by the floor polygon Link stands on. This takes that DETECTION and
// not the exit: a vanilla exit is a scene transition, which resets every actor, and the staircase
// ADR made storey moves in place so they carry on. So a warp tile is marked in its SurfaceType
// (WarpBits.h), and stepping on it arms the staircase's storey-move controller
// (RsStair_BeginWarpMove) with a landing worked out from the destination tile's own collision.
//
// Three jobs, in the order a player meets them:
//
//   1. THE SCAN. The first Player update in a scene (or its OnSceneInit, when a step warp from another
//      scene is arriving - below) reads every static collision polygon once, finds
//      the warp tiles, works out each one's centre, size and landing (the tile one tile-width away in
//      its landing direction, dead centre, facing that way), and checks it against the scene's route
//      table (WarpTable.cpp). A tile that fails any check is INERT for the visit, reported once as
//      `rs_warp tile=<n> event=bad_tile reason=...` - an authoring mistake you can see, never an
//      assert. Then one `rs_warp event=loaded ...` line. Only a scene WITH a route table is scanned:
//      the engine never reads these bits, but nothing proves every vanilla scene's data leaves them
//      at zero, so a scene nobody routed has no warp tiles whatever its collision says, and says
//      nothing at all.
//   2. THE DETECTOR. Every Player update, the polygon under Link says which tile, if any, he is on.
//      On one, the tile fires unless a guard refuses it - asked in a fixed order, so the reason
//      reported is the first that said no (`rs_warp tile=<n> event=refused reason=...`, once per
//      contact per reason):
//        inert         the scan found it broken
//        moving        a move is in flight - which is also every tick of the fade after it fired
//        busy          a textbox is open, or Link is in a cutscene
//        airborne      over it, within kOnTileY (30) of it, but not standing on it - a drop onto it
//                      fires on landing, if armed. Further above it than that, he is not on it at all
//        disarmed      he has not stepped off it since arriving on it - at a scene start, a void-out,
//                      or the move it fired - or it is LATCHED (below). Stepping off is standing,
//                      grounded, on some other floor: a tick with his centre past a ledge is not it
//        landing_only  it has no destinations: somewhere to arrive, never to leave from
//        aim           a PUSH tile, and he is not moving within 45 degrees of into it
//      The first four only postpone a fire: stand on an armed tile and it fires when they clear.
//   3. THE LATCH. Link lands facing away from the tile he arrives beside, with the camera seated
//      behind him - on the tile's side - so a stick held toward the camera through the move would
//      walk him straight back on and fire again, forever (#151's second bug, same geometry). So
//      when a warp move ends with the stick held, the tile he landed beside is latched: `disarmed`
//      until he LETS GO - the stick released, Link more than 10 beyond his landing from the tile
//      (`clear`), or off its storey (`left`). A move that ends with the stick at rest latches
//      nothing. Reported as `latch reason=move` and `rearm reason=released|clear|left on_tile=`. A
//      latch that lifts while he is still ON the tile leaves it `disarmed` until he steps off - a
//      step tile would otherwise fire the moment the held stick came to rest on it.
//
// A warp tile is a PLACE, so its table names its scene, like a staircase. The fade is the
// staircase's fade (`stairs fade`), because the move is the staircase's move.
//
// ANOTHER SCENE (sturdy-bassoon#148). A destination may be a tile in another scene, through an
// entrance (WarpDef.h, RsWarpDest). The detector, the guards and the markers are the same; the move
// becomes a real transition in the middle of the same fade (Stairs.cpp). When the destination scene
// loads, its scan runs at OnSceneInit - collision is in by then, and Player_Init has not run - and
// puts that tile's landing into the respawn slot the controller armed, so Player_Init stands Link
// on it. The latch carries across the load: the tile he arrives beside is pending, exactly as after
// an in-place move, and latched if the stick is still held when the move ends.

#ifdef __cplusplus
extern "C" {
#endif

// What registration refuses. BUG class, as for staircases and quests: a malformed table is a mistake
// in the source, so it is an error, a debug assert and a `refused` marker at boot. `warps badcheck`
// asks the silent validator about a malformed table and never reaches any of that.
typedef enum RsWarpProblem {
    RS_WARP_PROBLEM_NONE = 0,
    RS_WARP_PROBLEM_NULL_DEF,
    RS_WARP_PROBLEM_BAD_NAME,    // NULL, empty, or carries whitespace, '%', '#' or '"'
    RS_WARP_PROBLEM_NO_TILES,    // no tiles, or a count over a NULL array
    RS_WARP_PROBLEM_BAD_ID,      // a tile id outside 1..RS_WARP_TILE_ID_MAX
    RS_WARP_PROBLEM_DUPLICATE,   // two tiles with one id
    RS_WARP_PROBLEM_BAD_ENTRY,   // not an RsWarpEntry
    RS_WARP_PROBLEM_BAD_ROOM,    // negative or past 255 - a room index is a u8 in the engine
    RS_WARP_PROBLEM_DEST_COUNT,  // negative, over RS_WARP_MAX_DESTS, or a count over a NULL array
    RS_WARP_PROBLEM_BAD_DEST,    // a destination in this scene that is not a tile in this table, or
                                 // one in another scene whose tile id is outside 1..RS_WARP_TILE_ID_MAX
    RS_WARP_PROBLEM_SELF_DEST,   // a tile that sends Link to itself
    RS_WARP_PROBLEM_SCENE_TAKEN, // a different table is already registered for this scene
    // Another scene's tile (#148). Whether that scene's table HAS the tile is not checked here -
    // tables register in no set order - but by the scan, as `dest_unrouted`.
    RS_WARP_PROBLEM_BAD_ENTRANCE,  // neither RS_WARP_HERE nor an entrance id below ENTR_MAX
    RS_WARP_PROBLEM_ENTRANCE_HERE, // an entrance into this table's own scene: that is RS_WARP_TO
    // Generated rows only (#173 F3), found while merging them by map - RsWarp_RegisterGenerated:
    RS_WARP_PROBLEM_ROWS_DISAGREE, // one tile given twice with a different entry, room or destinations
    RS_WARP_PROBLEM_DEST_ORPHAN,   // a destination row for a tile with no RS_GEN_WARP row
    RS_WARP_PROBLEM_BAD_MAP,       // a map number under 1, here or in a destination
    RS_WARP_PROBLEM_COUNT,
} RsWarpProblem;

int32_t RsWarp_SceneDefProblem(const RsWarpSceneDef* def, int32_t* where);
const char* RsWarp_ProblemName(int32_t problem);

// Idempotent for the same pointer (ShipInit "*" functions re-run on preset apply and config load).
int32_t RsWarp_RegisterScene(const RsWarpSceneDef* def);
const RsWarpSceneDef* RsWarp_GetSceneDef(int32_t sceneId);

// GENERATED warp tiles (WarpDef.h): merge every exported scene's rows by map, check each map's table as
// RsWarp_SceneDefProblem checks a scene's (ids, entries, rooms, destination counts, a destination in the
// same map that is not one of its tiles, a tile sending Link to itself), and register one RsWarpMapDef
// per map that the registry owns. A map whose rows are refused is refused whole - BUG class, loud - and
// the rest still register. Once at boot (a ShipInit re-run is a no-op). Returns how many it refused.
int32_t RsWarp_RegisterGenerated(const RsWarpGenRow* rows, int32_t rowCount, const RsWarpGenDest* dests,
                                 int32_t destCount);
// The same, silent: the first refused map's problem, its number in `map` and the offending tile in `where`.
int32_t RsWarp_GeneratedProblem(const RsWarpGenRow* rows, int32_t rowCount, const RsWarpGenDest* dests,
                                int32_t destCount, int32_t* map, int32_t* where);
const RsWarpMapDef* RsWarp_GetMapDef(int32_t map);

// A TILE KEY: how this scene's scan names one tile. A scene holds up to its map count of SLOTS, one per
// map (SceneMaps.h; a hand scene is one slot), and each slot numbers its tiles 1..RS_WARP_TILE_ID_MAX,
// so the key is slot * RS_WARP_KEYS_PER_SLOT + local id. Slot 0's keys are the local ids themselves, so
// a hand scene's - and a solo scene's - keys are exactly its tile ids. Every `rs_warp tile=` field in a
// marker names a key by its TOKEN: the id in a hand scene, so those lines are unchanged, and
// `<map>:<id>` in a map-keyed one, so a stitched scene's two tile 1s are told apart.
#define RS_WARP_KEYS_PER_SLOT (RS_WARP_TILE_ID_MAX + 1)
void RsWarp_TileToken(int32_t key, char* out, int32_t size);

// The scene's WARP GROUP (sturdy-bassoon#157): every scene joined to it by a chain of step-warp
// routes into another scene (RS_WARP_TO_SCENE), in either direction - an overworld and the
// underground areas its trapdoors lead to (#148). Named by the smallest scene id in the group, so
// the answer is the same from any member; a scene no route touches is a group of one, its own id.
// Worked out from the registered tables on every call - a handful of tables - so it can never
// disagree with them. The static bake keeps what it has recorded until Link enters a scene of
// another group (StaticBakeRegistry.cpp).
int32_t RsWarp_SceneGroup(int32_t sceneId);

#ifdef __cplusplus
}

#include <string>
#include <vector>

// C++ only, for the console. What the scan found and what the detector holds, for this scene.
struct RsWarpTileReport {
    int32_t key;      // the scan's tile key
    int32_t id;       // the local id the collision carries
    int32_t map;      // its map number, 0 in a hand scene
    std::string token; // how markers name it: "3", or "1:3" in a map-keyed scene
    bool present;     // found in the collision
    bool routed;      // in the scene's table
    const char* bad;  // why it is inert, or nullptr
    int32_t entry;    // RsWarpEntry, or -1 when unrouted
    int32_t room;     // -1 when unrouted
    std::string dests; // "2", "5,7", "1@0x63E" (tile 1 through entrance 0x63E), or "-" for a landing only
    float cx, cz, y;  // centre of the tile, and its floor height
    int32_t dir;      // landing direction (WarpBits.h)
    float width;      // the tile's size along that direction - how far away the landing is
    float lx, ly, lz; // the landing
    int16_t yaw;
    bool mustLeave;   // disarmed until he steps off it
    bool latched;     // disarmed until he lets go
    int32_t fires;
    std::string picks; // fires per destination, "5:3,7:4" or "1@0x63E:2"
};

struct RsWarpSceneReport {
    bool scanned;
    int32_t sceneNum;
    const RsWarpSceneDef* def; // this scene's table, or nullptr
    bool mapKeyed;             // the scene's tiles are its maps' (SceneMaps.h), not a hand table's
    int32_t maps;              // how many maps it holds; 0 when not map-keyed
    int32_t orphans;           // warp polygons in no map's rectangle - an export mistake, so inert
    int32_t ok;
    int32_t bad;
    std::vector<RsWarpTileReport> tiles; // every tile found or routed, by id
    int32_t onTile;       // the tile KEY under Link as of the last detector tick, 0 for none
    int32_t pendingLatch; // the tile a warp move in flight will latch, if it ends with the stick held
    std::string last;     // the detector's last event (`fired` / `refused`), minus `rs_warp `
};

RsWarpSceneReport RsWarp_Report();
std::vector<const RsWarpSceneDef*> RsWarp_ListScenes(); // every registered table, in registration order
std::vector<const RsWarpMapDef*> RsWarp_ListMaps();     // every generated map table, by map number

// The tile key under Link right now (0 for none), read the way the detector reads it.
int32_t RsWarp_TileUnderPlayer();

const char* RsWarp_DirName(int32_t dir);     // "+z", "+x", "-z", "-x"
const char* RsWarp_EntryName(int32_t entry); // "step", "push"
#endif

#endif // SOH_RS_WARPS_H
