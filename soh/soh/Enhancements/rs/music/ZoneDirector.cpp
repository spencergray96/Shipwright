/*
 * The zone director (sturdy-bassoon#90 P0). See ZoneDirector.h for what this is, why the dwell
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
int32_t sPendingZone = NO_ZONE;
uint16_t sPlayingSeqId = NA_BGM_DISABLED;
int32_t sTransitions = 0;
int32_t sArmedScene = -1; // the opted-in scene the state above belongs to

// "Position was set, not walked": suppress the dwell for exactly one switch.
bool sWarpPending = false;
const char* sWarpReason = "warp";

char sDescription[288] = "rsmusic off";

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
 * The winning zone for a point: highest priority among the zones that contain it. Total coverage
 * is guaranteed by the fallback entry, which contains everything, so this only returns NO_ZONE if
 * somebody removed the fallback from the table.
 *
 * Ties go to the earlier entry, which makes the answer a pure function of the table rather than of
 * iteration order elsewhere. The table is a handful of entries and this runs once a frame; a
 * spatial index would be premature at any size we are going to author by hand.
 */
int32_t ResolveZone(int32_t rsX, int32_t rsY, float worldY) {
    int32_t best = NO_ZONE;
    int32_t bestPriority = 0;
    const int32_t count = RsMusicZones_Count();
    for (int32_t i = 0; i < count; i++) {
        const RsMusicZone* z = RsMusicZones_At(i);
        bool hit = (z->flags & RS_ZONE_FLAG_FALLBACK) != 0;
        for (uint8_t r = 0; !hit && r < z->rectCount; r++) {
            hit = RectContains(z->rects[r], rsX, rsY, worldY);
        }
        if (!hit) {
            continue;
        }
        if (best == NO_ZONE || z->priority > bestPriority) {
            best = i;
            bestPriority = z->priority;
        }
    }
    return best;
}

const char* ZoneName(int32_t index) {
    const RsMusicZone* z = RsMusicZones_At(index);
    return z != nullptr ? z->name : "none";
}

/*
 * Which track of the zone to play. P0 always takes the first: multi-track zones are P2, and that
 * is where the shuffle bag (shuffle the list, play through it, reshuffle on empty, and swap the
 * first entry of a fresh bag when it repeats the track that just ended) goes. Deliberately a
 * function rather than an inline `tracks[0]` so P2 changes one body.
 */
uint16_t PickTrack(int32_t zoneIndex) {
    const RsMusicZone* z = RsMusicZones_At(zoneIndex);
    if (z == nullptr || z->trackCount == 0) {
        return NA_BGM_DISABLED; // an empty track list is authored silence, not an error
    }
    return z->tracks[0].seqId;
}

// --- driving player 0 ---------------------------------------------------------------------------

void StartTrack(uint16_t seqId, int32_t fadeInUnits) {
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
    sPendingZone = zoneIndex;
    sActiveZone = zoneIndex;
    sCandidateZone = NO_ZONE;
    sDwellTicks = 0;

    if (wasSounding) {
        // Op 1, "disable seq player", with a fade. A duration of 0 here is an immediate disable
        // rather than a fade, which is exactly what a fade-out of zero seconds should mean.
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, fadeOutUnits);
        sPlayingSeqId = NA_BGM_DISABLED;
        sGapTicks = SecondsToTicks(fadeOutSec);
        sState = STATE_FADING;
    } else {
        sGapTicks = 0;
        StartTrack(PickTrack(zoneIndex), fadeInUnits);
    }

    RecordEvent("transition from=%s to=%s track=0x%X reason=%s fade_out=%d fade_in=%d gap=%d "
                "rs=%d,%d pos=%.0f,%.0f,%.0f n=%d frame=%u",
                from == NO_ZONE ? "none" : ZoneName(from), ZoneName(zoneIndex), PickTrack(zoneIndex), reason,
                fadeOutUnits, fadeInUnits, sGapTicks, rsX, rsY, pos.x, pos.y, pos.z, sTransitions,
                gPlayState != nullptr ? gPlayState->state.frames : 0u);
}

