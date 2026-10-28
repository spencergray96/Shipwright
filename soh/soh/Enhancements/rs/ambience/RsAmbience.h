#ifndef SOH_RS_AMBIENCE_H
#define SOH_RS_AMBIENCE_H

#include <stdint.h>
#include <string>
#include <vector>

// Area ambience, Phase A (sturdy-bassoon#275): an EXPERIMENT, not a system. It asks one question -
// does a handful of looping positional sounds make an RS area feel more alive, or is it noise? -
// and is built to be judged by ear and then kept, tuned or deleted. Background and the reasoning:
// sturdy-bassoon docs/notes/2026-10-27-area-ambience.md (#93).
//
// WHAT IT DOES. On Link's first tick in the F2P overworld it spawns one vanilla En_River_Sound per
// row of a hand-picked table: OoT's own ambient emitter, the actor Nintendo places for fountains,
// waterfalls, the market crowd and torches. Its flags keep it updating off screen and it re-requests
// its looping sfx every tick, which is the whole mechanism - no new actor, no export, no audio
// change. The rows are a few of RS's real ambient emitters on the Lumbridge square (the cache's own
// loc placements, listed by the #93 note's EmitterList.java), each given the nearest sound the
// actor's param table offers. Not every emitter, and not RS's sounds: OoT has no wind or birdsong
// in that table, and the sound's audible range is the vanilla sfx's, not RS's radius.
//
// THE CAP. Only the nearest N emitters to Link may ask for their sound (CVAR "RsAmbienceMax", default 2,
// `rsambience max`). Without it the env bank's 3 channels went to the water sounds every time and the
// fires never played, even beside Link: RsAmbience.cpp's CapToNearest has the arithmetic.
//
// THE CVAR. CVAR_ENHANCEMENT("RsAmbience"), default on: `rsambience on|off` writes and saves it and
// spawns or kills the emitters live, so an A/B needs no reload.
//
// `rsambience` - one parser and renderer behind two sinks, the RegionConsole.h arrangement: the
// human `rsambience ...` command and `agenttest rsambience ...`, which writes each line as an
// `[agenttest] rs_ambience <line>` marker. `args[0]` is the subcommand:
//   status (or no argument)
//       op=status enabled=0|1 scene=0xNN here=0|1 spawned=N max=M env_requested=R env_playing=P env_channels=C
//           here=1 when the current scene is the one the table is for. env_requested is the env
//           bank's (BANK_ENV, the bank En_River_Sound's sounds live in) entry list - every sound asking
//           to play this tick, cap 20 usable; env_playing those that hold a channel, of env_channels.
//           These count EVERY env sound in the scene, not only this feature's, so they read the
//           same budget anywhere - the fire-pressure measurement in ENGINE_BUDGETS.md § Sound effects
//       then one line per table row:
//       emitter=<name> sound=<name> tile=<x>,<y> pos=<x>,<y>,<z> dist=<units> spawned=0|1 active=0|1
//               requested=0|1 playing=0|1   (one line in the output)
//           pos is where it spawned (y from a floor raycast); dist is from Link, in OoT units (40 a
//           tile). active=1 when it is among the nearest N this tick; requested/playing are this
//           emitter's own entry in the env list
//   on | off
//       op=on|off result=ok enabled=0|1 spawned=N - writes the CVar, and spawns or kills now when in
//       the table's scene
//   max [<n>|default]
//       op=max result=ok max=N - with no argument, a read. n is 0..8 (the table's size); anything else
//       is op=max result=error error=range, rc=1
//
// Returns 0 for a read or a successful write, 1 for a refused argument or an unknown subcommand.
int32_t RsAmbienceConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_AMBIENCE_H
