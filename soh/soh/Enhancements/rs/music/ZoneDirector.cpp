/*
 * The zone director (sturdy-bassoon#90). See ZoneDirector.h for what this is, why the dwell
 * timer exists, why there is no crossfade, and why there is no resume-at-position path.
 *
 * The whole thing is the OnPlayerUpdate handler below plus the AfterSceneCommands handler that
 * makes the scene loader stand down. Everything else is bookkeeping for the console and the agent
 * test harness.
 */

#include "ZoneDirector.h"
#include "MusicZones.h"

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "seqcmd.h"
extern PlayState* gPlayState;
// code_800EC960.c's mini-boss stash, declared in no header. Written in exactly one place - see
// ReclaimFromOverride.
extern u16 sPrevMainBgmSeqId;
// audio_load.c's registry: sequenceMap[n] is the archive path sequence number n was given at boot,
// NULL for an unassigned number, over sequenceMapSize + 0xF slots (the same figure
// AudioLoad_SyncInitSeqPlayerInternal bounds-checks against). z64audio.h declares only the size.
// Read, never written. See RsMusic_ResolveTrackPath.
extern char** sequenceMap;
}

// --- the four knobs ----------------------------------------------------------------------------
//
// CVars rather than console state, because #90 wants them tunable for A/B while the feel is being
// found, and a CVar persists in shipofharkinian.json across a relaunch where console state does
// not. There is no `set` console command in this build, so the `rsmusic` command writes them
// (MusicConsole.cpp) - one source of truth, two ways in.
//
// They are read every frame rather than cached, so a change takes effect on the next tick without
// a scene reload. That is a few hash lookups per frame, which is nothing next to a game tick, and
// it is the difference between "tune it while walking around" and "restart to hear it".
//
// Fade-out and fade-in are SEPARATE knobs on purpose: they are separate engine commands, and an
// asymmetric setting (fast out, slow in, say) sounds very different from a symmetric one.

#define CVAR_RS_MUSIC_ON CVAR_ENHANCEMENT("RsMusicZones")
#define CVAR_RS_MUSIC_DWELL CVAR_ENHANCEMENT("RsMusicDwellSec")
#define CVAR_RS_MUSIC_FADE_OUT CVAR_ENHANCEMENT("RsMusicFadeOutSec")
#define CVAR_RS_MUSIC_FADE_IN CVAR_ENHANCEMENT("RsMusicFadeInSec")

// VALIDATED BY EAR, not starting points any more. #90 section 6 proposed 2.0/1.5/1.5; a human
// listening pass on 2026-09-09 kept the dwell and the fade-in, and a second pass on 2026-09-10
// raised the fade-out to 3.0 - at 1.5 s "it sounds like the songs fade to zero volume pretty
// fast". This is why the two fades are separate knobs: the asymmetry is the tuned result, not an
// oversight. Change these only from another listening pass, never from reading the code.
#define RS_MUSIC_DWELL_DEFAULT 2.0f
#define RS_MUSIC_FADE_OUT_DEFAULT 3.0f
#define RS_MUSIC_FADE_IN_DEFAULT 1.5f

namespace {

/*
 * Two different clocks, and mixing them up is the easy bug here.
 *
 *   GAME TICKS   - 20 per second. OnPlayerUpdate fires once per tick, so the dwell timer and the
 *                  quiet gap are counted in these.
 *   FADE UNITS   - 1/30 of a second, and an 8-BIT FIELD in the sequence command. So the longest
 *                  fade the engine can express is 255/30 = 8.5 seconds, and asking for more would
 *                  silently wrap. It is clamped below.
 *
 * (The raw field is shifted left by 3 on its way through Audio_ProcessSeqCmd and then scaled by
 * updatesPerFrame/4 in Audio_StartSequence, which is where the 1/30 comes from. Nothing here needs
 * to reproduce that arithmetic - it only needs to hand over a number in 0..255.)
 */
constexpr int32_t TICKS_PER_SECOND = 20;
constexpr int32_t FADE_UNITS_PER_SECOND = 30;
constexpr int32_t FADE_UNITS_MAX = 255;

/*
 * How long after issuing a play command before the "did somebody else take player 0?" check starts
 * believing what it reads. The command is queued for the audio thread, so the seq player is not
 * `enabled` yet on the next tick or two and func_800FA0B4 would report NA_BGM_DISABLED - which
 * would look exactly like the track having ended. Half a second is comfortably past that.
 */
constexpr int32_t START_GRACE_TICKS = 10;

constexpr int32_t EVENT_SLOTS = 8;
// Raised from 256 by #90 P3. `transition` and `advance` both grew a track NAME (an imported track's
// is `rs:book-of-spells`, not two hex digits), the resolved custom sequence number, the per-track end
// fade and which fade the advance used; the longest measured about 240 characters, which is close
// enough to 256 that the next field added would have truncated a line silently. A truncated
// key=value line is worse than a missing one: it parses, and the last field it carries is wrong.
constexpr int32_t EVENT_LEN = 384;
constexpr int32_t NO_ZONE = -1;

/*
 * The scriptCounter clock, for the duration timer.
 *
 * seqPlayer->scriptCounter (z64audio.h) is a u32 incremented once per audio update in
 * AudioSeq_SequencePlayerProcessSequence, and AudioSeq_ResetSequencePlayer zeroes it when a new
 * sequence is loaded onto the player. It is the right clock for a track duration because it is the
 * AUDIO's own clock: it advances with the sound the player is actually hearing rather than with
 * game ticks, and it freezes while the pause menu is open (the muted early-return sits above the
 * increment).
 *
 * DO NOT HARDCODE THE RATE. #90 section 9 says to read updatesPerFrame at runtime; the whole rate
 * is derivable and neither half of it is a constant:
 *
 *     updates/sec = frequency * updatesPerFrame / samplesPerFrameTarget
 *
 * samplesPerFrameTarget samples are consumed per audio frame at `frequency` samples/sec, so that
 * quotient is audio frames per second, and updatesPerFrame scriptCounter ticks happen in each.
 * audio_heap.c derives samplesPerFrameTarget from gAudioContext.refreshRate, which audio_load.c
 * sets to 50 on PAL and 60 elsewhere - so the "60 audio frames a second" in section 9 is an
 * NTSC-only figure and the arithmetic above avoids assuming it. Both fields get scaled by the
 * spec's frame-grouping factor and the factor cancels in the quotient.
 *
 * Returns 0 before the audio heap is initialised, which the caller reads as "no clock, do not
 * advance on duration".
 *
 * THIS IS THE REPORTED RATE, NOT THE ONE ANY COMPARISON USES. It is truncated to a whole number of
 * ticks per second - 176 here, where the true value is fractional - which is exactly why nothing
 * times a track with it. See ScriptTicksToMs.
 */
uint32_t ScriptTicksPerSecond() {
    const AudioBufferParameters* p = &gAudioContext.audioBufferParameters;
    if (p->frequency == 0 || p->samplesPerFrameTarget <= 0 || p->updatesPerFrame <= 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)p->frequency * (uint64_t)(uint32_t)p->updatesPerFrame) /
                      (uint64_t)(uint32_t)p->samplesPerFrameTarget);
}

/*
 * Script ticks -> milliseconds, in one exact rational step rather than through a truncated rate.
 *
 *     ms = ticks * 1000 * samplesPerFrameTarget / (frequency * updatesPerFrame)
 *
 * WHY NOT `ticks / ScriptTicksPerSecond()`. That integer divide was P2's arithmetic, and truncating
 * 176.4-ish to 176 fires a duration consistently about 1% EARLY - measured on the 2026-09-09 P2
 * run, where a 45 s track advanced at 44.6 s across four tracks. At P2 that was inside what an
 * authored policy number even meant, and it was recorded rather than fixed (AUDIO_SYSTEM.md
 * section 7).
 *
 * P3 is where it stops being inside the meaning. An imported track's duration is a MEASUREMENT of
 * where the music ends, and the end fade is aimed at that instant: 1% of a 136 s track is 1.4 s, so
 * a nominally 5 s ramp would reach zero a second and a half before the music did and the stop would
 * cut the last of it. In 64-bit integer arithmetic there is nothing to truncate, and the largest
 * intermediate here (a five-minute track, ~53k ticks x 1000 x samplesPerFrameTarget) is nine orders
 * of magnitude inside the type.
 *
 * Returns 0 when there is no clock yet, which the caller reads the same way as before.
 */
uint64_t ScriptTicksToMs(uint32_t ticks) {
    const AudioBufferParameters* p = &gAudioContext.audioBufferParameters;
    if (p->frequency == 0 || p->samplesPerFrameTarget <= 0 || p->updatesPerFrame <= 0) {
        return 0;
    }
    const uint64_t num = (uint64_t)ticks * 1000u * (uint64_t)(uint32_t)p->samplesPerFrameTarget;
    const uint64_t den = (uint64_t)p->frequency * (uint64_t)(uint32_t)p->updatesPerFrame;
    return num / den;
}

/* Does the audio clock exist yet? Separate from "the elapsed time is 0", which is a real answer on
 * the first tick of a track. */
bool HasAudioClock() {
    const AudioBufferParameters* p = &gAudioContext.audioBufferParameters;
    return p->frequency != 0 && p->samplesPerFrameTarget > 0 && p->updatesPerFrame > 0;
}

uint32_t ScriptCounter() {
    // Same unsynchronised read as func_800FA0B4 does of `enabled` right next to it: the audio
    // thread owns this word and it is not volatile. A stale tick-boundary value moves a track
    // boundary by milliseconds, which is beneath the resolution of anything here.
    return gAudioContext.seqPlayers[SEQ_PLAYER_BGM_MAIN].scriptCounter;
}

enum DirectorState {
    STATE_IDLE,    // nothing asserted; the next winning zone is taken immediately
    STATE_FADING,  // stop issued, waiting out the quiet gap before the start
    STATE_PLAYING, // our track is on player 0
    STATE_SILENT,  // the active zone has an empty track list - deliberate silence
    STATE_YIELDED, // something else owns player 0; stand down until it goes quiet
};

const char* StateName(DirectorState s) {
    switch (s) {
        case STATE_IDLE:
            return "idle";
        case STATE_FADING:
            return "fading";
        case STATE_PLAYING:
            return "playing";
        case STATE_SILENT:
            return "silent";
        case STATE_YIELDED:
            return "yielded";
    }
    return "unknown";
}

DirectorState sState = STATE_IDLE;
int32_t sActiveZone = NO_ZONE;    // the zone whose track is (or is about to be) playing
int32_t sCandidateZone = NO_ZONE; // the zone the dwell timer is counting toward
int32_t sDwellTicks = 0;
int32_t sGapTicks = 0;   // remaining ticks of deliberate quiet in STATE_FADING
int32_t sGraceTicks = 0; // remaining ticks before the yield check trusts func_800FA0B4
// The track chosen for the switch in flight. Chosen ONCE, in BeginSwitch, because choosing it is
// not a pure function any more - a first-visit pick spends a world flag - so calling the picker
// again to fill in a log line would spend it twice and hand STATE_FADING a different track from the
// one that was announced.
// The chosen entry itself, pointing into the static generated table, rather than a copy of its
// fields: since #90 P3 a track is four numbers and a string, and every consumer wants a different
// subset. NULL means authored silence.
const RsZoneTrack* sPendingEntry = nullptr;
bool sPendingFirstVisit = false;
uint8_t sPendingBagPos = 0; // 1-based position within the bag; 0 = not drawn from one
uint8_t sPendingBagSize = 0;
/*
 * The custom sequence number the pending entry's rsPath resolved to; -1 for a vanilla entry and for
 * an RS path that did not resolve, told apart by sPendingUnresolved.
 *
 * RESOLVED AT PICK TIME rather than at start time, so the `transition` line can report the number
 * it is about to play - or report the failure before it announces a track that cannot sound.
 * sequenceMap does not change after boot, so the answer cannot go stale inside the switch's own
 * quiet gap.
 */
int32_t sPendingAudioSeq = -1;
bool sPendingUnresolved = false;

