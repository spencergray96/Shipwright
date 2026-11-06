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
// A takes the helm only from a Link who is free to (RsSteer_LinkCanTake: not aiming, in first person,
// holding or carrying anything, airborne, talking or in a cutscene), and the helm lets go of itself when Link
// is hurt, dies or is knocked away from the tiller. Speeds and turn rates are live CVars (`boat tune`).
// Every let-go makes the boat Link's drown refuge (#273, SwimStamina.h): a drowned Link lands mid-deck on it, wherever
// it has drifted, until he stands on anything else. Taking the helm, or only walking aboard, sets nothing.
// Taking and letting go, being knocked off, standing Link at the tiller and the steering camera are the
// shared vehicle pieces in RsSteer.h (#272); where the tiller is and how far is knocked off stay here.
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
// THE WAKE (#268). A moving boat leaves water rings behind it, the ones swimming Link leaves (EffectSsGRipple): off
// the stern going ahead, off the bow going astern, on one line down the middle or alternating between the quarters,
// and going ahead a bow wave as well, rings off alternate bow shoulders that the hull leaves behind as a widening V.
// They are spaced by the distance the hull travels, as Link's are, so their density is the boat's speed and a stopped
// boat leaves none. Sized from the hull's beam and grown more the faster it goes; near top speed a splash at the
// trailing end now and then (foam). Every knob is a live CVar (`boat wake`). The rings share the scene's soft-sprite
// pool with every other effect: 160 slots since #268 (the game's 85 overflowed when Link dove beside a moving boat,
// z_play.c), and they never cost another effect its slot. They spawn at the lowest priority there is, which
// EffectSs_FindSlot hands to any other spawn first once the table is full (z_effect_soft_sprite.c), and only while
// more than `reserve` slots are free, so the wake thins out under pressure rather than churning.
//
// LINK'S SHADOW (#267). Over a boat Link has no feet shadows (Navi's light turned them round him). By default the boat
// casts his shadow onto itself (RS_VEHICLE_SHADOW_DRAPED, RsVehicleShadow_Draw): the drawn hull's own faces under it
// (RsVehicleDeck_FromArchive, read from the kind's archive as the boat spawns), clipped to its square and drawn again in the
// hull's matrix wearing vanilla's circle shadow laid on from above. So it rocks with the drawn deck rather than the
// collision's whole units, lies on the trim to its edge, over the covered bow's crown and down the slanted walls, and
// shows only once his feet are up on the deck he climbs onto; it follows his feet mid-climb and fades in over a few
// ticks. Built on the hull at a walk and relative to him at a run (RsVehicleShadowAnchor). 60-390 opaque words a tick over
// the circle's, by how many faces lie under it. `boat shadow` switches it (or to the flat circle, or the feet shadows),
// reads its last draw, and `boat shadow trace` its last RS_VEHICLE_SHADOW_TRACE.
//
// HEALTH (#284, boats/RsBoatHealth.h). A hit to 0 capsizes the boat (RsBoat_Capsize): it lets go, its collision goes
// so Link drops into the water where it went down, and the hull rolls over and sinks through the bed, taking no input
// (RS_BOAT_SINK_*), until it is all below the bed, or at a 30 s cap; then the player's own is stowed (and wrecked)
// and a fixture is gone. `boat sinkfade on` brings back a short drop that fades out instead (gSPFade, libultraship's;
// drawn at the frame's end, RsBoat_DrawFaded, so it fades over the water). Bumping the shore never hurts.
//
// Console: `boat ...` and `agenttest boat ...` (RsBoatConsole.h). Markers: `rs_boat event=<spawn|helm_on|
// helm_off|blocked|capsize|sunk> ...` (agent mode).

#include "z64.h"
#include "RsSteer.h"
#include "RsVehicleShadow.h"
#include "soh/Enhancements/rs/boats/RsBoatHazard.h"
#include "soh/Enhancements/staticbake/ArchivePropLists.h" // ArchivePropsAnimCopy: a facility's poses (#291)

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

