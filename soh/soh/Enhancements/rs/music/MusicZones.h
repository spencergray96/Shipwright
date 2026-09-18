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
 * The generator refuses a name that does not exist in Shipwright, an RS track id that is not in
 * sturdy-bassoon/tools/music/rs-tracks.record.json, a table with anything other than exactly one
 * fallback, an inverted rect, and a value too wide for the fields below - the whole class of
 * mistakes that compiles cleanly and then goes quiet in-game.
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

/*
 * The ceiling on the table, because the director's shuffle bags (#90 P2) are fixed-size statics.
 *
 * THE GENERATOR READS THESE TWO NUMBERS OUT OF THIS HEADER and refuses a table that exceeds
 * either. That is the only reason they are safe to raise: change them here, rebuild, and the
 * generator's limit moves with the code's, so a table can never grow past the array that holds its
 * state and start reading somebody else's.
 *
 * They are not engine limits - nothing in OoT knows this feature exists. They are the size of the
 * per-zone bag state (about 24 bytes a zone, so the ceiling costs ~1.5 KB of BSS, which is
 * nothing). If a real world needs more zones than this, raise the number. If the count ever gets
 * large enough for a flat array to be silly, the alternative is a bag keyed by zone name in a
 * small map - which costs an allocation the director deliberately does not make today.
 */
#define RS_MUSIC_MAX_ZONES 64
#define RS_MUSIC_MAX_TRACKS_PER_ZONE 16

/* An axis-aligned rectangle in RS absolute surface tiles, INCLUSIVE on all four edges.
 * (x0,y0) is the south-west corner and (x1,y1) the north-east one, because y grows north. */
typedef struct RsZoneRect {
    int16_t x0, y0;
    int16_t x1, y1;
    int16_t yMinUnits, yMaxUnits; /* OoT world Y band; RS_ZONE_Y_ANY_* = unconstrained */
} RsZoneRect;

/*
 * `endFadeMs` on a track whose duration is a POLICY CUT rather than the end of the music - every
 * vanilla NA_BGM_* entry, because those loop and the duration is "let this play for a minute, then
 * move on". Such an advance happens mid-song, so it takes the global RsMusicFadeOutSec like a zone
 * change or a teleport does.
 *
 * An explicit number, INCLUDING 0, means the opposite: this duration is where the music really
 * ends, so the fade belongs to the track (#90 P3, #91 section 7a). 0 = let the track's own ending
 * play. The distinction is per entry rather than per kind, which is why the sentinel is not
 * "rsPath == NULL".
 */
#define RS_TRACK_END_FADE_GLOBAL 0xFFFF

typedef struct RsZoneTrack {
    /*
     * The u8 id the rest of the game believes is playing.
     *
     * For a vanilla entry that is the track: NA_BGM_*. FOR AN IMPORTED RS TRACK IT IS THE
     * PLACEHOLDER (#91) - the vanilla-range id that rides through SEQCMD beside the real custom
     * number, because SEQCMD masks its id field to 8 bits and custom sequences number from ~110 up.
     * Audio_StartSequence stores THIS in gActiveSeqs[0].seqId, so it is what func_800FA0B4 reports,
     * what the enemy-music flag gate reads, and what a mini-boss's restart replays.
     *
     * Which is why the director's "is player 0 still ours" tests need no special case for RS
     * tracks: they compare against this field, and this field is what player 0 reports. The
     * alternative - comparing against the custom sequence number - makes the director yield to its
     * own track on the next tick, which is #90's comment of 2026-09-17 and #91's measured answer 1.
     */
    uint16_t seqId;
    uint16_t conditions; /* RS_ZONE_COND_* - not read yet */

    /*
     * How long this track plays before the queue advances, in MILLISECONDS. Read since #90 P2;
     * widened from whole seconds by P3.
     *
     * 0 STILL MEANS "PLAYS UNTIL STOPPED", so a zone whose tracks are all 0 never advances - which
     * is what every zone did through P1 and remains a legitimate authored choice, not a hole.
     *
     * WHY MILLISECONDS. An imported track's duration is its measured `audioSec`, which is
     * fractional (136.046 s), and P3 starts a fade a few seconds before it. Rounding the target to
     * a whole second is up to half a second of error on top of the clock's own, against a fade
     * whose whole job is to land on the ending. Whole seconds stay expressible; the generator
     * multiplies an authored `lengthSec` by 1000.
     *
     * IT IS STILL A DURATION TABLE, NOT AN END-OF-TRACK DETECTOR. #90 section 9 chose it
     * deliberately. For a looping vanilla NA_BGM_* id this number is a POLICY - "let this play for
     * a minute, then move on" - because the sequence data loops and would not end on its own. For
     * an imported RS track it is `audioSec` from tools/music/rs-tracks.record.json: when the MUSIC
     * ends, which is NOT when the sequence player stops. The player runs about 2% past `Length`
     * plus up to 1.25 s of tick rounding, so waiting for quiet would leave a silent tail (#91).
     *
     * The director does also use the engine's end-of-track signal, where it happens to be
     * available: player 0 going quiet advances the queue too. That is opportunistic, never the
     * mechanism. See ZoneDirector.h, "one end-of-track handler, two triggers".
     *
     * ALL-OR-NOTHING WITHIN A ZONE. The generator refuses a zone that mixes 0 with non-zero,
     * because such a queue advances until it draws the 0 and then silently stops advancing for the
     * rest of the session - a dead end that looks exactly like the feature not being finished.
     */
    uint32_t durationMs;

    /*
     * The fade-out, in milliseconds, for the in-zone advance this track's own duration triggers -
     * and the moment that advance fires is `durationMs - endFadeMs`, so the ramp reaches zero as
     * the music ends rather than running over a silence that is already there.
     *
     * RS_TRACK_END_FADE_GLOBAL defers to RsMusicFadeOutSec; see the #define above for when that is
     * the right answer. Everything that is NOT this trigger - zone changes, teleports, override
     * releases - keeps the global fade whatever this field says, because those happen mid-song.
     *
     * Capped at 8500 by the generator: a fade is an 8-bit field in units of 1/30 s (AUDIO_SYSTEM.md
     * section 3), so 8.5 s is the longest the engine can express and more would silently wrap.
     */
    uint16_t endFadeMs;

    /*
     * NULL for a vanilla NA_BGM_* entry. For an imported RS track, its `sequencePath` from
     * rs-tracks.record.json - e.g. "custom/music/rs/flute-salad".
     *
     * THE PATH IS THE NAME AND THE NUMBER IS NOT, so the number is never stored here. Custom
     * sequence numbers are handed out in sorted path order across EVERY mounted archive, so
     * installing any mod whose custom/music path sorts earlier renumbers ours - measured in #91,
     * where adding one archive moved two tracks in another. The director resolves this string
     * against audio_load.c's sequenceMap at pick time, every time.
     */
    const char* rsPath;
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
     * A single entry rather than an array because it is a seed, not a list. Since #90 P2 that is
     * literal: playing the opener records it as the zone bag's just-played track, so the bag's
     * very first draw cannot repeat it.
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