const RsZoneTrack* sPlayingEntry = nullptr;
// Kept apart from sPlayingEntry because it has a different lifetime: this is "our id" for every
// ownership test, and it is the PLACEHOLDER for an imported track, which is exactly what player 0
// reports back (#91). NA_BGM_DISABLED means nothing of ours is on player 0.
uint16_t sPlayingSeqId = NA_BGM_DISABLED;
bool sPlayingFirstVisit = false;
uint8_t sPlayingBagPos = 0;
uint8_t sPlayingBagSize = 0;
int32_t sPlayingAudioSeq = -1;

/*
 * Has player 0 actually been observed sounding OUR track since we asked for it?
 *
 * This is the discriminator between "the track ended" and "the track never started", and the
 * start-grace window is not that test on its own: for the first tick or two after a play command,
 * quiet is exactly what a perfectly good track looks like, because the command has not reached the
 * audio thread yet. So grace answers "is it too early to believe quiet", and this answers "was
 * there ever anything to end". A bad sequence id fails the second one forever, and the bag must
 * not move for it - otherwise one typo walks the whole bag (see EndOfTrack).
 */
bool sSounding = false;

/*
 * THE TRACK CLOCK IS seqPlayer->scriptCounter ITSELF, not a difference from a baseline taken when
 * the director first saw the track sounding. `now`, not `now - start`.
 *
 * AudioSeq_ResetSequencePlayer zeroes scriptCounter when a sequence is loaded onto the player, and
 * SEQCMD_PLAY_SEQUENCE always loads, so the counter IS "how long this track has been playing".
 * Subtracting a baseline threw away everything before the director noticed - the play command's
 * trip to the audio thread plus START_GRACE_TICKS, about **one second**, measured at 0.97 s on the
 * 2026-09-17 P3 run against `players[0] sec=`.
 *
 * P2 could not see that, because its two errors cancelled: truncating tick_hz fired the duration
 * ~1% early (1.15 s on a 115 s track) and the baseline fired it ~1 s late. Fixing the truncation
 * for P3's end fade uncovered the other half, and the run measured the fade landing 1.1 s after
 * the music ended rather than the predicted 0.1 s - on a track whose whole point is that the ramp
 * reaches zero AS the music does.
 *
 * WHAT THE BASELINE WAS FOR is now covered elsewhere. #90 P2 added it against a re-assert that
 * confirmed `sounding` against the OUTGOING playback and then had the counter zeroed under it; the
 * grace window's "nothing func_800FA0B4 says is trusted in here, positive or negative" is what
 * fixes that, and it is unchanged. And the unsigned-wrap runaway the old code guarded against
 * cannot happen without a subtraction at all: a sequence that writes its own counter backwards
 * (opcode 0xC5) now simply makes the duration fire later, never four billion ticks early.
 */
uint32_t sLastCounter = 0;      // previous reading, for the backwards-jump diagnostic only
bool sClockResetLogged = false; // one diagnostic per track, not one per frame

int32_t sTransitions = 0;
// Advances are counted apart from transitions and this is load-bearing. The negative every run of
// this feature asserts is "cross a boundary, come back inside the dwell, and `transitions` did not
// move"; folding an in-zone advance into it would make the number tick up on its own while a
// multi-track zone plays, and the strongest claim the feature can make would start reporting noise.
int32_t sAdvances = 0;
int32_t sBaseline = 0;         // see RsMusic_MarkBaseline; never touches sTransitions
int32_t sAdvanceBaseline = 0;  // the same bookmark, taken at the same moment, for sAdvances
int32_t sArmedScene = -1;      // the opted-in scene the state above belongs to

// "Position was set, not walked": suppress the dwell for exactly one switch.
bool sWarpPending = false;
const char* sWarpReason = "warp";

char sDescription[384] = "rsmusic off";

char sEvents[EVENT_SLOTS][EVENT_LEN];
int32_t sEventRead = 0;
int32_t sEventWrite = 0;
int32_t sEventsDropped = 0;

/*
 * DEFAULTS ON since #90 P2 (2026-09-09). The reason it defaulted OFF is worth keeping rather than
 * deleting, because an agent that finds a default with no rationale re-litigates it:
 *
 *   P1 left it off deliberately. #90's phase list said P1 closing should flip it on, but that line
 *   was written when P1 meant "the real table"; the 2026-09-09 amendment re-scoped P1's CONTENT to
 *   an explicit POC, and defaulting on would have made POC content the default experience in the
 *   two opted-in scenes.
 *
 * That decision was made and then reversed on purpose: the user wants the music system live for all
 * testing from here on, and the POC-content objection is answered by the per-scene opt-in, which
 * already protects vanilla and every scene not named in the table. The generator refuses a table
 * without exactly one fallback, so an opted-in scene cannot have a coverage hole either.
 *
 * `rsmusic off` remains the A/B lever, and the clean A/B is still "flip, then re-enter the scene",
 * because the loader stand-down happens at scene load.
 */
bool Enabled() {
    return CVarGetInteger(CVAR_RS_MUSIC_ON, 1) != 0;
}

bool InNormalPlay() {
    return gPlayState != nullptr && gSaveContext.gameMode == GAMEMODE_NORMAL;
}

float ClampSeconds(float v) {
    if (!(v > 0.0f)) { // also catches NaN
        return 0.0f;
    }
    return v > 20.0f ? 20.0f : v;
}

int32_t FadeUnits(float seconds) {
    const int32_t units = (int32_t)lroundf(ClampSeconds(seconds) * (float)FADE_UNITS_PER_SECOND);
    return units > FADE_UNITS_MAX ? FADE_UNITS_MAX : units;
}

int32_t SecondsToTicks(float seconds) {
    return (int32_t)lroundf(ClampSeconds(seconds) * (float)TICKS_PER_SECOND);
}

// The millisecond forms, for a per-track end fade (RsZoneTrack::endFadeMs) rather than a CVar in
// seconds. Same clamps, same 8-bit ceiling - a fade the engine cannot express would wrap, not
// saturate, so this is the last line of defence behind the generator's own cap.
int32_t FadeUnitsMs(uint32_t ms) {
    const int32_t units = (int32_t)(((uint64_t)ms * FADE_UNITS_PER_SECOND + 500u) / 1000u);
    return units > FADE_UNITS_MAX ? FADE_UNITS_MAX : units;
}

int32_t TicksFromMs(uint32_t ms) {
    return (int32_t)(((uint64_t)ms * TICKS_PER_SECOND + 500u) / 1000u);
}

void RecordEvent(const char* fmt, ...) {
    const int32_t next = (sEventWrite + 1) % EVENT_SLOTS;
    if (next == sEventRead) {
        sEventsDropped++; // AgentTest drains this in every session (#97), so it should stay at zero
        return;
    }
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(sEvents[sEventWrite], EVENT_LEN, fmt, args);
    va_end(args);
    sEventWrite = next;
}

// --- naming a track ------------------------------------------------------------------------------
//
// A vanilla entry is named by its id and an imported one by its path, and after #90 P3 those two
// live in one list. Everything below exists because the OBVIOUS name - RsZoneTrack::seqId - stopped
// identifying a track the moment RS entries arrived: every imported track carries the same
// placeholder (0x82 for all six Lumbridge tracks), so an id comparison says "same track" about two
// different songs and an id in a log line says nothing about which one played.

/*
 * Are these two entries the same track? THE BACK-TO-BACK GUARD DEPENDS ON THIS ANSWER, and getting
 * it from seqId would have silently disabled the guard for RS content: with every placeholder equal
 * the guard sees a repeat proposed every single refill, scans for an entry with a different id,
 * finds none, and reports `guard=unavoidable` forever - which is also exactly what it reports for
 * a zone legitimately authored as several copies of one track. A guard that cannot act, reporting
 * the outcome that means "correctly could not act".
 *
 * So: two RS entries are the same track when their paths match, two vanilla entries when their ids
 * match, and an RS entry is never the same track as a vanilla one. Comparing the path's CONTENT
 * rather than the pointer keeps the property #90 P2 wrote down for ids - a list authored with the
 * same track twice still guards correctly - and does not depend on whether the compiler pooled two
 * identical string literals.
 */
bool SameTrack(const RsZoneTrack* a, const RsZoneTrack* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    if ((a->rsPath == nullptr) != (b->rsPath == nullptr)) {
        return false;
    }
    if (a->rsPath != nullptr) {
        return std::strcmp(a->rsPath, b->rsPath) == 0;
    }
    return a->seqId == b->seqId;
}

/*
 * What a track is called on the marker channel and the console: `0x3C` for a vanilla id, or
 * `rs:flute-salad` for an imported one.
 *
 * ONE FORMATTER so `transition`, `advance`, `refill`, `status` and `zones` cannot disagree - the
 * same reason RsMusic_FormatTiles and FormatBag exist. The last path segment rather than the whole
 * path because the channel is `key=value` with no spaces and the full path is long; `rsmusic zones`
 * prints it in full, which is the surface for "which file is that".
 */
/* (The definition is RsMusic_FormatTrack, at the bottom of this file with the other exported
 * surfaces - MusicConsole.cpp names tracks too, and two spellings of a label is two things to keep
 * in step.) */

/* The same label with a caller-chosen word for NULL, because "no track" means different things in
 * different lines: `silence` where a zone authored none, `none` where one simply is not playing. */
void FormatTrackOr(char* buf, uint32_t size, const RsZoneTrack* t, const char* ifNull) {
    if (t == nullptr) {
        std::snprintf(buf, size, "%s", ifNull);
        return;
    }
    RsMusic_FormatTrack(buf, size, t);
}

// --- the coordinate frame ----------------------------------------------------------------------

/*
 * OoT world XZ -> RS absolute surface tiles. THE ONLY PLACE THE Y AXIS IS ALLOWED TO FLIP.
 *
 * OoT's world Z grows south and the terrain toolchain's tile y grows south; RS's y grows north.
 * All three conventions are documented and internally consistent, and they disagree, so exactly
 * one negation has to happen somewhere. It happens here, once, and the reason is written next to
 * it - see MusicZones.h and docs/decisions/2026-09-08-rs-terrain-coordinate-anchor.md.
 *
 * floorf rather than a cast: a cast truncates toward zero, so world x = -10 would land on tile 0
 * instead of tile -1 and every rect edge west or north of the scene's own origin would be off by
 * one tile.
 */
void WorldToRs(const RsMusicScene* scene, float worldX, float worldZ, int32_t* rsX, int32_t* rsY) {
    // A scene with no surface anchor has no position in this frame at all, and carries
    // unitsPerTile = 0 - so there is nothing to divide by and no answer that would mean anything.
    // Without this the division yields an infinity and the cast to int32 is undefined behaviour
    // that happened, on the 2026-09-09 P1 run, to print rs=-2147483648,-2147483648. Harmless,
    // because rect matching is skipped in such a scene either way, and still worth not doing.
    // Callers render RS_NO_TILE as `none` rather than as a coordinate somebody might believe.
    if ((scene->flags & RS_SCENE_FLAG_NO_SURFACE_ANCHOR) != 0 || scene->unitsPerTile <= 0) {
        *rsX = RS_NO_TILE;
        *rsY = RS_NO_TILE;
        return;
    }
    const float perTile = (float)scene->unitsPerTile;
    *rsX = (int32_t)scene->rsOriginX + (int32_t)floorf(worldX / perTile);
    *rsY = (int32_t)scene->rsOriginY - (int32_t)floorf(worldZ / perTile);
}

bool RectContains(const RsZoneRect& r, int32_t rsX, int32_t rsY, float worldY) {
    if (rsX < r.x0 || rsX > r.x1 || rsY < r.y0 || rsY > r.y1) {
        return false;
    }
    return worldY >= (float)r.yMinUnits && worldY <= (float)r.yMaxUnits;
}

/*
 * Does this zone claim the point Link is standing on, in the scene he is standing in?
 *
 * Four shapes, in the order they are decided:
 *
 *   1. THE FALLBACK matches everywhere, in every opted-in scene, at any priority. It is what makes
 *      coverage total, so the director never has to answer "what plays here" with "nothing".
 *   2. A ZONE BOUND TO ANOTHER SCENE never matches. Checked before the rects, which is the whole
 *      point of the binding.
 *   3. A ZONE BOUND TO THIS SCENE WITH NO RECTS matches the whole scene (#90 section 4) - what an
 *      underground or interior area wants, since those are separate scenes anyway and should not be
 *      forced to share the surface queue.
 *   4. ANYTHING ELSE matches on its rects - but ONLY in a scene that has a surface anchor. A scene
 *      flagged RS_SCENE_FLAG_NO_SURFACE_ANCHOR has no meaningful position in the world frame, so
 *      `rsX`/`rsY` there are arithmetic on a zero origin rather than a location; letting a rect see
 *      them would let a surface zone win inside an interior for no visible reason.
 */
