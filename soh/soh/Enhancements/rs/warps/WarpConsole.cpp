#include "WarpConsole.h"

#include <cstdio>
#include <string>

#include <ship/debug/Console.h>

#include "WarpTable.h"
#include "Warps.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/rs/maps/SceneMaps.h"
#include "soh/Enhancements/rs/stairs/Stairs.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

bool InPlay() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr;
}

// A tile key as markers name it (Warps.h): the id in a hand scene, `<map>:<id>` in a map-keyed one.
std::string Tok(int32_t key) {
    char buf[24];
    RsWarp_TileToken(key, buf, sizeof(buf));
    return buf;
}

int32_t List(std::vector<std::string>& lines) {
    const std::vector<const RsWarpSceneDef*> defs = RsWarp_ListScenes();
    Addf(lines, "op=list scenes=%d", static_cast<int>(defs.size()));
    for (const RsWarpSceneDef* def : defs) {
        Addf(lines, "scene[0x%X] name=%s tiles=%d here=%d", def->sceneId, def->name, def->tileCount,
             InPlay() && gPlayState->sceneNum == def->sceneId ? 1 : 0);
    }
    // The generated tables (#173 F3), one per map, after the hand ones: `here=` is whether the scene
    // Link is in holds that map.
    const std::vector<const RsWarpMapDef*> maps = RsWarp_ListMaps();
    for (const RsWarpMapDef* def : maps) {
        Addf(lines, "map[%d] tiles=%d here=%d", def->map, def->tileCount,
             InPlay() && RsMaps_SlotOf(gPlayState->sceneNum, def->map) >= 0 ? 1 : 0);
    }
    Addf(lines, "op=list_maps maps=%d", static_cast<int>(maps.size()));
    return 0;
}

// `warps maps`: the map->scene rows of the scene Link is in (SceneMaps.h), and which map his position is
// in - the first half of the position -> map -> (map, local id) lookup the scan and detector make.
int32_t Maps(std::vector<std::string>& lines) {
    if (!InPlay()) {
        lines.push_back("op=maps scene=none maps=0");
        return 0;
    }
    const int32_t scene = gPlayState->sceneNum;
    const int32_t count = RsMaps_MapCount(scene);
    const Vec3f pos = GET_PLAYER(gPlayState)->actor.world.pos;
    const int32_t slot = RsMaps_SlotAt(scene, pos.x, pos.z);
    const RsSceneMap* at = RsMaps_Map(scene, slot);
    Addf(lines, "op=maps scene=0x%X maps=%d world=%s entrance=0x%X pos=%.1f,%.1f,%.1f map_here=%d", scene, count,
         count > 0 ? RsMaps_WorldName(RsMaps_SceneWorld(scene)) : "none",
         static_cast<unsigned>(RsMaps_SceneEntrance(scene)) & 0xFFFF, pos.x, pos.y, pos.z, at != nullptr ? at->map : 0);
    for (int32_t i = 0; i < count; i++) {
        const RsSceneMap* m = RsMaps_Map(scene, i);
        Addf(lines, "slot[%d] map=%d rect=%d,%d..%d,%d warp_table=%d", i, m->map, m->minX, m->minZ, m->maxX, m->maxZ,
             RsWarp_GetMapDef(m->map) != nullptr ? 1 : 0);
    }
    return 0;
}

int32_t Dump(std::vector<std::string>& lines) {
    const RsWarpSceneReport report = RsWarp_Report();
    // A map-keyed scene (#173 F3) says so, and how many maps and stray polygons it has; a hand scene's
    // line is as it was.
    std::string mapFields;
    if (report.mapKeyed) {
        mapFields = " maps=" + std::to_string(report.maps) + " orphans=" + std::to_string(report.orphans);
    }
    Addf(lines, "op=dump scene=0x%X scanned=%d table=%s tiles=%d ok=%d bad=%d on_tile=%s pending_latch=%s%s",
         report.sceneNum, report.scanned ? 1 : 0,
         report.mapKeyed ? "by_map" : (report.def != nullptr ? report.def->name : "none"),
         static_cast<int>(report.tiles.size()), report.ok, report.bad, Tok(report.onTile).c_str(),
         Tok(report.pendingLatch).c_str(), mapFields.c_str());
    for (const RsWarpTileReport& t : report.tiles) {
        Addf(lines,
             "tile[%s] ok=%d reason=%s present=%d routed=%d entry=%s room=%d dests=%s centre=%.1f,%.1f y=%.1f dir=%s "
             "width=%.1f landing=%.1f,%.1f,%.1f yaw=%d must_leave=%d latched=%d fires=%d picks=%s",
             t.token.c_str(), t.bad == nullptr ? 1 : 0, t.bad != nullptr ? t.bad : "none", t.present ? 1 : 0, t.routed ? 1 : 0,
             RsWarp_EntryName(t.entry), t.room, t.dests.c_str(), t.cx, t.cz, t.y, RsWarp_DirName(t.dir), t.width, t.lx,
             t.ly, t.lz, t.yaw, t.mustLeave ? 1 : 0, t.latched ? 1 : 0, t.fires, t.picks.c_str());
    }
    return 0;
}

