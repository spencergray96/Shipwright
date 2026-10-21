#ifndef SOH_LIGHTS_BUDGET_H
#define SOH_LIGHTS_BUDGET_H

#include "z64.h"
#include "soh/cvar_prefixes.h"

// The scene's point-light limits at map scale (sturdy-bassoon#233). The code is in
// `soh/src/code/z_lights.c`; the console surface is LightsConsole.{h,cpp} beside this file.
//
// Two limits bind once a map carries many lit Rs_Fire torches (measured in #218 E3b):
//   - the pool: every light in a scene takes one node from LIGHTS_BUFFER_SIZE (vanilla 32, 5 taken
//     from the start). A node is freed only when its holder is destroyed, so a full pool stays full;
//   - the bind: an actor binds at most 7 lights a draw, in list order (newest first). The scene's
//     directional lights were inserted first of all, so six fires near Link drop one and seven drop
//     both: he goes flat and loses his sun shadow.
//
// What this changes:
//   - the pool is LIGHTS_BUFFER_SIZE 256, everywhere. Lights_SetPoolCap lowers the cap for the
//     running session only (the planted 32 control); it is not a setting and resets at launch;
//   - "sun first": when an actor has more lights in range than its 7 slots, the scene's directional
//     lights keep their slots and the point lights that fit are the NEAREST ones, not the newest.
//     The slot layout stays vanilla's (point lights first, directional lights last), because
//     ActorShadow_DrawFeet reads the last two slots as the sun. When everything fits, the bind is
//     vanilla's loop, untouched. Grid-tool scenes only, behind CVAR_LIGHTS_SUN_FIRST.
#define CVAR_LIGHTS_SUN_FIRST CVAR_ENHANCEMENT("LightsSunFirst")
#define LIGHTS_SUN_FIRST_DEFAULT 1

#define LIGHTS_BUFFER_SIZE 256
#define LIGHTS_POOL_CAP_VANILLA 32

// Smoothed feet shadows (sturdy-bassoon#250). A fire's light re-rolls its brightness every tick (En_Light's
// flicker, which Rs_Fire copies; vanilla's torches do the same), and ActorShadow_DrawFeet draws a shadow per
// bound point light at that brightness, so each shadow jumped by about a quarter of its strength every tick.
// The sun shadow is cut by 8x the point lights' summed strength, so near the cut-off the flicker blinked it
// on and off; and when the nearest five changed, a shadow popped in or out of the bound set.
//
// With the setting on, in grid-tool scenes, the feet shadow reads a SMOOTHED strength per actor and per
// light instead (Lights_ShadowCasts in z_lights.c):
//   - the light's flicker is divided out: its strength is read at its colour averaged over seconds, not
//     this tick's roll, so only distance and the slot changes move it;
//   - each tick, each bound light's strength moves a fraction `rate` of the way to that;
//   - a light that leaves the bound set fades out at the same rate on its last direction, and one that
//     joins fades in from 0;
//   - the sun's cut-off reads the smoothed sum, and its result is smoothed too, so the sun shadow fades
//     instead of blinking (the x8 makes a walk's steady change swing it several times as fast);
//   - a fresh start SNAPS instead of fading: a new or re-used table slot, a tick missed, a new scene, or
//     the actor moving more than LIGHTS_SHADOW_SNAP_DIST in one tick (a goto, a warp, a void-out).
// The flame and the actor's own lighting still flicker: only the shadow reads the smoothed value. The
// shadows' shape and direction are vanilla's. Vanilla scenes are untouched.
#define CVAR_LIGHTS_SHADOW_SMOOTH CVAR_ENHANCEMENT("LightsShadowSmooth")
#define LIGHTS_SHADOW_SMOOTH_DEFAULT 1
// The fraction a tick. 0.12 settles a change to 63% in 8 ticks (0.4 s) and 90% in 18 (0.9 s): how fast a
// shadow follows the actor walking toward or away from a fire, and how fast a light joining or leaving the
// bound set fades. The owner's pick at the look, over 0.06. `lights shadowrate` changes it for one session.
#define LIGHTS_SHADOW_RATE_DEFAULT 0.12f
// The pulse (the owner's ask at #250's look): on top of the smoothed strength, each light's shadow breathes on a
// slow sine, from its full strength to `amount` lighter and back over `seconds`, after the draw's clamp so a
// shadow at full dark breathes too. 25% over 2 s is the owner's pick at the look. Each light keeps one phase
// for as long as it holds its pool node, so several fires breathe out of step and a light re-bound into
// another slot, or snapped in after a warp, carries on where it was. The sun's cut-off reads the strength before the
// pulse, so the sun shadow stays steady. `lights shadowpulse off` is the plain fade.
#define CVAR_LIGHTS_SHADOW_PULSE CVAR_ENHANCEMENT("LightsShadowPulse")
#define LIGHTS_SHADOW_PULSE_DEFAULT 1
#define LIGHTS_SHADOW_PULSE_AMOUNT_DEFAULT 0.25f
#define LIGHTS_SHADOW_PULSE_SECONDS_DEFAULT 2.0f
#define LIGHTS_SHADOW_ACTORS 16    // actors smoothed at once; past it, an actor draws unsmoothed (counted)
// Lights per actor: up to 5 bound point lights and the rest fading. With 8, walking a path between 14
// fires cut 8 fades short; with 12, none. When one must be cut, it is the weakest fading light (counted).
#define LIGHTS_SHADOW_LIGHTS 12
#define LIGHTS_SHADOW_SNAP_DIST 100.0f

