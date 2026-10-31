#ifndef SOH_RS_BALLOON_H
#define SOH_RS_BALLOON_H

// Rs_Balloon (sturdy-bassoon#274): a crude balloon basket Link can climb into, lift off, fly and land. A PROTOTYPE,
// copied from the boat (Rs_Boat, RsBoat.h) where the two are the same and new where they are not: no envelope, no
// fuel, no landing sites, no wind.
//
// THE BASKET. A DynaPolyActor on DPM_UNK3 (position and yaw), the boat's moving platform: Link standing in it is
// carried and turned with it. Its collision is a box it builds at the first Init: a floor RS_BALLOON_FLOOR_Y over
// the actor's origin (the basket's bottom), four walls RS_BALLOON_RAIL_THICK thick whose flat tops are
// RS_BALLOON_TOP_Y over the bottom, and an underside. The walls are hands-climb walls (#179's wall type), so Link
// climbs onto a rail top from the ground outside (46 up) or from the floor inside (40 up) by pushing into it for
// 0.3 s, and walks or drops off the other side: that is the way in and the way out, on the ground or in the air.
// The camera passes through them. The actor's y is always a whole number: the engine truncates a moving dyna's
// collision to whole units (DynaPoly_ExpandSRT), so a basket climbing at a fractional rate would step under Link
// while the drawn floor moved smoothly. Its height is kept as a float (`alt`) and the actor stands on it rounded.
//
// THE CONTROLS. The boat's helm, through RsSteer.h: A within RS_BALLOON_CONTROLS_RANGE of the stand point (in front
// of the panel on the back wall) from a Link free to take them; B lets go, as do being hurt and being knocked off
// the stand point. At the controls the stick turns and drives (forward only; pulled back it brakes), C-up or
// Modifier 1 climbs, C-down or Modifier 2 sinks. C-up and C-down are free while the controls are held (Player sees
// no input), and on the owner's pad and keyboard they are the right stick and the arrow keys; Modifier 1 and 2 are
// what a GameCube X and Y are bound to, once bound (SoH's reset hotkey is Modifier 2 by default, so binding Y to it
// needs that moved first). A steering camera and the "Steering balloon" label (RsBoatHud.cpp) show it.
//
// FLYING. The height is its own state: a vertical velocity stepped toward what the buttons ask for (`lift`, the
// inertia), so it neither starts nor stops dead. With neither button held it eases to a hover, or sinks at `drift`
// if that is set. Each tick:
//   - the ground is the highest floor (or open water's surface: it floats, and can lift off again) under a 5 x 5
//     grid of points across its bottom, ~27 apart;
//   - sinking onto it lands it: it stops on the floor, and does not move or turn until it lifts off;
//   - climbing checks a line up from Link's head height over the same points, and stops under the first thing
//     overhead (a ceiling, a bridge's underside, a canopy), and at the tunable ceiling (`ceiling` over the spot it
//     was put down at);
//   - a move or turn checks a fence of lines round its edge and across it at four heights, from just over its bottom
//     to Link's head; a fence that meets something is slid along the wall it met once, then the turn alone and the
//     move alone are tried, and failing those it stops. Ground rising under it by less than RS_BALLOON_STEP is
//     skimmed (the basket rises onto it); more stops it.
//
// LEFT ALONE. Link out of the basket for RS_BALLOON_ABANDON_TICKS while it is in the air: by default it sinks at
// `abandonsink` until it lands, and waits there (`balloon abandon sink`). `hang` holds it where it is; `home` puts
// it back at the spot it was put down at, landed, once Link has been out of it that long anywhere, in the air or on
// the ground (RS never strands a balloon). Link knocked out of the controls lets go, as at the boat's helm.
//
// LINK'S SHADOW (#267). In the basket Link's shadow is the boats': cast onto the basket's own drawn faces, so Navi
// cannot turn it and it stops at the walls (RsVehicleShadow.h). Until the basket has a model, its faces are its
// placeholder boxes' (RsBalloon_Deck); once it is drawn from an archive, the deck is read from that archive, as the
// boats' are, and the shadow needs nothing else.
//
// Console: `balloon ...` and `agenttest balloon ...` (RsBalloonConsole.h). Markers: `rs_balloon event=<spawn|
// controls_on|controls_off|liftoff|landed|blocked|abandoned|home> ...` (agent mode).

#include "z64.h"
#include "RsSteer.h"

