#ifndef SOH_RS_BOAT_CONSOLE_H
#define SOH_RS_BOAT_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one renderer behind both console surfaces for Rs_Boat (sturdy-bassoon#256), RsFireConsole's
// arrangement: the human `boat ...` command joins `lines` into the ImGui console, and `agenttest boat ...`
// writes each as its own `[agenttest] rs_boat <line>` marker.
//
// Boats are numbered OLDEST FIRST (`idx`), so spawning another never renumbers the ones already there. A
// command that takes `[<idx>]` defaults to 0.
//
// `args[0]` is the subcommand:
//   list            one `boat idx=<i> x= y= z= yaw=<binang> speed=<x100> tiller=<x100> helm=0|1 on_water=0|1
//                   surface=<y> blocked=<none|no_water|shallow|wall|headroom> blocks=<n> link_on=0|1
//                   helm_dist=<n> cam=0|1` line per live boat, then `op=list result=ok count=<n> link_x= link_y=
//                   link_z= link_yaw= link_floor_bg=<n> link_input_disabled=0|1 link_swimming=0|1
//                   link_can_take_helm=0|1 drive_ticks=<n> hud=<hidden|not_steering|steering>`.
//                   blocked: why the last move was refused (RsBoatBlock), blocks: refused moves since spawn;
//                   link_on: Link is standing on that boat's deck; helm_dist: Link's xz distance from its
//                   tiller; cam: the steering camera holds the view; link_swimming: PLAYER_STATE1_IN_WATER;
//                   link_can_take_helm: A at the tiller would take it (RsBoat_LinkCanTakeHelm); hud: what the
//                   steering label drew on the last frame (RsBoatHud.h)
//   spawn [<x> <z> [<yaw>]]
//                   puts a boat down: at (x, z), or 220 units in front of Link facing his way, on the water's
//                   surface there. `op=spawn result=ok idx=<i> x= y= z= yaw=`
//   probe <x> <z>   what a boat would read there: `op=probe result=ok x= z= water=0|1 surface=<y> floor=<y>
//                   depth=<surface - floor>`. For finding a shore, or a spot deep enough to spawn one. Every
//                   boat's collision counts as floor, so probe away from them
//   water <x0> <z0> <x1> <z1> <y> | water clear
//                   adds a waterbox to the loaded scene for this visit (a grid-tool map has none), surface y over
//                   that xz rectangle, in every room; every boat draws its surface as a translucent sheet. A
//                   second `water` replaces the first. Coordinates within +-16000. A scene load or `water clear`
//                   takes it away. `op=water result=ok x0= z0= x1= z1= y= boxes=<n>` (boxes: the scene's own plus
//                   this one); `op=water result=ok cleared=0|1`
//   board [<idx>]   stands Link at the boat's tiller, a few units above the deck: what walking or climbing aboard
//                   does, for a loop that cannot steer Link across the water. `op=board result=ok x= y= z=`
//   helm on|off [<idx>]
//                   takes or lets go of the helm as A and B do. `op=helm result=ok helm=0|1`
//   drive <throttle> <turn> <ticks>
//                   for the next <ticks> ticks (0..1200) the boat at the helm reads throttle and turn (each
//                   -1..1, turn + is left) instead of the pad; `drive 0 0 0` hands it back at once.
//                   `op=drive result=ok throttle=<x100> turn=<x100> ticks=<n>`
//   lakehylia [on|off]
//                   whether Lake Hylia gets a boat on entry (saved; default on while the prototype is on its
//                   branch). `op=lakehylia result=ok setting=on|off`
//   lakelevel [full|drained]
//                   sets or clears EVENTCHKINF_RAISED_LAKE_HYLIA_WATER, the flag that fills Lake Hylia for an adult
//                   Link (a child always has the full lake). Read on scene load, so it shows on the next entry;
//                   kept by `agenttest save <n>`. `op=lakelevel result=ok raised=0|1 adult=0|1 in_lake=0|1`
//   cam [on|off|default] | cam height|dist|ease <n>
//                   the steering camera (saved): on or off, the eye's height over the deck (-20..600, default
//                   60), its distance behind Link (80..1000, default 240), and how much of the way to its target
//                   it moves a tick, in percent (5..100, default 25; 100 is a snap). Read every tick, so a change
//                   shows at the helm at once. `op=cam result=ok cam=on|off height= dist= ease=`. Works with no
//                   scene loaded
//   hud [on|off]    the "Not steering" / "Steering boat" label under the C buttons (saved, default on).
//                   `op=hud result=ok hud=on|off shown=<hidden|not_steering|steering>`. Works with no scene
//   clear           kills every boat. `op=clear result=ok killed=<n>`
//
// Refusals, each with rc 1:
//   op=boat result=error error=no_scene      no play state (title screen, file select); lakehylia, cam and
//                                            hud still work
//   op=cam result=error error=bad_value height=..  dist=.. ease=..
//                                            a knob not named, or a value outside its range
//   op=<sub> result=error error=bad_value    a number that does not parse or is out of range (spawn, probe,
//                                            water, drive, an idx); probe, water and drive add their usage
//   op=<sub> result=error error=no_boat idx=<i> count=<n>
//                                            board or helm named a boat that is not there
//   op=spawn result=error error=no_water x= z=
//                                            no waterbox at that point
//   op=helm result=error error=not_aboard    helm on with Link not standing on that boat's deck
//   op=water result=error error=too_many have=<n> max=<n>
//                                            the scene already has as many waterboxes as the copy holds
//   op=usage result=error usage="..."        an unknown subcommand or a wrong number of words
int32_t RsBoatConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_BOAT_CONSOLE_H
