#ifndef SOH_COL_VIEWER_CONSOLE_H
#define SOH_COL_VIEWER_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one renderer behind BOTH console surfaces for the Collision Viewer's climb colours
// (sturdy-bassoon#196), the arrangement RegionConsole.h describes:
//
//   - the human `colview ...` command, registered in ColViewerConsole.cpp, and
//   - `agenttest colview ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] colview <line>` marker in agent-log.txt.
//
// Why it exists: the climb colours show a placed prop's climb setting in game, and the agent loop
// has to switch them on and see that they drew. A marker cannot see a pixel, so the counts below -
// polys the LAST drawn frame coloured, by class - are the evidence.
//
// Every successful line is `op=<sub> result=ok` and the same fields:
//   enabled=  the viewer's switch (0|1)
//   scene=    the Scene layer: disabled|solid|transparent
//   bgactors= the Bg Actors layer, the same three
//   mode=     class (upstream's colours) | climb (sturdy-bassoon#196's)
//   vanilla= noclimb= hands= other=  walls of wall type 0, 1, 13 and any other, drawn last frame
//   nonwall=  floors and ceilings drawn last frame
//   frames=   frames drawn in climb mode since boot; it stops rising when the colours are off
// The counts are all 0 unless the viewer is on, in climb mode, with the Scene or Bg Actors layer
// drawing. They lag a write by a frame: read `status` again a few frames after `climb on`.
//
// `args[0]` is the subcommand; none is `status`:
//   status      read the switch and last frame's counts
//   climb on    viewer on, Scene layer to solid if it was disabled, climb colours on
//   climb off   climb colours off (back to class) and the viewer off
// Both writes save the settings, as the menu does.
// A bad or missing on|off prints `op=climb result=error error=bad_argument usage=climb(on|off)`, rc=1.
// An unknown subcommand prints `op=unknown result=error error=unknown_subcommand usage=...`, rc=1,
// without echoing the word.
//
// Returns 0 on success, 1 for a refusal, so `rc=` is the pass/fail bit.
int32_t ColViewerConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_COL_VIEWER_CONSOLE_H