bool ZoneClaims(const RsMusicZone* z, int16_t sceneNum, bool hasSurfaceAnchor, int32_t rsX, int32_t rsY,
                float worldY) {
    if ((z->flags & RS_ZONE_FLAG_FALLBACK) != 0) {
        return true;
    }
    if (z->sceneId != RS_ZONE_SCENE_ANY && z->sceneId != sceneNum) {
        return false;
    }
    if (z->rectCount == 0) {
        return z->sceneId == sceneNum;
    }
    if (!hasSurfaceAnchor) {
        return false;
    }
    for (uint8_t r = 0; r < z->rectCount; r++) {
        if (RectContains(z->rects[r], rsX, rsY, worldY)) {
            return true;
        }
    }
    return false;
}

/*
 * The winning zone for a point: highest priority among the zones that claim it. Total coverage is
 * guaranteed by the fallback entry, so this only returns NO_ZONE if somebody removed the fallback
 * from the table - which the generator refuses to emit.
 *
 * Ties go to the earlier entry, which makes the answer a pure function of the table rather than of
 * iteration order elsewhere. The table is a handful of entries and this runs once a frame; a
 * spatial index would be premature at any size we are going to author by hand.
 */
int32_t ResolveZone(int16_t sceneNum, bool hasSurfaceAnchor, int32_t rsX, int32_t rsY, float worldY) {
    int32_t best = NO_ZONE;
    int32_t bestPriority = 0;
    const int32_t count = RsMusicZones_Count();
    for (int32_t i = 0; i < count; i++) {
        const RsMusicZone* z = RsMusicZones_At(i);
        if (!ZoneClaims(z, sceneNum, hasSurfaceAnchor, rsX, rsY, worldY)) {
            continue;
        }
        if (best == NO_ZONE || z->priority > bestPriority) {
            best = i;
            bestPriority = z->priority;
        }
    }
    return best;
}

bool HasSurfaceAnchor(const RsMusicScene* scene) {
    return (scene->flags & RS_SCENE_FLAG_NO_SURFACE_ANCHOR) == 0;
}

const char* ZoneName(int32_t index) {
    const RsMusicZone* z = RsMusicZones_At(index);
    return z != nullptr ? z->name : "none";
}

// --- the shuffle bag ----------------------------------------------------------------------------

/*
 * One bag per zone (#90 section 8): shuffle the zone's track list, play through it, reshuffle when
 * it empties.
 *
 * IN MEMORY, PER ZONE, AND IT SURVIVES LEAVING THE ZONE. ResetState() deliberately does not clear
 * these - see the note there. A bag reset on every scene load or every activation would make a zone
 * you dip in and out of replay its opener forever, which is the failure the persistence exists to
 * prevent. It is discarded on reload and is NOT save data; it is a preference about what to hear
 * next, not a fact about the world.
 *
 * Fixed-size statics rather than an allocation, sized by RS_MUSIC_MAX_ZONES /
 * RS_MUSIC_MAX_TRACKS_PER_ZONE. The generator reads those two numbers out of MusicZones.h and
 * refuses a table that exceeds either, so the clamps below are unreachable by anything the
 * generator produced - they are there so a hand-edited table degrades to P1 behaviour instead of
 * reading past the array.
 */
struct TrackBag {
    uint8_t order[RS_MUSIC_MAX_TRACKS_PER_ZONE]; // indices into the zone's track list, shuffled
    uint8_t size;                                // entries in `order`
    uint8_t next;                                // next index to draw; next == size means empty
    /*
     * What this zone played last, for the guard below. NULL means nothing yet this session.
     *
     * A POINTER INTO THE GENERATED TABLE, not a copy and not an id. The table is static storage
     * that outlives every scene load, so the pointer stays valid exactly as long as the bag does.
     * It was a uint16_t seqId through P2; #90 P3 gave every imported track the same placeholder id,
     * which made an id comparison stop identifying a track at all - see SameTrack.
     */
    const RsZoneTrack* last;
};

TrackBag sBags[RS_MUSIC_MAX_ZONES];

/*
 * A private xorshift32, NOT the game's Rand_Next().
 *
 * Rand_Next() drives the shared RNG stream that actor behaviour draws from, so taking numbers out
 * of it here would perturb gameplay in a way that depends on how often the music happened to
 * change. Nothing about track choice needs a good generator; it needs one that is ours.
 *
 * SEEDED FROM A WALL CLOCK, NOT FROM THE FRAME COUNTER. It was seeded from the frame counter and
 * that was wrong in a way only a run catches: the first bag of a session is filled on the
 * scene-load activation, when state.frames is still ZERO, so every session shuffled identically.
 * Two consecutive runs on 2026-09-09 produced the same order (3,2,0,1) at the same frame numbers,
 * which is a "shuffle" with the same complaint the feature exists to fix, one level up. The frame
 * counter is still mixed in; it is the clock that carries the entropy.
 *
 * Forced non-zero because xorshift is stuck at zero.
 */
uint32_t sRngState = 0;

uint32_t NextRandom() {
    if (sRngState == 0) {
        const uint64_t now = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
        const uint32_t frames = (gPlayState != nullptr) ? (uint32_t)gPlayState->state.frames : 0u;
        sRngState = (uint32_t)(now ^ (now >> 32)) ^ (frames * 2654435761u) ^ 0x9E3779B9u;
        if (sRngState == 0) {
            sRngState = 0x1D872B41u;
        }
    }
    sRngState ^= sRngState << 13;
    sRngState ^= sRngState >> 17;
    sRngState ^= sRngState << 5;
    return sRngState;
}

void RefillBag(TrackBag* bag, const RsMusicZone* z) {
    uint8_t n = z->trackCount;
    if (n > RS_MUSIC_MAX_TRACKS_PER_ZONE) {
        n = RS_MUSIC_MAX_TRACKS_PER_ZONE; // unreachable: the generator refuses a longer list
    }
    for (uint8_t i = 0; i < n; i++) {
        bag->order[i] = i;
    }
    // Fisher-Yates, back to front.
    for (uint8_t i = n; i > 1; i--) {
        const uint8_t j = (uint8_t)(NextRandom() % i);
        const uint8_t tmp = bag->order[i - 1];
        bag->order[i - 1] = bag->order[j];
        bag->order[j] = tmp;
    }
    bag->size = n;
    bag->next = 0;

    /*
     * THE BACK-TO-BACK GUARD, AND IT IS THE ENTIRE POINT OF USING A BAG RATHER THAN A DIE.
     *
     * A bag already guarantees no repeat WITHIN a cycle. The one place a repeat can still happen is
     * across the seam: the last track of one bag and the first of the next. Pure random repeats
     * often enough to be noticeable, and "the music repeats" is the complaint this whole feature
     * exists to fix, so a bag that leaves the seam unguarded gives most of the benefit away at the
     * one moment a listener is most likely to notice.
     *
     * This is the first line a later "simplification" deletes, because it looks like a special
     * case bolted onto a clean shuffle. It is not. If you delete it, the seam repeat comes back.
     *
     * A zone with one track cannot avoid the repeat and should not try: with an imported
     * non-looping track (#91) a one-track zone with a duration means "when it ends, play it again",
     * which is exactly right. A list authored with the same track twice is handled too - the scan
     * skips past any entry that would repeat it, not only past the entry that was drawn.
     *
     * WHAT "THE SAME TRACK" MEANS IS NOT "THE SAME seqId" - see SameTrack. Every imported track
     * carries the same placeholder, so an id comparison here would report `unavoidable` on every
     * refill of an RS zone: the guard silently unable to act, wearing the outcome that means it
     * correctly could not.
     */
    const RsZoneTrack* shuffledFirst = (n > 0) ? &z->tracks[bag->order[0]] : nullptr;
    const char* guard = "not_needed";
    if (bag->last == nullptr) {
        guard = "no_last"; // first fill of the session: there is no just-played track to repeat
    } else if (n <= 1) {
        guard = "single"; // see above - a one-track zone repeating itself is correct, not a miss
    } else if (SameTrack(shuffledFirst, bag->last)) {
        // Overwritten below unless every OTHER entry is the same track too - a zone authored
        // as several copies of one track, where the repeat is arithmetic rather than a miss.
        guard = "unavoidable";
        const uint8_t start = (uint8_t)(NextRandom() % (uint32_t)(n - 1));
        for (uint8_t k = 0; k < n - 1; k++) {
            const uint8_t j = (uint8_t)(1 + ((start + k) % (n - 1)));
            if (!SameTrack(&z->tracks[bag->order[j]], bag->last)) {
                const uint8_t tmp = bag->order[0];
                bag->order[0] = bag->order[j];
                bag->order[j] = tmp;
                guard = "swapped";
                break;
            }
        }
    }

    /*
     * WHY THE REFILL IS LOGGED AT ALL, and it is not decoration.
     *
     * "No back-to-back repeat happened" is a passing count that proves nothing on its own: a
     * shuffle that never put the repeated track first would produce exactly the same log with the
     * guard deleted. So the line says what the raw shuffle proposed (`shuffled=`), what the zone
     * played last (`last=`), and what the guard did about it (`guard=`) - which makes
     * `guard=swapped` <=> `shuffled == last` checkable from one line, and makes a run able to show
     * the guard ACTING rather than merely not being needed.
     */
    char order[RS_MUSIC_MAX_TRACKS_PER_ZONE * 4 + 8];
    size_t used = 0;
    order[0] = '\0';
    for (uint8_t i = 0; i < n && used + 5 < sizeof(order); i++) {
        const int written =
            std::snprintf(order + used, sizeof(order) - used, i == 0 ? "%d" : ",%d", (int32_t)bag->order[i]);
        if (written <= 0) {
            break;
        }
        used += (size_t)written;
        if (used >= sizeof(order)) {
            used = sizeof(order) - 1;
            break;
        }
    }
    char last[48];
    FormatTrackOr(last, sizeof(last), bag->last, "none");
    char proposed[48];
    FormatTrackOr(proposed, sizeof(proposed), shuffledFirst, "none");
    RecordEvent("refill zone=%s size=%d last=%s shuffled=%s guard=%s order=%s frame=%u", z->name, (int32_t)n, last,
                proposed, guard, order, gPlayState != nullptr ? gPlayState->state.frames : 0u);
}

/*
 * What a track choice hands back: WHICH ENTRY, and where in the bag it came from so the marker
 * channel can carry a reconstructable cycle. bagPos is 1-based; 0 means "not drawn from the bag" -
 * the first-visit seed, or silence.
 *
 * The entry rather than a copy of its fields, because since #90 P3 a track is four numbers and a
 * string and every consumer wants a different subset. It points into the static generated table.
 * NULL means authored silence, which is a legitimate choice rather than an error.
 */
struct TrackPick {
    const RsZoneTrack* entry;
    uint8_t bagPos;
    uint8_t bagSize;
};

/*
 * Draw the next track out of a zone's bag, refilling it if it is empty.
 *
 * THIS FUNCTION CANNOT SPEND A FIRST-VISIT FLAG, and that is structural rather than a convention:
 * it never touches Flags_GetWorldFlag, Flags_SetWorldFlag or z->firstVisitTrack, so no future
 * caller can reach the opener through it however it is called. #90 P2 asks for exactly that
 * property, because P2 adds a SECOND entry point into track selection (EndOfTrack, which advances
 * within a zone without a switch) and P1's finding 3 is that SelectTrack stopped being safe to call
 * twice the moment it started spending a flag. Two functions, one of which is pure with respect to
 * save state, is the fix - not "the flag will already be set by then".
 */
