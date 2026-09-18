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
 * into `camindoor` applies on the next frame with no rebuild, and a wrong one cannot stick. The
 * console subcommand exists because this build of SoH has no console `set`, so the agent test loop
 * has no other way to change a CVar (`docs/reference/AGENT_TEST_LOOP.md`).
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
} CameraIndoorProbe;

void Camera_IndoorPullInProbe(Camera* camera, CameraIndoorProbe* out);

#ifdef __cplusplus
}
#endif

#endif // SOH_CAMERA_INDOOR_TUNING_H
