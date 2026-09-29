#ifndef SOH_STATIC_BAKE_CONSOLE_H
#define SOH_STATIC_BAKE_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for the static bake's runtime switch
// (sturdy-bassoon#142), the arrangement RegionConsole.h describes:
//
//   - the human `staticbake ...` command, registered in StaticBakeConsole.cpp, and
//   - `agenttest staticbake ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] staticbake <line>` marker in agent-log.txt.
//
// Why it exists: a baked/interpreted picture A/B across two sessions does not line up - a session
// at single-digit fps lands its camera 3-15 units away from one at full speed. Flipping the switch
// inside one session keeps the camera, the time and everything else still, and turning it back on
// replays the existing bakes rather than re-recording them.
//
// Every successful line is `op=<sub> result=ok` and the same seven fields: `active=` whether the bake
// runs (StaticBake_IsActive: the switch, and 0 on a backend that cannot bake), `setting=` the saved
// setting (sturdy-bassoon#153; it differs from active= when SOH_STATIC_BAKE decided the session, after
// an agent-loop switch, or on a backend that cannot bake), `registered=` display lists offered by the
// loaded scene's rooms, `baked=` those with a GPU buffer, `rejected=` those the recorder refused
// (interpreted for good), `supported=` whether this rendering backend can bake at all (DX11 only), `sort=` whether
// recordings are ordered by material (sturdy-bassoon#158). baked + rejected < registered means some
// have not been drawn since they were registered or invalidated. An unknown subcommand prints
// `op=unknown result=error error=unknown_subcommand usage=...` without echoing the word.
//
// `args[0]` is the subcommand; none is `status`:
//   status    read the switch and the registry
//   on        switch the bake on. Registered rooms record on their next draw, or replay if baked
//   off       switch it off. Every room is interpreted; bakes and registrations are kept
//   From the human command, on/off also save setting= (sturdy-bassoon#153), so a restart starts
//   there. From `agenttest staticbake` they switch for the session only - StaticBakeConsole_RunSession
//   - so an agent's A/B, or a run that dies halfway through one, never leaves the owner's bake off.
//   rebake    send every baked room back to be recorded again on its next draw, releasing its
//             buffer and textures - what a shader-cache clear, the alt-assets toggle and a filter
//             change do. For re-recording under a condition (cache pressure, a time of day)
//   sort on|off  order each recording by material (the default), or keep list order, and send every
//             baked room back to be recorded that way (sturdy-bassoon#158). Session only from both
//             sinks: for comparing the two orders, and the way back if content depends on list order
//
// `sort` without on or off prints `op=sort result=error error=bad_argument usage=sort(on|off)`.
//
// Returns 0 for all five, 1 for an unknown subcommand or a bad sort argument - so `rc=` on the agent
// loop's cmd marker is the pass/fail bit.
int32_t StaticBakeConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);
int32_t StaticBakeConsole_RunSession(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_STATIC_BAKE_CONSOLE_H
