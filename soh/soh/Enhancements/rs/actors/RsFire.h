#ifndef SOH_RS_FIRE_ACTOR_H
#define SOH_RS_FIRE_ACTOR_H

// Rs_Fire (sturdy-bassoon#232): OoT's torch fire on an RS torch, sized to fit, and lit like a
// vanilla torch. The RS torch is the stand - a baked archive prop, its flame faces removed in the
// kind editor - and this actor is only the fire on top of it.
//
// Built from two vanilla actors, without changing either:
//   - En_Light's DRAW (gEffFire1DL, two scrolling layers, billboarded), at params 4's colours.
//     En_Light itself cannot be used: its size is a fixed table picked by params, and it has no
//     collider, so nothing can light from it.
//   - Obj_Syokudai's INTERACTION, without its stand: a Deku stick lights from a lit fire and lights
//     an unlit one; a normal arrow through a lit fire becomes a fire arrow; a fire arrow or Din's
//     lights an unlit one.
//   - ONE ADDITION (sturdy-bassoon#235): an arrow that went through a lit fire EARLIER ON THE SAME TICK'S
//     STRETCH lights the unlit ones after it too. Obj_Syokudai misses that from close in: an arrow flies
//     150 units a tick, burns only from the lit fire's next update, and tells only the nearest fire of
//     its stretch (RsFire.c, RsFire_WalkStretch; the event `path_lit`).
//
// The scene's ActorEntry row carries (RsActorParams.h for params):
//   params  bits 0-11 the world flag set when it is lit, bit 12 it has one, bit 13 starts unlit,
//           bit 14 takes no light node. The flag must be a PLACEMENT flag (WorldFlagIds.h): the grid
//           tool's export hands one out per placement. Any other number is reported and ignored
//   rot.x   its size, as a percentage of En_Light params 4's fire (scale 0.004). 0 reads as 100.
//           Clamped to RS_FIRE_SCALE_MIN..RS_FIRE_SCALE_MAX
//   rot.y   unused: the fire turns to the camera every frame
//   rot.z   its HIT size, in the same percent: how far the flame reaches for a stick, an arrow, a fire
//           arrow or Din's. 0 reads as the visual size. Apart from rot.x because a small flame
//           is awkward to hit (the owner, 2026-10-13: the torch's 50% fire wants a bigger reach), and
//           no one floor suits every flame. Same clamp
// The row's position is the fire's base: the top of the torch's bowl.
//
// THE LIGHT (#233 is the slice that changes this): a lit fire with a light takes one node of the
// scene's 32 at the moment it lights - at Init, or when a stick or arrow lights it - and gives it
// back only when destroyed, so a puzzle torch costs nothing until it is lit. Unlit-light fires
// (params bit 14) never take one. When the pool is full, LightContext_InsertLight returns NULL and the fire
// burns with no light; nothing is taken from another holder.
//
// THE GLOW (sturdy-bassoon#243): the light's halo is on only while RsFire_GlowOn() says so - the
// CVar below if set, else RS_FIRE_GLOW_DEFAULT. Off, the light is LIGHT_POINT_NOGLOW: the same node,
// position, colour and radius, so it lights Link exactly as before (z_lights.c binds the two types
// with the same function), and only Lights_DrawGlow's halo goes. Read every tick the fire updates;
// `rsfire glow` also writes it into every fire at once, because a culled fire keeps its node (and
// its halo) without updating.
//
// THE COLLIDER AND THE DRAW DISTANCE: one AC cylinder over the flame, registered on every tick the
// fire updates. Flags 0, as En_Light: a fire culled from view neither updates nor draws, so it holds
// no slot, and an arrow can light any fire Link can see (the owner, 2026-10-13, option B of #232; a
// 1,000-unit gate on Link was dropped). The cull edge is uncullZoneForward + uncullZoneScale in front
// of the camera. #243 raised the forward part from Actor_Init's 1,000 to RS_FIRE_DRAW_DISTANCE, 6,000,
// for every fire (the owner: a glow with no flame until close in read badly in a stitched world; they
// picked 6,000 in game over vanilla Obj_Syokudai's 4,000). The scale stays Actor_Init's 350, so flames now
// cull about 6,350 out, not 1,350. The arrow range grows with them only as far as an arrow flies (about
// 2,200: En_Arrow lives 12 ticks), so past that the distance is draw cost alone. SoH's "Disable Draw
// Distance" multiplier (Ship_CalcShouldDrawAndUpdate) scales this edge too when it is above 1.
// NOT UPDATE_CULLING_DISABLED, which Obj_Syokudai also sets: that
// would register every fire in the scene in the 192-slot AC list every tick, seen or not (ENGINE_BUDGETS
// "Collision check subscriptions"); a larger uncull zone pays only for the fires in view. The CVar
// below overrides the distance for tuning; `rsfire distance` writes it into every live fire, because a
// culled fire never updates to read it.

