#include "WarpConsole.h"

#include <cstdio>
#include <string>

#include <ship/debug/Console.h>

#include "WarpTable.h"
#include "Warps.h"
#include "soh/Enhancements/console/ConsoleSink.h"
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

int32_t List(std::vector<std::string>& lines) {
    const RsWarpSceneDef* defs[64];
    const int32_t count = RsWarp_ListScenes(defs, 64);
    Addf(lines, "op=list scenes=%d", count);
    for (int32_t i = 0; i < count; i++) {
        Addf(lines, "scene[0x%X] name=%s tiles=%d here=%d", defs[i]->sceneId, defs[i]->name, defs[i]->tileCount,
             InPlay() && gPlayState->sceneNum == defs[i]->sceneId ? 1 : 0);
    }
    return 0;
}

int32_t Dump(std::vector<std::string>& lines) {
    const RsWarpSceneReport report = RsWarp_Report();
    Addf(lines, "op=dump scene=0x%X scanned=%d table=%s tiles=%d ok=%d bad=%d on_tile=%d pending_latch=%d",
         report.sceneNum, report.scanned ? 1 : 0, report.def != nullptr ? report.def->name : "none",
         static_cast<int>(report.tiles.size()), report.ok, report.bad, report.onTile, report.pendingLatch);
    for (const RsWarpTileReport& t : report.tiles) {
        Addf(lines,
             "tile[%d] ok=%d reason=%s present=%d routed=%d entry=%s room=%d dests=%s centre=%.1f,%.1f y=%.1f dir=%s "
             "width=%.1f landing=%.1f,%.1f,%.1f yaw=%d must_leave=%d latched=%d fires=%d picks=%s",
             t.id, t.bad == nullptr ? 1 : 0, t.bad != nullptr ? t.bad : "none", t.present ? 1 : 0, t.routed ? 1 : 0,
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
         "op=where scene=0x%X room=%d pos=%.1f,%.1f,%.1f yaw=%d floor_y=%.1f ground=%d tile=%d move_yaw=%d "
         "speed=%.1f stick=%d",
         gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num, player->actor.world.pos.x, player->actor.world.pos.y,
         player->actor.world.pos.z, player->actor.shape.rot.y, player->actor.floorHeight,
         (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 1 : 0, RsWarp_TileUnderPlayer(),
         player->actor.world.rot.y, player->linearVelocity, static_cast<int>(stick));
    return 0;
}

int32_t Status(std::vector<std::string>& lines) {
    const RsStairStatus move = RsStair_GetStatus();
    const RsWarpSceneReport report = RsWarp_Report();
    Addf(lines, "op=status moving=%d warp=%d phase=%s from_tile=%d to_tile=%d fade=%d on_tile=%d pending_latch=%d",
         move.moving ? 1 : 0, move.warp ? 1 : 0, move.phase, move.fromTile, move.toTile, RsStair_GetFadeTicks(),
         report.onTile, report.pendingLatch);
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
    return accepted == 0 ? 0 : 1;
}

const char* kUsage = "usage: warps list | dump | where | status | badcheck";

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
    "where | status | badcheck. `dump` shows every warp tile in this scene as its collision says - where it is, "
    "where it lands you, whether it is armed - and why any is inert. The move is the staircase's, so "
    "`stairs fade` sets its fade too.",
    { { "list|dump|where|status|badcheck", Ship::ArgumentType::TEXT } });

} // namespace
