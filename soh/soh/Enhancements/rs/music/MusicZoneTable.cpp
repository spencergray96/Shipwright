/*
 * The P0 music-zone table (sturdy-bassoon#90). Hand-written data; see MusicZones.h for the format
 * and ZoneDirector.h for what reads it.
 *
 * P0 exists to prove the loop, not to describe the world. Four entries covering the one place a
 * player can currently walk around in - the composited scene `terrain_f2p_settlement` (0x83,
 * `entrance 0x62C`) - with four vanilla NA_BGM_* tracks chosen to be instantly distinguishable by
 * ear, so a transition is audible without knowing anything about the music. They are placeholders.
 * Nothing here is a claim about what F2P should sound like.
 *
 * P1 replaces everything between the markers below with generator output from a world-level zone
 * file in sturdy-bassoon, following the shape tools/grid-scene-tool/server/tableRegistration.ts
 * already uses for the scene registry: rewrite the block between BEGIN/END idempotently, tagged
 * with the source. The markers are here from day one so that landing the generator is a
 * mechanical change rather than a rewrite of this file.
 */

#include "MusicZones.h"

#include <stddef.h>

extern "C" {
#include <z64.h>
#include "sequence.h"
#include "z64scene.h"
}

namespace {

// BEGIN RS MUSIC ZONE TABLE
// source: hand-authored P0 placeholder (sturdy-bassoon#90 P0) - no generator yet

/*
 * Rect derivation, all four verified against the emitted scene rather than estimated:
 *
 *   RS tile (2944, 3519) is terrain tile (0, 0) - the north-west corner of the 6x6 chunk grid
 *   tools/terrain/f2p.json describes. See the ADR for how that was checked.
 *
 *   rsX = 2944 + terrainTileX          terrainTileX = (worldX + 7680) / 40
 *   rsY = 3519 - terrainTileY          terrainTileY = (worldZ + 7680) / 40
 *
 * The composited settlement sits at terrain tile (56, 64) and is 62 x 79 tiles
 * (`Lumbridge Settlement X3@56,64`), i.e. terrain x 56..118, y 64..143, i.e. RS x 3000..3062,
 * y 3376..3455. Its centre is the `agenttest goto -4200 53 -3540` target that
 * docs/test-runs/2026-08-27-settlement-density/ measures from. Link's default spawn in this scene
 * is terrain tile (26, 118) = RS (2970, 3401), which is inside `west_fields` - so a fresh load
 * starts outside the settlement and walking east is the first transition you get.
 *
 * NOTE ON THE PLACE NAMES: these rects are where the *content* is, not where RuneScape's Falador
 * is. Terrain rows 2-5 of f2p.json are density stand-ins rather than geography (f2p.json's
 * scope_note), and the settlement was composited into a chunk labelled "falador-north-gate" as a
 * test placement. The coordinate frame is real and verified; the geography under it is not yet.
 * Names here describe the test fixture on purpose.
 */

const RsZoneRect kSettlementRects[] = {
    { 3000, 3376, 3062, 3455, RS_ZONE_Y_ANY_MIN, RS_ZONE_Y_ANY_MAX },
};

const RsZoneRect kWestFieldsRects[] = {
    { 2944, 3376, 2999, 3455, RS_ZONE_Y_ANY_MIN, RS_ZONE_Y_ANY_MAX },
};

const RsZoneRect kNorthMarchRects[] = {
    { 2944, 3456, 3062, 3519, RS_ZONE_Y_ANY_MIN, RS_ZONE_Y_ANY_MAX },
};

/*
 * One track per zone in P0. The shuffle bag that makes several tracks per zone meaningful is P2;
 * the array is plural now so P2 changes the data and not the format. lengthSec is 0 throughout:
 * these are vanilla sequences, which do not end on their own (AUDIO_SYSTEM.md), and P0 never
 * advances a queue.
 *
 * An accident worth knowing about rather than relying on: vanilla combat music only ducks a track
 * whose sSeqFlags entry has bit 0 set (Audio_SetSequenceMode, code_800EC960.c). KOKIRI (0x11) and
 * GERUDO_VALLEY (0x11) have it; KAKARIKO_KID (0x10) and LONLON (0x00) do not. So this fixture
 * demonstrates both behaviours without anyone choosing that.
 *
 * It stays an accident after #91, which is the part to design away: the gate reads
 * gActiveSeqs[0].seqId, and Audio_StartSequence stores the u8 id REQUESTED there
 * (code_800F9280.c:65) rather than the resolved custom id it hands the audio thread (:61). So a
 * custom RS track played through the seqToPlay back door ducks or does not duck according to the
 * vanilla placeholder id passed with it. Make that an explicit field on RsZoneTrack rather than a
 * side effect. AUDIO_SYSTEM.md section 5 has the detail and the per-actor VB_DETECT_BGM_ENEMY
 * lever that is the deliberate per-encounter control.
 */
const RsZoneTrack kSettlementTracks[] = {
    { NA_BGM_KAKARIKO_KID, RS_ZONE_COND_ANY, 0 },
};
const RsZoneTrack kWestFieldsTracks[] = {
    { NA_BGM_KOKIRI, RS_ZONE_COND_ANY, 0 },
};
const RsZoneTrack kNorthMarchTracks[] = {
    { NA_BGM_GERUDO_VALLEY, RS_ZONE_COND_ANY, 0 },
};
const RsZoneTrack kWildernessTracks[] = {
    { NA_BGM_LONLON, RS_ZONE_COND_ANY, 0 },
};

const RsMusicZone kZones[] = {
    { "settlement", 20, 0, 1, 1, kSettlementRects, kSettlementTracks },
    { "west_fields", 10, 0, 1, 1, kWestFieldsRects, kWestFieldsTracks },
    { "north_march", 10, 0, 1, 1, kNorthMarchRects, kNorthMarchTracks },
    /* The fallback: no rects, matches everywhere, lowest priority. It is what makes coverage
     * total, so every point in the world has an answer and the director never falls silent by
     * accident. Silence, if we ever want it somewhere, is an empty track list - not a hole. */
    { "wilderness", -32768, RS_ZONE_FLAG_FALLBACK, 0, 1, NULL, kWildernessTracks },
};

/*
 * Per-scene opt-in plus the anchor.
 *
 * SCENE_TERRAIN_F2P_SETTLEMENT's collision bounds are (-7680, -7680) .. (7680, 7680) - 384 tiles
 * of 40 units, recentred on the bake window - so terrain tile (0,0) is at world (-7680, -7680) and
 * OoT world (0, 0) is terrain tile (192, 192), which is RS (2944 + 192, 3519 - 192) =
 * (3136, 3327). Cross-checked against the emitted player spawn: ActorEntry position
 * (-6640, 13, -2960) must be terrain tile (26, 118), the spawn f2p.json authors, and
 * 26*40 - 7680 = -6640, 118*40 - 7680 = -2960. It is.
 */
const RsMusicScene kScenes[] = {
    { SCENE_TERRAIN_F2P_SETTLEMENT, 3136, 3327, 40 },
};

// END RS MUSIC ZONE TABLE

} // namespace

extern "C" const RsMusicZone* RsMusicZones_All(int32_t* count) {
    if (count != NULL) {
        *count = (int32_t)(sizeof(kZones) / sizeof(kZones[0]));
    }
    return kZones;
}

extern "C" int32_t RsMusicZones_Count(void) {
    return (int32_t)(sizeof(kZones) / sizeof(kZones[0]));
}

extern "C" const RsMusicZone* RsMusicZones_At(int32_t index) {
    if (index < 0 || index >= RsMusicZones_Count()) {
        return NULL;
    }
    return &kZones[index];
}

extern "C" const RsMusicScene* RsMusicZones_Scene(int16_t sceneId) {
    for (size_t i = 0; i < sizeof(kScenes) / sizeof(kScenes[0]); i++) {
        if (kScenes[i].sceneId == sceneId) {
            return &kScenes[i];
        }
    }
    return NULL;
}

extern "C" int32_t RsMusicZones_SceneCount(void) {
    return (int32_t)(sizeof(kScenes) / sizeof(kScenes[0]));
}

extern "C" const RsMusicScene* RsMusicZones_SceneAt(int32_t index) {
    if (index < 0 || index >= RsMusicZones_SceneCount()) {
        return NULL;
    }
    return &kScenes[index];
}
