#ifndef SOH_RS_AMBIENCE_H
#define SOH_RS_AMBIENCE_H

#include <stdint.h>
#include <string>
#include <vector>

// Area ambience (sturdy-bassoon#275, #276): looping and random positional sounds placed in a scene. How it
// came to be and why each part is the way it is: sturdy-bassoon docs/reference/AUDIO_SYSTEM.md § 12, and
// docs/notes/2026-10-27-area-ambience.md (#93).
//
// WHERE EMITTERS COME FROM (#276). On every scene init, the scene's emitters are built from:
//   - its exported sound placements: the grid tool writes each scene's as `<slug>_sounds.inc`, through the
//     aggregate GridToolSounds.inc, and the sound library (tools/sfx/sounds.json) as GridToolSoundLibrary.inc.
//     A placement names a library entry and may override its radius and gain. Positions are scene units.
//   - in the F2P overworld only, a built-in table: Phase A's hand-placed Lumbridge rows (#275), placed by RS
//     tile and naming library entries. They go when the owner has placed the same sounds with the grid tool.
// On Link's first tick each emitter spawns a vanilla En_River_Sound as the carrier of its position.
//
// TWO KINDS OF SOUND. A library entry plays either one of OoT's plain ambient loops through the engine
// (the actor's own sfx, played by this file with the emitter's own volume), or external files - RS sounds
// from the OSRS wiki, or any WAV/FLAC - through the ambience mixer (RsAmbienceMixer.h), which adds its own
// voices to SoH's finished output. Files loop, or one of several plays at random intervals the way RS's
// random ambient sounds do. Files come from rs/sfx/ in any mounted archive (`npm run fetch:rs-sfx` writes
// mods/rs-sfx.o2r).
//
// THE RADIUS AND GAIN. Each emitter reaches as far as its radius (RS tiles, XZ from Link), at full volume
// within a tile and fading linearly to nothing at the edge, and is as loud as its gain (0..1). Without them
// every OoT sound carried the engine's 250 tiles. `rsambience radius|gain` tunes them live and unsaved;
// RsAmbience.cpp's CapToNearest has why.
//
// THE CAP. Only the nearest N OoT-sound emitters in range of Link may ask for their sound (CVAR
// "RsAmbienceMax", default 2, `rsambience max`; file emitters have their own 8). Without it the env bank's
// 3 channels went to the water sounds every time and the fires never played, even beside Link:
// RsAmbience.cpp's CapToNearest has the arithmetic.
//
// THE CVAR. CVAR_ENHANCEMENT("RsAmbience"), default on: `rsambience on|off` writes and saves it and
// spawns or kills the emitters live, so an A/B needs no reload.
//
// `rsambience` - one parser and renderer behind two sinks, the RegionConsole.h arrangement: the
// human `rsambience ...` command and `agenttest rsambience ...`, which writes each line as an
// `[agenttest] rs_ambience <line>` marker. `args[0]` is the subcommand:
//   status (or no argument)
//       op=status enabled=0|1 scene=0xNN here=0|1 emitters=E spawned=N max=M env_requested=R env_playing=P
//               env_channels=C mixer_voices=V mixer_audible=A mixer_loaded=L mixer_batches=B   (one line)
//           here=1 when the current scene has emitters, E of them. env_requested is the env bank's
//           (BANK_ENV, the bank En_River_Sound's sounds live in) entry list - every sound asking to play
//           this tick, cap 20 usable; env_playing those that hold a channel, of env_channels. These count
//           EVERY env sound in the scene, not only this feature's, so they read the same budget anywhere -
//           the fire-pressure measurement in ENGINE_BUDGETS.md § Sound effects. mixer_*: voices the mixer
//           holds (fading ones too), those it added to its last batch, files decoded, and batches mixed
//           since start - if that stops climbing, the audio-thread hook is not being called
//       then one line per emitter of the scene:
//       emitter=<name> sound=<name> from=export|builtin pos=<x>,<y>,<z> dist=<units> spawned=0|1 active=0|1
//               requested=0|1 playing=0|1 radius=<tiles> gain=<0..1> vol=<0..1>   (one line in the output)
//           name is the placement's label, else <library id>-<n>; from says which table it came from. pos
//           is where it stands (a built-in row's y from a floor raycast); dist is from Link along the
//           ground (XZ), in OoT units (40 a tile). active=1 when it is in range and among the nearest N
//           this tick; requested/playing are this emitter's own entry in the env list; radius and gain
//           are the live values, vol what it asks for this tick (gain x the fade; 0 when not active).
//           A file emitter's sound is file:<id>|<id>..., requested=1 when it was handed to the mixer this
//           tick and playing=1 when the mixer's last batch carried it; the line ends
//           files=ok|missing|unknown (missing: no mounted archive holds one of its files - run
//           npm run fetch:rs-sfx) and plays=<n>, the one-shots started so far
//   on | off
//       op=on|off result=ok enabled=0|1 spawned=N - writes the CVar, and spawns or kills now
//   max [<n>|default]
//       op=max result=ok max=N - with no argument, a read. n is 0..3 (the env bank's channels); anything
//       else is op=max result=error error=range max_allowed=3, rc=1
//   radius <emitter|all> [<tiles>|default]
//   gain <emitter|all> [<0..1>|default]
//       op=radius|gain result=ok emitter=<name> radius|gain=<value> - one line per emitter named, a read
//       without a value. Kept by name across scene reloads, never saved: a game restart goes back to
//       the library and the placements. An unknown emitter (or a scene with none) is error=emitter, a
//       value outside 0.5..64 tiles or 0..1 is error=range min= max=, both rc=1 and nothing written
//
// Returns 0 for a read or a successful write, 1 for a refused argument or an unknown subcommand.
int32_t RsAmbienceConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_AMBIENCE_H
