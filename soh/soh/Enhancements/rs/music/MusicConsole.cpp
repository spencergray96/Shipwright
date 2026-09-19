/*
 * The console surface for the zone director (sturdy-bassoon#90 P0). See MusicConsole.h for the
 * subcommand list and why one renderer feeds two sinks.
 */

#include "MusicConsole.h"
#include "MusicZones.h"
#include "ZoneDirector.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/debug/Console.h>

#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "seqcmd.h"
extern PlayState* gPlayState;
// code_800EC960.c's own globals, declared in no header. Read, never written, here - see `players`.
// (ZoneDirector.cpp's ReclaimFromOverride is the one writer of sPrevMainBgmSeqId in the mod.)
extern u8 sPrevSeqMode;
extern u16 sPrevMainBgmSeqId;
// The table behind code_800EC960.c's static Audio_GetSeqFlags. Read by `testplay` to report whether
// a placeholder id lets enemy music duck the track (sturdy-bassoon#91).
extern u8 sSeqFlags[0x6F];
// audio_load.c's registry: sequenceMap[n] is the archive path sequence number n was given at boot,
// NULL for an unassigned number, sequenceMapSize + 0xF slots. z64audio.h declares only the size.
extern char** sequenceMap;
u16 AudioEditor_GetReplacementSeq(u16 seqId);
}

#define CVAR_RS_MUSIC_ON CVAR_ENHANCEMENT("RsMusicZones")
#define CVAR_RS_MUSIC_DWELL CVAR_ENHANCEMENT("RsMusicDwellSec")
#define CVAR_RS_MUSIC_FADE_OUT CVAR_ENHANCEMENT("RsMusicFadeOutSec")
#define CVAR_RS_MUSIC_FADE_IN CVAR_ENHANCEMENT("RsMusicFadeInSec")

namespace {

// Unqualified because this file has ~30 call sites. A using-declaration in the unnamed
// namespace reaches the whole translation unit, including the renderer at global scope below.
using ConsoleSink::Addf;

bool ParseSeconds(const std::string& s, float* out) {
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !(v >= 0.0f) || v > 20.0f) {
        return false;
    }
    *out = v;
    return true;
}

// Report the value the ENGINE will see, not the one that was typed: a fade is an 8-bit field in
// units of 1/30 s, so 12 seconds and 8.5 seconds are the same fade and a run log that echoed "12"
// would be lying about what it measured.
int32_t FadeUnits(float seconds) {
    const int32_t units = (int32_t)(seconds * 30.0f + 0.5f);
    return units > 255 ? 255 : units;
}

int32_t SetSeconds(const std::vector<std::string>& args, std::vector<std::string>& lines, const char* cvar,
                   const char* label, bool isFade) {
    if (args.size() < 2) {
        Addf(lines, "op=%s result=error error=missing_value", label);
        return 1;
    }
    float seconds = 0.0f;
    if (!ParseSeconds(args[1], &seconds)) {
        Addf(lines, "op=%s result=error error=bad_value (expects 0..20 seconds)", label);
        return 1;
    }
    CVarSetFloat(cvar, seconds);
    CVarSave();
    if (isFade) {
        Addf(lines, "op=%s seconds=%.2f units=%d result=ok", label, seconds, FadeUnits(seconds));
    } else {
        Addf(lines, "op=%s seconds=%.2f ticks=%d result=ok", label, seconds, (int32_t)(seconds * 20.0f + 0.5f));
    }
    return 0;
}

// --- TEST SURFACE for sturdy-bassoon#91: tracks / testplay / teststop ---------------------------
//
// #91 imports RS tracks and proves they play; #90 P3 wired them into the zone director. These
// subcommands start, stop and list custom tracks WITHOUT going through the director, so a track can
// be heard and measured on its own.
//
// P3 WAS EXPECTED TO RETIRE `testplay` AND DELIBERATELY DID NOT. The director's start is now the
// production path, but it only ever plays what the zone table says, in an opted-in scene, after a
// dwell - so it cannot answer "does this newly imported file sound right", which is the question
// every future import asks and the one the owner's #91 listening pass was made of. Isolating one
// track from all of the zone logic is the whole value; that is a different job from playing the
// right track in the right place, and it keeps its own command.
//
// What P3 DID take from here is the path->number lookup, which is now RsMusic_ResolveTrackPath in
// ZoneDirector.cpp and shared, so `testplay flute-salad` and the table cannot disagree about which
// sequence that is.

