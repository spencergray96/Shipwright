#ifndef SOH_RS_WARP_CONSOLE_H
#define SOH_RS_WARP_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one renderer behind BOTH console surfaces for step warps (sturdy-bassoon#154), the
// arrangement RegionConsole.h describes:
//
//   - the human `warps ...` command, registered in WarpConsole.cpp, and
//   - `agenttest warps ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] rs_warp <line>` marker in agent-log.txt.
//
// The tiles report THEMSELVES separately, as `rs_warp tile=<n> event=...` markers written from
// Warps.cpp as things happen (fired, refused, latch, rearm, bad_tile) and from the move controller
// in Stairs.cpp (move_begin, room_request, room, moved, landed, abort, refused), plus one
// `rs_warp event=loaded ...` per scene with tiles. Those are events; this command is for asking.
// No line below ever starts `tile=<n> event=`, so a grep for the events never matches an answer.
//
// `args[0]` is the subcommand:
//   list        every registered route table: `scene[0x<id>] name= tiles= here=`
//   dump        this scene, as the scan found it and as the detector holds it now:
//               `op=dump scene= scanned= table= tiles= ok= bad= on_tile= pending_latch=`, then one
//               `tile[<n>]` line per tile found or routed - `ok=`, `reason=` (why it is inert, or
//               none), `present=` (in the collision) `routed=` (in the table), `entry=`, `room=`,
//               `dests=`, `centre=x,z y=`, `dir=` (+z|+x|-z|-x) `width=`, `landing=x,y,z yaw=`,
//               `must_leave=` `latched=` (the two ways it is disarmed), `fires=`, and `picks=` - how
//               many times each destination was picked, `5:3,7:4`
//   where       Link's position, facing, floor, and the tile under him (`tile=0`: none), with the
//               three things the guards read: `move_yaw=`, `speed=` and `stick=`
//   status      the move controller (`moving=` `warp=` `phase=` `from_tile=` `to_tile=` `fade=`), the
//               detector (`on_tile=` `pending_latch=`), then `last_event="..."` - the detector's last
//               fired/refused line - and `last_move="..."` - the controller's last outcome, a warp's or
//               a staircase's - each on its own line and without its prefix
//   badcheck    the validator over the malformed tables (WarpTable.cpp), one line each
//
// Returns 0 for every read, 1 for an unknown subcommand or a badcheck row the validator ACCEPTED.
int32_t RsWarpConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_WARP_CONSOLE_H