TrackPick NextFromBag(int32_t zoneIndex) {
    TrackPick pick = { nullptr, 0, 0 };
    const RsMusicZone* z = RsMusicZones_At(zoneIndex);
    if (z == nullptr || z->trackCount == 0) {
        return pick; // an empty track list is authored silence, not an error
    }
    if (zoneIndex >= RS_MUSIC_MAX_ZONES) {
        // Unreachable: the generator refuses a table with more zones than this. Degrade to P1's
        // behaviour rather than index past sBags.
        pick.entry = &z->tracks[0];
        return pick;
    }

    TrackBag* bag = &sBags[zoneIndex];
    if (bag->next >= bag->size) {
        RefillBag(bag, z);
    }
    if (bag->size == 0) {
        return pick;
    }
    const uint8_t entry = bag->order[bag->next++];
    pick.entry = &z->tracks[entry];
    pick.bagPos = bag->next; // 1-based: "this was draw N of size"
    pick.bagSize = bag->size;
    bag->last = pick.entry;
    return pick;
}

/*
 * Which track of the zone to play on ACTIVATION, and THE ONE PLACE A FIRST-VISIT FLAG IS SPENT.
 *
 * CALL IT EXACTLY ONCE PER SWITCH. It is not a pure function: the first-visit branch writes a
 * world flag, so calling it twice - to fill in a log line, say - would burn the opener on a switch
 * that only ever played it once. BeginSwitch calls it once and everything downstream reads
 * sPendingEntry.
 *
 * WHEN THE FLAG IS SPENT is the design point (#90 section 10): on ACTIVATION, which is this call,
 * inside BeginSwitch, after the dwell has already expired. Not on boundary contact - clipping the
 * corner of a zone never reaches here, because clipping never produces a switch. And there is no
 * forced completion: the flag is gone the moment the opener starts, so walking straight back out
 * spends it. Enter, hear thirty seconds, leave, return - you get the normal track, which is exactly
 * what "first visit" should mean.
 *
 * Otherwise it draws from the zone's shuffle bag. The opener is a SEED rather than a special case
 * beside the bag: playing it records it as the zone's just-played track, so the bag's very first
 * draw cannot repeat it. That is the only coupling between the two, and it runs one way.
 */
TrackPick SelectTrack(int32_t zoneIndex, bool* firstVisit) {
    *firstVisit = false;
    const RsMusicZone* z = RsMusicZones_At(zoneIndex);
    if (z == nullptr || z->trackCount == 0) {
        const TrackPick silence = { nullptr, 0, 0 };
        return silence;
    }
    if (z->firstVisitTrack != nullptr && z->firstVisitFlag != RS_ZONE_NO_FIRST_VISIT &&
        !Flags_GetWorldFlag(z->firstVisitFlag)) {
        Flags_SetWorldFlag(z->firstVisitFlag);
        *firstVisit = true;
        const TrackPick opener = { z->firstVisitTrack, 0, 0 };
        // The opener counts as the just-played track for the back-to-back guard, so the bag's first
        // draw after it cannot repeat it. bagPos stays 0 - it was not drawn from the bag, and the
        // marker channel renders that as bag=seed rather than as a position it never occupied.
        if (zoneIndex < RS_MUSIC_MAX_ZONES) {
            sBags[zoneIndex].last = z->firstVisitTrack;
        }
        return opener;
    }
    return NextFromBag(zoneIndex);
}

/* `bag=` for the marker channel and the console: the 1-based draw and the bag's size, or `seed`
 * for the first-visit opener (which is not a draw), or `none` for authored silence. One formatter
 * so the `transition` line, the `advance` line and `status` cannot disagree about what "not from
 * the bag" looks like - the same reason RsMusic_FormatTiles exists. */
void FormatBag(char* buf, uint32_t size, uint8_t pos, uint8_t bagSize, bool firstVisit) {
    if (pos == 0) {
        // "%s" rather than handing the choice straight to snprintf as the format - it is not a
        // literal there, and a non-literal format is the shape -Wformat-security exists to catch.
        std::snprintf(buf, size, "%s", firstVisit ? "seed" : "none");
        return;
    }
    std::snprintf(buf, size, "%d/%d", (int32_t)pos, (int32_t)bagSize);
}

/* `audio_seq=` - the custom sequence number an imported track resolved to THIS boot. `none` for a
 * vanilla entry, which has no such number; `unresolved` when the path was not found, which is a
 * fault and is also reported on its own line by ApplyPick. Never a bare -1: a number on a key=value
 * line is a thing a reader believes. */
void FormatAudioSeq(char* buf, uint32_t size, const RsZoneTrack* entry, int32_t audioSeq) {
    if (entry == nullptr || entry->rsPath == nullptr) {
        std::snprintf(buf, size, "none");
        return;
    }
    if (audioSeq < 0) {
        std::snprintf(buf, size, "unresolved");
        return;
    }
    std::snprintf(buf, size, "0x%X", (uint32_t)audioSeq);
}

/* `end_fade_ms=` - `global` for a track whose duration is a policy cut and which therefore takes
 * RsMusicFadeOutSec, a number (including 0) for one whose duration is where the music ends. Spelled
 * out rather than printed as 65535, which reads as a fade sixty-five seconds long. */
void FormatEndFade(char* buf, uint32_t size, const RsZoneTrack* entry) {
    if (entry == nullptr) {
        std::snprintf(buf, size, "none");
        return;
    }
    if (entry->endFadeMs == RS_TRACK_END_FADE_GLOBAL) {
        std::snprintf(buf, size, "global");
        return;
    }
    std::snprintf(buf, size, "%u", (uint32_t)entry->endFadeMs);
}

// --- driving player 0 ---------------------------------------------------------------------------

/* Player 0 is no longer carrying our track - it has been stopped, or it went quiet. Clears the
 * "what is playing" half of the state so the console cannot report a length, a bag position and
 * sounding=1 for a track that has already been told to stop. It read that way through the first P2
 * run: `status.track track=0xFFFF ... sounding=1` during the fade of a switch (`track=none` now that
 * tracks are named), which is the same class of thing as the P1 run's rs=-2147483648 - a field a
 * reader believes. */
void MarkNothingPlaying() {
    sPlayingEntry = nullptr;
    sPlayingSeqId = NA_BGM_DISABLED;
    sPlayingBagPos = 0;
    sPlayingBagSize = 0;
    sPlayingFirstVisit = false;
    sPlayingAudioSeq = -1;
    sSounding = false;
    sLastCounter = 0;
}

/* The playing track's authored duration, in milliseconds, or 0 for "plays until stopped" (which is
 * also what silence answers). One accessor so no caller has to remember that sPlayingEntry may be
 * NULL. */
uint32_t PlayingDurationMs() {
    return sPlayingEntry != nullptr ? sPlayingEntry->durationMs : 0u;
}

/*
 * Turn a chosen entry into the pending state, RESOLVING AN IMPORTED TRACK'S PATH here rather than at
 * start time. Both entry points into track selection go through this, so there is one place that
 * knows how a pick becomes something playable.
 *
 * A path that does not resolve is left as sPendingUnresolved and handled by StartTrack; it is never
 * silently skipped, because "the zone went quiet" with nothing in the log is the failure this whole
 * feature's observability exists to prevent.
 */
void ApplyPick(const TrackPick& pick, bool firstVisit) {
    sPendingEntry = pick.entry;
    sPendingFirstVisit = firstVisit;
    sPendingBagPos = pick.bagPos;
    sPendingBagSize = pick.bagSize;
    sPendingAudioSeq = -1;
    sPendingUnresolved = false;
    if (pick.entry == nullptr || pick.entry->rsPath == nullptr) {
        return; // authored silence, or a vanilla id, which needs no lookup
    }
    const char* error = nullptr;
    const int32_t seq = RsMusic_ResolveTrackPath(pick.entry->rsPath, &error);
    if (seq < 0) {
        sPendingUnresolved = true;
        RecordEvent("track_unresolved zone=%s path=\"%s\" error=%s frame=%u", ZoneName(sActiveZone),
                    pick.entry->rsPath, error != nullptr ? error : "unknown",
                    gPlayState != nullptr ? gPlayState->state.frames : 0u);
        return;
    }
    sPendingAudioSeq = seq;
}

void StartTrack(int32_t fadeInUnits) {
    const RsZoneTrack* entry = sPendingEntry;
    sPlayingEntry = entry;
    sPlayingFirstVisit = sPendingFirstVisit;
    sPlayingBagPos = sPendingBagPos;
    sPlayingBagSize = sPendingBagSize;
    sPlayingAudioSeq = sPendingAudioSeq;
    // The duration is not counted until the track has been SEEN sounding - that is what tells "the
    // track ended" from "the track never started" - but it is counted from the sequence player's
    // own counter, which the load zeroed, so nothing before that moment is lost. See sLastCounter.
    sSounding = false;
    sLastCounter = 0;
    sClockResetLogged = false;

    /*
     * Two ways to end up with nothing to play, and they are NOT the same thing.
     *
     * Authored silence (no entry) is a legitimate choice a zone makes - #90 section 3. An RS path
     * that did not resolve is a fault, already named on the marker channel by ApplyPick; the zone
     * goes audibly quiet rather than playing something arbitrary, so the failure is visible from
     * the speakers as well as the log, and the next switch retries the lookup. Both land in
     * STATE_SILENT because the director's behaviour from here is identical: hold, and do not treat
     * player 0's quiet as a track that ended.
     */
    if (entry == nullptr || sPendingUnresolved) {
        sState = STATE_SILENT;
        // sPlayingEntry is deliberately KEPT for an unresolved track, so `rsmusic status` reads
        // `state=silent track=rs:dream audio_seq=unresolved` rather than `track=none` - "which
        // track failed" is the first question a silent zone raises, and the marker that answered it
        // has by then scrolled past. Nothing acts on it: the duration trigger needs STATE_PLAYING.
        sPlayingSeqId = NA_BGM_DISABLED;
        // Vanilla's "no music here" value, so anything reading the save context's idea of the
        // ambient track agrees with what the player can hear.
        gSaveContext.seqId = NA_BGM_NO_MUSIC;
        return;
    }

    /*
     * THE BACK DOOR, for an imported track (#91): the real custom number goes in seqToPlay with
     * seqReplaced set, and the placeholder rides through SEQCMD beside it, because SEQCMD masks its
     * id field to 8 bits. Audio_StartSequence prefers seqToPlay and CLEARS seqReplaced as it takes
     * it.
     *
     * Set immediately before the command that consumes it, and never earlier: a pending seqReplaced
     * goes to whichever play reaches player 0 next, and the audio thread's own opcodes 0xEB/0xC6
     * can read it too. `replaced=` on `rsmusic players` is the witness if one is ever left behind.
     */
    if (entry->rsPath != nullptr && sPendingAudioSeq >= 0) {
        gAudioContext.seqToPlay[SEQ_PLAYER_BGM_MAIN] = (u16)sPendingAudioSeq;
        gAudioContext.seqReplaced[SEQ_PLAYER_BGM_MAIN] = 1;
    }
    SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, fadeInUnits, 0, entry->seqId);
    sPlayingSeqId = entry->seqId;
    sState = STATE_PLAYING;
    sGraceTicks = START_GRACE_TICKS;
    // The gap is spent. Zeroing it is not bookkeeping for its own sake: STATE_FADING exits by
    // decrementing PAST zero, so a switch whose gap was 0 left `status.tuning gap=-1` behind for
    // anyone who looked - a negative number of ticks remaining, which is not a thing. Seen on the
    // 2026-09-17 P3 run after an endFadeSec 0 advance.
    sGapTicks = 0;
    // Keep the save context in step with what the director considers the ambient track:
    // Audio_SetSequenceMode reads gActiveSeqs[0].seqId to decide whether combat music may duck,
    // and z_play.c clears gSaveContext.seqId on transitions expecting the loader to have set it.
    // For an imported track this is the placeholder, which is the same value Audio_StartSequence
    // itself stores in gActiveSeqs[0].seqId - so the two agree by construction rather than by luck.
    gSaveContext.seqId = (u8)(entry->seqId & 0xFF);
}

/*
 * Take the switch: stop what is playing with a fade, wait that fade out, then start the new track
 * with its own fade - the sequential shape the engine forces (see ZoneDirector.h). If nothing was
 * playing there is nothing to fade out of and no reason to sit in silence, so the gap is skipped
 * and the new track starts on the same tick.
 *
 * The "beat of quiet" #90 asks for is not a separate wait, and does not need to be: it is the tail
 * of the outgoing fade plus the head of the incoming one, which starts from actual silence. There
 * is a real perceptual gap in the middle of that even though the two commands are back to back. If
 * a longer, deliberate silence turns out to sound better, it is a fourth CVar and about four lines
 * - do that rather than lengthening the fades, which changes a different thing.
 */
