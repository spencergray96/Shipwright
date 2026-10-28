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
// in that table. The actor is only the carrier: this file plays each row's sound itself, so the row's
// own volume applies.
//
// TWO KINDS OF ROW (2026-10-28). A row plays either an OoT sfx through the engine, as above, or an external
// file - an RS sound from the OSRS wiki, or any WAV/FLAC - through the ambience mixer (RsAmbienceMixer.h),
// which adds its own voices to SoH's finished output. File rows loop, or play one of several files at random
// intervals the way RS's random ambient sounds do. They share the radius and gain; they take no env channel
// and do not count against the cap. Files come from rs/sfx/ in any mounted archive, which
// `npm run fetch:rs-sfx` writes as mods/rs-sfx.o2r. How to add rows at scale: sturdy-bassoon
// docs/reference/AUDIO_SYSTEM.md, "The ambience mixer".
//
// THE RADIUS AND GAIN. Each row reaches as far as its radius (RS tiles, XZ from Link; seeded from the
// loc's RS radius), at full volume within a tile and fading linearly to nothing at the edge, and is
// as loud as its gain (0..1). Without them every sound carried the engine's 250 tiles.
// `rsambience radius|gain` tunes them live and unsaved; RsAmbience.cpp's CapToNearest has why.
//
// THE CAP. Only the nearest N OoT-sound emitters in range of Link may ask for their sound (CVAR "RsAmbienceMax",
// default 2, `rsambience max`; file rows have their own 8). Without it the env bank's 3 channels went to the
// water sounds every time and the fires never played, even beside Link: RsAmbience.cpp's CapToNearest has the
// arithmetic.
//
// THE CVAR. CVAR_ENHANCEMENT("RsAmbience"), default on: `rsambience on|off` writes and saves it and
// spawns or kills the emitters live, so an A/B needs no reload.
//
// `rsambience` - one parser and renderer behind two sinks, the RegionConsole.h arrangement: the
// human `rsambience ...` command and `agenttest rsambience ...`, which writes each line as an
// `[agenttest] rs_ambience <line>` marker. `args[0]` is the subcommand:
//   status (or no argument)
//       op=status enabled=0|1 scene=0xNN here=0|1 spawned=N max=M env_requested=R env_playing=P env_channels=C
//               mixer_voices=V mixer_audible=A mixer_loaded=L mixer_batches=B   (one line in the output)
//           here=1 when the current scene is the one the table is for. env_requested is the env
//           bank's (BANK_ENV, the bank En_River_Sound's sounds live in) entry list - every sound asking
//           to play this tick, cap 20 usable; env_playing those that hold a channel, of env_channels.
//           These count EVERY env sound in the scene, not only this feature's, so they read the
//           same budget anywhere - the fire-pressure measurement in ENGINE_BUDGETS.md § Sound effects.
//           mixer_*: voices the mixer holds (fading ones too), those it added to its last batch, files
//           decoded, and batches mixed since start - if that stops climbing, the audio-thread hook is not
//           being called
//       then one line per table row:
//       emitter=<name> sound=<name> tile=<x>,<y> pos=<x>,<y>,<z> dist=<units> spawned=0|1 active=0|1
//               requested=0|1 playing=0|1 radius=<tiles> gain=<0..1> vol=<0..1>   (one line in the output)
//           pos is where it spawned (y from a floor raycast); dist is from Link along the ground (XZ),
//           in OoT units (40 a tile). active=1 when it is in range and among the nearest N this tick;
//           requested/playing are this emitter's own entry in the env list; radius and gain are the
//           live values, vol what it asks for this tick (gain x the fade; 0 when not active).
//           A file row's sound is file:<id>|<id>..., requested=1 when it was handed to the mixer this tick
//           and playing=1 when the mixer's last batch carried it; the line ends files=ok|missing|unknown
//           (missing: no mounted archive holds one of its files - run npm run fetch:rs-sfx) and plays=<n>,
//           the one-shots started so far
//   on | off
//       op=on|off result=ok enabled=0|1 spawned=N - writes the CVar, and spawns or kills now when in
//       the table's scene
//   max [<n>|default]
//       op=max result=ok max=N - with no argument, a read. n is 0..8 (the table's size); anything else
//       is op=max result=error error=range, rc=1
//   radius <emitter|all> [<tiles>|default]
//   gain <emitter|all> [<0..1>|default]
//       op=radius|gain result=ok emitter=<name> radius|gain=<value> - one line per row named, a read
//       without a value. Unsaved: a scene reload keeps it, a game restart goes back to the table.
//       An unknown emitter is error=emitter, a value outside 0.5..64 tiles or 0..1 is error=range
//       min= max=, both rc=1 and nothing written
//
// Returns 0 for a read or a successful write, 1 for a refused argument or an unknown subcommand.
int32_t RsAmbienceConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_AMBIENCE_H
