#ifndef SOH_RS_BOAT_H
#define SOH_RS_BOAT_H

// Rs_Boat (sturdy-bassoon#256): a crude boat Link can board, steer across a waterbox and get off again.
// A PROTOTYPE - the issue's shape A, "a moving platform you stand on": a DynaPolyActor like Bg_Haka_Ship,
// whose speed and yaw come from the stick instead of a rail.
//
// THE KINDS (#262). The actor's params pick the hull: the Sailing skiff or the sloop (RsBoatKind), each with its
// own RS model and its own outline - a row of stations along the hull, measured off the kind's export: the
// floor's half-width, the gunwale's and the rail's height at each. The collision and the shore probes are both
// built from that outline, so they follow the hull's real shape, the bow's taper included.
//
// THE PLATFORM. A collision header built from the outline at the first Init of each kind: a flat deck, inner
// rail walls, a rail top Link can stand on and outer sides reaching RS_BOAT_SIDE_FOOT_Y under the water.
// DPM_UNK3 (position and yaw) is what carries Link along and turns him with the hull, the engine's own
// moving-platform path. The hull's origin is the waterline: the deck is the kind's deckY above it (RS_BOAT_DECK_Y
// for the skiff and the sloop).
//
// THE HELM. While Link stands on deck the stick moves Link, so steering is a toggle: A within
// RS_BOAT_HELM_RANGE of the tiller (the post at the stern) takes the helm. PLAYER_STATE1_INPUT_DISABLED
// then freezes Link's own input (RemoteBombchu's trick) and the boat reads the pad instead: the stick pushed
// forward drives, by how far it is pushed (#262), pulled back reverses, and its left-right turns, so a diagonal
// does both; B lets go. A does nothing at the helm, and its label is blank. The boat is ACTORCAT_BG, which
// updates before ACTORCAT_PLAYER, so the frame A takes the helm Player already sees no input and does not roll.
// A takes the helm only from a Link who is free to (RsBoat_LinkCanTakeHelm: not aiming, in first person,
// holding or carrying anything, airborne, talking or in a cutscene), and the helm lets go of itself when Link
// is hurt, dies or is knocked away from the tiller. Speeds and turn rates are live CVars (`boat tune`).
//
// THE CAMERA. At the helm a sub-camera follows behind the hull at a locked height over the deck, easing from
// wherever the normal camera was rather than snapping to it, and hands its place back to the normal camera
// when the helm is let go. Height, distance and ease are CVars, so `boat cam height <n>` tries a height live.
//
// THE LABEL. "Not steering" / "Steering boat" under the C buttons (RsBoatHud.cpp), while there are no
// steering animations to show it.
//
// THE TILLER IS TARGETABLE, like a sign: its attention focus sits over the tiller post at head height, so
// Navi's arrow finds it and Z locks on to it, and while Link could take the helm the A button reads "Check"
// (RsBoat_TillerInReach, read by Player_UpdateInterface). Locked on, A takes the helm from anywhere on the
// deck. Attention is off while the helm is held.
// THE WATER. Every tick the hull's height is the waterbox surface under it plus a slow bob. The roll and
// pitch (a second, slower sine) tilt the collision with the drawn hull (#262; #256 drew them only): Link rides
// the rocking deck, his feet on it as it rises and falls under him, upright, and at 2.5 degrees he never
// slides. The rock is sized so a corner dips the same distance on
// any hull (RS_BOAT_ROLL_DIP, RS_BOAT_PITCH_DIP): a bigger hull rocks through a smaller angle, as a real one
// does, and its corners stay out of the water. A waterbox buried under the ground is not water
// (RsBoat_SurfaceAt); a boat with no water under it settles on the ground and does not move.
//
// THE SHORE (#262). A move is tried, then checked at the outline's points: the floor's edge must be over open
// water deeper than RS_BOAT_DRAFT, the gunwale's edge clear of ground rising into the side, no wall between the
// hull's centre and the gunwale (at the waterline and at Link's waist), and nothing lower than
// RS_BOAT_HEADROOM over the deck. A point in trouble does not refuse the move outright: the hull is pushed out,
// along the wall's normal or down the shore's slope, a few times over (SM64's and Link's own way with walls), so
// a hull that meets a bank at an angle slides along it and keeps the speed the bank leaves it. Only a move the
// push cannot clear is refused; then the turn alone and the move alone are tried, and either is taken if it
// fits or leaves the hull less deep in trouble than it was, so a hull already touching land can always drive off.
//
// Console: `boat ...` and `agenttest boat ...` (RsBoatConsole.h). Markers: `rs_boat event=<spawn|helm_on|
// helm_off|blocked> ...` (agent mode).

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// The hulls, in the actor's params. The outlines and models are RsBoat.c's sRsBoatKinds.
typedef enum RsBoatKindId {
    RS_BOAT_KIND_SKIFF, // loc 59503, 2 x 6
    RS_BOAT_KIND_SLOOP, // loc 59508, 3 x 8
    RS_BOAT_KIND_RAFT,  // loc 59496, 1 x 3
    RS_BOAT_KIND_COUNT,
} RsBoatKindId;