// The segment a boat's lists call after each material its fit-out recolours (#283): the sail cloth's pattern and
// every material a tier changes. RsBoat_Draw fills it with one slot a material, RS_BOAT_RECOLOUR_STRIDE bytes apart.
// The same numbers as the `look`'s and `tiers`' "segment" in each rs_boat_<kind>_props.json and generate_props.py's
// RECOLOUR_STRIDE: change them together.
#define RS_BOAT_LOOK_SEGMENT 0x0C
#define RS_BOAT_RECOLOUR_STRIDE 32 // a SetPrimColor and an EndDisplayList, two 16-byte Gfx
#define RS_BOAT_RECOLOUR_MAX_SLOTS 32
#define RS_BOAT_RECOLOUR_MAX_TIERS 8
// A kind's facility rows (RsBoat.c's RS_BOAT_MAX_FACILITIES) and the flipbook copies one list holds at most (the
// sloop's two cannons), for the poses `boat list` reports (#291).
#define RS_BOAT_FACILITY_POSE_ROWS 6
#define RS_BOAT_FACILITY_POSE_COPIES 2

// One recolour slot of a boat's list, from its archive's `.recolour` file (generate_props.py, THE RECOLOUR FILE).
typedef struct RsBoatRecolourSlot {
    s32 part;      // RsBoatSlot whose fitted tier picks the colour, or -1 for a look alone
    s32 look;      // RsBoatLook whose chosen option recolours it first, or -1
    s32 tierCount; // colours a tier, the part's first first (a facility's is 1, #288); a tier past the last draws the last
    u8 rgb[RS_BOAT_RECOLOUR_MAX_TIERS][3];
    s32 image;     // the RsBoatSailId whose picture its texture coordinates are for (an `image` line, #287), or -1
} RsBoatRecolourSlot;

