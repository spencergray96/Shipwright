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
// HOW TO USE IT. Three things an RS encounter can want, and where each one lives:
//
//   1. AN ORDINARY MONSTER THAT BRINGS IN COMBAT MUSIC (it ducks the zone track and plays
//      NA_BGM_ENEMY on top, louder the closer it is, and stops when it leaves 500 units):
//        a. The actor sets ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE while it should count -
//           clear HOSTILE when it is dead, fleeing or asleep and the music lets go on its own.
//        b. Try it first without a rebuild: `rsmusic combat add <its actor id>`, stand near it,
//           `rsmusic players` - player 3 reads id=0x81A and player 0's last scale drops below 127.
//        c. Make it permanent: add `ACTOR_RS_<NAME>, // why` to kListedActors in CombatMusic.cpp
//           and rebuild. `rsmusic combat` then reports it in listed= and ids=.
//      Nothing else: every RS track already carries a duck-capable placeholder.
//   2. A BOSS OR MINI-BOSS THAT TAKES THE MUSIC OVER (its own track replaces the zone's for the
//      fight). Not this file - vanilla's own pair, which the director already yields to and recovers
//      from: call func_800F5ACC(NA_BGM_MINI_BOSS) (or NA_BGM_BOSS) when the fight starts, and
//      func_800F5B58() from the actor's Destroy when the fight is over, as En_Test does
//      (z_en_test.c). The override id must carry SEQ_FLAG_RESTORE (0x38 and 0x1B do) or the second
//      call restores nothing. On the way out the director logs `release via=quiet` and fades the
//      zone's next track in - including in whichever zone Link ended the fight in.
//   3. A TRACK THAT MUST NEVER DUCK, whatever is attacking (a sombre place, say): set that row's
//      placeholder in tools/music/rs-tracks.json to "0x82", then `npm run import:rs-music --
//      --refresh-record`, `npm run gen:music-zones` and rebuild - no re-encode. See ASSET_PIPELINE.md,
//      "RS music".
//
// To hear the stock rule (every hostile enemy) in a director scene for comparison:
// `rsmusic combat policy vanilla`, and `policy opt_in` to put the policy back (both persist).
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
