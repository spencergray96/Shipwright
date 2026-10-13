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
// THE COLLIDER: one AC cylinder over the flame, registered only while Link is within
// RS_FIRE_AC_RANGE. Obj_Syokudai registers two, from anywhere in the scene, every frame - the
// combination ENGINE_BUDGETS "Collision check subscriptions" says never to copy into a one-scene
// world. Not UPDATE_CULLING_DISABLED either: a culled fire neither updates nor draws, as En_Light.

#include "z64actor.h"
#include "z64light.h"

// Size, in percent of En_Light params 4 (scale 0.004).
#define RS_FIRE_SCALE_DEFAULT 100
#define RS_FIRE_SCALE_MIN 10
#define RS_FIRE_SCALE_MAX 400
// The flame collider is registered only while Link is this near, in units.
#define RS_FIRE_AC_RANGE 1000.0f

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

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_FIRE_ACTOR_H