void BeginSwitch(int32_t zoneIndex, const char* reason, int32_t rsX, int32_t rsY, const Vec3f& pos) {
    const float fadeOutSec = CVarGetFloat(CVAR_RS_MUSIC_FADE_OUT, RS_MUSIC_FADE_OUT_DEFAULT);
    const float fadeInSec = CVarGetFloat(CVAR_RS_MUSIC_FADE_IN, RS_MUSIC_FADE_IN_DEFAULT);
    const int32_t fadeOutUnits = FadeUnits(fadeOutSec);
    const int32_t fadeInUnits = FadeUnits(fadeInSec);
    // Something is sounding on player 0 that this switch has to fade out of: our own track, or - on
    // a warp out of a yield, the one place BeginSwitch is reached while YIELDED - the override's.
    // Without the second half that warp hard-cut the override (gap=0) instead of fading it like any
    // other switch.
    const bool wasSounding = (sState == STATE_PLAYING) ||
                             (sState == STATE_YIELDED && func_800FA0B4(SEQ_PLAYER_BGM_MAIN) != NA_BGM_DISABLED);

    const int32_t from = sActiveZone;
    sTransitions++;
    sActiveZone = zoneIndex;
    sCandidateZone = NO_ZONE;
    sDwellTicks = 0;

    // Choose once, here. See SelectTrack: the first-visit branch spends a world flag, so the track
    // has to be decided at the moment of activation and then carried, not re-derived. ApplyPick
    // also resolves an imported track's path, so a `track_unresolved` marker lands just before the
    // transition it belongs to rather than half a second later inside the quiet gap.
    //
    // TWO STATEMENTS, NOT ONE CALL. Writing this as `ApplyPick(SelectTrack(zoneIndex, &firstVisit),
    // firstVisit)` compiles and is wrong: the order in which a call's arguments are evaluated is
    // unspecified, and MSVC evaluates right to left, so `firstVisit` was read BEFORE SelectTrack
    // had set it. The opener still played and still spent its flag - only every report of it lied,
    // with `first_visit=0 bag=none` on the transition line and no `first_visit` marker at all. The
    // 2026-09-17 P3 run caught it against `rsmusic firstvisit`, which read set=1 a second later.
    bool firstVisit = false;
    const TrackPick pick = SelectTrack(zoneIndex, &firstVisit);
    ApplyPick(pick, firstVisit);

    if (wasSounding) {
        // Op 1, "disable seq player", with a fade. A duration of 0 here is an immediate disable
        // rather than a fade, which is exactly what a fade-out of zero seconds should mean.
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, fadeOutUnits);
        MarkNothingPlaying();
        sGapTicks = SecondsToTicks(fadeOutSec);
        sState = STATE_FADING;
    } else {
        sGapTicks = 0;
        StartTrack(fadeInUnits);
    }

    char tiles[32];
    RsMusic_FormatTiles(tiles, sizeof(tiles), rsX, rsY);
    char bag[16];
    FormatBag(bag, sizeof(bag), sPendingBagPos, sPendingBagSize, sPendingFirstVisit);
    char track[48];
    FormatTrackOr(track, sizeof(track), sPendingEntry, "silence");
    char audioSeq[16];
    FormatAudioSeq(audioSeq, sizeof(audioSeq), sPendingEntry, sPendingAudioSeq);
    char endFade[16];
    FormatEndFade(endFade, sizeof(endFade), sPendingEntry);
    RecordEvent("transition from=%s to=%s track=%s seq=0x%X audio_seq=%s first_visit=%d bag=%s len_ms=%u "
                "end_fade_ms=%s reason=%s fade_out=%d fade_in=%d gap=%d rs=%s pos=%.0f,%.0f,%.0f n=%d frame=%u",
                from == NO_ZONE ? "none" : ZoneName(from), ZoneName(zoneIndex), track,
                sPendingEntry != nullptr ? (uint32_t)sPendingEntry->seqId : (uint32_t)NA_BGM_DISABLED, audioSeq,
                sPendingFirstVisit ? 1 : 0, bag, sPendingEntry != nullptr ? sPendingEntry->durationMs : 0u, endFade,
                reason, fadeOutUnits, fadeInUnits, sGapTicks, tiles, pos.x, pos.y, pos.z, sTransitions,
                gPlayState != nullptr ? gPlayState->state.frames : 0u);

    // Logged after the transition it belongs to, so the channel reads in the order things happened.
    // This line fires on exactly the tick the flag is spent, and never for a boundary Link merely
    // clipped - clipping produces no switch, so it never reaches BeginSwitch at all. That is the
    // assertion the agent loop makes about the first-visit rule; `rsmusic firstvisit` is the other
    // half, reading the flag back.
    if (sPendingFirstVisit) {
        const RsMusicZone* z = RsMusicZones_At(zoneIndex);
        RecordEvent("first_visit zone=%s flag=%d track=%s reason=%s frame=%u", ZoneName(zoneIndex),
                    (int32_t)z->firstVisitFlag, track, reason,
                    gPlayState != nullptr ? gPlayState->state.frames : 0u);
    }
}

/*
 * When, on the audio clock, this track's in-zone advance should fire - in milliseconds since the
 * track was first heard.
 *
 * FOR A VANILLA ENTRY THAT IS SIMPLY ITS DURATION. The duration is a policy cut into a looping
 * song, the fade is the global one, and there is nothing to land on.
 *
 * FOR AN IMPORTED TRACK IT IS `endFadeMs` EARLIER, because the fade is aimed at the ending rather
 * than applied after it (#90 comment of 2026-09-17; #91 ADR section 7a). The advance IS the start
 * of the fade: there is no second event at durationMs. RS exports end inconsistently - Flute Salad
 * carries ~10 s of baked fade, Autumn Voyage stops dead - so a fade that begins when the music has
 * already stopped ramps a silence and leaves the abrupt ending abrupt.
 *
 * The fade then runs about 2% longer than its nominal seconds (the units assume 180 audio updates a
 * second and this build runs 176), so a nominal 5 s ramp reaches zero about 0.1 s AFTER the music
 * ends. That is deliberately not compensated for: the sequence player is still running at that
 * point - it does not stop until ~2% past its authored `Length`, which is at or beyond `audioSec` -
 * so the overhang lands in the track's own silent tail and cuts nothing. Compensating would mean
 * baking this machine's tick rate into the arithmetic to move an inaudible 0.1 s.
 */
uint32_t AdvanceAtMs(const RsZoneTrack* t) {
    if (t == nullptr || t->durationMs == 0) {
        return 0; // "plays until stopped": there is no advance point
    }
    if (t->endFadeMs == RS_TRACK_END_FADE_GLOBAL || t->endFadeMs >= t->durationMs) {
        // The generator refuses a fade at least as long as the track, so the second half is a
        // hand-edited-table guard: fire at the end rather than at a negative time.
        return t->durationMs;
    }
    return t->durationMs - t->endFadeMs;
}

/*
 * Has the playing track reached its advance point?
 *
 * durationMs 0 means "plays until stopped" and always answers no - which is what every zone did
 * through P1, and a zone whose tracks are all 0 therefore never advances at all.
 *
 * The clock is the audio thread's own - the sequence player's counter, zeroed when the track was
 * loaded onto it, so it measures the music rather than the queue. See sLastCounter for why it is
 * read directly rather than differenced against a baseline.
 */
bool DurationExpired() {
    if (PlayingDurationMs() == 0 || !sSounding || sState != STATE_PLAYING) {
        return false;
    }
    if (!HasAudioClock()) {
        return false; // audio heap not initialised: no clock, so no duration trigger
    }
    const uint32_t now = ScriptCounter();
    if (now < sLastCounter && !sClockResetLogged) {
        /*
         * THE CLOCK WENT BACKWARDS. Sequence opcode 0xC5 writes scriptCounter outright
         * (`seqPlayer->scriptCounter = (u16)AudioSeq_ScriptReadS16(seqScript)` in
         * AudioSeq_SequencePlayerProcessSequence), so the sequence DATA can move this clock under
         * us. No vanilla sequence is known to use the opcode, and it is recorded as a hazard rather
         * than as something observed.
         *
         * PURELY A DIAGNOSTIC NOW, and that is the point of reading the counter directly. Through
         * P2 this was a real hazard: the clock was `now - baseline`, and unsigned subtraction on a
         * backwards jump produced roughly four billion ticks and advanced the whole bag at frame
         * rate. With no subtraction, a backwards jump just means the duration fires later - so
         * this says so once and changes nothing. A FORWARD jump would advance early and is still
         * not detectable from outside.
         */
        sClockResetLogged = true;
        char track[48];
        FormatTrackOr(track, sizeof(track), sPlayingEntry, "none");
        RecordEvent("clock_reset zone=%s track=%s counter=%u was=%u frame=%u", ZoneName(sActiveZone), track, now,
                    sLastCounter, gPlayState != nullptr ? gPlayState->state.frames : 0u);
    }
    sLastCounter = now;
    return ScriptTicksToMs(now) >= (uint64_t)AdvanceAtMs(sPlayingEntry);
}

/*
 * THE ONE END-OF-TRACK HANDLER. Two triggers, one event, one place that decides what happens next.
 *
 * `trigger` is `duration` (the authored lengthSec ran out) or `quiet` (player 0 stopped on its
 * own), whichever came first. They are not two behaviours: a track ending is one thing, and giving
 * it two code paths is how the fade decision ends up being made twice and differently.
 *
 * WHICH ALSO SETTLES THE FADE. If the trigger was `quiet` there is nothing sounding to fade out of,
 * so the next track starts on the same tick. If it was `duration`, ask func_800FA0B4 whether player
 * 0 is still making noise and fade out if it is. One decision point rather than a rule per trigger.
 *
 * WHAT THIS DOES NOT MEAN: the duration table is still THE mechanism (#90 section 9). The end-of-
 * track signal is used opportunistically because it happens to exist, never depended on - vanilla
 * BGM appears never to end on its own, and that is recorded as INFERRED, not byte-verified. A zone
 * whose tracks are all lengthSec 0 relies on nothing here and simply never advances.
 *
 * IT IS NOT A ZONE SWITCH and does not count as one. sTransitions stays put; sAdvances moves. See
 * the declaration of sAdvances for why that separation is load-bearing.
 */