// Hull heights, world units, shared by the kinds: both models' floor is their origin, and both sit on it.
#define RS_BOAT_DECK_Y 4.0f   // the skiff's and the sloop's deck above the waterline: their floor, on the water
#define RS_BOAT_KEEL_Y -14.0f // the placeholder box's bottom below the waterline (drawn without the model)
// The collision sides reach further down than the drawn hull: a swimming adult Link floats ~45 below the surface
// and checks walls ~26 above that, so sides that stop at the keel let him swim straight under the boat instead
// of meeting its side (where he can climb out onto the rail).
#define RS_BOAT_SIDE_FOOT_Y -60.0f

#define RS_BOAT_DRAFT 10.0f           // a floor nearer the surface than this is the shore
#define RS_BOAT_HEADROOM 80.0f        // clear space wanted over the deck: adult Link is ~50 tall, plus a hop
#define RS_BOAT_HELM_RANGE 45.0f      // Link within this of the tiller can take the helm
#define RS_BOAT_HELM_FROM_STERN 22.0f // where Link stands to steer: this far in from the stern
#define RS_BOAT_HELM_KNOCKED 25.0f    // Link this far (xz) from the tiller at the helm has been moved off it

// The steering camera. CVAR_ENHANCEMENT is soh/cvar_prefixes.h's; it expands where these are used.
#define CVAR_RS_BOAT_CAM CVAR_ENHANCEMENT("RsBoatCam")              // 0: the normal camera stays at the helm
#define CVAR_RS_BOAT_CAM_HEIGHT CVAR_ENHANCEMENT("RsBoatCamHeight") // eye above the deck, world units
#define CVAR_RS_BOAT_CAM_DIST CVAR_ENHANCEMENT("RsBoatCamDist")     // eye behind Link, world units
#define CVAR_RS_BOAT_CAM_EASE CVAR_ENHANCEMENT("RsBoatCamEase")     // percent of the way to its target a tick
#define CVAR_RS_BOAT_HUD CVAR_ENHANCEMENT("RsBoatHud")              // 0: no steering label
#define CVAR_RS_BOAT_INTERIOR_CLIMB CVAR_ENHANCEMENT("RsBoatInteriorClimb") // 0: the rails hop, not climb hands-on
#define RS_BOAT_INTERIOR_CLIMB_DEFAULT 1 // the owner's pick (#262): onto a rail hands-on, as onto a prop
#define CVAR_RS_BOAT_KIND CVAR_ENHANCEMENT("RsBoatKind") // the kind Lake Hylia's boat and `boat spawn` put down
#define RS_BOAT_CAM_HEIGHT_DEFAULT 120 // the owner's pick: high enough to see obstacles in the water ahead
#define RS_BOAT_CAM_DIST_DEFAULT 240
#define RS_BOAT_CAM_EASE_DEFAULT 25 // ~90% of the way in 8 ticks (0.4 s): a fast catch-up, not a snap
#define RS_BOAT_CAM_HEIGHT_MIN -20
#define RS_BOAT_CAM_HEIGHT_MAX 600
#define RS_BOAT_CAM_DIST_MIN 80
#define RS_BOAT_CAM_DIST_MAX 1000
#define RS_BOAT_CAM_EASE_MIN 5
#define RS_BOAT_CAM_EASE_MAX 100

