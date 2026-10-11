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
//                       Song of Storms' gloomy sky, lightning). near is fog-space 0..1000 (a scene
//                       can have at most 996; 1000 is no fog), far world units 100..12800
//   <near> <far> <r> <g> <b>  the same, and pin the colour (0..255 each)
//   ... max=<m>         either form with a trailing max (sturdy-bassoon#169): the fog-space value where
//                       fog reaches 100%, which Play_SetFog passes to Gfx_SetFog in place of vanilla's
//                       1000. 1000 completes the fog at the far clip; lower completes it sooner. m runs
//                       near+4..999, or 1000 at any near: gSPFogPosition's multiplier 128000 / (m - near)
//                       is a signed 16-bit field. Without max= the band is exactly what it always was
//   world <start> <end> [r g b] [clip=<c>]
//                       world-unit fog (sturdy-bassoon#167; WorldFog.h): linear in view depth, clear out to
//                       start and complete at end, both world units (0..32767, start < end), in any scene,
//                       vanilla ones included. clip= sets the far clip (100..12800); without it the clip stays
//                       the scene's own. max= is vanilla's and is refused here
//   status              read what is live and what the scene would give
//   off                 hand fog and far clip back to the scene: its world-unit rows if it has them, else its
//                       light settings. A <near> <far> band is vanilla fog even in a scene with rows
//
// Every successful line starts `op=<sub> result=ok` (op=set for a band) and carries these fields:
// `mode=` override|scene, `near=` `far=` `max=` `color=r,g,b` the band being drawn (max is 1000 unless the
// override set one), `color_src=` scene|pinned, `start=` roughly where the fog begins, in world units
// (near inverted through the perspective; the formula is at DepthAt in the .cpp), then `end=` where it
// reaches 100% - only when max is below 1000, since at 1000 that is the far clip - `sky=` what the skybox
// filter does at this near (clear at 980 and above, tinted 951..979, replaced at 950 and below), `time=`
// `rain=` the clock and the rain's intensity, `scene_near=` `scene_far=` `scene_color=` the band the
// scene's own light settings give right now (weather and lightning included), and last `c=` - the live
// band as the last three initializer fields of an EnvLightSettings entry, ready to paste over one in a
// scene's table. `status` adds one `op=status setting` line per light setting of config 0 (dawn, day,
// dusk, night) and one per storm setting (8-11) when the scene has them, each ending in its own `c=`.
//
// World-unit fog (sturdy-bassoon#167) adds `kind=` vanilla|world after `mode=`, and `scene_kind=` vanilla|world
// (with `scene_start=` `scene_end=` for world) before `c=`, on every line. A `kind=world` line keeps the fields'
// meanings - `start=` `end=` where the fog begins and completes in world units, `far=` the clip, `near=` the
// fog-space value that still drives the sky filter - adds `at_far=`, the fog factor at the clip (1.000: nothing
// the clip cuts can show), and its `c={start,end,clip}` pastes as a WorldFogSetting row. `status` in a scene with
// world-unit rows adds one `op=status world_setting=<i> group=scene|storm name=... start= end= clip= c=` line per
// row, 0-3 and 8-11.
//
// Refusals, rc=1, never echoing the typed words:
//   op=set result=error error=bad_argument usage=...      a band, colour or max out of range or not a number
//   op=<sub> result=error error=no_scene                  no scene loaded
//   op=unknown result=error error=unknown_subcommand usage=...
int32_t DistanceFogConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_DISTANCE_FOG_CONSOLE_H
