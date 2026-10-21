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
#define LIGHTS_SHADOW_RECORD_TICKS 120
#define LIGHTS_SHADOW_NODE_DIR (-2) // a slot bound to a directional light (the sun)
typedef struct LightsShadowTick {
    u32 frame;       // state.frames of the tick
    s32 numLights;   // slots bound
    s16 node[7];     // each slot's node: its index in the pool, or LIGHTS_SHADOW_NODE_DIR
    u8 alpha[2][7];  // [foot][slot]: the alpha that slot's shadow drew at; 0 when it drew none
    s32 lightNum[7]; // foot 0's strength term per slot, before the 0.00005 scale and the clamp at 1
} LightsShadowTick;

void Lights_GetStats(LightsStats* out);
// From ActorShadow_DrawFeet, for Link only: the shadow `slot` drew under `foot` at `alpha`.
void Lights_RecordLinkShadow(s32 foot, s32 slot, s32 lightNum, u8 alpha);
// Oldest first, at most `max`; returns the count. `Lights_ClearShadowRecord` starts a trial afresh.
s32 Lights_GetShadowRecord(LightsShadowTick* out, s32 max);
void Lights_ClearShadowRecord(void);
// Clamps to 1..LIGHTS_BUFFER_SIZE and returns the cap applied. Holders over the cap keep their nodes;
// only new inserts are refused until the count drops below it.
s32 Lights_SetPoolCap(s32 cap);

#ifdef __cplusplus
}
#endif

#endif // SOH_LIGHTS_BUDGET_H