// The handling, live (#262 point 7): `boat tune <knob> <value>`, saved, read every tick. Speeds per 20 Hz tick
// (Link's run is ~6 as adult), turn rates in binang a tick at full tiller. Top speed and the turn rates are each
// kind's own (RsBoat_TopSpeed); the rest are one set for every kind (sturdy-bassoon#263).
typedef enum RsBoatTuneKnob {
    RS_BOAT_TUNE_FORWARD,     // top speed ahead for every kind, overriding each kind's own; 0: the kind's
    RS_BOAT_TUNE_REVERSE,     // top speed astern, as a share of the top speed in use
    RS_BOAT_TUNE_ACCEL,       // speed gained a tick, toward the throttle's target
    RS_BOAT_TUNE_DRAG,        // speed lost a tick, coasting toward a lower target
    RS_BOAT_TUNE_BRAKE,       // speed lost a tick while the stick opposes the way the boat moves
    RS_BOAT_TUNE_TURN_STILL,  // turn rate standing still, for every kind; 0: the kind's
    RS_BOAT_TUNE_TURN_MOVING, // added at the kind's default top speed, for every kind; 0: the kind's
    RS_BOAT_TUNE_STEPS,       // 0: the throttle follows the stick smoothly; n: it snaps to n steps
    RS_BOAT_TUNE_UPGRADE,     // 1: each kind's upgraded top speed, where it has one (a stand-in until #263's saves)
    RS_BOAT_TUNE_COUNT,
} RsBoatTuneKnob;

typedef struct RsBoatTuneInfo {
    const char* name; // the console's word
    const char* cvar;
    f32 def;
    f32 min;
    f32 max;
} RsBoatTuneInfo;

extern const RsBoatTuneInfo gRsBoatTune[RS_BOAT_TUNE_COUNT];

// A knob's live value, clamped to its range.
f32 RsBoat_Tune(s32 knob);

struct RsBoat;
// The top speed this boat uses now, units a tick: `fwd` when it is set, else its kind's upgraded speed while
// `upgrade` is on and the kind has one, else its kind's own.
f32 RsBoat_TopSpeed(const struct RsBoat* boat);

typedef enum RsBoatBlock {
    RS_BOAT_BLOCK_NONE,
    RS_BOAT_BLOCK_NO_WATER, // a hull point left the waterbox, or its surface is a different height
    RS_BOAT_BLOCK_SHALLOW,  // the floor's edge is over ground shallower than the draft, or ground rises into a side
    RS_BOAT_BLOCK_WALL,     // a line from the hull's centre to the gunwale crossed a wall, at the waterline or at
                            // Link's waist
    RS_BOAT_BLOCK_HEADROOM, // something over a hull point is lower than Link's head on the deck (a low bridge,
                            // an overhang): driving on would scrape him off
} RsBoatBlock;