#ifdef __cplusplus
extern "C" {
#endif

// The basket, world units, from its bottom (the actor's origin) and its centre.
#define RS_BALLOON_HALF_WIDTH 56.0f  // outer faces; 112 across, 84 inside
#define RS_BALLOON_RAIL_THICK 14.0f  // the walls; Player looks for a ledge 10 past a wall's face, so 14 is found
#define RS_BALLOON_FLOOR_Y 6.0f      // the floor over the bottom
#define RS_BALLOON_TOP_Y 46.0f       // the rail tops over the bottom: 40 over the floor
#define RS_BALLOON_CLEAR_Y 86.0f     // the space it needs over its bottom: the floor plus adult Link and a hop
#define RS_BALLOON_STAND_Z -18.0f    // where Link stands to steer, local z (+z is ahead): in front of the panel
#define RS_BALLOON_CONTROLS_RANGE 45.0f // Link within this (xz) of the stand point can take the controls
#define RS_BALLOON_KNOCKED 25.0f     // at the controls, this far from the stand point has been moved off it
#define RS_BALLOON_STEP 12.0f        // ground rising under a moving basket by up to this is skimmed
#define RS_BALLOON_ABANDON_TICKS 40  // 2 s out of the basket before it is left alone

// The steering camera, as the boat's (RsBoat.h) with its own settings.
#define CVAR_RS_BALLOON_CAM CVAR_ENHANCEMENT("RsBalloonCam")
#define CVAR_RS_BALLOON_CAM_HEIGHT CVAR_ENHANCEMENT("RsBalloonCamHeight") // eye over the floor
#define CVAR_RS_BALLOON_CAM_DIST CVAR_ENHANCEMENT("RsBalloonCamDist")     // eye behind Link
#define CVAR_RS_BALLOON_CAM_EASE CVAR_ENHANCEMENT("RsBalloonCamEase")     // percent of the way a tick
#define RS_BALLOON_CAM_HEIGHT_DEFAULT 120
#define RS_BALLOON_CAM_DIST_DEFAULT 240
#define RS_BALLOON_CAM_EASE_DEFAULT 25
#define RS_BALLOON_CAM_HEIGHT_MIN -200
#define RS_BALLOON_CAM_HEIGHT_MAX 1000
#define RS_BALLOON_CAM_DIST_MIN 80
#define RS_BALLOON_CAM_DIST_MAX 1500
#define RS_BALLOON_CAM_EASE_MIN 5
#define RS_BALLOON_CAM_EASE_MAX 100
#define CVAR_RS_BALLOON_ABANDON CVAR_ENHANCEMENT("RsBalloonAbandon") // RsBalloonAbandon

typedef enum RsBalloonAbandon {
    RS_BALLOON_ABANDON_SINK, // sinks at `abandonsink` until it lands, then waits there (the default)
    RS_BALLOON_ABANDON_HANG, // stays where it is
    RS_BALLOON_ABANDON_HOME, // put back at its launch spot, landed
    RS_BALLOON_ABANDON_COUNT,
} RsBalloonAbandon;

// The handling, live: `balloon tune <knob> <value>`, saved, read every tick. Rates per 20 Hz tick (Link's run is ~6
// as adult), the turn in binang a tick at full stick.
typedef enum RsBalloonTuneKnob {
    RS_BALLOON_TUNE_CLIMB,   // top climb rate
    RS_BALLOON_TUNE_SINK,    // top sink rate
    RS_BALLOON_TUNE_LIFT,    // vertical speed gained or lost a tick toward what the buttons ask: the inertia
    RS_BALLOON_TUNE_DRIFT,   // sink rate with neither button held; 0 hovers
    RS_BALLOON_TUNE_FORWARD, // top speed ahead
    RS_BALLOON_TUNE_ACCEL,   // speed gained a tick toward the stick's target
    RS_BALLOON_TUNE_DRAG,    // speed lost a tick coasting: how long it drifts on once the stick is let go
    RS_BALLOON_TUNE_BRAKE,   // speed lost a tick with the stick pulled back
    RS_BALLOON_TUNE_TURN,    // binang a tick at full stick
    RS_BALLOON_TUNE_CEILING, // how far over its launch spot it may climb
    RS_BALLOON_TUNE_ABANDON_SINK, // sink rate once left alone in the air (`abandon sink`)
    RS_BALLOON_TUNE_COUNT,
} RsBalloonTuneKnob;

typedef struct RsBalloonTuneInfo {
    const char* name; // the console's word
    const char* cvar;
    f32 def;
    f32 min;
    f32 max;
} RsBalloonTuneInfo;

extern const RsBalloonTuneInfo gRsBalloonTune[RS_BALLOON_TUNE_COUNT];

// A knob's live value, clamped to its range.
f32 RsBalloon_Tune(s32 knob);

// What a balloon left alone does now (RsBalloonAbandon, `balloon abandon`), clamped to the modes there are.
s32 RsBalloon_AbandonMode(void);

typedef enum RsBalloonBlock {
    RS_BALLOON_BLOCK_NONE,
    RS_BALLOON_BLOCK_WALL,    // the fence round its edge met something
    RS_BALLOON_BLOCK_GROUND,  // the ground under it rose by more than RS_BALLOON_STEP
    RS_BALLOON_BLOCK_OVERHEAD, // something over it stopped a climb
    RS_BALLOON_BLOCK_CEILING, // the tunable ceiling stopped a climb
} RsBalloonBlock;

typedef struct RsBalloon {
    DynaPolyActor dyna;
    s32 controls;    // 1 while Link holds them
    s32 landed;      // resting on the ground (or the water)
    s32 onWater;     // what it rests on, or is over, is open water
    f32 alt;         // the basket's bottom, exactly; world.pos.y is this rounded
    f32 vy;          // units a tick, + up
    f32 speed;       // units a tick ahead, never below 0
    f32 tiller;      // -1..1, eased toward the stick
    s16 yawVel;      // binang a tick
    f32 groundY;     // the ground under it as of this tick
    f32 launchY;     // the ground under the spot it was put down at (home.pos): what `ceiling` counts from
    s32 blocked;     // RsBalloonBlock of the last refused move or climb, or NONE
    s32 blockCount;  // refused moves and climbs since spawn
    s32 markedBlock; // the reason the last `blocked` marker named; cleared once it moves freely again
    s32 freeTicks;   // moves in a row that fitted
    s32 press;       // A was down last tick: a held A is not a second press
    s32 linkAboard;  // Link stood on the basket (floor or rail) as of last tick
    s32 awayTicks;   // ticks since he last did
    s32 abandoned;   // left alone: the RsBalloonAbandon mode running, plus 1; 0 until Link has been out a while
    f32 throttle;    // what the controls asked for last tick, for the console: ahead 0..1
    f32 lift;        // climb (+1) or sink (-1), or 0
    RsSteerCam cam;
} RsBalloon;

// The drive override for the agent loop: while `ticks` > 0 the balloon at the controls reads these instead of the
// pad, then counts down.
typedef struct RsBalloonDrive {
    s32 ticks;
    f32 throttle; // 0..1 ahead; below 0 brakes
    f32 turn;     // -1..1, + is left
    f32 lift;     // -1..1: + climbs, - sinks, by that share of the top rate
} RsBalloonDrive;
extern RsBalloonDrive gRsBalloonDrive;

const char* RsBalloon_BlockName(s32 block);
const char* RsBalloon_AbandonName(s32 mode);

// Takes or lets go of the controls as A and B would, for the console. Returns 0 if Link is not in the basket.
s32 RsBalloon_SetControls(RsBalloon* balloon, PlayState* play, s32 on);

// Link is standing on this basket: its floor or a rail top.
s32 RsBalloon_LinkAboard(const RsBalloon* balloon, const Player* player);

// The stand point in world space, at floor height.
void RsBalloon_StandPos(const RsBalloon* balloon, Vec3f* out);

// Stands Link at the controls facing ahead, `lift` over the floor.
void RsBalloon_StandAtControls(const RsBalloon* balloon, Player* player, f32 lift);

// 1 when A would take some balloon's controls right now; Player reads it for the A label ("Check").
s32 RsBalloon_ControlsInReach(PlayState* play);

// 1 while Link holds some balloon's controls; Player blanks the A label, and the steering label reads it.
s32 RsBalloon_LinkAtControls(PlayState* play);

// 1 when `bgId` is a live balloon's basket; 1 when Link stood in some balloon's basket as of last tick. Link's shadow
// reads both (RsVehicleShadow.h): over a basket he gets the basket's.
s32 RsBalloon_IsBasketFloor(PlayState* play, s32 bgId);
s32 RsBalloon_LinkWasAboard(PlayState* play);

// The ground a balloon at (x, z) with its bottom at `alt` would stand on: the highest floor under the probe grid
// across its bottom, looking down from its clearance over `alt`, or open water's surface when that is higher.
// `*outWater` (may be NULL) says which. `self` is skipped, and may be NULL.
f32 RsBalloon_GroundAt(PlayState* play, Actor* self, f32 x, f32 z, s16 yaw, f32 alt, s32* outWater);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_BALLOON_H
