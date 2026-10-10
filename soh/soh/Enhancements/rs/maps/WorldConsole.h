#ifndef SOH_RS_WORLD_CONSOLE_H
#define SOH_RS_WORLD_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for the world context and the scene picker
// (sturdy-bassoon#173 slice F5; WorldContext.h, SceneMaps.h), RegionConsole.h's arrangement:
//
//   - the human `worldctx ...` command, registered here, and
//   - `agenttest worldctx ...` (agenttest/AgentTest.cpp), one `[agenttest] rs_worldctx <line>` marker
//     per line.
//
// `args[0]` is the subcommand:
//   get                     `op=get ctx=<unset|solo|0xNN> value=<n> source=default|file|scene|console`.
//                           `source=file` is the persistence witness: a save's `rsWorld` section set it
//   set <unset|solo|0xNN>   the write, then the same fields and `from=`. A scene id must be a stitched
//                           scene's; anything else is refused (`error=bad_context`, rc=1)
//   pick <map>              what the scene picker would load for a trip to map <map> from here, now:
//                           `op=pick map= scene=0xNN entrance=0xNN rank=here|world|stitched|solo|neutral
//                           ctx=`, or `result=error error=no_scene` (rc=1) when no scene holds it
//   scenes <map>            every scene holding map <map>: `op=scenes map= count=`, then one
//                           `scene[i]=0xNN world= entrance=0xNN` line each
//
// Event markers the context writes on its own (WorldContext.cpp), not through this renderer:
// `rs_worldctx event=scene scene= scene_world= ctx= was= changed=` on every scene init,
// `rs_worldctx event=set from= to= cause=` on every change, `rs_worldctx event=repaired` on load.
//
// Returns 0 for a read or a write that happened, 1 for a refusal - `rc=` on the agent loop's cmd
// marker is the pass/fail bit.
int32_t RsWorldConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_WORLD_CONSOLE_H