typedef struct RsBoat {
    DynaPolyActor dyna;
    s32 kind;        // RsBoatKindId, from the params
    s32 helm;        // 1 while Link holds the helm
    f32 speed;       // signed, units per tick along the hull's heading
    f32 tiller;      // -1..1, eased toward the stick
    f32 rudder;      // -1..1, the drawn rudder's deflection: the tiller's going ahead, mirrored astern, eased
    s16 yawVel;      // binang per tick, from the tiller and the speed
    s16 roll;        // the rock's tilt, binang, about the hull's length; pitch about its width. In shape.rot
    s16 pitch;
    s32 timer;       // ticks since spawn: the bob's clock
    f32 surfaceY;    // the waterbox surface under the hull; the last good one when there is none
    s32 onWater;     // the hull's centre is over a waterbox
    s32 waterBox;    // the index of the box it is over, or -1: a hull point inside the same box is water
    s32 blocked;     // RsBoatBlock of the last refused move, or NONE
    s32 blockCount;  // refused moves since spawn, for the agent loop
    s32 touch;       // RsBoatBlock the last push-out cleared (a move that slid off the shore), or NONE
    s32 touchCount;  // moves that were pushed out since spawn
    s32 markedBlock; // the reason the last `blocked` marker named; cleared once the boat moves freely again,
                     // so a boat nudging the shore at a crawl reports it once rather than every few ticks
    s32 freeTicks;   // moves in a row that fitted: "freely again" is RS_BOAT_FREE_TICKS of them
    s32 helmPress;   // A was down last tick: a held A is not a second press
    s32 linkAboard;  // Link stood on this boat's deck or rail as of last tick (RsBoat_LinkWasAboard)
    s32 railPush;    // ticks Link has pushed into this hull's wall from its deck (RsBoat_RailHop)
    s32 railGrace;   // ticks without contact the push may still survive
    s32 railPushHands; // the wall pushed into climbs hands-on, no hop: a covered bow's, or a rail's (`boat climb`)
    f32 lift;        // how far the hull is raised this tick to keep its rocking floor out of the water
    f32 floorLow;    // the floor's lowest drawn point over the water this tick, lift included (`boat list`)
    f32 throttle;    // what the helm asked for on the last tick, -1..1, for the console
    s16 subCamId;    // the steering camera while the helm is held, else SUBCAM_NONE
    Vec3f camEye;    // where it is now, eased toward its target each tick
    Vec3f camAt;
} RsBoat;

// The drive override for the agent loop: when `ticks` > 0 the boat at the helm reads these instead of the
// pad, then counts down. Lets a run drive a known course with no stick injection. 0 ticks = the pad.
typedef struct RsBoatDrive {
    s32 ticks;
    f32 throttle; // -1..1
    f32 turn;     // -1..1, + turns left (counter-clockwise from above, the way stick-left does)
} RsBoatDrive;
extern RsBoatDrive gRsBoatDrive;

// A test waterbox (`boat water`, RsBoatConsole.h): a grid-tool map has no waterbox (#218 parked the attachment),
// so the console adds one to the loaded scene for the session, and every boat draws its surface as a
// translucent sheet so it can be seen. The console owns the collision side; this is only what the draw reads.
typedef struct RsBoatTestWater {
    s32 active;
    f32 x0, z0, x1, z1; // the box's xz rectangle, world units
    f32 y;              // its surface
} RsBoatTestWater;
extern RsBoatTestWater gRsBoatTestWater;

// The kind's console name ("skiff", "sloop"), or "?" out of range; and back, -1 for no such kind.
const char* RsBoat_KindName(s32 kind);
s32 RsBoat_KindByName(const char* name);

// The kind's collision triangles, for the console and the DynaPoly budget.
s32 RsBoat_KindPolyCount(s32 kind);

// Takes or lets go of the helm as A/B would, for the console. Returns 0 if Link is not on this boat's deck.
s32 RsBoat_SetHelm(RsBoat* boat, PlayState* play, s32 on);

// Link is standing on this boat's deck (or its rail).
s32 RsBoat_LinkOnDeck(const RsBoat* boat, const Player* player);

// 1 when A would take some boat's helm right now: Link on its deck, free to (RsBoat_LinkCanTakeHelm), and
// within reach of its tiller (RS_BOAT_HELM_RANGE), or anywhere on its deck while locked on to it. Player
// reads it for the A button's label.
s32 RsBoat_TillerInReach(PlayState* play);

// 1 while Link holds some boat's helm. Player reads it to blank the A button's label: A does nothing there.
s32 RsBoat_LinkAtHelm(PlayState* play);

