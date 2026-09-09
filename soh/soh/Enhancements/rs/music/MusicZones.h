#ifndef SOH_RS_MUSIC_ZONES_H
#define SOH_RS_MUSIC_ZONES_H

/*
 * The music-zone table (sturdy-bassoon#90) - the *data*. The runtime that reads it is
 * ZoneDirector.h; nothing here executes.
 *
 * A music zone is a named region of the world with a list of tracks. Every point in the world
 * belongs to exactly one zone, because one entry carries RS_ZONE_FLAG_FALLBACK and matches
 * everywhere; overlap between the rest is legal and resolved by `priority`, highest first, so a
 * small high-priority zone can sit inside a large one without anybody carving donut-shaped rect
 * unions by hand.
 *
 * THE ROWS ARE GENERATED. MusicZoneTable.cpp's marker block is written by
 * sturdy-bassoon/tools/music/generate-zone-table.ts from sturdy-bassoon/tools/music/zones.json.
 * Edit the zone file and re-run the generator; a hand edit to the block is lost on the next run.
 * The generator refuses a name that does not exist in Shipwright, a table with anything other than
 * exactly one fallback, an inverted rect, and a value too wide for the fields below - the whole
 * class of mistakes that compiles cleanly and then goes quiet in-game.
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
 * THE AXIS FLIP HAPPENS EXACTLY ONCE, in RsMusicZones' WorldToRs (ZoneDirector.cpp). OoT's world Z
 * grows south and the terrain toolchain's tile y grows south; RS's y grows north. Every one of
 * those three frames is documented and internally consistent, and the only place the sign is
 * allowed to change is that one function. Do not flip it again anywhere else - and note that THE
 * GENERATOR DOES NOT FLIP EITHER: its input and its output are both RS tiles, so it has nothing to
 * convert. That is stated in three places on purpose (here, zones.json's `axis_flip` note, and the
 * generator's own header); they move together or not at all.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* No Y constraint on a rect. Vertical space above the ground inherits the ground zone (#90
 * non-goals: "Y-aware zones"); the field exists so the axis is additive later without a format
 * change, and every rect authored so far uses these. Units are OoT world units, not tiles - a Y
 * band is about "which floor of a building", which is an engine-space question. */
#define RS_ZONE_Y_ANY_MIN (-32768)
#define RS_ZONE_Y_ANY_MAX (32767)

/* Track conditions: day/night and weather are a wanted-eventually axis, not alpha 1.0 (#90
 * non-goals). Entries are structs rather than bare ids precisely so that axis costs nothing to add
 * later. The generator emits RS_ZONE_COND_ANY everywhere and the director does not read the field. */
#define RS_ZONE_COND_ANY 0

/* Matches everywhere, at any priority. Exactly one zone in the table carries this - it is what
 * makes coverage total, so the director never has to answer "what plays here" with "nothing". */
#define RS_ZONE_FLAG_FALLBACK 0x01

/* `sceneId` on a zone that is NOT bound to one particular scene - the normal case for a surface
 * zone, which is placed by its rects in the world frame and is therefore answerable from any
 * opted-in scene that has a surface anchor. */
#define RS_ZONE_SCENE_ANY (-1)

/* `firstVisitFlag` on a zone with no first-visit track. Not 0: 0 is a real WorldFlagId. */
#define RS_ZONE_NO_FIRST_VISIT (-1)

/* An axis-aligned rectangle in RS absolute surface tiles, INCLUSIVE on all four edges.
 * (x0,y0) is the south-west corner and (x1,y1) the north-east one, because y grows north. */
typedef struct RsZoneRect {
    int16_t x0, y0;
    int16_t x1, y1;
    int16_t yMinUnits, yMaxUnits; /* OoT world Y band; RS_ZONE_Y_ANY_* = unconstrained */
} RsZoneRect;

typedef struct RsZoneTrack {
    uint16_t seqId;      /* NA_BGM_* today; a custom sequence number after #91 */
    uint16_t conditions; /* RS_ZONE_COND_* - not read yet */
    uint16_t lengthSec;  /* 0 = "plays until stopped". Not read yet: #90 P2's queue advances on this
                          * table rather than on end-of-track detection; see AUDIO_SYSTEM.md. */
} RsZoneTrack;

typedef struct RsMusicZone {
    const char* name; /* single token, no spaces - it goes on the marker channel as zone=<name> */
    int16_t priority; /* highest wins where zones overlap */

    /*
     * Optional binding to one scene (#90 section 4). RS_ZONE_SCENE_ANY for a surface zone.
     *
     * A zone with a scene id and NO rects matches that whole scene - which is what an underground
     * or interior area wants. They are planned as separate scenes anyway (fewer tris, easier to
     * edit, and you can never see the surface from below), and they should not be forced to share
     * the surface queue. Same table, same track machinery; the match is `sceneId ==` instead of
     * `rect contains XZ`.
     *
     * A zone with a scene id AND rects means "these rects, but only in that scene" - the natural
     * generalisation, and how a second surface scene would carve up differently from the first.
     */
    int16_t sceneId;

    /*
     * The world flag consumed by the first-visit track (#90 section 10), or RS_ZONE_NO_FIRST_VISIT.
     *
     * The generator emits the WorldFlagId *enumerator*, never a number: world-flag numbers are
     * serialized into save files and are reserved by hand in rs/quest/WorldFlagIds.h, so exactly
     * one file gets to say what the number is.
     */
    int16_t firstVisitFlag;

    uint8_t flags; /* RS_ZONE_FLAG_* */
    uint8_t rectCount;
    uint8_t trackCount;
    const RsZoneRect* rects;
    const RsZoneTrack* tracks; /* trackCount == 0 means SILENCE, and that is a legitimate choice */

    /*
     * The one-shot opener, or NULL. Played instead of the normal pick the first time this zone is
     * ACTIVATED - after the dwell, not on boundary contact - and the flag is spent at that moment,
     * with no forced completion. Enter, hear thirty seconds of the intended opener, leave, come
     * back: you get the normal track, because the flag is already gone.
     *
     * A single entry rather than an array because it is a seed, not a list: in P2 it seeds the
     * shuffle bag instead of a random pick.
     */
    const RsZoneTrack* firstVisitTrack;
} RsMusicZone;

/*
 * A scene whose position does not map into the world's surface coordinate frame - an interior, an
 * underground area, or any scene with its own local origin. Rect matching is SKIPPED entirely
 * there; only zones bound to that scene id, plus the fallback, can win.
 *
 * This flag is load-bearing rather than tidy. Without it such a scene needs some rsOrigin, and
 * whatever number was picked would put Link at meaningless RS tiles that can land inside a real
 * surface rect - so a surface zone would outrank the scene's own zone, in a scene that has nothing
 * to do with the surface, for no visible reason.
 */
#define RS_SCENE_FLAG_NO_SURFACE_ANCHOR 0x01

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
 * in zones.json and in the anchor ADR. Meaningless, and zero, when RS_SCENE_FLAG_NO_SURFACE_ANCHOR
 * is set.
 */
typedef struct RsMusicScene {
    int16_t sceneId;
    int16_t rsOriginX;
    int16_t rsOriginY;
    int16_t unitsPerTile;
    uint8_t flags; /* RS_SCENE_FLAG_* */
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
