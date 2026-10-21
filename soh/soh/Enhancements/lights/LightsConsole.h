#ifndef SOH_LIGHTS_CONSOLE_H
#define SOH_LIGHTS_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The `lights` console command: the scene's point-light pool and per-actor bind (sturdy-bassoon#233).
// The behaviour is in `soh/src/code/z_lights.c`; the limits and the setting are in LightsBudget.h
// beside this file. The human `lights ...` command joins `lines` into the ImGui console, and
// `agenttest lights ...` writes each as its own `[agenttest] lights <line>` marker.
//
// `args[0]` is the subcommand (none means `status`):
//   status          three lines:
//                   `op=status result=ok occupied=<n> cap=<n> size=<n> peak=<n> refused=<n> sunfirst=on|off
//                   gate=<0|1> scene=0x<hex>`: the nodes held now, the cap new inserts are held to, the
//                   buffer's size, the most held at once and the inserts refused since the scene began;
//                   the setting, and whether this tick's binds run sun first (the setting AND a grid-tool
//                   scene);
//                   `tick frame=<n> binds=<n> walked=<n> overflowed=<n> max_in_range=<n>`: the last complete
//                   tick's Lights_BindAll calls, the list nodes they visited (the cost the pool size
//                   drives; a grid-tool scene walks each list twice), the binds with more lights in range
//                   than 7 slots, and the most point lights in range of one bind. A vanilla scene counts
//                   binds and walked only: it runs vanilla's loop and nothing more;
//                   `link in_range=<n> points=<n> dirs=<n>`: Link's own bind in that tick: point lights in
//                   range, point lights bound, directional lights bound. -1 each when he did not draw, and
//                   always in a vanilla scene. dirs=2 is "he keeps the sun"
//   sunfirst [on|off|default]
//                   sets the setting (saved; default on), then, or bare,
//                   `op=sunfirst result=ok sunfirst=on|off default=on override=0|1 gate=<0|1>`. gate is
//                   the last tick's, so it follows a change one tick later
//   pool [<n>|default]
//                   holds new inserts to n (1..size) for THIS SESSION ONLY, or back to the size; then, or
//                   bare, `op=pool result=ok cap=<n> size=<n> occupied=<n>`. Never saved: `pool 32` plants
//                   vanilla's starvation for a control and cannot outlive the run. Holders over a lowered
//                   cap keep their nodes
//   shadow [clear]  Link's feet shadows over the last LIGHTS_SHADOW_RECORD_TICKS ticks (#250), oldest first:
//                   `op=shadow result=ok ticks=<n> scene=0x<hex>`, then one line a tick:
//                   `t=<frame> n=<slots> ids=<7> f0=<7> f1=<7> num=<7>`, each a comma list over the 7 slots
//                   (`-` past the bound ones). ids: the node each slot bound, its pool index, or `s` for a
//                   directional light; f0/f1: the alpha that slot's shadow drew at under each foot (0 for
//                   none); num: foot 0's strength term before the scale and clamp. The last two bound slots
//                   are the ones ActorShadow_DrawFeet treats as the sun. `clear` empties the record and
//                   prints `op=shadow result=ok cleared=1`
//
// Returns 0 on success (and for the reads), 1 for a refused operation: an unknown subcommand, an extra
// argument or a bad value (`op=<sub> result=error error=bad_value`).
int32_t LightsConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_LIGHTS_CONSOLE_H