void EndOfTrack(const char* trigger, int32_t rsX, int32_t rsY, const Vec3f& pos) {
    const float fadeInSec = CVarGetFloat(CVAR_RS_MUSIC_FADE_IN, RS_MUSIC_FADE_IN_DEFAULT);
    const int32_t fadeInUnits = FadeUnits(fadeInSec);
    const RsZoneTrack* previous = sPlayingEntry; // captured before MarkNothingPlaying clears it

    /*
     * WHOSE FADE IS THIS? The one place in the director where the answer is not RsMusicFadeOutSec.
     *
     * A vanilla entry's duration is a policy cut into a song that would have gone on, so the global
     * fade is right - it is the same kind of interruption a zone change is. An imported track's
     * duration is where the music ENDS, and the advance has already been brought forward by
     * endFadeMs (see AdvanceAtMs), so the ramp that starts here is the track's own ending and must
     * be exactly that long. endFadeMs 0 then means "let the track's own ending play": a stop with a
     * fade of zero is an immediate disable, and the music is over anyway.
     *
     * Only this trigger. Zone changes, teleports and override releases go through BeginSwitch and
     * keep the global fade, because they happen mid-song.
     */
    const bool endFadeIsOwn = previous != nullptr && previous->endFadeMs != RS_TRACK_END_FADE_GLOBAL;
    const uint32_t fadeOutMs = endFadeIsOwn
                                   ? (uint32_t)previous->endFadeMs
                                   : (uint32_t)lroundf(ClampSeconds(CVarGetFloat(CVAR_RS_MUSIC_FADE_OUT,
                                                                                 RS_MUSIC_FADE_OUT_DEFAULT)) *
                                                       1000.0f);

    // NextFromBag, not SelectTrack: this is the second entry point into track selection that #90 P2
    // warns about, and it is structurally unable to reach the first-visit branch. See NextFromBag.
    ApplyPick(NextFromBag(sActiveZone), false);
    sAdvances++;

    const bool stillSounding = func_800FA0B4(SEQ_PLAYER_BGM_MAIN) != NA_BGM_DISABLED;
    int32_t fadeOutUnits = 0;
    if (stillSounding) {
        fadeOutUnits = FadeUnitsMs(fadeOutMs);
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, fadeOutUnits);
        MarkNothingPlaying();
        sGapTicks = TicksFromMs(fadeOutMs);
        sState = STATE_FADING;
    } else {
        sGapTicks = 0;
        StartTrack(fadeInUnits);
    }

    char tiles[32];
    RsMusic_FormatTiles(tiles, sizeof(tiles), rsX, rsY);
    char bag[16];
    FormatBag(bag, sizeof(bag), sPendingBagPos, sPendingBagSize, false);
    char fromTrack[48];
    FormatTrackOr(fromTrack, sizeof(fromTrack), previous, "none");
    char track[48];
    FormatTrackOr(track, sizeof(track), sPendingEntry, "silence");
    char audioSeq[16];
    FormatAudioSeq(audioSeq, sizeof(audioSeq), sPendingEntry, sPendingAudioSeq);
    char endFade[16];
    FormatEndFade(endFade, sizeof(endFade), sPendingEntry);
    // Everything a full bag cycle needs to be reconstructed FROM THE LOG: which zone, what stopped,
    // what started, which trigger fired, and where in the bag the new track came from. Polling for
    // this would cost a harness round trip per sample, and a cycle is minutes long.
    //
    // fade_source= says which fade the line above used, so "the end fade fired" is checkable from
    // one line instead of by comparing fade_out= against a CVar the reader has to go and look up.
    RecordEvent("advance zone=%s from=%s track=%s seq=0x%X audio_seq=%s trigger=%s bag=%s len_ms=%u end_fade_ms=%s "
                "fade_out=%d fade_source=%s fade_in=%d gap=%d rs=%s pos=%.0f,%.0f,%.0f n=%d frame=%u",
                ZoneName(sActiveZone), fromTrack, track,
                sPendingEntry != nullptr ? (uint32_t)sPendingEntry->seqId : (uint32_t)NA_BGM_DISABLED, audioSeq,
                trigger, bag, sPendingEntry != nullptr ? sPendingEntry->durationMs : 0u, endFade, fadeOutUnits,
                endFadeIsOwn ? "track_end" : "global", fadeInUnits, sGapTicks, tiles, pos.x, pos.y, pos.z, sAdvances,
                gPlayState != nullptr ? gPlayState->state.frames : 0u);
}

/*
 * Taking player 0 back from an override on a warp - and the one write the mod makes to vanilla's
 * audio state.
 *
 * func_800F5ACC stashes what was on player 0 in sPrevMainBgmSeqId so that func_800F5B58 can hand it
 * back when the fight ends, and while that stash is set Audio_SetSequenceMode does nothing at all:
 * enemy music is off. If the director takes the player back first, nothing ever releases the stash.
 * func_800F5B58 later finds our new track (no SEQ_FLAG_RESTORE), restores nothing and clears nothing,
 * and enemy music stays dead until the next scene load - the #90 P5 run caught exactly that
 * (docs/test-runs/2026-09-11-zone-music-p5, scenario E).
 *
 * So the stash is cleared here, but ONLY when it holds our own track, which is the stash this reclaim
 * supersedes. Anything else in it belongs to somebody else and is left alone (`stash=kept`). With it
 * cleared, the mini-boss's eventual func_800F5B58 is a no-op - its first test is a set stash - which
 * is right: its fight music has already been faded out.
 *
 * Logged so a run can show which happened; the transition that follows carries reason=teleport.
 */
void ReclaimFromOverride(int32_t winner, const char* reason, int32_t rsX, int32_t rsY) {
    const uint16_t theirs = func_800FA0B4(SEQ_PLAYER_BGM_MAIN);
    const bool stashIsOurs = (sPlayingSeqId != NA_BGM_DISABLED && sPrevMainBgmSeqId == sPlayingSeqId);
    if (stashIsOurs) {
        sPrevMainBgmSeqId = NA_BGM_DISABLED;
    }
    char tiles[32];
    RsMusic_FormatTiles(tiles, sizeof(tiles), rsX, rsY);
    RecordEvent("warp_reclaim zone=%s winner=%s theirs=0x%X stash=%s reason=%s rs=%s frame=%u",
                ZoneName(sActiveZone), ZoneName(winner), theirs, stashIsOurs ? "cleared" : "kept", reason, tiles,
                gPlayState != nullptr ? gPlayState->state.frames : 0u);
}

void ResetState() {
    sState = STATE_IDLE;
    sActiveZone = NO_ZONE;
    sCandidateZone = NO_ZONE;
    sDwellTicks = 0;
    sGapTicks = 0;
    sGraceTicks = 0;
    sPendingEntry = nullptr;
    sPendingFirstVisit = false;
    sPendingBagPos = 0;
    sPendingBagSize = 0;
    sPendingAudioSeq = -1;
    sPendingUnresolved = false;
    sPlayingEntry = nullptr;
    sPlayingSeqId = NA_BGM_DISABLED;
    sPlayingFirstVisit = false;
    sPlayingBagPos = 0;
    sPlayingBagSize = 0;
    sPlayingAudioSeq = -1;
    sSounding = false;
    sLastCounter = 0;
    sClockResetLogged = false;
    // NOTE WHAT IS NOT RESET: sBags. Bag state persists across leaving and re-entering a zone
    // within a session, and this function runs on every scene load and every disable. Clearing the
    // bags here would make a zone you dip in and out of replay its opener forever, which is the
    // exact complaint the shuffle bag exists to answer. See the sBags declaration.
}

// --- the per-frame handler ----------------------------------------------------------------------

