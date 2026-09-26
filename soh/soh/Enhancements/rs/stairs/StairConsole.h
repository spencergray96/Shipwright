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
// Stairs.cpp as it happens (move_begin, moved, room_request, room, landed, abort, refused), and
// the actor adds `event=open` and `event=choice`. Those are events; this command is for asking.
//
// `args[0]` is the subcommand:
//   list                    every registered staircase: id, name, scene, rows, the storeys it serves
//   dump <id>               one staircase in full: each row's storey (index AND the label the live
//                           convention gives it), landing position, facing and room; then each
//                           row's menu, exactly as the textbox composes it
//   menu <id> <row>         that one menu alone - what a player standing on that row would read
//   where                   where Link is (pos, yaw, room) and, for every staircase in this scene,
//                           which of its rows he is standing on (`row=-1`: none within half a storey)
//   go <id> <row>           THE MOVE, through the same controller the menu uses, from whichever row
//                           Link is nearest. A write; the markers above report how it went
//   status                  the controller: phase, the move in flight, and the last move's final
//                           marker line (`last="..."`, last on its own line because it has spaces)
//   fade [ticks]            the fade length each way, in game ticks; 0 is a hard cut. With an
//                           argument it sets and saves the CVar (0..40)
//   actors                  every live staircase actor: params decoded, reserved bits, room, pos
//   badcheck                the validator over the malformed table (StairTable.cpp), one line each
//
// Returns 0 when the operation succeeded (or for read-only subcommands), 1 otherwise: a refused
// move, a bad argument, an unknown subcommand, or a badcheck row the validator ACCEPTED.
int32_t RsStairConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_STAIR_CONSOLE_H
