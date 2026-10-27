#ifndef SOH_RS_COMBAT_MUSIC_H
#define SOH_RS_COMBAT_MUSIC_H

#include <stdint.h>
#include <string>
#include <vector>

// Which enemies may start combat music in the scenes the zone director owns (sturdy-bassoon#90 P6).
//
// THE POLICY, decided by the owner 2026-10-26 (docs/decisions/2026-10-26-combat-music-opt-in.md):
// combat music is POSSIBLE over every RS track, and NO enemy starts it unless it is listed. Two
// halves, and each one is useless without the other:
//
//   - every imported track carries a duck-capable placeholder (NA_BGM_SARIA_THEME, sSeqFlags bit 0),
//     because Audio_SetSequenceMode's gate reads the PLACEHOLDER, never the RS track
//     (AUDIO_SYSTEM.md section 5). That is tools/music/rs-tracks.json, not this file.
//   - VB_DETECT_BGM_ENEMY is forced false here for every actor that is not listed, and true for a
//     listed one in range - the shape of audio/LeeverEnemyBGM.cpp, which forces it on for one id.
//
// Bosses and mini-bosses are NOT this lever. They take player 0 outright through func_800F5ACC,
// which ignores sSeqFlags and this gate alike; the director yields to them (ZoneDirector.cpp).
//
// Applies only where the director is on AND the scene is opted in to the zone table. Everywhere else
// - vanilla scenes, test maps, a director switched off - the vanilla rule stands untouched.
//
// `rsmusic combat ...` (and `agenttest music combat ...`), routed here by MusicConsole_Run with the
// word `combat` already sliced off:
//   (none) | status     `combat policy=opt_in|vanilla applies=0|1 global_off=0|1 listed=<n> test=<n>
//                       ids=<hex,...|none> suppressed=<n> last_suppressed=<hex|none> allowed=<n>
//                       last_allowed=<hex|none>`. suppressed= counts every time the gate turned a
//                       VANILLA yes into a no - the witness that the policy was challenged rather
//                       than idle. allowed= counts the times it said yes for a listed actor. Both
//                       are per actor per frame and never reset; compare two readings
//   add <actor_id>      list an actor for this session only (the test list; never saved).
//                       `op=combat_add actor=0x55 result=ok test=1`
//   remove <actor_id>   `op=combat_remove actor=0x55 result=ok test=0`
//   clear               `op=combat_clear result=ok test=0`
//   policy opt_in|vanilla
//                       the CVar (persisted). `vanilla` restores the stock rule in director scenes
//                       too - every hostile enemy in range - which is the A/B for this lever
//
// Returns 0 on success and for reads; 1 for a refused operation (bad or missing argument, adding an
// id already listed, removing one that is not, a full test list).
int32_t RsCombatMusic_Console(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_COMBAT_MUSIC_H