void ResetState() {
    sState = STATE_IDLE;
    sActiveZone = NO_ZONE;
    sCandidateZone = NO_ZONE;
    sPendingZone = NO_ZONE;
    sDwellTicks = 0;
    sGapTicks = 0;
    sGraceTicks = 0;
    sPlayingSeqId = NA_BGM_DISABLED;
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
    const int32_t winner = ResolveZone(rsX, rsY, pos.y);
    if (winner == NO_ZONE) {
        return; // no fallback in the table - nothing sensible to do, and nothing worth breaking
    }

    // 1. Finish a switch that is sitting in its quiet gap.
    if (sState == STATE_FADING) {
        if (--sGapTicks <= 0) {
            StartTrack(PickTrack(sPendingZone),
                       FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_IN, RS_MUSIC_FADE_IN_DEFAULT)));
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
            RecordEvent("warp_same_zone zone=%s reason=%s rs=%d,%d frame=%u", ZoneName(winner), reason, rsX, rsY,
                        play->state.frames);
        }
        return;
    }

    // 4. While yielded, wait for player 0 to go quiet. The dwell machinery below still runs, so a
    //    zone change during an override is taken as soon as it earns its dwell - that is the
    //    "re-assert when it goes quiet OR the zone changes" rule, with the dwell attached so a
    //    cutscene that pans Link across a boundary for half a second does not cut its own music.
    if (sState == STATE_YIELDED && live == NA_BGM_DISABLED) {
        // Restart from the top rather than resuming - see ZoneDirector.h on why the asymmetry with
        // combat ducking is deliberate.
        sState = STATE_IDLE;
        BeginSwitch(winner, "override_release", rsX, rsY, pos);
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
        if (sState == STATE_YIELDED) {
            sState = STATE_IDLE; // take player 0 back; the zone genuinely changed under the override
        }
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

extern "C" int32_t RsMusic_TransitionCount(void) {
    return sTransitions;
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
        *zoneIndex = ResolveZone(x, y, player->actor.world.pos.y);
    }
    return 1;
}

extern "C" const char* RsMusic_Describe(void) {
    if (!Enabled()) {
        std::snprintf(sDescription, sizeof(sDescription), "rsmusic on=0 transitions=%d", sTransitions);
        return sDescription;
    }
    int16_t sceneId = -1;
    int32_t rsX = 0;
    int32_t rsY = 0;
    int32_t winner = NO_ZONE;
    const int32_t inZone = RsMusic_Probe(&sceneId, &rsX, &rsY, &winner);
    const int32_t dwellTarget = SecondsToTicks(CVarGetFloat(CVAR_RS_MUSIC_DWELL, RS_MUSIC_DWELL_DEFAULT));
    std::snprintf(sDescription, sizeof(sDescription),
                  "rsmusic on=1 scene=0x%X opted_in=%d state=%s zone=%s track=0x%X winner=%s "
                  "candidate=%s dwell=%d/%d rs=%d,%d fade_out=%d fade_in=%d transitions=%d",
                  sceneId, inZone, StateName(sState), sActiveZone == NO_ZONE ? "none" : ZoneName(sActiveZone),
                  sPlayingSeqId, winner == NO_ZONE ? "none" : ZoneName(winner),
                  sCandidateZone == NO_ZONE ? "none" : ZoneName(sCandidateZone), sDwellTicks, dwellTarget, rsX, rsY,
                  FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_OUT, RS_MUSIC_FADE_OUT_DEFAULT)),
                  FadeUnits(CVarGetFloat(CVAR_RS_MUSIC_FADE_IN, RS_MUSIC_FADE_IN_DEFAULT)), sTransitions);
    return sDescription;
}

extern "C" int32_t RsMusic_TakeEvent(char* buf, uint32_t size) {
    if (sEventRead == sEventWrite || buf == nullptr || size == 0) {
        return 0;
    }
    std::snprintf(buf, size, "%s%s", sEvents[sEventRead], sEventsDropped > 0 ? " (events dropped)" : "");
    sEventRead = (sEventRead + 1) % EVENT_SLOTS;
    return 1;
}