// 1 when Link stood on some boat's deck or rail as of last tick. Player reads it as he leaves the ground: running
// off a boat always hops, however near the ground beside it is (vanilla hops only over a drop of more than 20).
s32 RsBoat_LinkWasAboard(PlayState* play);

// 1 when Link stood on a boat last tick and is on other ground now: he walked off the rail onto a bank level with
// it, never leaving the ground, so the hop above is never asked about. Player hops him there too.
s32 RsBoat_SteppedOffBoat(PlayState* play, const Player* player);

// 1 when `bgId` is a live boat's collision. Player reads it for the floor under Link as he leaves an edge: stepping
// off the rail onto the deck below is a drop, not a hop, which at a run could carry him over the far side.
s32 RsBoat_IsBoatFloor(PlayState* play, s32 bgId);

// After Player's update (OnPlayerUpdate): Link standing on a boat is put on the floor under him as the hull's own
// float transform has it (#262). The engine truncates a moving hull's collision to whole units each tick
// (DynaPoly_ExpandSRT), so as the hull bobs and rocks the floor under him steps a unit at a time while the drawn hull
// moves smoothly, and he visibly jolts. Player stands him on the truncated floor again next tick (a grounded actor
// follows its floor), so only what is drawn and what the camera follows changes, by under 2 units.
void RsBoat_SmoothLinkOnDeck(PlayState* play);

// The height of a floor at (x, z) exactly, when `poly` is a boat's (the hull's float transform, as above), else
// `truncated`, the engine's own. Player's leg IK asks it for each foot, so Link's feet meet the deck his smoothed body
// stands on rather than reaching for the truncated one (his legs wobbled with the bob).
f32 RsBoat_ExactFloorY(PlayState* play, CollisionPoly* poly, s32 bgId, f32 x, f32 z, f32 truncated);

// How far over Link's feet Player looks for walls: `vanilla` (26), or lower while he stands on a boat, where a wall
// under ~18 would otherwise be walked straight up. The boarding notch's inner wall is that low, and it must stop him
// on the deck rather than let him step out of the boat.
f32 RsBoat_WallCheckHeight(PlayState* play, const Player* player, f32 vanilla);

// The lowest top Player takes as a ledge to climb: `vanilla` (18), or lower for a hull's wall from its deck, so the
// notch is climbed hands-on like the rest of the rail rather than refused.
f32 RsBoat_LedgeMin(PlayState* play, const Player* player, f32 vanilla);

// The height of Player's forward line looking for a wall to climb: `vanilla` (18), or lower on a boat, where the
// boarding notch's inner wall is only 17 tall and the line would pass over it.
f32 RsBoat_InteractHeight(PlayState* play, const Player* player, f32 vanilla);

// How far past his wall radius that line reaches: `vanilla` (10), or further on a boat, so a run into the rail at an
// angle still finds it and climbs.
f32 RsBoat_InteractReach(PlayState* play, const Player* player, f32 vanilla);

// How far off straight at a wall Link may face for Player to look for a ledge on it: `vanilla` (0x3000), or wider
// for a swimmer against a hull, who turns along it as he glances off.
s32 RsBoat_InteractAngle(PlayState* play, const Player* player, s32 vanilla);

// How flat (a normal's y, s16) a ledge's top must be for Player's hands-on climb: `vanilla` (28000), or steeper for a
// hull's wall from its deck, so the notch's sloped ends climb rather than fall back to the hop.
s32 RsBoat_LedgeFlat(PlayState* play, const Player* player, s32 vanilla);

// How far a wall may lean (its normal's y, s16) and still be climbed: `vanilla` (600), or more for a hull's wall, which
// the engine's truncation of its moving collision tilts.
s32 RsBoat_WallLean(PlayState* play, const Player* player, s32 vanilla);

// `boat still`: every boat level at its float height, no bob and no rock (a test aid, not saved; #262).
extern s32 gRsBoatStill;

