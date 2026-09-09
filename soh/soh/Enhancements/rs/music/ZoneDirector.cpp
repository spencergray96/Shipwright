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

// Starting points to tune from (#90 section 6). Not measured, not sacred.
#define RS_MUSIC_DWELL_DEFAULT 2.0f
#define RS_MUSIC_FADE_OUT_DEFAULT 1.5f
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
constexpr int32_t EVENT_LEN = 256;
constexpr int32_t NO_ZONE = -1;

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
uint16_t sPendingSeqId = NA_BGM_DISABLED;
bool sPendingFirstVisit = false;
uint16_t sPlayingSeqId = NA_BGM_DISABLED;
bool sPlayingFirstVisit = false;
int32_t sTransitions = 0;
int32_t sBaseline = 0;    // see RsMusic_MarkBaseline; never touches sTransitions
int32_t sArmedScene = -1; // the opted-in scene the state above belongs to

// "Position was set, not walked": suppress the dwell for exactly one switch.
bool sWarpPending = false;
const char* sWarpReason = "warp";

char sDescription[384] = "rsmusic off";

char sEvents[EVENT_SLOTS][EVENT_LEN];
int32_t sEventRead = 0;
int32_t sEventWrite = 0;
int32_t sEventsDropped = 0;

bool Enabled() {
    return CVarGetInteger(CVAR_RS_MUSIC_ON, 0) != 0;
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

void RecordEvent(const char* fmt, ...) {
    const int32_t next = (sEventWrite + 1) % EVENT_SLOTS;
    if (next == sEventRead) {
        sEventsDropped++; // the harness drains this every tick, so it should stay at zero
        return;
    }
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(sEvents[sEventWrite], EVENT_LEN, fmt, args);
    va_end(args);
    sEventWrite = next;
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

/*
 * Which track of the zone to play, and THE ONE PLACE A FIRST-VISIT FLAG IS SPENT.
 *
 * CALL IT EXACTLY ONCE PER SWITCH. It is not a pure function: the first-visit branch writes a
 * world flag, so calling it twice - to fill in a log line, say - would burn the opener on a switch
 * that only ever played it once. BeginSwitch calls it and everything downstream reads sPendingSeqId.
 *
 * WHEN THE FLAG IS SPENT is the design point (#90 section 10): on ACTIVATION, which is this call,
 * inside BeginSwitch, after the dwell has already expired. Not on boundary contact - clipping the
 * corner of a zone never reaches here, because clipping never produces a switch. And there is no
 * forced completion: the flag is gone the moment the opener starts, so walking straight back out
 * spends it. Enter, hear thirty seconds, leave, return - you get the normal track, which is exactly
 * what "first visit" should mean.
 *
 * Otherwise it takes the first track. Multi-track zones are P2, and that is where the shuffle bag
 * (shuffle the list, play through it, reshuffle on empty, and swap the first entry of a fresh bag
 * when it repeats the track that just ended) goes; the first-visit pick becomes the bag's SEED
 * there rather than a special case beside it. lengthSec and conditions stay unread until then.
 */
uint16_t SelectTrack(int32_t zoneIndex, bool* firstVisit) {
    *firstVisit = false;
    const RsMusicZone* z = RsMusicZones_At(zoneIndex);
    if (z == nullptr || z->trackCount == 0) {
        return NA_BGM_DISABLED; // an empty track list is authored silence, not an error
    }
    if (z->firstVisitTrack != nullptr && z->firstVisitFlag != RS_ZONE_NO_FIRST_VISIT &&
        !Flags_GetWorldFlag(z->firstVisitFlag)) {
        Flags_SetWorldFlag(z->firstVisitFlag);
        *firstVisit = true;
        return z->firstVisitTrack->seqId;
    }
    return z->tracks[0].seqId;
}

// --- driving player 0 ---------------------------------------------------------------------------

void StartTrack(uint16_t seqId, int32_t fadeInUnits) {
    sPlayingFirstVisit = sPendingFirstVisit;
    if (seqId == NA_BGM_DISABLED) {
        sState = STATE_SILENT;
        sPlayingSeqId = NA_BGM_DISABLED;
        // Vanilla's "no music here" value, so anything reading the save context's idea of the
        // ambient track agrees with what the player can hear.
        gSaveContext.seqId = NA_BGM_NO_MUSIC;
        return;
    }
    SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, fadeInUnits, 0, seqId);
    sPlayingSeqId = seqId;
    sState = STATE_PLAYING;
    sGraceTicks = START_GRACE_TICKS;
    // Keep the save context in step with what the director considers the ambient track:
    // Audio_SetSequenceMode reads gActiveSeqs[0].seqId to decide whether combat music may duck,
    // and z_play.c clears gSaveContext.seqId on transitions expecting the loader to have set it.
    gSaveContext.seqId = (u8)(seqId & 0xFF);
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
    const bool wasSounding = (sState == STATE_PLAYING);

    const int32_t from = sActiveZone;
    sTransitions++;
    sActiveZone = zoneIndex;
    sCandidateZone = NO_ZONE;
    sDwellTicks = 0;

    // Choose once, here. See SelectTrack: the first-visit branch spends a world flag, so the track
    // has to be decided at the moment of activation and then carried, not re-derived.
    sPendingSeqId = SelectTrack(zoneIndex, &sPendingFirstVisit);

    if (wasSounding) {
        // Op 1, "disable seq player", with a fade. A duration of 0 here is an immediate disable
        // rather than a fade, which is exactly what a fade-out of zero seconds should mean.
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, fadeOutUnits);
        sPlayingSeqId = NA_BGM_DISABLED;
        sGapTicks = SecondsToTicks(fadeOutSec);
        sState = STATE_FADING;
    } else {
        sGapTicks = 0;
        StartTrack(sPendingSeqId, fadeInUnits);
    }

    char tiles[32];
    RsMusic_FormatTiles(tiles, sizeof(tiles), rsX, rsY);
    RecordEvent("transition from=%s to=%s track=0x%X first_visit=%d reason=%s fade_out=%d fade_in=%d gap=%d "
                "rs=%s pos=%.0f,%.0f,%.0f n=%d frame=%u",
                from == NO_ZONE ? "none" : ZoneName(from), ZoneName(zoneIndex), sPendingSeqId,
                sPendingFirstVisit ? 1 : 0, reason, fadeOutUnits, fadeInUnits, sGapTicks, tiles, pos.x, pos.y,
                pos.z, sTransitions, gPlayState != nullptr ? gPlayState->state.frames : 0u);

    // Logged after the transition it belongs to, so the channel reads in the order things happened.
    // This line fires on exactly the tick the flag is spent, and never for a boundary Link merely
    // clipped - clipping produces no switch, so it never reaches BeginSwitch at all. That is the
    // assertion the agent loop makes about the first-visit rule; `rsmusic firstvisit` is the other
    // half, reading the flag back.
    if (sPendingFirstVisit) {
        const RsMusicZone* z = RsMusicZones_At(zoneIndex);
        RecordEvent("first_visit zone=%s flag=%d track=0x%X reason=%s frame=%u", ZoneName(zoneIndex),
                    (int32_t)z->firstVisitFlag, sPendingSeqId, reason,
                    gPlayState != nullptr ? gPlayState->state.frames : 0u);
    }
}

