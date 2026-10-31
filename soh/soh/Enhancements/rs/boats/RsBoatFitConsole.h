#ifndef SOH_RS_BOAT_FIT_CONSOLE_H
#define SOH_RS_BOAT_FIT_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The fit-out's subcommands of `boat` and `agenttest boat` (sturdy-bassoon#263, RsBoatFit.h). RsBoatConsole_Run hands
// them here. All work with no scene: the store is the save file's, not the scene's. Every boat already in the scene
// reads its fit-out each tick, so a change shows at once.
//
// `args[0]` is the subcommand:
//   fitout [<kind>]  one `fitout kind=<k> owned=0|1 <slot>=<tier>/<name|-> ... <look>=<option> top=<x100>
//                    accel=<x100>` line per kind (or the one named), then `op=fitout result=ok`. A slot's name is
//                    its tier's (`-` for an empty hotspot); top and accel are the fit-out's stats, 0 where it leaves
//                    the boat's own standing
//   fitout hud [on|off]
//                    the debug readout under the steering label (saved). `op=fitout result=ok hud=0|1`
//   fitout reset     every kind back to unowned and its defaults, as a new save. `op=fitout result=ok reset=1`
//   upgrades         one `upgrade name=<n> unlock_flag=<f> unlocked=0|1 check=<result> fail_index=<i>
//                    fits=<kind>.<slot|own>:<tier>,... costs=rupees:<n>,... placeholder=0|1` line per upgrade (check:
//                    what building it now would answer), then `op=upgrades result=ok count=<n>`
//   build <upgrade>  builds it as a shipwright would: its unlock, the boat owned, its requirements, then pays.
//                    `op=build result=ok upgrade=<n> rupees=<after>`
//   grant <upgrade>  builds it free, as a quest reward does. `op=grant result=ok upgrade=<n>`
//   look <kind> [<look> [<option>]]
//                    reads (no option) or swaps a look, free. `op=look result=ok kind= <look>=<option> options=a,b,..`
//   own <kind> 0|1   TEST AID: owns or disowns the kind outright, no rules. `op=own result=ok kind= owned=0|1`
//   fit <kind> <slot> <tier>
//                    TEST AID: sets the tier outright, up or down, no rules. `op=fit result=ok kind= slot= tier=`
//
// Refusals, each with rc 1:
//   op=<build|grant> result=error error=<unknown|already|locked|not_owned|requirement|cant_afford>
//                    upgrade=<n> fail_index=<i>   the rule that refused it (RsBoatFitResult; fail_index names the
//                                                 requirement or the cost)
//   op=look result=error error=<not_owned|requirement> kind=<k>
//                                                 a look on a boat the player does not own, or an option whose
//                                                 requirements do not hold
//   op=<sub> result=error error=bad_value         a kind, slot, look, option, upgrade or number that is no such thing
//   op=usage result=error usage="..."             a wrong number of words
//
// Returns -1 for a subcommand that is not one of these, so RsBoatConsole_Run carries on with its own.
int32_t RsBoatFitConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

// The grammar, for `boat`'s usage line and help text.
#define RS_BOAT_FIT_GRAMMAR                                                                                        \
    "fitout [<kind>] | fitout hud [on|off] | fitout reset | upgrades | build <upgrade> | grant <upgrade> | "         \
    "look <kind> [<look> [<option>]] | own <kind> 0|1 | fit <kind> <slot> <tier>"

#endif // SOH_RS_BOAT_FIT_CONSOLE_H