// Every custom sequence is listed from this virtual path (audio_load.c, ListFiles "custom/music/*").
constexpr const char* CUSTOM_MUSIC_PREFIX = "custom/music/";
// The mod's own tracks. See ASSET_PIPELINE.md, "RS music".
constexpr const char* RS_TRACK_PREFIX = "custom/music/rs/";

size_t SequenceSlots() {
    // audio_load.c allocates sequenceMapSize + 0xF slots, and AudioLoad_SyncInitSeqPlayerInternal
    // bounds-checks against the same figure.
    return sequenceMap == nullptr ? 0 : sequenceMapSize + 0xF;
}

bool StartsWith(const char* s, const char* prefix) {
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

// (Resolving a path to a sequence number lives in ZoneDirector.cpp as RsMusic_ResolveTrackPath. It
// started here, for #91's probe; the zone director is the consumer that matters now, and two
// answers to "which number is this path" is one answer too many.)

bool ParseSeqByte(const std::string& s, uint32_t* out) {
    char* end = nullptr;
    const unsigned long v = std::strtoul(s.c_str(), &end, 0);
    if (end == s.c_str() || *end != '\0' || v > 0xFF) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

// The same arithmetic as ZoneDirector.cpp's ScriptTicksPerSecond, which carries the reasoning.
// Repeated rather than exported because #91 leaves the director alone. 0 = the audio spec is not up.
uint32_t AudioTicksPerSecond() {
    const AudioBufferParameters* p = &gAudioContext.audioBufferParameters;
    if (p->frequency == 0 || p->samplesPerFrameTarget <= 0 || p->updatesPerFrame <= 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)p->frequency * (uint64_t)(uint32_t)p->updatesPerFrame) /
                      (uint64_t)(uint32_t)p->samplesPerFrameTarget);
}

} // namespace