// The shipped defaults (#243). The CVars only override them; unset, these are what every fire uses.
#define RS_FIRE_DRAW_DISTANCE 6000 // uncullZoneForward, the owner's pick. Actor_Init's is 1,000
                                   // (Obj_Syokudai's 4,000)
#define RS_FIRE_DRAW_DISTANCE_MIN 100
#define RS_FIRE_DRAW_DISTANCE_MAX 30000
#define RS_FIRE_GLOW_DEFAULT 0 // 1 draws the halo
// The overrides. CVAR_ENHANCEMENT is soh/cvar_prefixes.h's; it expands where these are used.
#define CVAR_RS_FIRE_DRAW_DISTANCE CVAR_ENHANCEMENT("RsFireDrawDistance")
#define CVAR_RS_FIRE_GLOW CVAR_ENHANCEMENT("RsFireGlow")

#include "z64actor.h"
#include "z64light.h"

// Size, in percent of En_Light params 4 (scale 0.004).
#define RS_FIRE_SCALE_DEFAULT 100
#define RS_FIRE_SCALE_MIN 10
#define RS_FIRE_SCALE_MAX 400

typedef struct RsFire {
    /* 0x0000 */ Actor actor;
    /* */ ColliderCylinder collider;
    /* */ LightNode* lightNode; // NULL: no light (none wanted, not lit yet, or the pool was full)
    /* */ LightInfo lightInfo;
    /* */ float size;           // rot.x as a factor: 1.0 is params 4's fire
    /* */ float hitSize;        // rot.z as a factor, the same scale: the reach of the flame
    /* */ int16_t timer;        // the flame's texture scroll
    /* */ int16_t growTicks;    // ticks since it lit, for the flame growing in (0..RS_FIRE_GROW_TICKS)
    /* */ uint8_t lit;
    /* */ uint8_t lightWanted;  // params bit 14 clear
    /* */ int16_t flag;         // the world flag it sets when lit; -1 when it has none
    /* */ int32_t litBy;        // what lit it last: RsFireLitBy
} RsFire;

typedef enum RsFireLitBy {
    RS_FIRE_LIT_BY_NONE,  // lit at Init, or never lit
    RS_FIRE_LIT_BY_FLAG,  // its world flag was already set when it spawned
    RS_FIRE_LIT_BY_STICK, // a burning Deku stick
    RS_FIRE_LIT_BY_FIRE,  // a fire arrow, Din's Fire, or an arrow lit by another fire
} RsFireLitBy;

#ifdef __cplusplus
extern "C" {
#endif

// The console's view (RsFireConsole): what a marker cannot otherwise see.
const char* RsFire_LitByName(int32_t litBy);
// The settings in force: the CVar if set, else the default above. The distance is clamped to
// RS_FIRE_DRAW_DISTANCE_MIN..MAX.
int32_t RsFire_DrawDistance(void);
int32_t RsFire_GlowOn(void);
// Writes both settings into one live fire, as its next update would.
void RsFire_ApplySettings(RsFire* fire);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_FIRE_ACTOR_H
