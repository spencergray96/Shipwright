#ifndef SOH_DISTANCE_FOG_CONSOLE_H
#define SOH_DISTANCE_FOG_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for tuning a scene's distance fog live
// (sturdy-bassoon#144), the arrangement RegionConsole.h describes:
//
//   - the human `fog ...` command, registered in DistanceFogConsole.cpp, and
//   - `agenttest fog ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] fog <line>` marker in agent-log.txt.
//
// The override replaces ONLY the fog: lightCtx's fog colour, fogNear and fogFar are rewritten at
// OnPlayDrawBegin, after Environment_Update has lit the frame and before Play_Draw sets the fog and
// builds the perspective from fogFar. Ambient and directional light keep following time of day and
// weather. fogFar is also the view's far clip plane, so `far` un-draws everything past it. A scene
// load hands fog back to the scene, so re-apply after every `entrance`. Nothing is saved.
//
// `args[0]` is the subcommand; none is `status`:
//   <near> <far>        override the band; the colour keeps following the scene (time of day, the
//                       Song of Storms' gloomy sky, lightning). near is fog-space 0..996, far world
//                       units 100..12800
//   <near> <far> <r> <g> <b>  the same, and pin the colour (0..255 each)
//   status              read what is live and what the scene would give
//   off                 hand fog and far clip back to the scene's light settings
//
// Every successful line starts `op=<sub> result=ok` (op=set for a band) and carries the same fields:
// `mode=` override|scene, `near=` `far=` `color=r,g,b` the band being drawn, `color_src=` scene|pinned,
// `start=` roughly where the fog begins, in world units (near inverted through the perspective, see
// DistanceFog_StartDistance), `sky=` what the skybox filter does at this near (clear above 980,
// tinted 951..979, replaced at 950 and below), then `scene_near=` `scene_far=` `scene_color=` the
// band the scene's own light settings give right now (weather and lightning included).
// `status` adds one `op=status setting` line per light setting of config 0 (dawn, day, dusk, night),
// ending in `c=` - the setting's last three initializer fields, ready to paste over the ones in a
// scene's EnvLightSettings table - and one for the storm settings (8-11) when the scene has them.
//
// Refusals, rc=1, never echoing the typed words:
//   op=set result=error error=bad_argument usage=...      a band or colour out of range or not a number
//   op=<sub> result=error error=no_scene                  no scene loaded
//   op=unknown result=error error=unknown_subcommand usage=...
int32_t DistanceFogConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_DISTANCE_FOG_CONSOLE_H
