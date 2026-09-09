#ifndef SOH_RS_MUSIC_ZONE_DIRECTOR_H
#define SOH_RS_MUSIC_ZONE_DIRECTOR_H

/*
 * The zone director (sturdy-bassoon#90 P0) - background music chosen by WHERE LINK IS, rather than
 * by which scene loaded.
 *
 * OoT has no precedent for this. Scene music is one id set at scene load and changed only by
 * events; nothing in the vanilla game plays a queue of tracks for a place. So this is new
 * machinery, and the closest thing in the tree to copy is `Enhancements/roomdist/RoomDist.h` -
 * same shape, per-frame XZ position folded into a state machine with hysteresis, plus an
 * event-pop function so the agent test harness can read what it did.
 *
 * WHAT IT DOES, once per gameplay frame (GameInteractor::OnPlayerUpdate), and only in a scene that
 * opted in (MusicZones.h):
 *
 *   1. Convert Link's OoT world XZ into RS absolute surface tiles.
 *   2. Find the highest-priority zone containing that tile - always at least the fallback.
 *   3. If it differs from the active zone it becomes the CANDIDATE and a dwell timer starts. If
 *      the candidate changes the timer resets. When the timer expires with the candidate
 *      unchanged, the switch happens.
 *   4. A switch is stop-with-a-fade, a beat of quiet, then start-with-a-fade.
 *
 * WHY THE DWELL TIMER EXISTS, because this is the first thing a future refactor deletes: it is not
 * debounce for its own sake. Running *along* a zone border must not switch the music, and clipping
 * the corner of B while walking A->C must not switch it either. That is the whole point of the
 * feature - music that changes because you went somewhere, not because you wobbled. The rejected
 * alternative was per-zone accumulated presence time, which trips both zones eventually when you
 * alternate between them.
 *
 * WHY IT IS NOT A CROSSFADE. Engine fact, not taste. Starting a sequence on a seq player that is
 * already playing is a hard cut followed by a fade in - SEQCMD op 0 runs
 * AudioLoad_SyncInitSeqPlayer, which repoints the script PC at the new data and kills the old
 * sequence mid-note, and only then starts the fade. A real overlap would need the outgoing track
 * parked on SEQ_PLAYER_BGM_SUB (player 3), and that is where vanilla's combat ducking lives. So:
 * sequential, and the beat of quiet is deliberate - it reads as a transition rather than a glitch.
 *
 * WHY TELEPORTS SKIP THE DWELL. The dwell filters *walking* across a boundary. Applying it to a
 * teleport is just wrong, and teleport spells and tablets are planned content rather than an edge
 * case. This is an explicit signal (RsMusic_NotifyWarped) rather than an inferred one: a large
 * position delta would misfire on fast travel and on cutscene camera moves.
 *
 * WHY THERE IS NO RESUME-AT-POSITION PATH, and please do not add one. The asymmetry between combat
 * and cutscenes is intentional:
 *   - Combat DUCKS. Vanilla's Audio_SetSequenceMode starts enemy music on player 3 and applies a
 *     volume scale to player 0; it never restarts player 0. So the zone track keeps advancing
 *     underneath at low volume and comes back where it would have been. Free, and there is no
 *     state to keep.
 *   - Cutscenes REPLACE, and on release the zone track restarts from the top. Combat is frequent
 *     and short, so a seamless resume is right there; cutscenes are rare and important, and RS
 *     overworld tracks have real starts and ends. Landing back in the middle of a somber passage
 *     after a cutscene feels wrong. A fresh start reads as intentional.
 * (Vanilla does have a resume mechanism - a few scene sequences write progress into soundScriptIO
 * and branch on it at restart - but it is authored into the sequence data itself, so it would not
 * transfer to imported tracks anyway.)
 *
 * OWNERSHIP. The director takes player 0 by making the scene loader stand down rather than by
 * fighting it: in AfterSceneCommands it sets sequenceCtx.seqId = NA_BGM_NO_MUSIC and
 * natureAmbienceId = NATURE_ID_NONE, and with both set Environment_PlaySceneSequence returns early
 * without touching the player at all. It gives player 0 back the moment anything else takes it -
 * cutscene, mini-boss, minigame - and RE-ASSERTS ONLY WHEN PLAYER 0 GOES QUIET. A director that
 * competed would leave the music wrong forever after one cutscene.
 *
 * (#90 section 13 originally read "when it goes quiet OR the zone changes". The second half was
 * deleted on 2026-09-09: taken literally it lets a cutscene which walks Link across a boundary have
 * its own music cut, and it buys nothing, because the release path recomputes the winning zone from
 * scratch anyway. See the comment at the yield check in ZoneDirector.cpp.)
 *
 * THE FIRST-VISIT TRACK is a one-shot opener, consumed on zone ACTIVATION - the moment the switch is
 * actually taken, after the dwell - and never on boundary contact. There is no forced completion:
 * walk in, hear thirty seconds of it, walk out, and it is spent. The flag lives in the project's own
 * world-flag store (worldstate/WorldFlags.h, #54); there is deliberately no new SaveManager section
 * and no new save format.
 *
 * COST WHILE OFF. One CVar read per frame and nothing else; no scene is modified, and the vanilla
 * loader keeps setting the music it always set.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * "Link's position was SET, not walked" - the signal that suppresses the dwell timer for one
 * switch. `reason` is a short single-token string that lands verbatim on the marker channel
 * (scene_load, teleport, ...); it must be a string literal or otherwise outlive the call.
 *
 * Call this from anything that moves Link discontinuously. Today: the AfterSceneCommands handler
 * inside this file (which covers scene loads, entrance warps, save-game loads and death respawns,
 * since all four go through a scene load) and `agenttest goto`. Teleport spells and tablets, when
 * they exist, call it too.
 */
