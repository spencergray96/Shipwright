#ifndef SOH_TEXTURE_MIPS_CONSOLE_H
#define SOH_TEXTURE_MIPS_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for the mipmaps of this mod's own scene
// textures (sturdy-bassoon#146), the arrangement RegionConsole.h describes:
//
//   - the human `mipmaps ...` command, registered in TextureMipsConsole.cpp, and
//   - `agenttest mipmaps ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] mipmaps <line>` marker in agent-log.txt.
//
// Why it exists: a with/without picture A/B across two sessions does not line up the camera; flipping
// the switch inside one session keeps everything else still. A flip clears the texture cache and
// re-records every bake, so the next frame of both paths is drawn the new way.
//
// Every successful line is `op=<sub> result=ok` and the same seven fields: `active=` whether mips
// are built (the switch, and 0 on a backend that cannot), `setting=` the saved setting, `supported=`
// whether this backend builds them (DX11 only), `lists=` display lists named as the mod's own since
// boot, `addrs=` raw addresses those lists have named with G_SETTIMG (palettes too, so an upper bound
// on textures), `mipped=` uploads that really built a chain of more than one level since boot (a
// re-upload after a cache clear counts again; a texture that is not a power of two never does),
// `filter=` the texture filter the renderer runs (three_point, linear or none): mips reach three-point
// and linear, and under none are sampled from the nearest level. An unknown subcommand prints
// `op=unknown result=error error=unknown_subcommand usage=...` without echoing the word.
//
// `args[0]` is the subcommand; none is `status`:
//   status    read the switch and the counters
//   on        build mips for the mod's textures from the next upload; clears the cache so that is now
//   off       upload them with one level, as before
//   From the human command, on/off also save setting=, so a restart starts there. From
//   `agenttest mipmaps` they switch for the session only - TextureMipsConsole_RunSession.
//
// Returns 0 for all three, 1 for an unknown subcommand, so `rc=` is the pass/fail bit.
int32_t TextureMipsConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);
int32_t TextureMipsConsole_RunSession(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_TEXTURE_MIPS_CONSOLE_H
