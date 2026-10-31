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
//                   `t=<frame> sm=<0|1> n=<n> ids=<10> f0=<10> f1=<10> num=<10> deck=<0|1>`, each a comma list over
//                   LIGHTS_SHADOW_CASTS_MAX entries (`-` past the n used). sm=0: the entries are the bound
//                   slots, as vanilla draws them; sm=1: the smoothed casts, fading lights included, then the
//                   sun's two. ids: each entry's node, its pool index, or `s` for a directional light;
//                   f0/f1: the alpha its shadow drew at under each foot (0 for none); num: foot 0's strength
//                   term before the scale and clamp. The last two entries are the ones ActorShadow_DrawFeet
//                   treats as the sun. deck=1: Link was over a boat and drew its plain circle instead
//                   (RsVehicleShadow_Mode, #267), so every f0/f1 is 0. `clear` empties the record:
//                   `op=shadow result=ok cleared=1`
//   shadowsmooth [on|off|default]
//                   sets the smoothed feet shadows (saved; default on; grid-tool scenes only), then, or bare,
//                   `op=shadowsmooth result=ok shadowsmooth=on|off default=on override=0|1 gate=<0|1>
//                   rate=<k> actors=<n> snaps=<n> full=<n> nobind=<n> cut=<n>`: gate is the last tick's;
//                   actors the table slots in use; since the scene began, snaps counts fresh starts, full the
//                   draws left unsmoothed for a full table, nobind those whose bind could not be identified,
//                   cut the fading lights dropped early to make room
//   shadowrate [<k>|default]
//                   the fraction a tick each shadow moves toward its light's strength, 0.01..1, for THIS
//                   SESSION ONLY (1 is unsmoothed); then, or bare, `op=shadowrate result=ok rate=<k> default=<k>`
//   shadowpulse [on|off|default|<amount> <seconds>]
//                   on|off sets the pulse (saved; default on; only while the smoothing is): each smoothed shadow
//                   breathes by <amount> of its strength over <seconds>, each light on its own phase. Off is the
//                   plain fade. <amount> <seconds> tunes it for THIS SESSION ONLY (0..0.9, 0.5..30 s); default
//                   clears both. Then, or bare, `op=shadowpulse result=ok shadowpulse=on|off default=on
//                   override=0|1 gate=<0|1> amount=<a> seconds=<s>`
//   shadowinterp [clear|on|off|default|pair <on|off|default>|probe <link|id>]
//                   bare: frame interpolation's effect on one actor's shadow matrices (sturdy-bassoon#254, the probe
//                   in frame_interpolation.cpp), over the last LIGHTS_SHADOW_RECORD_TICKS ticks that recorded one,
//                   oldest first: `op=shadowinterp result=ok ticks=<n> min_size=<s> min_cos=<c> dips=<n>
//                   max_slide=<u> world=on|off pair=on|off probe=link|0x<id> scene=0x<hex>`, then one line a tick:
//                   `t=<frame> id=0x<id> yaw=0x<hex> dyaw=<deg>|- m=<n> ws=<n> fr=<n> cmp=<n> size=<s> cos=<c>
//                   slide=<u> jump=<u>`: the actor's shape yaw as it drew, its change from the tick before (`-`
//                   when that tick is not in the record), the matrices its shadowDraw recorded and how many of them
//                   in world space, the in-between frames drawn before the tick, the matrix-frames compared, the
//                   smallest in-between size and long-axis cosine against what the tick drew (1 and 1 are
//                   untouched; size is a shadow's linear scale, cos -1 is reversed), the most an in-between
//                   shadow's floor offset from its actor differed from the tick's (slide, units), and the
//                   furthest an in-between frame drew the actor from where the tick drew it (jump). dips counts
//                   the ticks under size 0.9. Empty at 20 fps, where nothing is interpolated.
//                   `clear` empties the record: `op=shadowinterp result=ok cleared=1`.
//                   on|off|default sets world-space interpolation of the feet and circle shadows (saved; default
//                   on; every scene); `pair on|off|default` sets pairing each foot shadow with its own light's
//                   (saved; default on; every scene); `probe link|<id>` picks the probed actor for THIS SESSION
//                   ONLY (the first of that id drawn each tick; 0x-hex ok) and clears the record. Each then prints
//                   `op=shadowinterp result=ok world=on|off default=on override=0|1 pair=on|off pair_default=on
//                   pair_override=0|1 probe=link|0x<id>`
//
// Returns 0 on success (and for the reads), 1 for a refused operation: an unknown subcommand, an extra
// argument or a bad value (`op=<sub> result=error error=bad_value`).
int32_t LightsConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_LIGHTS_CONSOLE_H