int32_t Where(std::vector<std::string>& lines) {
    if (!InPlay()) {
        lines.push_back("op=where scene=none tile=0");
        return 0;
    }
    Player* player = GET_PLAYER(gPlayState);
    f32 stick = 0.0f;
    s16 stickAngle = 0;
    func_80077D10(&stick, &stickAngle, &gPlayState->state.input[0]);
    Addf(lines,
         "op=where scene=0x%X room=%d pos=%.1f,%.1f,%.1f yaw=%d floor_y=%.1f ground=%d tile=%s move_yaw=%d "
         "speed=%.1f stick=%d",
         gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num, player->actor.world.pos.x, player->actor.world.pos.y,
         player->actor.world.pos.z, player->actor.shape.rot.y, player->actor.floorHeight,
         (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 1 : 0, Tok(RsWarp_TileUnderPlayer()).c_str(),
         player->actor.world.rot.y, player->linearVelocity, static_cast<int>(stick));
    return 0;
}

int32_t Status(std::vector<std::string>& lines) {
    const RsStairStatus move = RsStair_GetStatus();
    const RsWarpSceneReport report = RsWarp_Report();
    Addf(lines, "op=status moving=%d warp=%d phase=%s from_tile=%s to_tile=%s fade=%d on_tile=%s pending_latch=%s",
         move.moving ? 1 : 0, move.warp ? 1 : 0, move.phase, Tok(move.fromTile).c_str(), Tok(move.toTile).c_str(),
         RsStair_GetFadeTicks(), Tok(report.onTile).c_str(), Tok(report.pendingLatch).c_str());
    Addf(lines, "last_event=\"%s\"", report.last.c_str());
    Addf(lines, "last_move=\"%s\"", move.last.c_str());
    return 0;
}

int32_t BadCheck(std::vector<std::string>& lines) {
    const int32_t count = RsWarpTable_BadCount();
    int32_t accepted = 0;
    for (int32_t i = 0; i < count; i++) {
        int32_t where = -1;
        const int32_t problem = RsWarp_SceneDefProblem(RsWarpTable_Bad(i), &where);
        accepted += problem == RS_WARP_PROBLEM_NONE ? 1 : 0;
        // The kind and the row, never the table's name - one of them exists to have an unhygienic one.
        Addf(lines, "bad[%d] problem=%s row=%d", i, RsWarp_ProblemName(problem), where);
    }
    Addf(lines, "op=badcheck rows=%d refused=%d accepted=%d result=%s", count, count - accepted, accepted,
         accepted == 0 ? "ok" : "error");
    // The generated rows' merge, and the map->scene validator (#173 F3): planted tables, each through
    // the same code the boot registration runs, each with the problem it must find. The last of each is
    // a good table it must accept.
    const int32_t genCount = RsWarpTable_BadGenCount();
    int32_t genWrong = 0;
    for (int32_t i = 0; i < genCount; i++) {
        const RsWarpBadGen* bad = RsWarpTable_BadGen(i);
        int32_t map = -1;
        int32_t where = -1;
        const int32_t problem = RsWarp_GeneratedProblem(bad->rows, bad->rowCount, bad->dests, bad->destCount, &map, &where);
        genWrong += problem == bad->expect ? 0 : 1;
        Addf(lines, "badgen[%d] problem=%s map=%d row=%d want=%s %s", i, RsWarp_ProblemName(problem), map, where,
             RsWarp_ProblemName(bad->expect), problem == bad->expect ? "ok" : "wrong");
    }
    Addf(lines, "op=badcheck_gen tables=%d wrong=%d result=%s", genCount, genWrong, genWrong == 0 ? "ok" : "error");
    const int32_t mapsCount = RsWarpTable_BadMapsCount();
    int32_t mapsWrong = 0;
    for (int32_t i = 0; i < mapsCount; i++) {
        const RsMapsBad* bad = RsWarpTable_BadMaps(i);
        int32_t where = -1;
        const int32_t problem = RsMaps_TableProblem(bad->scenes, bad->sceneCount, bad->maps, bad->mapCount, &where);
        mapsWrong += problem == bad->expect ? 0 : 1;
        Addf(lines, "badmaps[%d] problem=%s row=%d want=%s %s", i, RsMaps_ProblemName(problem), where,
             RsMaps_ProblemName(bad->expect), problem == bad->expect ? "ok" : "wrong");
    }
    Addf(lines, "op=badcheck_maps tables=%d wrong=%d result=%s", mapsCount, mapsWrong, mapsWrong == 0 ? "ok" : "error");
    return (accepted == 0 && genWrong == 0 && mapsWrong == 0) ? 0 : 1;
}

const char* kUsage = "usage: warps list | dump | where | maps | status | badcheck";

} // namespace

int32_t RsWarpConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string& sub = args[0];
    if (sub == "list") {
        return List(lines);
    }
    if (sub == "dump") {
        return Dump(lines);
    }
    if (sub == "where") {
        return Where(lines);
    }
    if (sub == "maps") {
        return Maps(lines);
    }
    if (sub == "status") {
        return Status(lines);
    }
    if (sub == "badcheck") {
        return BadCheck(lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink -----------------------------------------------------------------------------
//
// `warps` collides with nothing in debugconsole.cpp's command list.

namespace {

const ConsoleSink::Command warpsCommand(
    "warps", RsWarpConsole_Run,
    "Step warps - tiles that move Link in place when he steps onto them (sturdy-bassoon#154): list | dump | "
    "where | maps | status | badcheck. `maps` is the scene's grid-tool maps and which one Link stands in (#173). `dump` shows every warp tile in this scene as its collision says - where it is, "
    "where it lands you, whether it is armed - and why any is inert. The move is the staircase's, so "
    "`stairs fade` sets its fade too.",
    { { "list|dump|where|maps|status|badcheck", Ship::ArgumentType::TEXT } });

} // namespace
