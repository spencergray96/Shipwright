#ifndef SOH_RS_BALLOON_CONSOLE_H
#define SOH_RS_BALLOON_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one renderer behind both console surfaces for Rs_Balloon (sturdy-bassoon#274), RsBoatConsole's arrangement:
// the human `balloon ...` command joins `lines` into the ImGui console, and `agenttest balloon ...` writes each as
// its own `[agenttest] rs_balloon <line>` marker.
//
// Balloons are numbered OLDEST FIRST (`idx`), so spawning another never renumbers the ones already there. A command
// that takes `[<idx>]` defaults to 0.
//
// `args[0]` is the subcommand:
//   list            one `balloon idx=<i> x= y= z= yaw=<binang> alt=<x100> vy=<x100> speed=<x100> throttle=<x100>
//                   lift=<x100> tiller=<x100> controls=0|1 landed=0|1 on_water=0|1 ground=<y> height=<over the
//                   ground> ceiling=<y> blocked=<none|wall|ground|overhead|ceiling> blocks=<n> link_on=0|1
//                   away=<ticks> abandoned=0|1 stand_dist=<n> cam=0|1 map_arrow=<drawn|controls|no_minimap>` line
//                   per live balloon, then `op=list
//                   result=ok count=<n> link_x= link_y= link_z= link_yaw= link_floor_bg=<n> link_ground=0|1
//                   link_input_disabled=0|1 link_can_take=0|1 controls_in_reach=0|1 a_label=<DO_ACTION_*>
//                   link_ledge=<climb type>/<delay ticks>/<ledge height> link_health=<n> drive_ticks=<n>
//                   abandon=<sink|hang|home> hud=<hidden|not_steering|steering|steering_balloon>`.
//                   alt: the basket's bottom exactly (world y is it rounded); vy: climb rate, + up; throttle: what
//                   the controls asked for last tick (ahead 0..100, braking below 0); lift: the climb (100) or sink
//                   (-100) button; ground: what it stands on or is over (open water's surface counts); ceiling: the
//                   highest it may climb (`tune ceiling` over its launch spot's ground); blocked: why the last move or
//                   climb was refused, blocks: refusals since spawn; link_on: Link stands on that basket's floor or
//                   rail; away: ticks since he last did; abandoned: the `abandon` behaviour is running; stand_dist:
//                   Link's xz distance from the stand point; cam: the steering camera holds the view;
//                   link_can_take: Link is free to take controls (RsSteer_LinkCanTake); controls_in_reach: A would
//                   take some balloon's now; a_label: the A button's do-action id (0 is none, as at the controls);
//                   link_ledge: Player's ledgeClimbType, ledgeClimbDelayTimer and yDistToLedge; link_health: his
//                   health in sixteenths of a heart, for fall damage; hud: what the steering label drew last frame;
//                   map_arrow: what the last minimap draw did with that balloon's orange arrow, as `boat list`'s
//   spawn [<x> <z> [<yaw> [<height>]]]
//                   puts a balloon down: at (x, z), or 200 units in front of Link facing his way, on the ground (or
//                   open water) there, or <height> units over it, hovering. `op=spawn result=ok idx=<i> x= y= z= yaw=
//                   landed=0|1`
//   board [<idx>]   stands Link at the balloon's controls, a few units over the floor: what climbing in does, for a
//                   loop that cannot. `op=board result=ok x= y= z=`
//   controls on|off [<idx>]
//                   takes or lets go of the controls as A and B do. `op=controls result=ok controls=0|1`
//   drive <throttle> <turn> <lift> <ticks>
//                   for the next <ticks> ticks (0..1200) the balloon at the controls reads these instead of the pad:
//                   throttle -1..1 (ahead; below 0 brakes), turn -1..1 (+ left), lift -1..1 (+ climbs, - sinks, as a
//                   share of the top rate). `drive 0 0 0 0` hands it back at once. `op=drive result=ok throttle=<x100>
//                   turn=<x100> lift=<x100> ticks=<n>`
//   tune [default] | tune <knob> <value>
//                   the handling (saved, read every tick): climb and sink (top rates, units a tick; 3 and 3), lift
//                   (vertical speed gained or lost a tick: the inertia; 0.15), drift (sink rate with no button held;
//                   0, a hover), fwd (top speed; 8), accel (0.2), drag (lost a tick coasting; 0.06), brake (lost a tick
//                   with the stick pulled back; 0.3), turn (binang a tick at full stick; 200), ceiling (how far over
//                   its launch spot it may climb; 1200), abandonsink (sink rate once left alone; 1). `op=tune
//                   result=ok climb= sink= ...`. Works with no scene
//   cam [on|off|default] | cam height|dist|ease <n>
//                   the steering camera (saved), as `boat cam`: the eye's height over the floor (-200..1000, default
//                   120), its distance behind Link (80..1500, default 240), the ease in percent a tick (5..100,
//                   default 25). `op=cam result=ok cam=on|off height= dist= ease=`. Works with no scene
//   abandon [sink|hang|home]
//                   what a balloon does once Link has been out of it for 2 s (saved): sink (the default) sinks it at
//                   `abandonsink` until it lands, if it is in the air; hang holds it where it is; home puts it back
//                   where it was put down, landed. `op=abandon result=ok abandon=`. Works with no scene
//   field [on|off]  whether Hyrule Field gets a balloon in front of Link on entry (saved; default on while the
//                   prototype is on its branch). `op=field result=ok setting=on|off`. Works with no scene
//   clear           kills every balloon. `op=clear result=ok killed=<n>`
//
// Refusals, each with rc 1:
//   op=balloon result=error error=no_scene   no play state (title screen, file select); tune, cam, abandon and
//                                            field still work
//   op=<sub> result=error error=bad_value    a number that does not parse or is out of range, a knob or mode not
//                                            named; tune and cam add the ranges, drive its usage
//   op=<sub> result=error error=no_balloon idx=<i> count=<n>
//                                            board or controls named a balloon that is not there
//   op=spawn result=error error=no_ground x= z=
//                                            nothing to stand on under that point
//   op=controls result=error error=not_aboard
//                                            controls on with Link not standing in that basket
//   op=usage result=error usage="..."        an unknown subcommand or a wrong number of words
int32_t RsBalloonConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_BALLOON_CONSOLE_H