void OnPlayerUpdateMusic() {
    if (!Enabled()) {
        if (sArmedScene != -1) {
            // Flipped off mid-session. Stop asserting and forget everything; whatever is playing
            // keeps playing until the next scene load hands control back to the vanilla loader.
            sArmedScene = -1;
            ResetState();
            RecordEvent("disabled transitions=%d advances=%d", sTransitions, sAdvances);
        }
        return;
    }
    if (!InNormalPlay()) {
        return;
    }

    PlayState* play = gPlayState;
    const RsMusicScene* scene = RsMusicZones_Scene(play->sceneNum);
    if (scene == nullptr) {
        // Not an opted-in scene: the vanilla loader owns the music here and the director is inert.
        if (sArmedScene != -1) {
            sArmedScene = -1;
            ResetState();
        }
        return;
    }
    if (sArmedScene != play->sceneNum) {
        sArmedScene = play->sceneNum;
        ResetState();
    }

    Player* player = GET_PLAYER(play);
    const Vec3f pos = player->actor.world.pos;
    int32_t rsX = 0;
    int32_t rsY = 0;
    WorldToRs(scene, pos.x, pos.z, &rsX, &rsY);
    const int32_t winner = ResolveZone(play->sceneNum, HasSurfaceAnchor(scene), rsX, rsY, pos.y);
    if (winner == NO_ZONE) {
        return; // no fallback in the table - nothing sensible to do, and nothing worth breaking
    }

    // 1. Finish a switch that is sitting in its quiet gap. It starts sPendingEntry, the track chosen
    //    back in BeginSwitch - not a fresh pick, which would spend a second first-visit flag and
    //    could play something other than what the transition line announced.
    if (sState == STATE_FADING) {
        if (--sGapTicks <= 0) {
            StartTrack(FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_IN, RS_MUSIC_FADE_IN_DEFAULT)));
        }
        return; // no zone decisions while a switch is mid-flight
    }

    // 2. Has anything else taken player 0? Cutscenes (Audio_PlaySequenceInCutscene) and mini-boss
    //    / minigame overrides (func_800F5ACC) hard-start it behind our back. Stand down rather
    //    than compete: a director that fought them would leave the music wrong forever after one
    //    cutscene, and OoT-style set pieces are wanted content.
    //
    //    gAudioContext.seqPlayers[] is written by the audio thread and SequencePlayer::enabled is
    //    not volatile, so this read can be a tick-boundary stale value. Vanilla reads it exactly
    //    this way (func_800FA0B4), and nothing below breaks on one stale tick.
    const uint16_t live = func_800FA0B4(SEQ_PLAYER_BGM_MAIN);

    /*
     * IS A SCENE TRANSITION IN FLIGHT? If so, quiet on player 0 means nothing.
     *
     * z_play.c's TRANS_MODE_SETUP fades the BGM out and sets gSaveContext.seqId to NA_BGM_DISABLED
     * unless the destination entrance carries ENTRANCE_INFO_CONTINUE_BGM_FLAG - so the engine takes
     * player 0 away several frames before the new scene loads. Without this, every exit from an
     * opted-in scene (a warp, a door, a void-out) read as "the track ended" and burned a bag entry
     * on the way out. Caught by the first P2 run: a void-out logged
     * `advance ... trigger=quiet ... pos=-5480,-4090,-5400` half a second before `scene_loaded`.
     *
     * This is the duration table being THE mechanism in practice. The quiet signal is used where it
     * happens to be available, and a scene transition is exactly a moment when it is not.
     */
    const bool transitioning =
        play->transitionTrigger != TRANS_TRIGGER_OFF || play->transitionMode != TRANS_MODE_OFF;

    if (sGraceTicks > 0) {
        /*
         * NOTHING func_800FA0B4 SAYS IS TRUSTED IN HERE - not the negative, and not the positive
         * either. The negative is the obvious one: for a tick or two after a play command, quiet is
         * what a perfectly good track looks like, because the command has not reached the audio
         * thread yet.
         *
         * The positive was tried and is just as unreliable, which a run caught rather than review:
         * re-asserting a zone whose next track happened to be the id ALREADY on player 0 confirmed
         * against the outgoing playback, took the duration baseline off the outgoing sequence's
         * scriptCounter, and then saw the counter zeroed under it when the new sequence actually
         * loaded (AudioSeq_ResetSequencePlayer). The backwards-jump guard caught that and
         * re-baselined without advancing - correctly - but a diagnostic that fires in normal
         * operation stops being a diagnostic. So the grace window now means what it says.
         *
         * The cost is that the duration clock starts up to START_GRACE_TICKS late, half a second on
         * a track measured in tens of seconds.
         */
        sGraceTicks--;
    } else if (sState == STATE_PLAYING) {
        if (live == sPlayingSeqId) {
            // Our track really is on player 0, so the duration may start counting. It counts the
            // sequence player's own scriptCounter, zeroed when this track was loaded, rather than
            // the time since THIS tick - the difference is the second or so the play command spent
            // reaching the audio thread plus the grace window, which is a second of the track that
            // already played (see sLastCounter).
            sSounding = true;
        } else if (live == NA_BGM_DISABLED) {
            if (transitioning) {
                // Not our event, and not a diagnostic either. The scene load that follows resets
                // the director anyway, so there is nothing to do and nothing to say.
            } else if (sSounding) {
                // The track was playing and has stopped: end of track, the `quiet` trigger. One
                // handler, whichever trigger got here first - see EndOfTrack.
                EndOfTrack("quiet", rsX, rsY, pos);
                return;
            } else {
                // Never sounded, so nothing ended - a sequence id that does not start. Keep P0's
                // diagnostic exactly as it was, because naming the id is the whole value of it, and DO
                // NOT ADVANCE THE BAG: "quiet means advance" applied here would walk the entire bag at
                // frame rate on one typo.
                //
                // Drop to idle with the active zone remembered, so the next tick re-asserts it without
                // a dwell. With a genuinely bad id that becomes a reassert every START_GRACE_TICKS + 1
                // ticks, which is deliberately loud rather than silent: the log fills with `track_gone`
                // / `reassert` pairs naming the id, a far better failure than music that quietly never
                // plays. Note that a reassert is a switch, so it does redraw from the bag - but it logs
                // as `transition reason=reassert` rather than as an `advance`, so the two are still
                // told apart in the log. It inflates `transitions=`, so a run asserting the negative
                // should read the reasons and not only the count.
                sState = STATE_IDLE;
                char track[48];
                FormatTrackOr(track, sizeof(track), sPlayingEntry, "none");
                char audioSeq[16];
                FormatAudioSeq(audioSeq, sizeof(audioSeq), sPlayingEntry, sPlayingAudioSeq);
                RecordEvent("track_gone zone=%s track=%s seq=0x%X audio_seq=%s frame=%u", ZoneName(sActiveZone), track,
                            (uint32_t)sPlayingSeqId, audioSeq, play->state.frames);
            }
        } else {
            // Something else is on player 0. `ours=` is the id player 0 was carrying for us, which
            // for an imported track is its placeholder - the same value this comparison used, so
            // the line says what was compared rather than what was conceptually playing.
            sState = STATE_YIELDED;
            RecordEvent("yield zone=%s ours=0x%X theirs=0x%X frame=%u", ZoneName(sActiveZone), sPlayingSeqId, live,
                        play->state.frames);
        }
    } else if (sState == STATE_SILENT && live != NA_BGM_DISABLED) {
        sState = STATE_YIELDED;
        RecordEvent("yield zone=%s ours=silent theirs=0x%X frame=%u", ZoneName(sActiveZone), live, play->state.frames);
    }

    // 3. A warp beats everything: no dwell, no debounce. The dwell filters *walking* across a
    //    boundary, and applying it to a teleport is straightforwardly wrong.
    if (sWarpPending) {
        sWarpPending = false;
        const char* reason = sWarpReason;
        sWarpReason = "warp";
        // A WARP ENDS A YIELD: teleporting away from an encounter takes the music with you. The
        // switch below runs as it would from anywhere (YIELDED is not PLAYING, so even a same-zone
        // warp switches - the zone track was not playing), and ReclaimFromOverride first undoes the
        // one piece of vanilla state that taking the player back would otherwise leave behind.
        //
        // Decided 2026-09-11, after the #90 P5 run had briefly made this a refusal. Teleports are
        // planned player content, and a teleport that leaves the mini-boss music playing wherever
        // you land reads as a bug. What the P5 run's scenario E actually caught was not the switch
        // itself but two side effects of it, both fixed rather than avoided: a hard cut (now faded,
        // see wasSounding in BeginSwitch) and a stale mini-boss stash that silenced enemy music for
        // the rest of the scene (see ReclaimFromOverride).
        if (sState == STATE_YIELDED) {
            ReclaimFromOverride(winner, reason, rsX, rsY);
        }
        // ...but a warp that lands in the zone you were already in is not a switch. What #90 s7
        // says is that a teleport skips the DWELL, not that it must restart the music: restarting
        // here would send the track back to the top every time you teleport home, or step through
        // a door and back, which is precisely the "music changed for no reason" this feature
        // exists to remove. Found by the 2026-09-08 P0 run, which logged
        // `transition from=west_fields to=west_fields`.
        // The state test is what keeps a scene load working: ResetState clears sActiveZone, so a
        // fresh scene always takes the first branch.
        if (winner != sActiveZone || (sState != STATE_PLAYING && sState != STATE_SILENT)) {
            BeginSwitch(winner, reason, rsX, rsY, pos);
        } else {
            sCandidateZone = NO_ZONE;
            sDwellTicks = 0;
            char tiles[32];
            RsMusic_FormatTiles(tiles, sizeof(tiles), rsX, rsY);
            RecordEvent("warp_same_zone zone=%s reason=%s rs=%s frame=%u", ZoneName(winner), reason, tiles,
                        play->state.frames);
        }
        return;
    }

    // 4. While yielded, wait for the override to hand player 0 back. Apart from a warp (step 3),
    //    THAT IS THE ONLY THING THAT ENDS A YIELD - a zone change never does.
    //
    //    #90 section 13 originally said "re-assert when it goes quiet OR the zone changes", and P0
    //    implemented the second half with the dwell attached to it. The clause was deleted on
    //    2026-09-09 and this is where it used to be. Two reasons:
    //
    //      - Taken literally it lets a cutscene which walks Link across a boundary have its own
    //        music cut out from under it. P0's dwell narrowed that window without closing it: a
    //        cutscene that parks him in a new zone for longer than the dwell still loses its music.
    //      - It bought nothing. The release path below calls BeginSwitch with a FRESHLY COMPUTED
    //        winner, so the correct zone plays the moment the override ends whether or not the
    //        clause exists. Its only effect was to let the director interrupt an override that had
    //        not finished.
    //
    //    So while yielded the dwell is held at zero rather than left counting toward a switch that
    //    must not happen, and this returns before the dwell block further down.
    //
    //    TWO WAYS AN OVERRIDE HANDS PLAYER 0 BACK, and until the #90 P5 run only the first was known:
    //
    //      - QUIET. Cutscene music ends with a real stop (Audio_StopSequenceInCutscene).
    //      - OUR OWN ID, RESTARTED. Every func_800F5ACC override - the mini-bosses and the timed
    //        minigames - ends in func_800F5B58, which replays the id func_800F5ACC stashed when it
    //        took the player. In an opted-in scene that id is OURS, so player 0 comes back already
    //        sounding our track from the top and is never quiet. A director that waited for quiet
    //        stayed yielded until the next scene load: no release, no dwell, no queue, and the
    //        pre-fight track looping whatever zone Link walked into
    //        (docs/test-runs/2026-09-11-zone-music-p5, D2).
    //
    //    sPlayingSeqId is kept across the yield for exactly this, and it cannot fire early: a yield
    //    only starts when live != sPlayingSeqId, so seeing it again means the override gave the
    //    player back. Both exits take the same release with a freshly computed winner, so a fight
    //    that ends in a different zone from the one it began in plays the new zone.
    if (sState == STATE_YIELDED) {
        if (live == NA_BGM_DISABLED || live == sPlayingSeqId) {
            // Which exit fired, so a run can tell the two apart - the transition line alone reads the
            // same for both. Logged first, so the channel reads in the order things happened.
            RecordEvent("release via=%s zone=%s winner=%s ours=0x%X frame=%u",
                        live == NA_BGM_DISABLED ? "quiet" : "restored", ZoneName(sActiveZone), ZoneName(winner),
                        sPlayingSeqId, play->state.frames);
            // Restart from the top rather than resuming - see ZoneDirector.h on why the asymmetry
            // with combat ducking is deliberate. On the `restored` exit the fresh pick lands on top
            // of vanilla's restart about a tick after it began; whether that is audible is a
            // listening-pass question, and the lever is in the P5 run's README.
            sState = STATE_IDLE;
            BeginSwitch(winner, "override_release", rsX, rsY, pos);
        } else {
            sCandidateZone = NO_ZONE;
            sDwellTicks = 0;
        }
        return;
    }

    // 5. Nothing asserted yet (fresh scene, or our track vanished): take the winner now.
    if (sState == STATE_IDLE) {
        BeginSwitch(winner, sActiveZone == NO_ZONE ? "activate" : "reassert", rsX, rsY, pos);
        return;
    }

    // 6. The dwell timer. THIS IS THE FEATURE, not debounce for its own sake: running along a
    //    border must not switch the music, and clipping the corner of B on the way from A to C
    //    must not either. Resetting on a candidate change is what makes the corner case work; a
    //    per-zone accumulated-presence design (the rejected alternative) eventually trips both
    //    zones when you alternate between them.
    if (winner == sActiveZone) {
        sCandidateZone = NO_ZONE;
        sDwellTicks = 0;
    } else if (winner != sCandidateZone) {
        sCandidateZone = winner;
        sDwellTicks = 0;
    } else if (++sDwellTicks >= SecondsToTicks(CVarGetFloat(CVAR_RS_MUSIC_DWELL, RS_MUSIC_DWELL_DEFAULT))) {
        BeginSwitch(winner, "dwell", rsX, rsY, pos);
        // A zone switch supersedes an in-zone advance rather than racing it: the switch picks a
        // fresh track out of the new zone's bag anyway, so advancing the OLD zone's bag on the same
        // tick would spend an entry nobody ever hears. This is why the duration check sits after
        // the dwell rather than before it.
        return;
    }

    // 7. The duration timer - the other trigger for the one end-of-track handler. Reached only
    //    while our own track is playing and the zone is not changing, so it is genuinely "this
    //    track has had its turn" rather than anything to do with position.
    if (DurationExpired()) {
        EndOfTrack("duration", rsX, rsY, pos);
    }
}

/*
 * Take ownership of the music by making the scene loader stand down, rather than by fighting it.
 *
 * AfterSceneCommands fires once the scene's command list has run (and for the hand-rolled custom
 * grid-tool scenes, at the end of their own init - they call the hook themselves), which is before
 * Play_Init reaches Environment_PlaySceneSequence. With sequenceCtx.seqId == NA_BGM_NO_MUSIC and
 * natureAmbienceId == NATURE_ID_NONE, that function returns early without touching player 0 at
 * all: the loader does nothing instead of being overridden a frame later.
 *
 * Nature ambience goes off wholesale in the scenes the director owns. RS-style area ambience is
 * its own problem (#93) and could not use this path anyway, because OoT's nature ambience is not a
 * layer - it occupies player 0, the music player.
 */
void OnAfterSceneCommandsMusic(int16_t sceneNum) {
    if (!Enabled() || gPlayState == nullptr) {
        return;
    }
    if (RsMusicZones_Scene(sceneNum) == nullptr) {
        return; // per-scene opt-in: an unrelated test map keeps whatever music it always had
    }
    gPlayState->sequenceCtx.seqId = NA_BGM_NO_MUSIC;
    gPlayState->sequenceCtx.natureAmbienceId = NATURE_ID_NONE;
    ResetState();
    sArmedScene = -1; // re-armed by the first OnPlayerUpdate in the new scene
    RsMusic_NotifyWarped("scene_load");
}

void RegisterZoneDirector() {
    // Registered unconditionally and gated on the CVar inside, rather than COND_HOOK'd on the CVar
    // value: ShipInit re-runs on preset apply and on an ImGui widget change, but this build has no
    // console `set` command, so a CVar written by the `rsmusic` console command would never
    // re-trigger registration. Reading the CVar in the handler makes both paths work, and the cost
    // while off is one hash lookup per frame.
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateMusic);
    COND_HOOK(AfterSceneCommands, true, OnAfterSceneCommandsMusic);
}

RegisterShipInitFunc zoneDirectorInitFunc(RegisterZoneDirector);

} // namespace

extern "C" void RsMusic_NotifyWarped(const char* reason) {
    sWarpPending = true;
    sWarpReason = (reason != nullptr) ? reason : "warp";
}

extern "C" void RsMusic_FormatTiles(char* buf, uint32_t size, int32_t rsX, int32_t rsY) {
    if (buf == nullptr || size == 0) {
        return;
    }
    if (rsX == RS_NO_TILE || rsY == RS_NO_TILE) {
        std::snprintf(buf, size, "none");
        return;
    }
    std::snprintf(buf, size, "%d,%d", rsX, rsY);
}

extern "C" void RsMusic_FormatTrack(char* buf, uint32_t size, const RsZoneTrack* track) {
    if (buf == nullptr || size == 0) {
        return;
    }
    if (track == nullptr) {
        std::snprintf(buf, size, "none");
        return;
    }
    if (track->rsPath == nullptr) {
        std::snprintf(buf, size, "0x%X", (uint32_t)track->seqId);
        return;
    }
    // The last path segment, not the whole path: the channel is key=value with no spaces in a
    // value, and "custom/music/rs/flute-salad" is four times the width of what it distinguishes.
    // `rsmusic zones` prints the path in full, which is the surface for "which file is that".
    const char* slash = std::strrchr(track->rsPath, '/');
    std::snprintf(buf, size, "rs:%s", slash != nullptr ? slash + 1 : track->rsPath);
}

extern "C" int32_t RsMusic_ResolveTrackPath(const char* name, const char** error) {
    // Every custom sequence is listed from this virtual path (audio_load.c, ListFiles
    // "custom/music/*"), so the prefix is what separates a custom entry from a vanilla one in the
    // same array.
    static const char* const kCustomMusicPrefix = "custom/music/";
    const char* ignored = nullptr;
    if (error == nullptr) {
        error = &ignored;
    }
    *error = "not_registered";
    if (name == nullptr || sequenceMap == nullptr) {
        return -1;
    }
    // audio_load.c allocates sequenceMapSize + 0xF slots and AudioLoad_SyncInitSeqPlayerInternal
    // bounds-checks against the same figure.
    const size_t slots = (size_t)sequenceMapSize + 0xF;
    const size_t prefixLen = std::strlen(kCustomMusicPrefix);
    int32_t found = -1;
    int32_t matches = 0;
    for (size_t i = 0; i < slots; i++) {
        const char* path = sequenceMap[i];
        if (path == nullptr || std::strncmp(path, kCustomMusicPrefix, prefixLen) != 0) {
            continue;
        }
        if (std::strcmp(name, path) == 0) {
            *error = nullptr;
            return (int32_t)i; // a full path is exact: no ambiguity to resolve
        }
        const char* slash = std::strrchr(path, '/');
        if (std::strcmp(name, slash != nullptr ? slash + 1 : path) == 0) {
            found = (int32_t)i;
            matches++;
        }
    }
    if (matches == 1) {
        *error = nullptr;
        return found;
    }
    *error = (matches == 0) ? "not_registered" : "ambiguous_name";
    return -1;
}

