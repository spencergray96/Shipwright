#ifndef SOH_RS_BOAT_H
#define SOH_RS_BOAT_H

// Rs_Boat (sturdy-bassoon#256): a crude boat Link can board, steer across a waterbox and get off again.
// A PROTOTYPE - the issue's shape A, "a moving platform you stand on": a DynaPolyActor like Bg_Haka_Ship,
// whose speed and yaw come from the stick instead of a rail.
//
// THE PLATFORM. A box hull with its own collision header (a deck and four sides), built at the first Init.
// DPM_UNK3 (position and yaw) is what carries Link along and turns him with the hull, the engine's own
// moving-platform path. The hull's origin is the waterline: the deck is RS_BOAT_DECK_Y above it.
//
// THE HELM. While Link stands on deck the stick moves Link, so steering is a toggle: A within
// RS_BOAT_HELM_RANGE of the tiller (the post at the stern) takes the helm. PLAYER_STATE1_INPUT_DISABLED
// then freezes Link's own input (RemoteBombchu's trick) and the boat reads the pad instead: stick x turns,
// A held drives forward, stick pulled back reverses, B lets go. The boat is ACTORCAT_BG, which updates
// before ACTORCAT_PLAYER, so the frame A takes the helm Player already sees no input and does not roll.
// A takes the helm only from a Link who is free to (RsBoat_LinkCanTakeHelm: not aiming, in first person,
// holding or carrying anything, airborne, talking or in a cutscene), and the helm lets go of itself when Link
// is hurt, dies or is knocked away from the tiller.
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
// pitch (a second, slower sine) are DRAW-ONLY: the collision stays level, so Link stands upright on a deck
// that rocks a few degrees under him and never slides. A waterbox buried under the ground is not water
// (RsBoat_SurfaceAt); a boat with no water under it settles on the ground and does not move.
//
// THE SHORE. A move is tried, then refused if any of eight points on the hull's edge (the corners and the
// middle of each side) would be off the water, over a floor shallower than RS_BOAT_DRAFT, behind a wall (at the
// waterline or at Link's waist), or under something lower than RS_BOAT_HEADROOM above the deck. A refused move
// tries the turn alone and the translation alone before giving up, so the boat slides round a corner rather than
// sticking to it. `blocked` says why the last move was refused. Only the eight points are probed: a post thin
// enough to fall between two of them (they are up to 120 units apart) is not seen.
//
// Console: `boat ...` and `agenttest boat ...` (RsBoatConsole.h). Markers: `rs_boat event=<spawn|helm_on|
// helm_off|blocked> ...` (agent mode).

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Hull size, world units: the Sailing skiff's (loc 59503, RsBoatModel.cpp), whose export spans x -51..51 and
// z -87..137 with its bow at +z (tapering to 4 wide), a 70-wide transom at the stern, its inside floor at y 0
// and its gunwale's top at 34. The collision is a box over its floor (the bow's taper and the gunwales are not
// in it: Link walks through a gunwale's drawing). Loc 59508, first taken for it, is the sloop's 3 x 8 hull.
#define RS_BOAT_HALF_LENGTH 108.0f // along the hull's +z (the bow)
#define RS_BOAT_HALF_WIDTH 46.0f
#define RS_BOAT_DECK_Y 4.0f   // deck above the waterline: the skiff's floor, so its hull sits on the water
#define RS_BOAT_KEEL_Y -14.0f // the placeholder box's bottom below the waterline (drawn without the model)
// The skiff's export is centred 25 units bow-ward of its origin; drawn this far back, it sits on the collision.
#define RS_BOAT_MODEL_Z (-25.0f)
// The collision sides reach further down than the drawn hull: a swimming adult Link floats ~45 below the surface
// and checks walls ~26 above that, so sides that stop at the keel let him swim straight under the boat instead
// of meeting its side (where he can climb out onto the deck).
#define RS_BOAT_SIDE_FOOT_Y -60.0f

#define RS_BOAT_DRAFT 10.0f           // a floor nearer the surface than this is the shore
#define RS_BOAT_HEADROOM 80.0f        // clear space wanted over the deck: adult Link is ~50 tall, plus a hop
#define RS_BOAT_HELM_RANGE 45.0f      // Link within this of the tiller can take the helm
#define RS_BOAT_HELM_Z (-RS_BOAT_HALF_LENGTH + 22.0f) // where Link stands to steer, along the hull
#define RS_BOAT_HELM_KNOCKED 25.0f   // Link this far (xz) from the tiller at the helm has been moved off it

