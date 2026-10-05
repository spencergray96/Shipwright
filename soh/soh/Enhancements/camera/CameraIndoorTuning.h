#ifndef SOH_CAMERA_INDOOR_TUNING_H
#define SOH_CAMERA_INDOOR_TUNING_H

#include "z64.h"
#include "soh/cvar_prefixes.h"

/*
 * Shared surface for the indoor camera pull-in (sturdy-bassoon#108): the CVar names and defaults,
 * and the probe the `camindoor` console command reads.
 *
 * The behaviour itself lives in `soh/src/code/z_camera.c`, beside the #38 / #103 camera work it
 * sits on top of - see `Camera_IndoorPullInScale` and its call in `Camera_Normal1`. This header is
 * only what the C engine side and the C++ console side both need to agree on.
 *
 * Every knob is a CVar READ AT THE POINT OF USE, never cached and never latched: a value typed
 * into `camindoor` applies on the next frame with no rebuild, and a wrong one cannot stick. One
 * exception, forced by the engine: the #136 height feeds parameters vanilla caches on a reload, so
 * z_camera.c remembers the last t it reloaded for (Camera_ReloadOnHeightChange) to know when to
 * force another. SoH's console `set` would also reach these CVars; the subcommands exist for their
 * range checks and their `status` readout.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Master switch. Off restores exactly the vanilla distance clamp on grid-tool scenes too.
#define CVAR_CAM_INDOOR_ON CVAR_ENHANCEMENT("CamIndoorPullIn")
#define CAM_INDOOR_ON_DEFAULT 1

// What distMin/distMax are multiplied by while indoors. 1.0 is a no-op.
//
// 0.65, measured rather than guessed. The issue proposed starting at 0.8 or 0.9; both are
// measurably nothing while STANDING in a small room - the complaint this feature exists for -
// because the eye there is already pinned by its own collision, so capping a target it is not
// sitting at changes no pixels. Standing in the level-1 room at (680, 84, -300): eye-to-Link
// 170.3 at 1.0, 170.4 at 0.8, 146.8 at 0.65. While MOVING, 0.8 does bite (mean eye-to-Link 143
// against 162) - the two regimes disagree, and the standing one is the one a player complains
// about. 0.5 goes further (moving mean 108) and was judged likely too close to tune down to
// without a human looking at it. Full table in
// docs/test-runs/2026-09-17-issue-108-camera-indoor/README.md.
#define CVAR_CAM_INDOOR_SCALE CVAR_ENHANCEMENT("CamIndoorScale")
#define CAM_INDOOR_SCALE_DEFAULT 0.65f
// Below a third of the tuned distance the eye is inside the player. Above 1 would push the eye
// further back indoors, which is the opposite of the point and only fights the eye's collision.
#define CAM_INDOOR_SCALE_MIN 0.33f
#define CAM_INDOOR_SCALE_MAX 1.0f

// How far above the floor the player stands on a ceiling has to be to count as "indoors", in OoT
// units. One grid-tool storey is 80 floor to floor, plus a 4-unit slab, so ~100 catches a single
// storey and leaves a two-storey hall at the normal distance.
#define CVAR_CAM_INDOOR_HEIGHT CVAR_ENHANCEMENT("CamIndoorCeilHeight")
#define CAM_INDOOR_HEIGHT_DEFAULT 100.0f
// BgCheck_AnyCheckCeiling needs a positive height. Past five storeys every roofed hall in a scene
// counts as indoors and the knob has stopped discriminating.
#define CAM_INDOOR_HEIGHT_MIN 1.0f
#define CAM_INDOOR_HEIGHT_MAX 400.0f

// Fraction of the normal per-frame step taken while the distance is being pulled IN because of
// this feature. 1.0 is the vanilla rate. Smaller means a short pass under an overhead barely moves
// the camera before the target flips back - a dwell with no counter to go stale. Pushing back OUT
// is never slowed, so stepping outdoors opens the view at the normal rate.
//
// Ships at 1.0, i.e. the mechanism is in and switched off, because nothing measured asks for it.
// The doorway pulse it was meant to damp turned out to be vanilla's own and LARGER than ours
// (30.5 units of it with the pull-in numerically disabled, 21.4 with it on), and Lumbridge Castle
// has no overhead with open ground on both sides at one level to exercise the other case. The
// tested alternative is 0.25: after two seconds under cover the distance has reached 179 instead
// of the 163 cap, and standing still in a room it takes 4.0 s to settle instead of 2.5 s. 0.10
// takes 9.5 s, which is too slow to feel like a camera. Turn it down if a feel walk finds
// walkways and archways pulling in when they should not.
#define CVAR_CAM_INDOOR_EASE_IN CVAR_ENHANCEMENT("CamIndoorEaseIn")
#define CAM_INDOOR_EASE_IN_DEFAULT 1.0f
// Not 0: a zero step would freeze the distance entirely while indoors, including the vanilla
// pull-in this sits on top of.
#define CAM_INDOOR_EASE_IN_MIN 0.01f
#define CAM_INDOOR_EASE_IN_MAX 1.0f

// Ring of extra ceiling samples around the player, on top of the one at his feet. 0 = that single
// sample only. Capped at CAM_INDOOR_RING_MAX.
//
// Ships at 0. The ring is for rejecting thin overheads, and on the scenes that exist it rejects
// nothing: sweeping the covered strip at z=160 in Lumbridge Castle, a 4-point ring at radius 20
// with k=3, and an 8-point ring at radius 30 with k=5, both mark exactly the same cells indoors
// as the single sample does, because every covered span in the scene is wider than any ring that
// fits around the player. It costs nothing to leave in (8 extra queries per frame do not show
// above the run-to-run spread in tick_ms) and nothing to leave off. `bias` below is the half of
// this that does something.
#define CVAR_CAM_INDOOR_RING_SAMPLES CVAR_ENHANCEMENT("CamIndoorRingSamples")
#define CAM_INDOOR_RING_SAMPLES_DEFAULT 0

// Ring radius in OoT units. A grid-tool tile is 40.
#define CVAR_CAM_INDOOR_RING_RADIUS CVAR_ENHANCEMENT("CamIndoorRingRadius")
#define CAM_INDOOR_RING_RADIUS_DEFAULT 20.0f
// Five tiles. A ring wider than the room it is sampling stops describing the room.
#define CAM_INDOOR_RING_RADIUS_MAX 200.0f

// How far ahead of the player's facing the ring's centre is pushed, in OoT units, so the pull-in
// starts just before a threshold instead of on it. The centre sample stays at the player.
//
// Ships at 0, and it does work: a bias of 60 (1.5 tiles) moved the whole indoor band 60 units
// earlier along the approach to the covered strip at z=160. It moves the FAR edge earlier by the
// same amount, though, so the camera also opens up before the player is actually out from under
// cover. Whether leading in is worth trailing out is a feel judgement, so it is left off rather
// than guessed at. Needs `ring` above to be non-zero to do anything.
#define CVAR_CAM_INDOOR_RING_BIAS CVAR_ENHANCEMENT("CamIndoorRingBias")
#define CAM_INDOOR_RING_BIAS_DEFAULT 0.0f
#define CAM_INDOOR_RING_BIAS_MAX 200.0f

// How many of the 1 + ring samples must find a ceiling. Clamped to the sample count, so raising
// the threshold without raising the ring size can never switch the feature off by accident.
#define CVAR_CAM_INDOOR_RING_K CVAR_ENHANCEMENT("CamIndoorRingK")
#define CAM_INDOOR_RING_K_DEFAULT 1

#define CAM_INDOOR_RING_MAX 8

/*
 * Bisect switches for the camera bounce (sturdy-bassoon#152). Each turns one of our own camera
 * corrections off, engine-wide, so a live session can find which one is fighting without a
 * rebuild. They are diagnostics, not features: all three ship ON, and `off` is only ever a
 * question asked of a running game. The swing path they sit beside already has a switch of its
 * own in SoH (`set gEnhancements.FixCameraSwing 1`).
 */
