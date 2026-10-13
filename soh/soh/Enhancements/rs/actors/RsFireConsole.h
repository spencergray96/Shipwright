#ifndef SOH_RS_FIRE_CONSOLE_H
#define SOH_RS_FIRE_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one renderer behind both console surfaces for Rs_Fire (sturdy-bassoon#232), RegionConsole's
// arrangement: the human `rsfire ...` command joins `lines` into the ImGui console, and
// `agenttest rsfire ...` (agenttest/AgentTest.cpp) writes each as its own `[agenttest] rs_fire
// <line>` marker. What it shows is what a capture cannot: where each fire is, whether it is lit and
// by what, and whether it holds a light node. A fire's world flag is read and cleared with the
// existing `agenttest worldflag <n> [0|1]`.
//
// `args[0]` is the subcommand:
//   list            one `fire idx=<i> x= y= z= size=<percent> hit=<percent> lit=0|1 by=<none|flag|stick|fire>
//                   flag=<world flag|-1> want_light=0|1 light=0|1 params=0x<hex>` line per live
//                   Rs_Fire, in
//                   the actor list's order, then `fires count=<n> lit=<n> lights=<n>`. Positions are
//                   world units, rounded. This is how a run reads where an export put each fire
//   pool            `pool nodes=<n> of=32 fires=<k>`: the light nodes in the scene's list (every
//                   holder, vanilla's too) and how many of them are fires'. 32 is LIGHTS_BUFFER_SIZE
//                   in z_lights.c, which #233 may raise
//
// Returns 0 on success (and for the reads), 1 for a refused operation: no scene, a bad argument.
int32_t RsFireConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_FIRE_CONSOLE_H
