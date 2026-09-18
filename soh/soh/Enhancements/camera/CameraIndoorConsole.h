#ifndef SOH_CAMERA_INDOOR_CONSOLE_H
#define SOH_CAMERA_INDOOR_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The `camindoor` console command: the tuning surface for the indoor camera pull-in
// (sturdy-bassoon#108). The behaviour is in `soh/src/code/z_camera.c`; the knobs and their defaults
// are in CameraIndoorTuning.h beside this file.
//
// It exists because this build of SoH has no console `set`, so a CVar cannot otherwise be changed
// from the agent test loop, which drives the game through console commands only. That matters more
// here than for most features: this one is a feel feature with seven interacting numbers, and
// re-tuning it through a rebuild each time would cost an hour per value. `camindoor` is registered
// as an ordinary console command, so `Send-SohCommand 'camindoor scale 0.9'` reaches it from a run
// script and the reply comes back on the command's own `out=` marker.
//
// `args[0]` is the subcommand:
//   status               everything the mechanism is reading right now, in grouped key=value
//                        lines: the scene gate, the sample count and how many found a ceiling,
//                        the scale that would apply, and what Camera_Normal1 last really used.
//                        The detection is RECOMPUTED on every call, so this answers even while
//                        Normal1 is not the camera running (Z-target, cutscene, pause) - compare
//                        `applied_frame` with `frame` to tell
//   on | off             the master CVar
//   scale <f>            multiplier on distMin/distMax while indoors (P1)
//   height <f>           how far above the floor a ceiling still counts as indoors (P1)
//   ease <f>             fraction of the normal step taken while pulling IN (P2). 1 = vanilla rate
//   ring <n>             extra ceiling samples around the player, 0-8 (P3). 0 = the feet sample only
//   radius <f>           ring radius in OoT units. A grid-tool tile is 40 (P3)
//   bias <f>             how far ahead of the player's facing the ring's centre sits (P3)
//   k <n>                how many of the 1 + ring samples must find a ceiling (P3)
//   defaults             clear every knob back to its compiled-in default, so an A/B run has a
//                        known starting point that does not depend on shipofharkinian.json
//
// Every write persists (CVarSave), which is the point: a value that feels right in a session is
// still there afterwards to be copied into CameraIndoorTuning.h as the new default.
//
// Returns 0 when the operation succeeded (or for `status`), 1 otherwise, so `rc=` on the agent
// loop's cmd marker is the pass/fail bit.
int32_t CameraIndoorConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_CAMERA_INDOOR_CONSOLE_H