extern "C" int32_t RsMusic_TransitionCount(void) {
    return sTransitions;
}

// Records a bookmark and returns it. It does NOT touch sTransitions, and there is deliberately no
// way to: an unresettable counter makes "the count did not move" a strictly stronger assertion than
// a resettable one. The subcommand that calls this was named `reset` through P0 and reset nothing,
// which read as a broken feature to the first human who tried it; the behaviour was right and the
// name was wrong (#90, human tuning pass 2026-09-09).
extern "C" int32_t RsMusic_MarkBaseline(void) {
    sBaseline = sTransitions;
    sAdvanceBaseline = sAdvances; // same moment, so status can report both differences from one mark
    return sBaseline;
}

extern "C" int32_t RsMusic_Baseline(void) {
    return sBaseline;
}

extern "C" int32_t RsMusic_AdvanceCount(void) {
    return sAdvances;
}

extern "C" int32_t RsMusic_BagLine(int32_t index, char* buf, uint32_t size) {
    if (buf == nullptr || size == 0 || index < 0 || index >= RsMusicZones_Count() || index >= RS_MUSIC_MAX_ZONES) {
        return 0;
    }
    const RsMusicZone* z = RsMusicZones_At(index);
    const TrackBag* bag = &sBags[index];

    // The shuffled order as track indices, so "no repeat within a bag" and "the refill did not
    // produce a back-to-back" are both readable without waiting for the bag to play out. Bounded by
    // RS_MUSIC_MAX_TRACKS_PER_ZONE, so it cannot outgrow the line.
    char order[RS_MUSIC_MAX_TRACKS_PER_ZONE * 4 + 8];
    size_t used = 0;
    order[0] = '\0';
    for (uint8_t i = 0; i < bag->size && used + 5 < sizeof(order); i++) {
        const int written = std::snprintf(order + used, sizeof(order) - used, i == 0 ? "%d" : ",%d",
                                          (int32_t)bag->order[i]);
        if (written <= 0) {
            break;
        }
        // snprintf returns what it WOULD have written, so clamp before it is used as an offset.
        used += (size_t)written;
        if (used >= sizeof(order)) {
            used = sizeof(order) - 1;
            break;
        }
    }
    if (bag->size == 0) {
        std::snprintf(order, sizeof(order), "unshuffled");
    }

    char last[48];
    FormatTrackOr(last, sizeof(last), bag->last, "none");
    std::snprintf(buf, size, "bag[%d]=%s tracks=%d drawn=%d/%d last=%s order=%s", index, z->name,
                  (int32_t)z->trackCount, (int32_t)bag->next, (int32_t)bag->size, last, order);
    return 1;
}

extern "C" int32_t RsMusic_Probe(int16_t* sceneId, int32_t* rsX, int32_t* rsY, int32_t* zoneIndex) {
    if (sceneId != nullptr) {
        *sceneId = (gPlayState != nullptr) ? gPlayState->sceneNum : -1;
    }
    if (!InNormalPlay()) {
        return 0;
    }
    const RsMusicScene* scene = RsMusicZones_Scene(gPlayState->sceneNum);
    if (scene == nullptr) {
        return 0;
    }
    Player* player = GET_PLAYER(gPlayState);
    int32_t x = 0;
    int32_t y = 0;
    WorldToRs(scene, player->actor.world.pos.x, player->actor.world.pos.z, &x, &y);
    if (rsX != nullptr) {
        *rsX = x;
    }
    if (rsY != nullptr) {
        *rsY = y;
    }
    if (zoneIndex != nullptr) {
        *zoneIndex = ResolveZone(gPlayState->sceneNum, HasSurfaceAnchor(scene), x, y, player->actor.world.pos.y);
    }
    return 1;
}

extern "C" const char* RsMusic_Describe(void) {
    if (!Enabled()) {
        std::snprintf(sDescription, sizeof(sDescription), "rsmusic on=0 transitions=%d advances=%d baseline=%d",
                      sTransitions, sAdvances, sBaseline);
        return sDescription;
    }
    int16_t sceneId = -1;
    int32_t rsX = 0;
    int32_t rsY = 0;
    int32_t winner = NO_ZONE;
    const int32_t inZone = RsMusic_Probe(&sceneId, &rsX, &rsY, &winner);
    const int32_t dwellTarget = SecondsToTicks(CVarGetFloat(CVAR_RS_MUSIC_DWELL, RS_MUSIC_DWELL_DEFAULT));
    char tiles[32];
    RsMusic_FormatTiles(tiles, sizeof(tiles), rsX, rsY);
    char bag[16];
    FormatBag(bag, sizeof(bag), sPlayingBagPos, sPlayingBagSize, sPlayingFirstVisit);
    char track[48];
    FormatTrackOr(track, sizeof(track), sPlayingEntry, "none");
    std::snprintf(sDescription, sizeof(sDescription),
                  "rsmusic on=1 scene=0x%X opted_in=%d state=%s zone=%s track=%s first_visit=%d bag=%s len_ms=%u "
                  "winner=%s candidate=%s dwell=%d/%d rs=%s fade_out=%d fade_in=%d transitions=%d advances=%d "
                  "baseline=%d",
                  sceneId, inZone, StateName(sState), sActiveZone == NO_ZONE ? "none" : ZoneName(sActiveZone), track,
                  sPlayingFirstVisit ? 1 : 0, bag, PlayingDurationMs(),
                  winner == NO_ZONE ? "none" : ZoneName(winner),
                  sCandidateZone == NO_ZONE ? "none" : ZoneName(sCandidateZone), sDwellTicks, dwellTarget, tiles,
                  FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_OUT, RS_MUSIC_FADE_OUT_DEFAULT)),
                  FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_IN, RS_MUSIC_FADE_IN_DEFAULT)), sTransitions, sAdvances,
                  sBaseline);
    return sDescription;
}

/*
 * The same state as RsMusic_Describe, in grouped lines: director, zone and track, tunables,
 * counters. Rendered here rather than in MusicConsole.cpp because this is where the state is, and
 * because both console sinks have to say the same thing.
 *
 * Every line stays single-line key=value. Splitting is for the human reading an ImGui console that
 * does not wrap; greppability is for the agent loop, and the two are not in tension.
 */
extern "C" int32_t RsMusic_DescribeLine(int32_t index, char* buf, uint32_t size) {
    if (buf == nullptr || size == 0) {
        return 0;
    }
    int16_t sceneId = -1;
    int32_t rsX = 0;
    int32_t rsY = 0;
    int32_t winner = NO_ZONE;
    const int32_t inZone = RsMusic_Probe(&sceneId, &rsX, &rsY, &winner);
    const bool on = Enabled();

    switch (index) {
        case 0:
            // scene_transition= is why "nothing happened" can be the correct answer for a few
            // frames: the engine has taken player 0 for the fade-out and the quiet trigger stands
            // down until the new scene loads.
            std::snprintf(buf, size, "status.director on=%d state=%s scene=0x%X opted_in=%d scene_transition=%d",
                          on ? 1 : 0, StateName(sState), sceneId, inZone,
                          (gPlayState != nullptr && (gPlayState->transitionTrigger != TRANS_TRIGGER_OFF ||
                                                     gPlayState->transitionMode != TRANS_MODE_OFF))
                              ? 1
                              : 0);
            return 1;
        case 1:
        {
            char tiles[32];
            RsMusic_FormatTiles(tiles, sizeof(tiles), rsX, rsY);
            char track[48];
            FormatTrackOr(track, sizeof(track), sPlayingEntry, "none");
            std::snprintf(buf, size, "status.zone active=%s track=%s first_visit=%d winner=%s candidate=%s rs=%s",
                          sActiveZone == NO_ZONE ? "none" : ZoneName(sActiveZone), track, sPlayingFirstVisit ? 1 : 0,
                          winner == NO_ZONE ? "none" : ZoneName(winner),
                          sCandidateZone == NO_ZONE ? "none" : ZoneName(sCandidateZone), tiles);
            return 1;
        }
        case 2:
        {
            // The queue's own state: how far through the current track's authored duration it is,
            // where in the bag that track came from, and - for an imported track - which custom
            // sequence number its path resolved to this boot, which is the number `rsmusic players`
            // reports as audio_seq= and the only way to check that the right FILE is playing.
            //
            // elapsed_ms= is off the audio clock and reads -1 when there is no clock to read (audio
            // heap not up yet). advance_at_ms= is when this track's in-zone advance is due, which
            // for an imported track is endFadeMs BEFORE len_ms - so "the fade started on time" is
            // two numbers on one line rather than an inference.
            char bag[16];
            FormatBag(bag, sizeof(bag), sPlayingBagPos, sPlayingBagSize, sPlayingFirstVisit);
            char track[48];
            FormatTrackOr(track, sizeof(track), sPlayingEntry, "none");
            char audioSeq[16];
            FormatAudioSeq(audioSeq, sizeof(audioSeq), sPlayingEntry, sPlayingAudioSeq);
            char endFade[16];
            FormatEndFade(endFade, sizeof(endFade), sPlayingEntry);
            int64_t elapsed = -1;
            // Not while yielded: scriptCounter then belongs to whatever took player 0, and the P5
            // run read elapsed=59 and elapsed=157 off a mini-boss's clock. -1 rather than a number
            // somebody believes.
            if (HasAudioClock() && sSounding && sState != STATE_YIELDED) {
                elapsed = (int64_t)ScriptTicksToMs(ScriptCounter());
            }
            std::snprintf(buf, size,
                          "status.track track=%s seq=0x%X audio_seq=%s bag=%s len_ms=%u end_fade_ms=%s "
                          "advance_at_ms=%u elapsed_ms=%lld sounding=%d tick_hz=%u",
                          track, (uint32_t)sPlayingSeqId, audioSeq, bag, PlayingDurationMs(), endFade,
                          AdvanceAtMs(sPlayingEntry), (long long)elapsed, sSounding ? 1 : 0, ScriptTicksPerSecond());
            return 1;
        }
        case 3:
            std::snprintf(buf, size, "status.tuning dwell=%d/%d fade_out=%d fade_in=%d gap=%d grace=%d", sDwellTicks,
                          SecondsToTicks(CVarGetFloat(CVAR_RS_MUSIC_DWELL, RS_MUSIC_DWELL_DEFAULT)),
                          FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_OUT, RS_MUSIC_FADE_OUT_DEFAULT)),
                          FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_IN, RS_MUSIC_FADE_IN_DEFAULT)), sGapTicks,
                          sGraceTicks);
            return 1;
        case 4:
            // since= is the number a run actually asserts on. transitions= is monotonic and cannot be
            // zeroed; baseline= is the bookmark `rsmusic baseline` took. advances= counts in-zone
            // queue moves and is deliberately NOT folded into transitions= - see sAdvances.
            std::snprintf(buf, size,
                          "status.counters transitions=%d baseline=%d since=%d advances=%d advances_since=%d "
                          "dropped=%d",
                          sTransitions, sBaseline, sTransitions - sBaseline, sAdvances,
                          sAdvances - sAdvanceBaseline, sEventsDropped);
            return 1;
        default:
            return 0;
    }
}

extern "C" int32_t RsMusic_TakeEvent(char* buf, uint32_t size) {
    if (sEventRead == sEventWrite || buf == nullptr || size == 0) {
        return 0;
    }
    std::snprintf(buf, size, "%s%s", sEvents[sEventRead], sEventsDropped > 0 ? " (events dropped)" : "");
    sEventRead = (sEventRead + 1) % EVENT_SLOTS;
    return 1;
}