int32_t MusicConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? std::string("status") : args[0];

    if (sub == "status") {
        // Grouped lines rather than one, because the ImGui console does not wrap and P0's single
        // line had to be read by dragging the window out to full width. RsMusic_Describe() still
        // exists and is still one line - that is the right shape for a marker or the echo after
        // `on`, and the wrong shape for a human.
        char line[256];
        for (int32_t i = 0; RsMusic_DescribeLine(i, line, sizeof(line)); i++) {
            lines.emplace_back(line);
        }
        return 0;
    }

    if (sub == "where") {
        int16_t sceneId = -1;
        int32_t rsX = 0;
        int32_t rsY = 0;
        int32_t zone = -1;
        const int32_t inZone = RsMusic_Probe(&sceneId, &rsX, &rsY, &zone);
        if (!inZone) {
            // Not a failure: "this scene is not opted in" is the answer, and it is the first thing
            // to check when a run reports no music at all.
            Addf(lines, "where scene=0x%X opted_in=0 reason=scene_not_in_table", sceneId);
            return 0;
        }
        const Player* player = GET_PLAYER(gPlayState);
        const RsMusicZone* z = RsMusicZones_At(zone);
        const RsMusicScene* s = RsMusicZones_Scene(sceneId);
        // surface_anchor=0 means rs= is arithmetic on a placeholder origin rather than a place -
        // an interior or underground scene, where rect matching is skipped and only a zone bound to
        // this scene (or the fallback) can win. Printed next to rs= so nobody reads those two
        // numbers as a location.
        const int32_t anchored = (s != NULL && (s->flags & RS_SCENE_FLAG_NO_SURFACE_ANCHOR)) ? 0 : 1;
        char tiles[32];
        RsMusic_FormatTiles(tiles, sizeof(tiles), rsX, rsY);
        Addf(lines, "where scene=0x%X opted_in=1 surface_anchor=%d pos=%.1f,%.1f,%.1f rs=%s zone=%s", sceneId,
             anchored, player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z, tiles,
             z != nullptr ? z->name : "none");
        return 0;
    }

    if (sub == "zones") {
        const int32_t count = RsMusicZones_Count();
        Addf(lines, "zones count=%d", count);
        for (int32_t i = 0; i < count; i++) {
            const RsMusicZone* z = RsMusicZones_At(i);
            // scene= is the optional binding: `any` for a surface zone placed by its rects, a scene
            // id for one that only exists inside that scene (an underground area). A bound zone
            // with rects=0 matches that whole scene.
            char scene[16];
            if (z->sceneId == RS_ZONE_SCENE_ANY) {
                std::snprintf(scene, sizeof(scene), "any");
            } else {
                std::snprintf(scene, sizeof(scene), "0x%X", (uint32_t)(uint16_t)z->sceneId);
            }
            Addf(lines, "zone[%d]=%s priority=%d fallback=%d scene=%s rects=%d tracks=%d first_visit_flag=%d", i,
                 z->name, (int32_t)z->priority, (z->flags & RS_ZONE_FLAG_FALLBACK) ? 1 : 0, scene,
                 (int32_t)z->rectCount, (int32_t)z->trackCount, (int32_t)z->firstVisitFlag);
            if (z->firstVisitTrack != NULL) {
                char label[48];
                RsMusic_FormatTrack(label, sizeof(label), z->firstVisitTrack);
                Addf(lines, "first_visit[%d]=%s flag=%d set=%d", i, label, (int32_t)z->firstVisitFlag,
                     Flags_GetWorldFlag(z->firstVisitFlag) ? 1 : 0);
            }
            for (uint8_t r = 0; r < z->rectCount; r++) {
                const RsZoneRect& rect = z->rects[r];
                Addf(lines, "rect[%d.%d]=%d,%d..%d,%d y=%d..%d", i, (int32_t)r, (int32_t)rect.x0, (int32_t)rect.y0,
                     (int32_t)rect.x1, (int32_t)rect.y1, (int32_t)rect.yMinUnits, (int32_t)rect.yMaxUnits);
            }
            // The one surface that prints an imported track's FULL path and its live sequence
            // number, because "which file is that" and "did it resolve this boot" are the two
            // questions the short label cannot answer. A vanilla entry reports rs=none.
            for (uint8_t t = 0; t < z->trackCount; t++) {
                const RsZoneTrack& track = z->tracks[t];
                char label[48];
                RsMusic_FormatTrack(label, sizeof(label), &track);
                char endFade[16];
                if (track.endFadeMs == RS_TRACK_END_FADE_GLOBAL) {
                    std::snprintf(endFade, sizeof(endFade), "global");
                } else {
                    std::snprintf(endFade, sizeof(endFade), "%u", (uint32_t)track.endFadeMs);
                }
                char resolved[16];
                if (track.rsPath == NULL) {
                    std::snprintf(resolved, sizeof(resolved), "none");
                } else {
                    const char* error = nullptr;
                    const int32_t seq = RsMusic_ResolveTrackPath(track.rsPath, &error);
                    if (seq < 0) {
                        std::snprintf(resolved, sizeof(resolved), "%s", error != nullptr ? error : "unknown");
                    } else {
                        std::snprintf(resolved, sizeof(resolved), "0x%X", (uint32_t)seq);
                    }
                }
                Addf(lines, "track[%d.%d]=%s seq=0x%X cond=0x%X len_ms=%u end_fade_ms=%s audio_seq=%s rs=\"%s\"", i,
                     (int32_t)t, label, (uint32_t)track.seqId, (uint32_t)track.conditions, track.durationMs, endFade,
                     resolved, track.rsPath != NULL ? track.rsPath : "none");
            }
        }
        return 0;
    }

    if (sub == "scenes") {
        const int32_t count = RsMusicZones_SceneCount();
        Addf(lines, "scenes count=%d", count);
        for (int32_t i = 0; i < count; i++) {
            const RsMusicScene* s = RsMusicZones_SceneAt(i);
            // rs_origin is the RS tile that OoT world (0,0) sits on; it is per-scene because
            // emit-scene.ts recentres each bake on its own bounding box. surface_anchor=0 means
            // the scene has no position in the world frame at all - an interior or underground
            // scene - so rect matching is skipped there and rs_origin is a placeholder, not a
            // location. Reported rather than hidden because "my rect does not fire in this scene"
            // is otherwise a mystery.
            Addf(lines, "scene[%d]=0x%X surface_anchor=%d rs_origin=%d,%d units_per_tile=%d", i,
                 (uint32_t)(uint16_t)s->sceneId, (s->flags & RS_SCENE_FLAG_NO_SURFACE_ANCHOR) ? 0 : 1,
                 (int32_t)s->rsOriginX, (int32_t)s->rsOriginY, (int32_t)s->unitsPerTile);
        }
        return 0;
    }

    if (sub == "bags") {
        // Every zone's shuffle bag: the shuffled order, how far through it we are, and what it
        // played last. The `advance` marker already makes a full cycle reconstructable from the log
        // alone - which is the requirement, because polling costs a harness round trip per sample
        // and a cycle is minutes long. This is for reading the bag at a moment without waiting for
        // it to move, and for seeing the refill's back-to-back guard in the order itself.
        char line[256];
        int32_t emitted = 0;
        for (int32_t i = 0; RsMusic_BagLine(i, line, sizeof(line)); i++) {
            lines.emplace_back(line);
            emitted++;
        }
        // Last, so the count witnesses that the loop ran rather than heading a list that may be
        // empty for an uninteresting reason - the same shape `firstvisit` uses.
        Addf(lines, "bags count=%d", emitted);
        return 0;
    }

    if (sub == "firstvisit") {
        // The one-shot openers and whether each has been spent. This is what lets a run assert the
        // first-visit rule without ears: the flag must still be 0 after clipping the boundary (no
        // switch, so no activation) and 1 after the dwell fires.
        int32_t withOpener = 0;
        const int32_t count = RsMusicZones_Count();
        for (int32_t i = 0; i < count; i++) {
            const RsMusicZone* z = RsMusicZones_At(i);
            if (z->firstVisitTrack == NULL || z->firstVisitFlag == RS_ZONE_NO_FIRST_VISIT) {
                continue;
            }
            withOpener++;
            char label[48];
            RsMusic_FormatTrack(label, sizeof(label), z->firstVisitTrack);
            Addf(lines, "firstvisit zone=%s flag=%d track=%s set=%d", z->name, (int32_t)z->firstVisitFlag, label,
                 Flags_GetWorldFlag(z->firstVisitFlag) ? 1 : 0);
        }
        // Emitted last so the count is a witness that the loop ran, not a header a zero-zone table
        // could produce by doing nothing. `agenttest worldflag <n> 0` clears one to re-test.
        Addf(lines, "firstvisit count=%d", withOpener);
        return 0;
    }

    if (sub == "players") {
        /*
         * What is on all four sequence players right now, read the way the engine reads them
         * (sturdy-bassoon#90 P5). Read-only: polling it changes nothing.
         *
         * WHY THIS EXISTS. The director asks the audio engine exactly one question - "is player 0
         * still ours?" - and a DUCK never changes that answer. Enemy music starts on player 3 and
         * lowers player 0 through volume scale 3; an item fanfare plays on player 1 and zeroes player
         * 0's scale 1. Neither touches player 0's sequence id, which is what makes it a duck, and is
         * also why nothing the director logs can show that one happened. "A duck is not a yield" is a
         * negative, and a negative needs its trigger proven - this is the witness.
         *
         *   id=          func_800FA0B4(p): NA_BGM_DISABLED (0xFFFF) when the player is not enabled
         *   scales=      gActiveSeqs[p].volScales[0..3], the game side's multipliers, 127 = 1.0
         *   vol_cur=     gActiveSeqs[p].volCur, what the game side last derived from those scales
         *   fade_scale=  the audio thread's seqPlayer->fadeVolumeScale, which is where vol_cur lands
         *                (command 0x41). Audio_StartSequence re-sends it to a NEW sequence when vol_cur
         *                is not 1.0 - so a switch made under a duck shows here whether the duck survived
         *   fade=        the audio thread's fadeVolume. NOT 1.0 at rest: it read 0.51-0.55 on tracks
         *                long past their fade-in through the whole P5 run, so the sequence data sets
         *                it. A fade ramps it; compare it across samples, never against 1.0
         *
         * The last line is the combat-music state machine. seq_mode= is sPrevSeqMode: bit 7 set means
         * Audio_SetSequenceMode's enemy-capable branch last ran, low bits are SEQ_MODE_* (1 = enemy).
         * prev_main= is sPrevMainBgmSeqId, the id func_800F5ACC stashed for a mini-boss to hand back;
         * while it is anything but 0xFFFF, Audio_SetSequenceMode does nothing at all, so enemy music
         * is off. save_seq= is gSaveContext.seqId.
         *
         * fade_scale= and fade= are audio-thread words read without a lock - the same unsynchronised
         * read func_800FA0B4 makes of `enabled`. Expect a tick-boundary stale value now and then.
         *
         * Added for sturdy-bassoon#91, the imported-track probe, and appended so existing parsers of
         * the fields above are unaffected:
         *
         *   audio_seq=   the AUDIO THREAD's seqPlayer->seqId, the sequence actually loaded. id= is the
         *                game side, and Audio_StartSequence stores the u8 id that was REQUESTED there -
         *                so after a seqToPlay/seqReplaced start the two can differ: id= the placeholder,
         *                audio_seq= the custom sequence. Not cleared when the player stops
         *   counter=     seqPlayer->scriptCounter. Zeroed when a sequence loads, and it stops moving when
         *   sec=         the player does: the increment sits below the `enabled` early return. sec= is
         *                counter over tick_hz=. So one sample taken after a track has ended reads how
         *                long it played, on the audio clock
         *   replaced=    gAudioContext.seqReplaced[p], a back-door start not yet consumed.
         *                Audio_StartSequence clears it when it takes it; a 1 that lingers is the trap
         */
        const uint32_t tickHz = AudioTicksPerSecond();
        for (int32_t p = 0; p < 4; p++) {
            const ActiveSequence* a = &gActiveSeqs[p];
            const SequencePlayer* sp = &gAudioContext.seqPlayers[p];
            Addf(lines,
                 "players[%d] id=0x%X scales=%d,%d,%d,%d vol_cur=%.2f fade_scale=%.2f fade=%.2f audio_seq=0x%X "
                 "counter=%u sec=%.2f replaced=%d",
                 p, (uint32_t)func_800FA0B4((u8)p), (int32_t)a->volScales[0], (int32_t)a->volScales[1],
                 (int32_t)a->volScales[2], (int32_t)a->volScales[3], a->volCur, sp->fadeVolumeScale, sp->fadeVolume,
                 (uint32_t)sp->seqId, (uint32_t)sp->scriptCounter,
                 tickHz != 0 ? (double)sp->scriptCounter / (double)tickHz : -1.0,
                 (int32_t)gAudioContext.seqReplaced[p]);
        }
        Addf(lines, "players seq_mode=0x%X prev_main=0x%X save_seq=0x%X tick_hz=%u", (uint32_t)sPrevSeqMode,
             (uint32_t)sPrevMainBgmSeqId, (uint32_t)gSaveContext.seqId, tickHz);
        return 0;
    }

    if (sub == "tracks") {
        // TEST SURFACE (#91). Every custom sequence and the number it was given this boot, for two jobs:
        // - The witness for "does adding a track renumber the others": compare two boots' lines.
        // - The loud end of the soh.o2r copy trap. No RS track registered almost always means the
        //   archive next to soh.exe is stale, so that is rc=1, not an empty list that reads as success.
        int32_t count = 0;
        int32_t rs = 0;
        const size_t slots = SequenceSlots();
        for (size_t i = 0; i < slots; i++) {
            const char* path = sequenceMap[i];
            if (path == nullptr || !StartsWith(path, CUSTOM_MUSIC_PREFIX)) {
                continue;
            }
            const int32_t isRs = StartsWith(path, RS_TRACK_PREFIX) ? 1 : 0;
            Addf(lines, "track seq=0x%X rs=%d path=\"%s\"", (uint32_t)i, isRs, path);
            count++;
            rs += isRs;
        }
        Addf(lines, "tracks count=%d rs=%d slots=%u", count, rs, (uint32_t)slots);
        if (rs == 0) {
            Addf(lines, "tracks result=error error=no_rs_tracks hint=soh.o2r_next_to_soh.exe_is_stale_or_missing");
            return 1;
        }
        return 0;
    }

    if (sub == "testplay") {
        // TEST SURFACE (#91): start a custom track on player 0 through the seqToPlay/seqReplaced back
        // door, the route #90 P3 will take because SEQCMD ids are masked to 8 bits (AUDIO_SYSTEM.md §2).
        //
        //   testplay <track> <placeholder 0..0xFF> [fade_in_sec]
        //
        // The placeholder is the u8 id that rides through SEQCMD beside the real one, and it is not
        // decoration. Audio_StartSequence stores IT in gActiveSeqs[0].seqId, so it is:
        // - what func_800FA0B4 reports,
        // - what the enemy-music gate reads flags from (ducks=),
        // - what a mini-boss's func_800F5B58 restarts (restart_path=, resolved through the audio
        //   editor's mapping exactly as Audio_StartSequence resolves it; "none" means that restart
        //   loads nothing and player 0 goes quiet).
        //
        // In a scene the director owns it sees a sequence it did not ask for and YIELDS. That is its
        // override path working, not a fault. To hear a track end on its own, test outside the table
        // (director_scene=0) or `rsmusic off` and re-enter the scene.
        if (args.size() < 3) {
            Addf(lines, "op=testplay result=error error=usage (testplay <track> <placeholder 0..0xFF> [fade_in_sec])");
            return 1;
        }
        const char* error = nullptr;
        const int32_t seq = RsMusic_ResolveTrackPath(args[1].c_str(), &error);
        if (seq < 0) {
            Addf(lines, "op=testplay result=error error=%s track=%s", error, args[1].c_str());
            return 1;
        }
        uint32_t placeholder = 0;
        if (!ParseSeqByte(args[2], &placeholder)) {
            Addf(lines, "op=testplay result=error error=bad_placeholder (expects 0..0xFF)");
            return 1;
        }
        float fadeInSec = 0.0f;
        if (args.size() >= 4 && !ParseSeconds(args[3], &fadeInSec)) {
            Addf(lines, "op=testplay result=error error=bad_fade (expects 0..20 seconds)");
            return 1;
        }
        const uint32_t flags = (placeholder < ARRAY_COUNT(sSeqFlags)) ? sSeqFlags[placeholder] : 0;
        const u16 restartSeq = AudioEditor_GetReplacementSeq((u16)placeholder);
        const char* restartPath = (restartSeq < SequenceSlots()) ? sequenceMap[restartSeq] : nullptr;
        int32_t directorScene = 0;
        if (gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr) {
            int16_t sceneId = -1;
            int32_t rsX = 0;
            int32_t rsY = 0;
            int32_t zone = -1;
            directorScene = RsMusic_Probe(&sceneId, &rsX, &rsY, &zone) ? 1 : 0;
        }

        gAudioContext.seqToPlay[SEQ_PLAYER_BGM_MAIN] = (u16)seq;
        gAudioContext.seqReplaced[SEQ_PLAYER_BGM_MAIN] = 1;
        SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, FadeUnits(fadeInSec), 0, placeholder);

        Addf(lines,
             "op=testplay result=ok seq=0x%X placeholder=0x%X flags=0x%X ducks=%d fade_in=%d director_scene=%d "
             "restart_seq=0x%X restart_path=\"%s\" path=\"%s\"",
             (uint32_t)seq, placeholder, flags, (flags & 0x1) ? 1 : 0, FadeUnits(fadeInSec), directorScene,
             (uint32_t)restartSeq, restartPath != nullptr ? restartPath : "none", sequenceMap[seq]);
        return 0;
    }

    if (sub == "teststop") {
        // TEST SURFACE (#91): stop player 0, with an optional fade, to end a listening test early.
        float fadeOutSec = 0.0f;
        if (args.size() >= 2 && !ParseSeconds(args[1], &fadeOutSec)) {
            Addf(lines, "op=teststop result=error error=bad_fade (expects 0..20 seconds)");
            return 1;
        }
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, FadeUnits(fadeOutSec));
        Addf(lines, "op=teststop result=ok fade_out=%d", FadeUnits(fadeOutSec));
        return 0;
    }

    if (sub == "on" || sub == "off") {
        const int32_t value = (sub == "on") ? 1 : 0;
        CVarSetInteger(CVAR_RS_MUSIC_ON, value);
        CVarSave();
        // Flipping ON mid-scene does not silence the vanilla loader retroactively - the stand-down
        // happens in AfterSceneCommands, which has already run. The director will still take
        // player 0 on its next tick, but the clean A/B is: flip, then re-enter the scene.
        Addf(lines, "op=%s on=%d result=ok note=reload_scene_for_clean_ab", sub.c_str(), value);
        lines.emplace_back(RsMusic_Describe());
        return 0;
    }

    if (sub == "dwell") {
        return SetSeconds(args, lines, CVAR_RS_MUSIC_DWELL, "dwell", false);
    }
    if (sub == "fadeout") {
        return SetSeconds(args, lines, CVAR_RS_MUSIC_FADE_OUT, "fadeout", true);
    }
    if (sub == "fadein") {
        return SetSeconds(args, lines, CVAR_RS_MUSIC_FADE_IN, "fadein", true);
    }

    if (sub == "baseline") {
        // Bookmarks the count; it does NOT zero it, and there is deliberately no way to. A run that
        // wants to assert the negative takes a baseline here, walks, and compares - see the
        // negative assertion in AUDIO_SYSTEM.md: cross a boundary and come back inside the dwell
        // window, and the count must not move.
        //
        // This was `reset` through P0. The behaviour was right and the name was a lie: a human
        // typed it, watched the number stay, and concluded the feature was broken (#90, human
        // tuning pass 2026-09-09). `status` now reports transitions=, baseline= and the difference,
        // so the workflow reads correctly from either end.
        Addf(lines, "op=baseline baseline=%d transitions=%d", RsMusic_MarkBaseline(), RsMusic_TransitionCount());
        return 0;
    }

    if (sub == "reset") {
        // Retired name, kept as a refusal rather than an alias: it never reset anything, and
        // silently accepting it would keep the lie alive in muscle memory and in old run notes.
        // docs/test-runs/2026-09-08-zone-music-p0 uses it and stays as the historical record.
        Addf(lines, "op=reset result=error error=renamed_to_baseline (it never reset the counter; "
                    "`baseline` bookmarks it and `status` reports the difference)");
        return 1;
    }

    Addf(lines, "op=%s result=error error=unknown_subcommand", sub.c_str());
    return 1;
}

