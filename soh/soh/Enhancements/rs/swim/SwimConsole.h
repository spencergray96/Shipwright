#ifndef SOH_RS_SWIM_CONSOLE_H
#define SOH_RS_SWIM_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for swim stamina (sturdy-bassoon#257,
// SwimStamina.h), RegionConsole.h's arrangement:
//
//   - the human `swim ...` command, registered here, and
//   - `agenttest swim ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] rs_swim <line>` marker in agent-log.txt.
//
// `args[0]` is the subcommand. Every write ends with the status line, so a run reads the result of
// the write it made rather than polling for it:
//   status              op=status enabled= phase=idle|dive|sinking|moving budget=<ticks> max=<ticks>
//                       seconds=<clock> length=<s> length_source=default|cvar swimming= grounded=
//                       hold=none|message|cutscene|timer swim_clock=<the main timer is the swim clock>
//                       refuge=none|world|actor drowns=<n> last=none|refuge|point:<name>|voidout
//   on | off            the switch (CVar, saved). Refills the budget. `op=on|off result=ok ...`
//   length <s>|default  the length in seconds, 1..600, or back to the build default (30)
//   budget <ticks>      sets what is left, clamped to [1, max]: a run's way to a drown without
//                       swimming for the whole length
//   drown               drowns him now, whatever the budget. Refused (rc=1) when off or already
//                       drowning
//   refuge here|clear   `here` makes where Link is his refuge (a world position, the boat's stand-in);
//                       standing on anything clears it. `clear` clears it
//   points              one line per drown point: op=points name= scene=0x<id> rs=<x>,<y> unlocked=
//                       in_scene= placed= x= y= z= wet= - `wet=1` is a point in the water, a mistake
//
// Returns 0 for a read or a write that took, 1 for a refusal - so `rc=` is the pass/fail bit.
int32_t RsSwimConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_SWIM_CONSOLE_H