#ifdef __cplusplus
extern "C" {
#endif

// What `lights status` reports. The per-tick figures are the last COMPLETE tick's (SoH records one
// display list per 20 Hz tick and replays it for the interpolated frames, so a tick is the unit).
typedef struct LightsStats {
    s32 occupied;     // nodes held now (vanilla's 5 included)
    s32 cap;          // the pool cap in force (LIGHTS_BUFFER_SIZE unless lowered for a test)
    s32 peak;         // most nodes held at once since the scene began
    s32 refused;      // inserts refused because the pool was at its cap, since the scene began
    s32 sunFirstGate; // 1 when this tick's binds ran sun first (the CVar on and a grid-tool scene)
    u32 tick;         // state.frames of the tick the figures below describe
    s32 binds;        // Lights_BindAll calls (each drawn actor, the room draw, soft sprites)
    s32 walked;       // list nodes visited: in a grid-tool scene each bind walks its list twice (count, bind)
    s32 overflowed;   // binds with more lights in range than 7 slots (grid-tool scenes; 0 in vanilla ones)
    s32 maxInRange;   // most point lights in range of one bind (likewise)
    // Link's own bind in that tick (-1 when he did not draw, and always in a vanilla scene)
    s32 linkInRange; // point lights within their radius of him
    s32 linkPoints;  // point lights bound
    s32 linkDirs;    // directional lights bound
} LightsStats;

// Link's feet shadows, one record a tick, for `lights shadow` (sturdy-bassoon#250). Lights_BindAll opens
// the tick's record when it binds Link (which node each slot took); ActorShadow_DrawFeet fills in the
// alpha each slot's foot shadow drew at. SoH records one display list per tick and replays it for the
// interpolated frames, and frame interpolation moves matrices only, so a tick's alpha IS every frame's.
// Smoothed, the record lists the casts (the smoothed lights, fading ones included, then the two sun slots)
// rather than the bound slots.
#define LIGHTS_SHADOW_RECORD_TICKS 120
#define LIGHTS_SHADOW_CASTS_MAX (LIGHTS_SHADOW_LIGHTS + 2)
#define LIGHTS_SHADOW_NODE_DIR (-2) // a slot bound to a directional light (the sun)
typedef struct LightsShadowTick {
    u32 frame;                                // state.frames of the tick
    s32 numLights;                            // slots bound, or casts when smoothed
    s32 smoothed;                             // 1 when the shadow read the smoothed strengths
    s16 node[LIGHTS_SHADOW_CASTS_MAX];        // each slot's node: its pool index, or LIGHTS_SHADOW_NODE_DIR
    u8 alpha[2][LIGHTS_SHADOW_CASTS_MAX];     // [foot][slot]: the alpha its shadow drew at; 0 when none
    s32 lightNum[LIGHTS_SHADOW_CASTS_MAX];    // foot 0's strength term, before the 0.00005 scale and clamp
} LightsShadowTick;

// One shadow for ActorShadow_DrawFeet to draw: the light's direction (in `light`) and its strength.
typedef struct LightsShadowCast {
    Light light;
    s32 lightNum;
} LightsShadowCast;

// `lights shadowsmooth`'s counters, since the scene began.
typedef struct LightsShadowStats {
    s32 gate;    // 1 when this tick's feet shadows are smoothed (the setting AND a grid-tool scene)
    f32 rate;    // the fraction a tick in force
    s32 pulseGate;     // 1 when this tick's smoothed shadows pulse (the smoothing's gate AND the pulse setting)
    f32 pulseAmount;   // the pulse's depth, a fraction of the strength
    f32 pulseSeconds;  // the pulse's period
    s32 actors;  // table slots held by an actor drawn this tick or the last
    s32 snaps;   // fresh starts: snapped rather than faded
    s32 full;    // draws left unsmoothed because the table was full
    s32 nobind;  // draws left unsmoothed because the bind's lights could not be identified
    s32 cut;     // fading lights dropped early to make room for a joining one
} LightsShadowStats;

void Lights_GetStats(LightsStats* out);
// From ActorShadow_DrawFeet, for Link only: the shadow `slot` drew under `foot` at `alpha`.
void Lights_RecordLinkShadow(s32 foot, s32 slot, s32 lightNum, u8 alpha);
// Oldest first, at most `max`; returns the count. `Lights_ClearShadowRecord` starts a trial afresh.
s32 Lights_GetShadowRecord(LightsShadowTick* out, s32 max);
void Lights_ClearShadowRecord(void);
// The smoothed shadows for `actor` drawn with `lights` (its bind this tick), into `casts`
// (LIGHTS_SHADOW_CASTS_MAX). Returns the count, or -1 to draw vanilla's unsmoothed shadows: the setting
// off, a vanilla scene, a full table (counted as `full`), or a bind it cannot identify (counted as `nobind`).
s32 Lights_ShadowCasts(Actor* actor, Lights* lights, LightsShadowCast* casts);
void Lights_GetShadowStats(LightsShadowStats* out);
// For this session only; clamped to 0.01..1 (1 is unsmoothed). Returns the rate applied.
f32 Lights_SetShadowRate(f32 rate);
// For this session only; the amount clamped to 0..0.9, the period to 0.5..30 s.
void Lights_SetShadowPulse(f32 amount, f32 seconds);
// A light whose colour flickers round a known mean says so once it holds its node: the shadows start their
// brightness average there, so a shadow that snaps in after a warp is right from its first tick instead of
// settling from one random roll. Kept for that node and info only; a node re-taken by another light ignores it.
void Lights_SetShadowMeanColor(LightNode* node, LightInfo* info, f32 colorSum);
// Clamps to 1..LIGHTS_BUFFER_SIZE and returns the cap applied. Holders over the cap keep their nodes;
// only new inserts are refused until the count drops below it.
s32 Lights_SetPoolCap(s32 cap);

#ifdef __cplusplus
}
#endif

#endif // SOH_LIGHTS_BUDGET_H