// --- the human sink: the `rsmusic` console command ----------------------------------------------
//
// The mechanical half is ConsoleSink (sturdy-bassoon#112). What stays here is the one part of this
// surface that is not mechanical: the marker echo.

namespace {

// #91's test subcommands also reach the engine log from a human session, so a listening pass can be
// read back afterwards the way #97 made director events readable. Only these: the read-only
// subcommands get polled, and a poll is not an event.
//
// This wraps the renderer rather than living inside it, because `agenttest music` writes its own
// markers for every subcommand and would double these.
int32_t RunAndEchoTestMarkers(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const int32_t rc = MusicConsole_Run(args, lines);
    if (!args.empty() && (args[0] == "testplay" || args[0] == "teststop" || args[0] == "tracks")) {
        for (const std::string& line : lines) {
            AgentTest_WriteMarker(("rs_music " + line).c_str());
        }
    }
    return rc;
}

const ConsoleSink::Command musicCommand(
    "rsmusic", RunAndEchoTestMarkers,
    "Zone-based overworld music (sturdy-bassoon#90): status | where | zones | scenes | bags | "
    "firstvisit | players | on | off | dwell <sec> | fadeout <sec> | fadein <sec> | baseline | tracks | "
    "testplay <track> <placeholder> [fade_in_sec] | teststop [fade_out_sec]. `players` "
    "reads all four sequence players, which is how a duck (enemy music, a fanfare) is seen at all. "
    "`tracks`, `testplay` and `teststop` are the imported-track probe (sturdy-bassoon#91): they "
    "list custom sequences and start or stop one on player 0 WITHOUT the director. "
    "`where` prints Link's "
    "position in both OoT world units and RS absolute tiles plus the zone that wins there - "
    "that is how you check a rect against where he actually is. Fades are an 8-bit field in "
    "units of 1/30 s, so they clamp at 8.5 seconds and the reported unit count is what the "
    "engine really gets. `baseline` bookmarks the transition count so `status` can report the "
    "difference; it does NOT zero the counter, on purpose.",
    { { "status|where|zones|scenes|bags|firstvisit|players|on|off|dwell|fadeout|fadein|baseline|tracks|"
        "testplay|teststop",
        Ship::ArgumentType::TEXT },
      { "seconds|track", Ship::ArgumentType::TEXT, true },
      { "placeholder", Ship::ArgumentType::TEXT, true },
      { "fade_in_sec", Ship::ArgumentType::TEXT, true } });

} // namespace