void RsMusic_NotifyWarped(const char* reason);

/* The `rsX`/`rsY` a scene with no surface anchor reports - an interior or an underground area,
 * which has no position in the world's surface frame at all. Console surfaces render it as `none`
 * rather than as a number, because a number there would be believed. */
#define RS_NO_TILE (-2147483647 - 1)

/* Writes `<x>,<y>`, or `none` if either is RS_NO_TILE. One function so every surface that prints a
 * tile pair - the marker channel, `status`, `where` - agrees about what "nowhere" looks like. */
void RsMusic_FormatTiles(char* buf, uint32_t size, int32_t rsX, int32_t rsY);

/* One-line description of the live state: `zone=<name> track=0x<hex> state=<name> ...`. Never NULL.
 *
 * STAYS ONE LINE. It is the form for a place where one line is correct - a perf marker, or the echo
 * after `rsmusic on`. The human-readable `rsmusic status` uses RsMusic_DescribeLine instead, because
 * the ImGui console does not wrap and one 200-character line means dragging the window out to full
 * width to read it (#90, human tuning pass 2026-09-09). */
const char* RsMusic_Describe(void);

/*
 * The same state as RsMusic_Describe, split into grouped lines - director, zone and track, the
 * tunables, the counters. Writes line `index` into `buf` and returns 1; returns 0 once `index` is
 * past the last line, so a caller loops until it gets 0.
 *
 * EVERY LINE IS STILL SINGLE-LINE key=value with no spaces inside a value. That is what the marker
 * channel requires, and it is not the same requirement as "one line in total" - `rsmusic zones`
 * already emits a line per entry. Splitting is for the human; greppability is for the agent loop.
 */
int32_t RsMusic_DescribeLine(int32_t index, char* buf, uint32_t size);

/*
 * Pops the oldest unread event, if any, into `buf` (a printf-ready fragment: what changed, why,
 * and where Link was in both frames). Returns 1 if an event was written. The agent-test hook polls
 * this once per tick and puts it on the marker channel, which is why nothing here does file I/O.
 * The buffer holds a few events so a burst is not lost; `dropped=` is appended if it ever
 * overflows.
 */
int32_t RsMusic_TakeEvent(char* buf, uint32_t size);

/* Everything a console surface needs to answer "where am I and what should be playing". Any
 * pointer may be NULL. Returns 0 when Link is not in an opted-in scene, in which case only
 * `sceneId` is written. */
int32_t RsMusic_Probe(int16_t* sceneId, int32_t* rsX, int32_t* rsY, int32_t* zoneIndex);

/*
 * How many switches this session has actually taken. The assertable form of "cross a boundary and
 * come back inside the dwell window, and NOTHING happens".
 *
 * MONOTONIC, AND THERE IS DELIBERATELY NO WAY TO ZERO IT. A counter nothing can reset makes "the
 * count did not move" a strictly stronger claim than one that can.
 */
int32_t RsMusic_TransitionCount(void);

/*
 * The bookmark half of that assertion: `RsMusic_MarkBaseline` remembers the current count and
 * returns it, `RsMusic_Baseline` reads the bookmark back (0 = never marked, i.e. session start).
 * A run marks a baseline, walks, and checks that `transitions - baseline` is what it expects.
 *
 * Note what this is NOT: it does not touch the counter. The console subcommand that calls it was
 * called `reset` through P0 and reset nothing, which cost a human real confusion - it is `baseline`
 * now (#90, human tuning pass 2026-09-09). The tamper-proof property is unchanged.
 */
int32_t RsMusic_MarkBaseline(void);
int32_t RsMusic_Baseline(void);

#ifdef __cplusplus
}
#endif

#endif /* SOH_RS_MUSIC_ZONE_DIRECTOR_H */