// The hookshot's line test (BgCheck_EntityLineTest1, every surface, as ArmsHook_Shoot asks), but meeting a boat's hull
// as projectiles do (#262): past the walls only Link meets (RS_BOAT_POLY_SHOTS_PASS), onto the drawn plank only they
// meet (RS_BOAT_POLY_SHOTS_ONLY), on a skinned stretch of rail.
s32 RsBoat_HookshotLineTest(PlayState* play, Vec3f* from, Vec3f* to, Vec3f* result, CollisionPoly** outPoly,
                             s32* outBgId);

// How much higher than vanilla a swimming Link reaches to climb out onto the ledge in front of him: some when the
// wall he swims against is a boat's hull, so both rails are climbable from the water anywhere; 0 otherwise.
f32 RsBoat_SwimReach(PlayState* play, const Player* player);

// The ticks Link must push into the wall in front of him before he hops or climbs onto its top: `vanilla` (3 or 6),
// or never against a boat's hull from its deck, where RsBoat_RailHop decides instead; against a wall the boat
// climbs hands-on (a covered bow, or the rails under `boat climb hands`), 0 once the boat has counted 0.3 s.
s32 RsBoat_ClimbDelay(PlayState* play, const Player* player, s32 vanilla);

// How far past the wall's face a ledge climb stands Link on the top: `vanilla` (his distance from the face plus 0.5),
// or half as far again onto a boat's rail under `boat climb hands`, so he lands on the rail top rather than at its
// inner edge (the owner's pick, #262). Every other climb, a covered bow's and a prop's included, is vanilla's.
f32 RsBoat_ClimbInset(PlayState* play, const Player* player, f32 vanilla);

// The height of the rail Link should hop onto now, or 0: he has pushed into a boat's hull from its deck for 0.6 s,
// so brushing past the rail does not put him over the side. Player hops him as onto a ledge that high.
f32 RsBoat_RailHop(PlayState* play, const Player* player);

// Link is free to take the helm: on nothing but his feet, empty-handed and doing nothing else. 0 while he aims
// or looks in first person, has a sword or item out, holds, carries or throws something, shields, hangs,
// jumps, falls, talks, is hurt or is in a cutscene - each of which A at the tiller used to override, locking
// him into it at the helm.
s32 RsBoat_LinkCanTakeHelm(const Player* player);

// Stands Link at the tiller facing the bow, as a teleport (prevPos, home.pos and fallStartHeight move with
// him). `lift` > 0 also sets his height to that far above the deck, for a Link who is not on it yet (in the
// water, or on the rail).
void RsBoat_StandAtHelm(const RsBoat* boat, Player* player, f32 lift);

// The water surface at (x, z): 1 and the height when open water is there, 0 when not. Of the waterboxes over
// the point, the highest one not buried under the ground there (drained Lake Hylia lowers its main box under
// the lakebed). `skip` is a boat whose collision does not count as ground over the water (NULL: every boat's
// does). `*outY` is read as well as written in Zora's Domain (WaterBox_GetSurface1), so pass a nearby height.
s32 RsBoat_SurfaceAt(PlayState* play, f32 x, f32 z, f32* outY, Actor* skip);

// The hull's tiller position in world space, at deck height.
void RsBoat_HelmPos(const RsBoat* boat, Vec3f* out);

// A kind's display list, in its own model space (RsBoatModel.cpp), by its archive path, or NULL when its archive
// is not in mods/ - the boat then draws its placeholder box.
Gfx* RsBoat_ModelList(const char* listPath);

const char* RsBoat_BlockName(s32 block);

// What the boat's outline meets where it is now, for `boat contact`: `out` gets "points=" and one
// ` <reason>:<depth>@<push x>,<push z>` per outline point in trouble (the push a unit vector x100, world axes).
// Returns the deepest one's RsBoatBlock.
s32 RsBoat_ContactReport(RsBoat* boat, PlayState* play, char* out, s32 size);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_BOAT_H