// Camera_KeepEyeUnderCeiling (#103): hold the eye under a ceiling between it and `at`.
#define CVAR_CAM_CEIL_CLAMP_ON CVAR_ENHANCEMENT("CamCeilClamp")
#define CAM_CEIL_CLAMP_ON_DEFAULT 1
// Camera_FloorAheadIfReachable (#103): the slope probe's reachability rule.
#define CVAR_CAM_FLOOR_AHEAD_ON CVAR_ENHANCEMENT("CamFloorAhead")
#define CAM_FLOOR_AHEAD_ON_DEFAULT 1
// The #152 fix. When Camera_KeepEyeUnderCeiling's segment hits a wall first, it looks for a ceiling
// straight above the wall hit, this many units back toward `at` (the room's side of the wall), and
// clamps under one it finds. 0 = the #103 rule exactly (any wall first means no clamp).
//
// 2, measured at the #152 spot in Lumbridge Castle, standing still: clamp engage/release flips
// went from 12 in 945 ticks at 0 to 1 in 3,842 at 2. It only has to put the probe clear of the
// wall's own face. Much larger and it samples a ceiling nearer Link than the one the eye is under.
// The alternative tried first - looking PAST the wall for a ceiling within 4 units - only slowed
// the bounce to a 7-tick sawtooth, because on some ticks the raised eye is outside the building
// and there is no ceiling past the wall to find. Full run:
// docs/test-runs/2026-09-30-issue-152-camera-bounce/ in sturdy-bassoon.
#define CVAR_CAM_CEIL_CORNER_BACK CVAR_ENHANCEMENT("CamCeilCornerBack")
#define CAM_CEIL_CORNER_BACK_DEFAULT 2.0f
#define CAM_CEIL_CORNER_BACK_MAX 40.0f
// Hysteresis on the same clamp (#152): on the frame after it found a ceiling, its test segment
// reaches this many units higher, so an eye it has just lowered stays under that ceiling rather
// than dropping out of the test and being let go. 0 = no hysteresis (the #103 rule).
//
// 12, measured at the level-2 doorway in Lumbridge Castle (Link just inside, the eye out on the
// balcony): 14 clamp engage/release flips per scripted try at 0, 0 at 12, with the #152 wall spot
// at 0 either way. One clearance's worth: the lowered eye sits 12 under the ceiling, so reaching 12
// more tests the segment as though the eye were still up at the ceiling. Too large and the clamp holds
// the eye low a little after Link walks out from under a ceiling; it still expires on the first
// frame the longer test misses.
#define CVAR_CAM_CEIL_HOLD CVAR_ENHANCEMENT("CamCeilHold")
#define CAM_CEIL_HOLD_DEFAULT 12.0f
#define CAM_CEIL_HOLD_MAX 80.0f
// The #38 clamp on the slope probe's origin in func_80044ADC.
#define CVAR_CAM_PROBE_CEIL_ON CVAR_ENHANCEMENT("CamProbeCeil")
#define CAM_PROBE_CEIL_ON_DEFAULT 1