// The steering camera. CVAR_ENHANCEMENT is soh/cvar_prefixes.h's; it expands where these are used.
#define CVAR_RS_BOAT_CAM CVAR_ENHANCEMENT("RsBoatCam")              // 0: the normal camera stays at the helm
#define CVAR_RS_BOAT_CAM_HEIGHT CVAR_ENHANCEMENT("RsBoatCamHeight") // eye above the deck, world units
#define CVAR_RS_BOAT_CAM_DIST CVAR_ENHANCEMENT("RsBoatCamDist")     // eye behind Link, world units
#define CVAR_RS_BOAT_CAM_EASE CVAR_ENHANCEMENT("RsBoatCamEase")     // percent of the way to its target a tick
#define CVAR_RS_BOAT_HUD CVAR_ENHANCEMENT("RsBoatHud")              // 0: no steering label
#define RS_BOAT_CAM_HEIGHT_DEFAULT 120 // the owner's pick: high enough to see obstacles in the water ahead
#define RS_BOAT_CAM_DIST_DEFAULT 240
#define RS_BOAT_CAM_EASE_DEFAULT 25 // ~90% of the way in 8 ticks (0.4 s): a fast catch-up, not a snap
#define RS_BOAT_CAM_HEIGHT_MIN -20
#define RS_BOAT_CAM_HEIGHT_MAX 600
#define RS_BOAT_CAM_DIST_MIN 80
#define RS_BOAT_CAM_DIST_MAX 1000
#define RS_BOAT_CAM_EASE_MIN 5
#define RS_BOAT_CAM_EASE_MAX 100

typedef enum RsBoatBlock {
    RS_BOAT_BLOCK_NONE,
    RS_BOAT_BLOCK_NO_WATER, // a hull point left the waterbox, or its surface is a different height
    RS_BOAT_BLOCK_SHALLOW,  // a hull point is over a floor shallower than the draft
    RS_BOAT_BLOCK_WALL,     // a line from the hull's centre to a point crossed a wall, at the waterline or at
                            // Link's waist
    RS_BOAT_BLOCK_HEADROOM, // something over a hull point is lower than Link's head on the deck (a low bridge,
                            // an overhang): driving on would scrape him off
} RsBoatBlock;

typedef struct RsBoat {
    DynaPolyActor dyna;
    s32 helm;        // 1 while Link holds the helm
    f32 speed;       // signed, units per tick along the hull's heading
    f32 tiller;      // -1..1, eased toward the stick
    s16 yawVel;      // binang per tick, from the tiller and the speed
    s16 roll;        // draw-only tilt, binang
    s16 pitch;
    s32 timer;       // ticks since spawn: the bob's clock
    f32 surfaceY;    // the waterbox surface under the hull; the last good one when there is none
    s32 onWater;     // the hull's centre is over a waterbox
    s32 blocked;     // RsBoatBlock of the last refused move, or NONE
    s32 blockCount;  // refused moves since spawn, for the agent loop
    s32 markedBlock; // the reason the last `blocked` marker named; cleared once the boat moves freely again,
                     // so a boat nudging the shore at a crawl reports it once rather than every few ticks
    s32 freeTicks;   // moves in a row that fitted: "freely again" is RS_BOAT_FREE_TICKS of them
    s32 helmPress;   // A was down last tick: a held A is not a second press
    s32 throttleArmed; // A has been let go since the helm was taken, so a held A is throttle and not the take
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

// Takes or lets go of the helm as A/B would, for the console. Returns 0 if Link is not on this boat's deck.
s32 RsBoat_SetHelm(RsBoat* boat, PlayState* play, s32 on);

// Link is standing on this boat's deck.
s32 RsBoat_LinkOnDeck(const RsBoat* boat, const Player* player);

// 1 when A would take some boat's helm right now: Link on its deck, free to (RsBoat_LinkCanTakeHelm), and
// within reach of its tiller (RS_BOAT_HELM_RANGE), or anywhere on its deck while locked on to it. Player
// reads it for the A button's label.
s32 RsBoat_TillerInReach(PlayState* play);

// Link is free to take the helm: on nothing but his feet, empty-handed and doing nothing else. 0 while he aims
// or looks in first person, has a sword or item out, holds, carries or throws something, shields, hangs,
// jumps, falls, talks, is hurt or is in a cutscene - each of which A at the tiller used to override, locking
// him into it at the helm.
s32 RsBoat_LinkCanTakeHelm(const Player* player);

// Stands Link at the tiller facing the bow, as a teleport (prevPos, home.pos and fallStartHeight move with
// him). `lift` > 0 also sets his height to that far above the deck, for a Link who is not aboard yet.
void RsBoat_StandAtHelm(const RsBoat* boat, Player* player, f32 lift);

// The water surface at (x, z): 1 and the height when open water is there, 0 when not. Of the waterboxes over
// the point, the highest one not buried under the ground there (drained Lake Hylia lowers its main box under
// the lakebed). `skip` is a boat whose collision does not count as ground over the water (NULL: every boat's
// does). `*outY` is read as well as written in Zora's Domain (WaterBox_GetSurface1), so pass a nearby height.
s32 RsBoat_SurfaceAt(PlayState* play, f32 x, f32 z, f32* outY, Actor* skip);

// The hull's tiller position in world space, at deck height.
void RsBoat_HelmPos(const RsBoat* boat, Vec3f* out);

// The skiff's display list, in its own model space (RsBoatModel.cpp), or NULL when its archive is not in mods/ -
// the boat then draws its placeholder box.
Gfx* RsBoat_ModelList(void);

const char* RsBoat_BlockName(s32 block);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_BOAT_H
