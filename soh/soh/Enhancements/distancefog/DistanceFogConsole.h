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
//                       vanilla ones included. clip= sets the far clip (100..32767, past vanilla's 12800 since
//                       sturdy-bassoon#227); without it the clip stays the scene's own. max= is vanilla's and is
//                       refused here
//   edge ...            boundary fog (sturdy-bassoon#169; WorldFog.h): under world-unit fog only, geometry also
//                       fogs by how far its world position lies past the playable area, ramping from b0 world
//                       units outside it to full at b1. Its own override, beside the band's:
//     edge [status]               read it
//     edge <b0> <b1>              the ramp (0 <= b0 < b1 <= 32767); the rectangles stay the scene's or the hand one
//     edge rect <x0> <z0> <x1> <z1> [<b0> <b1>]
//                                 one rectangle by hand in place of the scene's, in any scene (x0 < x1, z0 < z1);
//                                 without a ramp it keeps the live one, and with none at all it is refused
//     edge off                    no boundary term, even in a scene with one
//     edge scene                  hand the boundary back to the scene's
//                       Each prints `op=edge result=ok action=status|ramp|rect|off|scene edge=on|off|none
//                       kind=world|vanilla active=0|1 rects=<n> rect<i>=x0,z0,x1,z1 ... rect_src=scene|hand
//                       b0= b1= ramp_src=scene|override link=x,z past=<d> b=<0..1> c={{x0,z0,x1,z1},...},b0,b1`:
//                       edge=none when there are no rectangles anywhere, active=1 when the term draws this frame,
//                       past= Link's distance past the boundary, b= the ramp there, c= the boundary as a scene's
//                       C (its WorldFogRect rows, then the ramp). With no ramp anywhere b0= and b1= read none,
//                       with no rectangles past= and b= read none, and with no player all three of link= past= b=.
//                       `status` adds the same line as op=status. `fog off` hands back the band only; a scene
//                       load hands back both.
//                       Refusals: error=bad_argument, error=no_ramp for a rect with no ramp anywhere, and
//                       error=unknown_subcommand for a word that is none of the above
//   haze ...            the horizon haze band (sturdy-bassoon#168; HorizonHaze.h): under world-unit fog only, a band of
//                       the fog colour over the sky and under the world, opaque up to H degrees above the eye's
//                       horizontal plane and fading out over a further F. Its own override, beside the band's:
//     haze [status]               read it
//     haze <H> <F>                height and fade, degrees, 0..45 each (decimals allowed), in place of the scene's;
//                                 also switches it back on
//     haze off                    no band, even under world-unit fog
//     haze on                     undo off, keeping any <H> <F>
//     haze scene                  hand height and fade back to the scene's, and on
//                       Each prints `op=haze result=ok action=status|set|off|on|scene haze=on|off kind=world|vanilla
//                       drawn=0|1 draws=<n> h=<H> f=<F> top=<H+F> src=scene|override scene_h= scene_f=
//                       color=r,g,b|none fog=r,g,b c=WorldFog_SetHaze(<H>f,<F>f)`: kind= whether world-unit fog is
//                       live (the band draws only then), drawn= whether the last frame drew it, draws= the ticks
//                       drawn since boot, color= the colour it last drew with, fog= lightCtx's fog colour now, c=
//                       the scene's InitScene call. drawn= and color= are the last frame's. `status` adds the same
//                       line as op=status. `fog off` leaves the haze alone; a scene load hands it back.
//                       Refusals: error=bad_argument and error=unknown_subcommand
//   sky ...             sky-colour fog (sturdy-bassoon#226; WorldFog.h, an experiment, DX11 only): fogged pixels blend
//                       toward the sky behind them, a copy of the frame taken before the rooms draw, instead of the fog
//                       colour. Only the colour changes. Its own setting, kept across scene loads (unlike the band
//                       and the edge), off at launch:
//     sky [status]                read it
//     sky on | sky off            switch it, on with the last method (mirror at launch)
//     sky mirror | sky clamp      choose the method and switch it on: mirror reads the copy as it is, so fogged ground
//                                 below the horizon takes the skybox's mirrored sky; clamp gives it the sky just above
//                                 the horizon in its column
//     sky dump [<path-prefix>]    at the end of the next frame that copied the sky, write <prefix>-copy.ppm and
//                                 <prefix>-frame.ppm: the copy and the finished frame (before ImGui), same frame, 8-bit
//                                 RGB, pixel for pixel. Default prefix <TEMP>/skyfog-<n>. Refused with sky off
//     sky hold on|off             a measurement aid: keep fogging toward the copy already taken instead of a fresh one
//                                 each frame, so captures a moment apart see the same sky in their fog (the sky itself
//                                 still drifts). For a camera that does not move: the horizon is the held copy's
//                       Each prints `op=sky result=ok action=... sky=off|mirror|clamp method=mirror|clamp fb=<n>
//                       copies=<n> armed=0|1 size=<w>x<h> origin=<x>,<y> msaa=<n> horizon_row=<row|none>
//                       horizon=<a>,<b>,<c> dumps=<n> dump_ok=0|1 dump_pending=0|1 hold=0|1`: armed=1 when the last
//                       rendered frame armed sky fog (with hold=1, from the held copy), size= the copy (the internal
//                       resolution), origin= where it came from in the render target, horizon_row= clamp's horizon
//                       in the copy at its centre column, horizon= the line a x + b y + c over render-target pixels
//                       (above it > 0). `status` adds it as op=status.
//                       Refusals: error=sky_off (a dump with it off), error=bad_argument, error=unknown_subcommand
//   rows <start> <end> <clip>
//                       a test aid (sturdy-bassoon#227; WorldFog_SetSceneRows): every world-unit row of the loaded
//                       scene becomes this one, in memory, until the next scene load. Unlike `world`, which writes
//                       band and clip after Environment_Update, it goes through the scene's own path (the rows'
//                       range checks, the blend, Environment_Update's clamp), as an export of the row would. Prints
//                       the usual line as op=rows. Refusals: error=bad_argument, error=no_world_rows
//   znear [status] | znear off | znear <n>
//                       the near clip plane (sturdy-bassoon#227), 1..60 in place of View_Init's 10, to judge distant
//                       z-fighting against close-up clipping. z_camera.c's CAM_EYE_CEILING_CLEARANCE assumes 10, so
//                       a larger near cuts into ceilings under covered floors. Prints `op=znear result=ok
//                       action=status|set|off znear=<now> src=override|game default=10`. A scene load hands it back;
//                       `fog off` leaves it alone. Refusals: error=bad_argument
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