/*
 * The ledge look-down (sturdy-bassoon#155). The slope probe in func_80044ADC reads a drop ahead as
 * a downhill slope: atan2(0.8 * dNear, 1.0 h) + atan2(0.2 * dFar, 2.5 h) for player height h, and
 * Camera_CalcDefaultPitch applies a falling slope undamped. So the look-down grows with the height
 * of the drop. Measured on Lumbridge Castle's level-2 bridge (both probes over a 164 drop): slope
 * -73.6 degrees, camera pitch 57.9 at the parapet against a resting ~10; the level-1 balcony (84):
 * -50.3 and 54.9. Grid-tool scenes only, and Camera_Normal1 only in effect: Normal3 and Parallel1
 * call the probe with arg2 = 1, which reads Link's own ground for both probes, so their slope is
 * always 0 and nothing here can reach them.
 */
// Largest drop the probe may report, in OoT units below the feet. 0 = no cap. Vanilla behaviour is
// this at 0 with CamLedgeRail off as well.
//
// 20, picked by the owner's feel walk from a live sweep. A capped drop gives the same lean at any
// height, so the bridge and the balcony read alike and so will #178's tall and grand storeys:
// adult camera pitch at the bridge parapet / at the balcony edge, against ~10 resting -
//   cap 10: 15.7 / 19.4    cap 20: 22.1 / 24.3    cap 30: 27.9 / 30.5    cap 40: 33.2 / 36.1.
// It also shrinks the lean when only the far probe sees the drop (looking across the bridge from
// its centre) from 18.9 to 10.1, so a separate "near probe only" rule was not needed.
// The alternatives were measured and deleted: scaling the drop's pitch (0.5: 40.0 / 34.0) still
// grows with the drop, and vanilla's cos(x)*x rise damping applied to drops peaks near 49 degrees
// and then falls, so the bridge leaned less than the balcony (33.5 / 40.8) and a 264 drop would
// lean under 1 degree. Young Link's probes are shorter (44 / 110), so the same cap leans him
// harder: about 22 degrees of slope at cap 20, against 14.6 for adult.
// docs/test-runs/2026-10-04-issue-155-ledge-look-down/ in sturdy-bassoon.
#define CVAR_CAM_LEDGE_DROP_CAP CVAR_ENHANCEMENT("CamLedgeDropCap")
#define CAM_LEDGE_DROP_CAP_DEFAULT 20.0f
#define CAM_LEDGE_DROP_CAP_MAX 400.0f
// 1 = when the far probe reads a drop below Link's ground, ignore a rise the near probe reads: a
// low top with a drop behind it is a parapet or a rail, not ground he could walk up onto. 0 = the
// rise counts (vanilla, and #103's "feet to head height is left alone").
//
// Found by the feel walk on the cap: looking at a bridge parapet at an angle, the near probe lands
// on its top (+26) and the far probe past it, over the 164 drop. The rise pitches the camera low
// behind Link, looking up at him. Vanilla's undamped drop half cancelled it (slope +6); the cap
// takes that counterweight away (+15.7; the eye 7.8 below `at` scripted, 9.7 in the owner's trace).
// Only a rise with a drop behind it is dropped, so a real step or a ramp, and a prop with floor
// beyond it (the uphill case in #155's comments), keep the vanilla reaction. "Rise" and "drop" mean
// more than 1 unit off Link's ground, the tolerance Camera_FloorAheadIfReachable calls level.
#define CVAR_CAM_LEDGE_RAIL CVAR_ENHANCEMENT("CamLedgeRail")
#define CAM_LEDGE_RAIL_DEFAULT 1