typedef struct RsBoatRecolour {
    s32 count;
    RsBoatRecolourSlot slots[RS_BOAT_RECOLOUR_MAX_SLOTS];
} RsBoatRecolour;

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
// kind's own (RsBoat_TopSpeed), and its fit-out may raise them (its hull) or set its acceleration (its sails;
// boats/RsBoatFit.h, sturdy-bassoon#263); the rest are one set for every kind.
typedef enum RsBoatTuneKnob {
    RS_BOAT_TUNE_FORWARD,     // top speed ahead for every kind, overriding each kind's own; 0: the kind's
    RS_BOAT_TUNE_REVERSE,     // top speed astern, as a share of the top speed in use; 0 is each kind's own
    RS_BOAT_TUNE_ACCEL,       // speed gained a tick, toward the throttle's target
    RS_BOAT_TUNE_DRAG,        // speed lost a tick, coasting toward a lower target
    RS_BOAT_TUNE_BRAKE,       // speed lost a tick while the stick opposes the way the boat moves
    RS_BOAT_TUNE_TURN_STILL,  // turn rate standing still, for every kind; 0: the kind's
    RS_BOAT_TUNE_TURN_MOVING, // added at the kind's default top speed, for every kind; 0: the kind's
    RS_BOAT_TUNE_STEPS,       // 0: the throttle follows the stick smoothly; n: it snaps to n steps
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

// The wake's knobs (#268): `boat wake <knob> <value>`, saved, read every time a ring is due. RsBoatTuneInfo's shape.
typedef enum RsBoatWakeKnob {
    RS_BOAT_WAKE_ON,      // 0: no wake
    RS_BOAT_WAKE_SPACING, // units the hull travels between rings: the trail's density
    RS_BOAT_WAKE_SIZE,    // ring size, times the hull's own (its beam's share of the skiff's)
    RS_BOAT_WAKE_GROW,    // how many times its first size a ring grows to at top speed (Link's are 5); half-way there
                          // at a crawl
    RS_BOAT_WAKE_LINES,   // 1: the trail down the middle; 2: alternating between the trailing end's quarters
    RS_BOAT_WAKE_BOW,     // 1: going ahead, a bow wave off alternate shoulders as well
    RS_BOAT_WAKE_FOAM,    // a splash at the trailing end every third ring at this share of top speed and over; 0: none
    RS_BOAT_WAKE_RESERVE, // soft-sprite slots kept free for every other effect: no ring while this few are free
    RS_BOAT_WAKE_COUNT,
} RsBoatWakeKnob;

extern const RsBoatTuneInfo gRsBoatWake[RS_BOAT_WAKE_COUNT];

// A wake knob's live value, clamped to its range.
f32 RsBoat_WakeTune(s32 knob);

// The scene's soft-sprite pool (EffectSs_InitInfo's table) as it stands. 0s outside a scene, but for the counts.
typedef struct RsBoatEffectPool {
    s32 size;    // its slots (160; `boat wake slots`)
    s32 used;    // those holding an effect
    s32 ripples; // of those, water rings: anyone's, Link's and other actors' included
    s32 wake;    // of those, the wake's own (spawned at the priority that yields its slot first)
    s32 yielded; // EffectSs_FindSlot's counts since the game started: slots handed over by a wake effect,
    s32 evicted; // taken from any other live effect (vanilla's own overflow),
    s32 refused; // and spawns that found no slot at all
    s32 peak;    // the most slots alive at the end of a tick since the scene loaded or RsBoat_EffectPoolResetPeak
} RsBoatEffectPool;

void RsBoat_EffectPool(RsBoatEffectPool* pool);
void RsBoat_EffectPoolResetPeak(void);

// The pool's slot `index` while it holds an effect, else NULL (free, or out of range). For the console's breakdown.
const EffectSs* RsBoat_EffectSlot(s32 index);

struct RsBoat;
// The top speed this boat uses now, units a tick: `fwd` when it is set, else its kind's fit-out's (a hull past its
// default, RsBoatFit_Stats), else its kind's own; then its speed modifiers (RsBoatSpeedMod) on that.
f32 RsBoat_TopSpeed(const struct RsBoat* boat);

// What is changing a boat's top speed this tick, the one input every source writes (sturdy-bassoon#285): the cap is
// the top speed times `share`, plus `burst`. A hazard water sets `share` (boats/RsBoatHazard.h); #289's rapids will set
// `burst`, RS's speed stacks over the hull's cap. Each source sets its own every tick, so a modifier ends when its
// source does.
typedef struct RsBoatSpeedMod {
    f32 share; // 1 for none; a hazard's slowdown, the strongest of those the boat is in
    f32 burst; // units a tick over the cap; 0 for none (nothing sets it yet)
} RsBoatSpeedMod;

typedef enum RsBoatBlock {
    RS_BOAT_BLOCK_NONE,
    RS_BOAT_BLOCK_NO_WATER, // a hull point left the waterbox, or its surface is a different height
    RS_BOAT_BLOCK_SHALLOW,  // the floor's edge is over ground shallower than the draft, or ground rises into a side
    RS_BOAT_BLOCK_WALL,     // a line from the hull's centre to the gunwale crossed a wall, at the waterline or at
                            // Link's waist
    RS_BOAT_BLOCK_HEADROOM, // something over a hull point is lower than Link's head on the deck (a low bridge,
                            // an overhang): driving on would scrape him off
} RsBoatBlock;

// The actor's params: the kind in the low byte, and RS_BOAT_PARAM_OWNED on the player's own boat, which the docks
// spawn and track (boats/RsBoatDock.h, sturdy-bassoon#282). A boat without it is a fixture: Lake Hylia's, `boat
// spawn`'s, a trial's. Nothing about a fixture is saved.
#define RS_BOAT_PARAM_OWNED 0x100
#define RS_BOAT_PARAMS(kind, owned) ((s16)((kind) | ((owned) ? RS_BOAT_PARAM_OWNED : 0)))

typedef struct RsBoat {
    DynaPolyActor dyna;
    s32 kind;        // RsBoatKindId, from the params
    s32 owned;       // 1 for the player's own boat (RS_BOAT_PARAM_OWNED), 0 for a fixture
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
    f32 wakeRun;     // units travelled since the last wake ring (#268)
    s32 wakeSide;    // +1 or -1: the quarter and the bow shoulder the next rings go off
    s32 wakeRings;   // wake rings spawned since spawn, splashes included, for the console
    s32 wakeHeld;    // rings held back because the pool was down to its reserve
    s32 wakeFoam;    // rings due at foam speed: every RS_BOAT_WAKE_FOAM_EVERY-th brings a splash
    RsSteerCam cam;  // the steering camera while the helm is held (RsSteer.h)
    s32 damage;      // a fixture's damage, what it is missing from its most (#284); the player's own is the store's
    s32 sinkTicks;   // ticks since it capsized, 0 while afloat (#284)
    s32 culled;      // RsBoat_Draw left it out on the last frame: no part of it could be on screen (#293)
    f32 sinkFloorY;  // the bed under it when it capsized, BGCHECK_Y_MIN for none: gone once all of it is below
    // #285's: this tick's top-speed modifiers, and the hazard waters (boats/RsBoatHazard.h) as of its last tick.
    RsBoatSpeedMod speedMod;
    u32 hazardIn;   // the hazard kinds its helm is in, a bit each
    u32 hazardHarm; // those of them its fit-out does not protect against: what slows it, hits it and the HUD lists
    s32 hazardTicks[RS_BOAT_HAZARD_COUNT]; // ticks with Link aboard toward each kind's next hit
    s32 hazardHits[RS_BOAT_HAZARD_COUNT];  // each kind's hits since it entered: icy's grow with them
    // The pose each facility row's flipbook copies drew on its last draw (#291), -1 for none (a still facility, one not
    // fitted, or a culled boat), for `boat list`'s facility_poses=.
    s16 facilityPose[RS_BOAT_FACILITY_POSE_ROWS][RS_BOAT_FACILITY_POSE_COPIES];
    // Where each facility row's moving part (#291: a hook's anchor, a catcher's mote) was turned on its last draw, in
    // RS client cycles round its loop, -1 for none, for `boat list`'s facility_parts=.
    f32 facilityPart[RS_BOAT_FACILITY_POSE_ROWS];
} RsBoat;

// How a capsized boat goes down (#284). It rolls onto its side over RS_BOAT_SINK_ROLL_TICKS and drops, gathering speed
// to RS_BOAT_SINK_SPEED, unfaded: drawn in its own place, before the water, its underwater part is tinted as any boat's
// is. With no collision it goes straight through the bed, which hides what has gone below it (the owner, 2026-11-04).
// It is removed once its reach (RsBoat_KindReach) is all below the bed measured under it at the capsize, or, where that
// takes too long (deep water, no bed), at RS_BOAT_SINK_CAP_TICKS, a large buffer (30 s), with no fade: a fade there
// would draw the hull after the water, untinted (the owner, 2026-11-04).
#define RS_BOAT_SINK_ROLL_TICKS 30
#define RS_BOAT_SINK_ACCEL 0.25f // units a tick, a tick
#define RS_BOAT_SINK_SPEED 4.0f  // units a tick: 80 a second
#define RS_BOAT_SINK_CAP_TICKS 600
// The first prototype's sink, which `boat sinkfade on` brings back (saved): a 2 s drop of 120, fading out from a
// quarter of the way down, then removed. The fade is gSPFade, drawn by RsBoat_DrawFaded.
#define CVAR_RS_BOAT_SINK_FADE CVAR_ENHANCEMENT("RsBoatSinkFade")
#define RS_BOAT_SINK_TICKS 40
#define RS_BOAT_SINK_DEPTH 120.0f

// Capsizes the boat now, as its HP reaching 0 does (RsBoatHealth.h): lets go of the helm, drops its collision so Link
// is left in the water, spends his refuge on it, and starts it sinking. Nothing for a boat already sinking. Marks
// `capsize`, and `sunk` when it has gone under.
void RsBoat_Capsize(struct RsBoat* boat, PlayState* play);

// 1 when Link is aboard the boat for a hit (RsBoatHealth.h): on its deck or rail, or at its helm.
s32 RsBoat_LinkAboard(const struct RsBoat* boat, PlayState* play);

// 1 when (x, z) lies inside the boat's outline grown by `margin` all round: its stations, stern to bow, each its outer
// half width, taken between stations as the hull tapers. What sailing over something is: a boat heart (#284), a trial's
// crate (#289, which tests a rectangle of its own today), a rapid.
s32 RsBoat_HullContains(const struct RsBoat* boat, f32 x, f32 z, f32 margin);

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
// The kind's archive list path ("objects/rs_props/rs_boat_skiff/props"), or NULL out of range: where its sails'
// templates live beside it (RsBoatSail.h).
const char* RsBoat_KindListPath(s32 kind);

// The kind's collision triangles, for the console and the DynaPoly budget.
s32 RsBoat_KindPolyCount(s32 kind);
// The most collision a boat of the kind can register: its hull's triangles and corners plus room for every facility box
// it could be fitted with. Builds the hull if no boat of the kind has yet, so a spawn can be checked against the
// DynaPoly pools before it is made (RsBoatDock.cpp). 0 for no such kind.
void RsBoat_KindCollisionMax(s32 kind, s32* polys, s32* vtx);
// The kind's half beam (its widest station's outer half width) and half length, units: where a mooring puts it.
f32 RsBoat_KindHalfBeam(s32 kind);
f32 RsBoat_KindHalfLength(s32 kind);
// Of those, its fitted facilities' boxes' (#288): 10 a box, 0 with none fitted that is solid.
s32 RsBoat_FacilityPolyCount(s32 kind);
// The kind's facilities drawn now (#288), for `boat list`: each list's name less its "rs_boat_" ("sloop_cannon"),
// comma-separated, "?" after one whose archive is not mounted, or "-" for none fitted.
void RsBoat_FacilityNames(s32 kind, char* buf, size_t len);
// The list a kind's facility row `i` draws (#291): a wind catcher's active list while the kind's catcher is active.
const char* RsBoat_FacilityList(s32 kind, s32 i);
// Its fitted facilities' flipbook poses on the boat's last draw (#291), for `boat list`: `<name>:<pose>` a facility,
// `/<pose>` for each further copy (the sloop's two cannons), comma-separated, or "-" for none.
void RsBoat_FacilityPoses(const RsBoat* boat, char* buf, size_t len);
// `boat list`'s facility_parts=: each drawn part's row and where it was in its loop (RsBoat.facilityPart); "-": none.
void RsBoat_FacilityParts(const RsBoat* boat, char* buf, size_t len);
// The kind's wind catcher drawn active, its cups and stored mote spinning (#291), or idle (RS's idle catcher, mote
// hidden; the default). Per kind, not saved: `boat catcher` sets it.
s32 RsBoat_CatcherActive(s32 kind);
void RsBoat_SetCatcherActive(s32 kind, s32 on);

// Takes or lets go of the helm as A/B would, for the console. Returns 0 if Link is not on this boat's deck.
s32 RsBoat_SetHelm(RsBoat* boat, PlayState* play, s32 on);

// Link is standing on this boat's deck (or its rail).
s32 RsBoat_LinkOnDeck(const RsBoat* boat, const Player* player);

// 1 when A would take some boat's helm right now: Link on its deck, free to (RsSteer_LinkCanTake), and
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

// 1 when `bgId` is a live boat's collision.
s32 RsBoat_IsBoatFloor(PlayState* play, s32 bgId);


// 1 when Link, aboard as of last tick, is leaving an edge over a boat's own floor: off its rail or its covered bow onto
// its deck. Player gives that no hop (a walk off), or on a kind that hops inward the small hop at any pace over 0.5, a
// walk included (the full one at a run carried him over the far side), from any drop over 10.
s32 RsBoat_StepOntoOwnDeck(PlayState* play, const Player* player);

// RsBoat_StepOntoOwnDeck, onto a boat whose kind hops inward (the sloop). Onto the others' decks Link walks off.
s32 RsBoat_HopOntoOwnDeck(PlayState* play, const Player* player);

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
// angle still finds it and climbs, and further again for a swimmer at a hull, who is held further off it.
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

// `boat fade`: every boat drawn faded by `amount` (0 off, to 255), with both passes or one alone, as a sinking hull is
// (#284; a test aid for the fade's look, not saved).
typedef enum RsBoatFadePasses {
    RS_BOAT_FADE_BOTH,   // the depth pass, then the faded colour where it matches (what a sinking hull draws)
    RS_BOAT_FADE_DEPTH,  // the depth pass alone: nothing on screen, its depth written
    RS_BOAT_FADE_COLOUR, // the faded colour alone, with no depth pass before it
} RsBoatFadePasses;
typedef struct RsBoatFadeTest {
    s32 amount;
    s32 passes; // RsBoatFadePasses
} RsBoatFadeTest;
extern RsBoatFadeTest gRsBoatFadeTest;

// Draws every faded boat's lists (a sinking hull, `boat fade`), which RsBoat_Draw leaves out: at the frame's end, after
// every water, so a faded hull blends over the water (#284). The scene water's end-of-frame draw calls it, after its
// own sheets (RsSceneWater.cpp).
void RsBoat_DrawFaded(PlayState* play);

// `boat cull`: a boat nobody is aboard draws only when some part of it can be on screen (1, the default), or every
// frame wherever it is (0, as before #293: an A/B for the agent loop, not saved). RsBoat_Draw decides (RsBoat.c).
extern s32 gRsBoatCull;
// The kind's draw cull radius, world units from the boat's origin: its farthest drawn vertex, read from its archive
// (hull, sails, rudder and every facility) once, or RS_BOAT_CULL_REACH_NO_MODEL when the archive is not mounted then.
#define RS_BOAT_CULL_REACH_NO_MODEL 300.0f // the placeholder boxes', with room to spare
f32 RsBoat_KindReach(s32 kind);
// How far past each edge of the screen the cull still counts as on screen, as a share of the view (0.15: the sides
// 15% wider, the top and bottom 15% taller): the frames interpolated between two ticks turn the camera on from the
// view the draw was decided in.
#define RS_BOAT_CULL_EDGE_PAD 0.15f
// Whether the cull may leave this boat out (`boat cull` on, nobody aboard, no helm, no test water), and whether it was
// drawn on the last frame: Actor_DrawAll drew it and the cull did not leave it out (`boat list`'s cull= and drawn=).
s32 RsBoat_CullApplies(const RsBoat* boat);
s32 RsBoat_Drawn(const RsBoat* boat);

// The hookshot's and the boomerang's line test (BgCheck_EntityLineTest1, every surface, as ArmsHook_Shoot and EnBoom_Fly
// ask), but meeting a boat's hull as projectiles do (#262): past the walls only Link meets (RS_BOAT_POLY_SHOTS_PASS),
// so through the sloop's holes, onto the drawn plank only they meet (RS_BOAT_POLY_SHOTS_ONLY), on a skinned stretch.
s32 RsBoat_ShotLineTest(PlayState* play, Vec3f* from, Vec3f* to, Vec3f* result, CollisionPoly** outPoly,
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

// Each tick of a swimmer's climb onto a hull: lifts his lowered model through the part of the rise over vanilla's
// climb-out limit, over the climb's first ticks, so his hands meet the top. Nothing on any other climb.
void RsBoat_ClimbLift(Player* player);

// The height of the rail Link should hop onto now, or 0: he has pushed into a boat's hull from its deck for 0.6 s,
// so brushing past the rail does not put him over the side. Player hops him as onto a ledge that high.
f32 RsBoat_RailHop(PlayState* play, const Player* player);

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
// The list's translucent twin, `<listPath>.xlu` (generate_props.py's THE TRANSLUCENT LIST, #216), or NULL when its
// kinds draw nothing translucent: a facility's faded faces (#288). Not logged when absent, which is the usual case.
Gfx* RsBoat_TranslucentList(const char* listPath);
// A list's flipbook copy `copy` (#291), in `<listPath>.anim`'s order, or NULL past the last or when the list has no
// table: its poses' lists, in the list's own space, and its clock, which RsAnimProp_PoseAt reads. Read once.
const ArchivePropsAnimCopy* RsBoat_AnimCopy(const char* listPath, s32 copy);
// That copy's rest pose's list path, for the draw cull's reach, or NULL when there is no such copy.
const char* RsBoat_AnimRestPath(const char* listPath, s32 copy);
// The list's recolour slots (#283), read once from `<listPath>.recolour` in its archive, or NULL when it has none (an
// archive from before tiers, whose one slot is the sail pattern's). `kind` is checked against: a part's tier locs
// that differ from RsBoatFit's catalogue are logged once.
const RsBoatRecolour* RsBoat_Recolour(s32 kind, const char* listPath);


const char* RsBoat_BlockName(s32 block);

// What the boat's outline meets where it is now, for `boat contact`: `out` gets "points=" and one
// ` <reason>:<depth>@<push x>,<push z>` per outline point in trouble (the push a unit vector x100, world axes).
// Returns the deepest one's RsBoatBlock.
s32 RsBoat_ContactReport(RsBoat* boat, PlayState* play, char* out, s32 size);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_BOAT_H
