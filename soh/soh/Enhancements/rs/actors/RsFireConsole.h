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
//   list            one `fire idx=<i> x= y= z= size=<percent> hit=<percent> lit=0|1
//                   by=<none|flag|stick|fire> flag=<world flag|-1> want_light=0|1 light=0|1
//                   params=0x<hex> awake=0|1 drawn=0|1 forward=<n> glow=0|1` line per live Rs_Fire,
//                   in the actor list's order, then `op=list result=ok count=<n> lit=<n> lights=<n>
//                   awake=<n> drawn=<n>`. Positions are world units, rounded. This is how a run reads
//                   where an export put each fire. awake: inside its cull volume, so it updates and
//                   registers its collider (an arrow can light it); drawn: the last frame drew it;
//                   forward: its uncullZoneForward; glow: it holds a light that draws the halo
//   distance [<n>|default]
//                   sets the draw distance (sturdy-bassoon#243) to n (100..30000) or back to the
//                   built-in RS_FIRE_DRAW_DISTANCE, saves it, and writes it into every live fire at once;
//                   then, or bare, `op=distance result=ok forward=<n> default=<n> override=0|1 fires=<n>
//                   applied=<n>`. applied counts the fires now carrying forward=<n>
//   glow [on|off|default]
//                   the same for the glow halo (RS_FIRE_GLOW_DEFAULT): `op=glow result=ok glow=on|off
//                   default=on|off override=0|1 lights=<n> applied=<n>`. lights counts the fires holding
//                   a light node, applied those whose light matches the setting
//   pool            `op=pool result=ok nodes=<n> of=<cap> fires=<k>`: the light nodes in the scene's list
//                   (every holder, vanilla's too) and how many of them are fires'. `of` is the pool's cap
//                   in force (lights/LightsBudget.h, #233): 256, unless `lights pool <n>` lowered it
//
// Returns 0 on success (and for the reads), 1 for a refused operation: no scene, an unknown
// subcommand, an extra argument or a bad value (`op=<sub> result=error error=bad_value`).
int32_t RsFireConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_FIRE_CONSOLE_H