void ResetState() {
    sState = STATE_IDLE;
    sActiveZone = NO_ZONE;
    sCandidateZone = NO_ZONE;
    sDwellTicks = 0;
    sGapTicks = 0;
    sGraceTicks = 0;
    sPendingSeqId = NA_BGM_DISABLED;
    sPendingFirstVisit = false;
    sPlayingSeqId = NA_BGM_DISABLED;
    sPlayingFirstVisit = false;
}

// --- the per-frame handler ----------------------------------------------------------------------

void OnPlayerUpdateMusic() {
    if (!Enabled()) {
        if (sArmedScene != -1) {
            // Flipped off mid-session. Stop asserting and forget everything; whatever is playing
            // keeps playing until the next scene load hands control back to the vanilla loader.
            sArmedScene = -1;
            ResetState();
            RecordEvent("disabled transitions=%d", sTransitions);
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

    // 1. Finish a switch that is sitting in its quiet gap. It starts sPendingSeqId, the track chosen
    //    back in BeginSwitch - not a fresh pick, which would spend a second first-visit flag and
    //    could play something other than what the transition line announced.
    if (sState == STATE_FADING) {
        if (--sGapTicks <= 0) {
            StartTrack(sPendingSeqId, FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_IN, RS_MUSIC_FADE_IN_DEFAULT)));
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
    if (sGraceTicks > 0) {
        sGraceTicks--;
    } else if (sState == STATE_PLAYING) {
        if (live == NA_BGM_DISABLED) {
            // Our track is gone: it ended, or somebody disabled the player. Drop to idle with the
            // active zone remembered, so the next tick re-asserts it without a dwell.
            //
            // If a track id is simply bad and never starts, this becomes a reassert every
            // START_GRACE_TICKS + 1 ticks. That is deliberately loud rather than silent: the log
            // fills with `track_gone` / `reassert` pairs naming the id, which is a far better
            // failure than music that quietly never plays. It does inflate `transitions=`, so a
            // run asserting the negative should check the reasons and not just the count.
            sState = STATE_IDLE;
            RecordEvent("track_gone zone=%s track=0x%X frame=%u", ZoneName(sActiveZone), sPlayingSeqId,
                        play->state.frames);
        } else if (live != sPlayingSeqId) {
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

    // 4. While yielded, wait for player 0 to go quiet. THAT IS THE ONLY THING THAT ENDS A YIELD.
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
    if (sState == STATE_YIELDED) {
        if (live == NA_BGM_DISABLED) {
            // Restart from the top rather than resuming - see ZoneDirector.h on why the asymmetry
            // with combat ducking is deliberate.
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
        return;
    }
    if (winner != sCandidateZone) {
        sCandidateZone = winner;
        sDwellTicks = 0;
        return;
    }
    if (++sDwellTicks >= SecondsToTicks(CVarGetFloat(CVAR_RS_MUSIC_DWELL, RS_MUSIC_DWELL_DEFAULT))) {
        BeginSwitch(winner, "dwell", rsX, rsY, pos);
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
    return sBaseline;
}

extern "C" int32_t RsMusic_Baseline(void) {
    return sBaseline;
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
        std::snprintf(sDescription, sizeof(sDescription), "rsmusic on=0 transitions=%d baseline=%d", sTransitions,
                      sBaseline);
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
    std::snprintf(sDescription, sizeof(sDescription),
                  "rsmusic on=1 scene=0x%X opted_in=%d state=%s zone=%s track=0x%X first_visit=%d winner=%s "
                  "candidate=%s dwell=%d/%d rs=%s fade_out=%d fade_in=%d transitions=%d baseline=%d",
                  sceneId, inZone, StateName(sState), sActiveZone == NO_ZONE ? "none" : ZoneName(sActiveZone),
                  sPlayingSeqId, sPlayingFirstVisit ? 1 : 0, winner == NO_ZONE ? "none" : ZoneName(winner),
                  sCandidateZone == NO_ZONE ? "none" : ZoneName(sCandidateZone), sDwellTicks, dwellTarget, tiles,
                  FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_OUT, RS_MUSIC_FADE_OUT_DEFAULT)),
                  FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_IN, RS_MUSIC_FADE_IN_DEFAULT)), sTransitions, sBaseline);
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
            std::snprintf(buf, size, "status.director on=%d state=%s scene=0x%X opted_in=%d", on ? 1 : 0,
                          StateName(sState), sceneId, inZone);
            return 1;
        case 1:
        {
            char tiles[32];
            RsMusic_FormatTiles(tiles, sizeof(tiles), rsX, rsY);
            std::snprintf(buf, size, "status.zone active=%s track=0x%X first_visit=%d winner=%s candidate=%s rs=%s",
                          sActiveZone == NO_ZONE ? "none" : ZoneName(sActiveZone), sPlayingSeqId,
                          sPlayingFirstVisit ? 1 : 0, winner == NO_ZONE ? "none" : ZoneName(winner),
                          sCandidateZone == NO_ZONE ? "none" : ZoneName(sCandidateZone), tiles);
            return 1;
        }
        case 2:
            std::snprintf(buf, size, "status.tuning dwell=%d/%d fade_out=%d fade_in=%d gap=%d grace=%d", sDwellTicks,
                          SecondsToTicks(CVarGetFloat(CVAR_RS_MUSIC_DWELL, RS_MUSIC_DWELL_DEFAULT)),
                          FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_OUT, RS_MUSIC_FADE_OUT_DEFAULT)),
                          FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_IN, RS_MUSIC_FADE_IN_DEFAULT)), sGapTicks,
                          sGraceTicks);
            return 1;
        case 3:
            // since= is the number a run actually asserts on. transitions= is monotonic and cannot be
            // zeroed; baseline= is the bookmark `rsmusic baseline` took.
            std::snprintf(buf, size, "status.counters transitions=%d baseline=%d since=%d dropped=%d", sTransitions,
                          sBaseline, sTransitions - sBaseline, sEventsDropped);
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
