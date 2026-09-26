#ifndef SOH_RS_STAIR_CONSOLE_H
#define SOH_RS_STAIR_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one renderer behind BOTH console surfaces for staircases (sturdy-bassoon#147), the
// arrangement RegionConsole.h describes:
//
//   - the human `stairs ...` command, registered in StairConsole.cpp, and
//   - `agenttest stairs ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] rs_stairs <line>` marker in agent-log.txt.
//
// The MOVE reports itself separately, as `rs_stairs stair=<n> event=...` markers written from
// Stairs.cpp as it happens (move_begin, room_request, room, moved, no_placement, landed, abort,
// refused), and the actor adds `event=open` and `event=choice`. Those are events; this command is
// for asking.
//
// `args[0]` is the subcommand:
//   list                    every registered staircase: id, name, scene, rows, the storeys it serves
//   dump <id>               one staircase in full: `land_forward=`, then each row's storey (index
//                           AND the label the live convention gives it) and room, with the landing
//                           computed from its placement (`placed=1 landing=x,y,z yaw=`) or
//                           `placed=0` when that placement is not loaded; then each row's menu,
//                           exactly as the textbox composes it
//   menu <id> <row>         that one menu alone - what a player standing on that row would read
//   where                   where Link is (pos, yaw, room) and, for every staircase in this scene,
//                           which of its rows he is standing on (`row=-1`: none within half a storey)
//   go <id> <row>           THE MOVE, through the same controller the menu uses, from whichever row
//                           Link is nearest. A write; `op=go result=ok` or `result=error error=<why>`
//                           (busy, wrong_scene, ...), and the markers above report how it went
//   status                  the controller: phase, the move in flight, and the last move's outcome
//                           (`last="stair=<n> event=landed ..."` - the event line WITHOUT its
//                           `rs_stairs ` prefix, so it never matches a grep for the event itself;
//                           on its own line because it has spaces)
//   fade [ticks|default]    the fade length each way, in game ticks; the build default is 6, and 0
//                           is a hard cut. A number sets and saves the override CVar (0..40);
//                           `default` clears it. Reports `source=cvar|default`
//   actors                  every live staircase actor: params decoded, reserved bits, room, the
//                           yaw it faces (which is the landing's facing), pos
//   badcheck                the validator over the malformed table (StairTable.cpp), one line each
//
// Returns 0 when the operation succeeded (or for read-only subcommands), 1 otherwise: a refused
// move, a bad argument, an unknown subcommand, or a badcheck row the validator ACCEPTED.
int32_t RsStairConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_STAIR_CONSOLE_H
