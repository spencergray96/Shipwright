#ifndef SOH_RS_REGION_CONSOLE_H
#define SOH_RS_REGION_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for the region setting
// (sturdy-bassoon#94), the arrangement QuestConsole.h describes and for the same reason:
//
//   - the human `region ...` command, registered here, which joins `lines` with newlines into the
//     ImGui console, and
//   - `agenttest region ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] rs_region <line>` marker in agent-log.txt.
//
// One implementation, two sinks, so the two can never drift. Every line is single-line and
// greppable (`key=value` fields, quoted only where a value can contain a space).
//
// `args[0]` is the subcommand:
//   get                   the live convention, and WHERE IT CAME FROM - `source=file` means a
//                         save's `rsPrefs` section set it, `source=default` means nothing has.
//                         Both are legitimate, and only one is evidence that persistence worked
//   set uk | set us       the write. A bad argument is an outcome-class refusal (rc=1) and never
//                         an assert - the console pre-validates, so RsPrefs_SetFloorConvention's
//                         own debug assert stays unreachable from here
//   toggle                the other convention, for an A/B without retyping
//   expand <text...>      THE SUBSTITUTION GRAMMAR, DIRECTLY - this feature's `quest parse`.
//                         Prints the input expanded under EVERY convention, one line each, so a
//                         run asserts both readings from one command; a malformed token is the
//                         error kind and a byte offset, and the input is never echoed back.
//                         A '"' in the input is refused (`error=quote`) - this is the one handler
//                         fed arbitrary typed text, so it is the one place the `key="value"` field
//                         contract has to be defended rather than inherited from the prose gates
//   overlay [on|off]      the on-screen overlay switch (RegionOverlay.h). Reports `enabled=` and
//                         what the last frame drew, since a marker cannot see a pixel
//
// Returns 0 when the operation succeeded (or for read-only subcommands), 1 otherwise - so `rc=` on
// the agent loop's cmd marker is the pass/fail bit.
int32_t RsRegionConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_REGION_CONSOLE_H
