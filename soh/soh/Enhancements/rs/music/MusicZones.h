#ifndef SOH_RS_MUSIC_ZONES_H
#define SOH_RS_MUSIC_ZONES_H

/*
 * The music-zone table (sturdy-bassoon#90 P0) - the *data*. The runtime that reads it is
 * ZoneDirector.h; nothing here executes.
 *
 * A music zone is a named region of the world with a list of tracks. Every point in the world
 * belongs to exactly one zone, because one entry carries RS_ZONE_FLAG_FALLBACK and matches
 * everywhere; overlap between the rest is legal and resolved by `priority`, highest first, so a
 * small high-priority zone can sit inside a large one without anybody carving donut-shaped rect
 * unions by hand.
 *
 * COORDINATES ARE RS ABSOLUTE SURFACE TILES, y increasing NORTH.
 *
 * Not OoT world units, and not the terrain toolchain's local 0..383 tile coordinates. The reason
 * is that both of those move: the emitted scene's origin is the centre of whatever bake window was
 * baked (tools/terrain/emit-scene.ts recentres on the model bounds), and the local frame is
 * anchored to the north-west corner of a chunk extent that is explicitly a testing approximation
 * and will grow. RS's own frame is anchored by Jagex and never moves in any direction, so a rect
 * authored today survives every re-bake and every expansion. See
 * sturdy-bassoon/docs/decisions/2026-09-08-rs-terrain-coordinate-anchor.md, which also records how
 * the anchor below was verified against the wiki's own map data rather than taken from memory.
 *
 * THE AXIS FLIP HAPPENS EXACTLY ONCE, in RsMusicZones_WorldToRs (ZoneDirector.cpp). OoT's world Z
 * grows south and the terrain toolchain's tile y grows south; RS's y grows north. Every one of
 * those three frames is documented and internally consistent, and the only place the sign is
 * allowed to change is that one function. Do not flip it again anywhere else.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* No Y constraint on a rect. Vertical space above the ground inherits the ground zone (#90
 * non-goals: "Y-aware zones"); the field exists so the axis is additive later without a format
 * change, and every P0 rect uses these. Units are OoT world units, not tiles - a Y band is about
 * "which floor of a building", which is an engine-space question. */
#define RS_ZONE_Y_ANY_MIN (-32768)
#define RS_ZONE_Y_ANY_MAX (32767)

/* Track conditions: day/night and weather are a wanted-eventually axis, not alpha 1.0 (#90
 * non-goals). Entries are structs rather than bare ids precisely so that axis costs nothing to add
 * later. P0 emits RS_ZONE_COND_ANY everywhere and the director does not read the field. */
#define RS_ZONE_COND_ANY 0

/* Matches everywhere, at any priority. Exactly one zone in the table carries this - it is what
 * makes coverage total, so the director never has to answer "what plays here" with "nothing". */
#define RS_ZONE_FLAG_FALLBACK 0x01

/* An axis-aligned rectangle in RS absolute surface tiles, INCLUSIVE on all four edges.
 * (x0,y0) is the south-west corner and (x1,y1) the north-east one, because y grows north. */
typedef struct RsZoneRect {
    int16_t x0, y0;
    int16_t x1, y1;
    int16_t yMinUnits, yMaxUnits; /* OoT world Y band; RS_ZONE_Y_ANY_* = unconstrained */
} RsZoneRect;

typedef struct RsZoneTrack {
    uint16_t seqId;      /* NA_BGM_* today; a custom sequence number after #91 */
    uint16_t conditions; /* RS_ZONE_COND_* - unread in P0 */
    uint16_t lengthSec;  /* 0 = "plays until stopped". The P2 queue advances on this table rather
                          * than on end-of-track detection; see AUDIO_SYSTEM.md. */
} RsZoneTrack;

typedef struct RsMusicZone {
    const char* name; /* single token, no spaces - it goes on the marker channel as zone=<name> */
    int16_t priority; /* highest wins where zones overlap */
    uint8_t flags;    /* RS_ZONE_FLAG_* */
    uint8_t rectCount;
    uint8_t trackCount;
    const RsZoneRect* rects;
    const RsZoneTrack* tracks; /* trackCount == 0 means SILENCE, and that is a legitimate choice */
} RsMusicZone;

/*
 * Per-scene opt-in, and the anchor that makes a world-space rect answerable from an OoT position.
 *
 * Opt-in rather than global: there are ~20 generated grid-tool scenes plus all of vanilla, and the
 * zone table describes only F2P. Silencing every scene to route it through this table would mute
 * every unrelated test map. A scene that is not in this list is untouched - the vanilla loader
 * still sets its music and the director never runs.
 *
 * rsOriginX / rsOriginY are the RS tile the scene's OoT world origin (0, 0) sits on. They are
 * per-scene because emit-scene.ts recentres each bake on its own bounding box, so the same terrain
 * tile has a different world position in a different bake. Deriving them belongs to the world
 * manifest (#92); until that exists they are computed by hand and their derivation is written down
 * in the ADR.
 */
typedef struct RsMusicScene {
    int16_t sceneId;
    int16_t rsOriginX;
    int16_t rsOriginY;
    int16_t unitsPerTile;
} RsMusicScene;

const RsMusicZone* RsMusicZones_All(int32_t* count);
const RsMusicZone* RsMusicZones_At(int32_t index);
int32_t RsMusicZones_Count(void);

/* NULL when the scene is not opted in. */
const RsMusicScene* RsMusicZones_Scene(int16_t sceneId);
const RsMusicScene* RsMusicZones_SceneAt(int32_t index);
int32_t RsMusicZones_SceneCount(void);

#ifdef __cplusplus
}
#endif

#endif /* SOH_RS_MUSIC_ZONES_H */