/*
 * Young Link's framing on adult Link (sturdy-bassoon#136). The camera reads a camera-only player
 * height, lerp(68, 44, t) for adult Link on grid-tool scenes, everywhere z_camera.c derived its
 * framing from Player_GetHeight. Player_GetHeight itself is untouched, and so is everything else
 * that reads it. 0 = vanilla adult, 1 = exactly Young Link's numbers. The horse's +32 is kept.
 *
 * The per-setting parameters are cached on a reload (RELOAD_PARAMS), so a change of t forces one
 * on the main camera; see Camera_ReloadOnHeightChange.
 */
#define CVAR_CAM_ADULT_HEIGHT_T CVAR_ENHANCEMENT("CamAdultHeightT")
#define CAM_ADULT_HEIGHT_T_DEFAULT 0.0f
#define CAM_ADULT_HEIGHT_T_MIN 0.0f
#define CAM_ADULT_HEIGHT_T_MAX 1.0f
#define CAM_HEIGHT_ADULT 68.0f
#define CAM_HEIGHT_CHILD 44.0f

/*
 * One Camera_Normal1 frame, for `agenttest trace` (sturdy-bassoon#152). Written by the camera and
 * read by nothing in it, like the `applied*` mirror below. `frame` says which frame it describes,
 * so a trace line taken while Normal1 was not running reads as stale rather than as current.
 */
typedef struct {
    s32 valid;       // 0 until Normal1 has run once this boot
    u32 frame;       // play frame Normal1 last ran
    s16 pitchIn;     // at-to-eyeNext pitch on entry (binang)
    s16 pitchOut;    // pitch Normal1 chose, after its own 79.65 / -85 degree clamp
    s16 slopeRaw;    // func_80044ADC's answer this frame (0 when the setting does not probe)
    s16 slopeAdj;    // anim->slopePitchAdj after easing - what the pitch target is offset by
    s16 branch;      // 0 idle re-centre, 1 obstructed swing, 2 follow
    s16 swingTimer;  // anim->startSwingTimer
    s16 swingActive; // anim->swing.unk_18
    s16 colCase;     // func_80046E20's collision case this frame, -1 when it did not run
    s16 idleBgHit;   // the idle path's Camera_BGCheck found geometry (-1 when that path did not run)
    s16 ceilState;   // Camera_KeepEyeUnderCeiling: -3 a wall first and no ceiling over it, -2 off,
                     // -1 nothing crossed, 0 crossed but eye already under the limit, 1 lowered,
                     // 2 lowered onto `at`
    f32 ceilOverWallY; // when a wall came first: the ceiling found over the wall hit (-1 none)
    f32 ceilHold;      // the hysteresis reach applied this frame (0 unless a ceiling was found last frame)
    f32 eyeYPre;     // eyeNext.y before the ceiling clamp
    f32 eyeYPost;    // and after
    f32 ceilY;       // the ceiling the clamp found, clamped or not (0 when none)
    f32 atY;         // at.y this frame
    f32 dropNear;    // slope probe floors, relative to the ground under Link, as the probe
    f32 dropFar;     // last read them (before the #155 knobs; they hold between odd frames)
    f32 height;      // the camera's player height this frame (#136)
} CameraFrameDiag;

void Camera_GetFrameDiag(CameraFrameDiag* out);

/*
 * A read of the whole mechanism at one moment, for `camindoor status`.
 *
 * Everything except the `applied*` pair is RECOMPUTED when the probe is called, so it answers
 * "what would this frame decide" even when the normal follow camera is not the one running.
 * The `applied*` pair is a diagnostic mirror of what `Camera_Normal1` last actually used; it is
 * written by the camera and read by nothing else, so it can never feed the behaviour back. Compare
 * `appliedFrame` with `frame` to see whether Normal1 ran at all (Z-target, cutscenes and the pause
 * menu all stop it).
 */
typedef struct {
    s32 sceneId;
    s32 gridToolScene; // 1 when the scene gate passes
    s32 enabled;       // the master CVar
    s32 hits;          // samples that found a ceiling
    s32 samples;       // 1 + ring count
    s32 needed;        // k, after clamping
    s32 indoors;       // hits >= needed
    f32 scale;         // what would be applied now: the scale CVar, or 1.0
    f32 checkHeight;
    f32 ringRadius;
    f32 ringBias;
    f32 easeIn;
    f32 dist;        // camera->dist right now
    f32 distMin;     // Normal1's unscaled distMin/distMax, as of appliedFrame
    f32 distMax;     //
    s32 appliedValid; // 0 until Camera_Normal1 has run once this boot: the `applied*` fields and
                      // distMin/distMax below are meaningless before that, and 0.0 is not a
                      // distinguishable value. Same trap `indoors` had, named rather than inferred
    f32 appliedScale;
    u32 appliedFrame;
    u32 frame;
    f32 yOffset;     // Normal1's resolved yOffset, as of appliedFrame (#136)
    f32 heightT;     // the #136 knob, after clamping
    f32 height;      // the camera's player height right now (#136)
} CameraIndoorProbe;

void Camera_IndoorPullInProbe(Camera* camera, CameraIndoorProbe* out);

#ifdef __cplusplus
}
#endif

#endif // SOH_CAMERA_INDOOR_TUNING_H
