/*
 * RsBoat.c - a crude steerable boat (sturdy-bassoon#256). RsBoat.h has the design: the platform, the helm,
 * the water and the shore.
 *
 * Same /W3 /WX rules as the other rs/ actors: explicit casts, no `s32 pad`, no ICHAIN.
 */

#include <math.h>
#include <stdio.h> // snprintf, for the agent-loop markers
#include <string.h> // strcmp, for the kinds' names

#include "RsBoat.h"
#include "RsActors.h"
#include "global.h"

// z_camera.c's, not in functions.h; RemoteBombchu.cpp declares it the same way. Pulls `to` back to the first
// wall between `from` and it, so the steering camera never sits inside the scenery.
s32 Camera_BGCheck(Camera* camera, Vec3f* from, Vec3f* to);

void RsBoat_Init(Actor* thisx, PlayState* play);
void RsBoat_Destroy(Actor* thisx, PlayState* play);
void RsBoat_Update(Actor* thisx, PlayState* play);
void RsBoat_Draw(Actor* thisx, PlayState* play);

RsBoatDrive gRsBoatDrive = { 0, 0.0f, 0.0f };
RsBoatTestWater gRsBoatTestWater = { 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

// --- tuning -------------------------------------------------------------------------------------
//
// The handling is live (`boat tune`, RsBoat.h): per 20 Hz tick, Link's run being ~6 units a tick as adult. Top speed is
// the kind's own (sRsBoatKinds: the owner's first pass, 2026-10-25, sturdy-bassoon#263); `fwd` overrides it for every
// kind while it is above 0. Reverse is a share of the top speed in use, so an upgrade backs up faster too (25%: the
// raft's 2.5 at 10, which the owner liked, and the skiff's 3.5 at 14). Accel and drag were tuned and kept; brake is
// the rate when the stick opposes the way the boat moves (pulling back while going ahead took 4.7 s at the drag
// rate before the skiff began to back up; 0.6 then felt too strong, and 0.4 takes half as long again: ~1.75 s from
// the skiff's 14). Turn rates are the kind's own too (sRsBoatKinds), and turnstill / turnmove override them for every
// kind while above 0. The fwd, rev and turn CVars are new names: the old ones held one value for every kind.
const RsBoatTuneInfo gRsBoatTune[RS_BOAT_TUNE_COUNT] = {
    { "fwd", CVAR_ENHANCEMENT("RsBoatTuneTopSpeed"), 0.0f, 0.0f, 40.0f },
    { "rev", CVAR_ENHANCEMENT("RsBoatTuneReverseShare"), 0.25f, 0.05f, 1.0f },
    { "accel", CVAR_ENHANCEMENT("RsBoatTuneAccel"), 0.25f, 0.01f, 5.0f },
    { "drag", CVAR_ENHANCEMENT("RsBoatTuneDrag"), 0.15f, 0.01f, 5.0f },
    { "brake", CVAR_ENHANCEMENT("RsBoatTuneBrake"), 0.4f, 0.01f, 5.0f },
    { "turnstill", CVAR_ENHANCEMENT("RsBoatTuneTurnAtRest"), 0.0f, 0.0f, 1000.0f },
    { "turnmove", CVAR_ENHANCEMENT("RsBoatTuneTurnAtSpeed"), 0.0f, 0.0f, 2000.0f },
    { "steps", CVAR_ENHANCEMENT("RsBoatTuneSteps"), 0.0f, 0.0f, 10.0f },
    { "upgrade", CVAR_ENHANCEMENT("RsBoatTuneUpgrade"), 0.0f, 0.0f, 1.0f },
};

f32 RsBoat_Tune(s32 knob) {
    const RsBoatTuneInfo* info = &gRsBoatTune[knob];

    return CLAMP(CVarGetFloat(info->cvar, info->def), info->min, info->max);
}

#define RS_BOAT_TILLER_EASE 0.25f // of the way to the stick's target, each tick: turning has some weight
// The stick reads -85..85 a side. Inside the dead zone it is centred; at RS_BOAT_STICK_FULL from the centre it
// is full throttle and full tiller, so the rim is reached a little before the gate.
#define RS_BOAT_STICK_DEAD 8.0f
#define RS_BOAT_STICK_FULL 70.0f
// Which way the stick points scales the throttle: in full within the first angle of straight ahead (so a diagonal
// drives at full speed), fading to nothing at the second; the same astern with its own pair, and between them
// (hard left or right) it only turns. Radians: 45 and 70 degrees ahead, 30 and 55 astern.
#define RS_BOAT_ARC_AHEAD_FULL 0.7854f
#define RS_BOAT_ARC_AHEAD_END 1.2217f
#define RS_BOAT_ARC_ASTERN_FULL 0.5236f
#define RS_BOAT_ARC_ASTERN_END 0.9599f

// The bob and the rock (tier 1 of the issue's waves): a slow sine on the height, a slower one on the tilt. The
// rock is a dip at the hull's edge rather than an angle (#262 point 6): the roll lowers the widest point of the
// gunwale this far and the pitch the stern and the bow, so a bigger hull rocks through a smaller angle. The
// skiff's are the angles it had (2.5 and 1.2 degrees); the sloop's come out at 1.7 and 0.8.
#define RS_BOAT_BOB_AMP 1.8f
#define RS_BOAT_BOB_PERIOD 64.0f   // ticks: ~3.2 s
#define RS_BOAT_ROCK_PERIOD 101.0f // ticks: ~5 s, out of step with the bob on purpose
#define RS_BOAT_ROLL_DIP 2.2f
#define RS_BOAT_PITCH_DIP 2.35f
// The floor's lowest drawn point is kept this far over the water: when the bob and both rocks line up past the
// deck's height (the skiff's stern corners went ~1.2 under, about one moment in ten), the hull is lifted by the
// difference, as a tipping hull rises (RsBoat_FloorLow).
#define RS_BOAT_FLOAT_MARGIN 1.0f

// The wall probe runs this far above the surface: under the deck, above any lip a shore has at the waterline.
#define RS_BOAT_PROBE_Y 8.0f
// The second wall probe, at Link's waist on the deck: a wall that starts above the waterline (a dock, a rail).
#define RS_BOAT_WAIST_Y 30.0f
// Ground under the gunwale's edge higher than this over the deck is a bank rising into the hull's side, which
// flares out to its widest about this far up.
#define RS_BOAT_SIDE_CLEAR 10.0f
// Moves in a row that must fit before a refusal is reported again (the `blocked` marker): half a second.
#define RS_BOAT_FREE_TICKS 10
// A waterbox surface this far from the boat's own is a different body of water (a pool at another height).
#define RS_BOAT_SURFACE_TOLERANCE 2.0f
// Pushing a move out of the shore: this many pushes at most, this far in all in one tick, and this far for a
// point whose way out has no direction of its own (off the water, under an overhang, on flat ground).
#define RS_BOAT_PUSH_TRIES 3
#define RS_BOAT_PUSH_MAX 12.0f
#define RS_BOAT_STEP_OUT 4.0f

// --- the kinds ----------------------------------------------------------------------------------
//
// Each hull is a row of stations from the stern to the bow, read off its export (`<root>/collision/<kind>
// .collision.json`, the native mesh): at each, the floor's half-width where the hull sits on the water, the
// gunwale's half-width (the widest the hull is there) and the rail's height over the deck. Their z is boat space,
// the model's own z plus `modelZ`, the shift that centres the drawn model on the boat's origin. Measured by
// slicing the mesh across z every 10 units (#262); the stations keep the slices where the outline bends. Both
// rails have low notches (the skiff's at model z -26..-16, the sloop's two at -66..-56 and -26..-16), which the
// collision leaves out: Link climbs aboard anywhere.
#define RS_BOAT_MAX_STATIONS 16

typedef struct RsBoatStation {
    f32 z;
    f32 floorHw;
    f32 outerHw;
    f32 railY;
} RsBoatStation;

typedef struct RsBoatKind {
    const char* name;
    const char* list; // the archive's display list: the kind once, at the origin, turn 0 (RsBoatModel.cpp)
    f32 deckY;  // where Link stands, over the waterline: the floor of a hull, the top of a raft's logs
    f32 modelY; // where the model's origin is drawn, over the waterline
    f32 modelZ;
    s32 helmInModel;     // its archive carries Sailing's helm as well as the hull: no placeholder tiller post
    f32 topSpeed;        // units a tick ahead, by default
    f32 topSpeedUpgrade; // with its speed upgrade unlocked (sturdy-bassoon#263), or 0 for none
    f32 turnAtRest;      // binang a tick at full tiller, standing still: how lively it feels, not a balance
    f32 turnAtSpeed;     // added at its default top speed: (rest + this) sets its full-speed turning circle
    s32 numStations;
    RsBoatStation stations[RS_BOAT_MAX_STATIONS];
} RsBoatKind;

static const RsBoatKind sRsBoatKinds[RS_BOAT_KIND_COUNT] = {
    // The Sailing skiff, loc 59503: model z -87..137 (bow at +z), x -51..51, a 70-wide transom, the bow tapering
    // to 4 wide; its rail 22 over the floor, rising to 28 at the bow. Its archive also carries Sailing's default
    // trim (59626, the gunwale cap in the hull's wood: without it the stern's inner corners are open), keel
    // (59516), helm (60558) and still sails (29480) at the template boats' offsets (#262).
    { "skiff",
      "objects/rs_props/rs_boat_skiff/props",
      RS_BOAT_DECK_Y,
      RS_BOAT_DECK_Y,
      -25.0f,
      1,
      14.0f, // the fastest, for now: the middle boat's use
      18.0f,
      220.0f, // full-speed circle ~650 across
      230.0f,
      13,
      {
          { -112.0f, 24.0f, 35.0f, 22.0f },
          { -91.0f, 30.0f, 40.0f, 22.0f },
          { -71.0f, 34.0f, 44.5f, 22.0f },
          { -51.0f, 38.0f, 48.0f, 22.0f },
          { -31.0f, 39.0f, 50.0f, 22.0f },
          { -11.0f, 41.0f, 51.0f, 22.0f },
          { 9.0f, 40.0f, 51.0f, 22.0f },
          { 29.0f, 38.0f, 49.0f, 22.0f },
          { 49.0f, 34.0f, 46.0f, 22.0f },
          { 69.0f, 29.0f, 39.0f, 28.0f },
          { 89.0f, 18.0f, 30.0f, 26.0f },
          { 99.0f, 6.0f, 18.5f, 25.0f },
          { 112.0f, 2.0f, 4.0f, 25.0f },
      } },
    // The sloop, loc 59508: model z -167..173, x -75..75, a 110-wide stern, its rail 32 over the floor and 37 at
    // the raised bow.
    { "sloop",
      "objects/rs_props/rs_boat_sloop/props",
      RS_BOAT_DECK_Y,
      RS_BOAT_DECK_Y,
      -3.0f,
      0,
      12.0f,
      14.0f,
      200.0f, // the widest full-speed circle, ~780 across: 20% wider than the skiff's
      121.0f,
      15,
      {
          { -170.0f, 40.0f, 55.0f, 32.0f },
          { -149.0f, 46.5f, 61.0f, 32.0f },
          { -129.0f, 50.5f, 66.0f, 32.0f },
          { -109.0f, 56.0f, 69.5f, 32.0f },
          { -89.0f, 57.5f, 71.5f, 32.0f },
          { -49.0f, 60.0f, 75.0f, 32.0f },
          { -9.0f, 60.0f, 75.0f, 32.0f },
          { 21.0f, 60.0f, 73.0f, 32.0f },
          { 51.0f, 56.5f, 70.0f, 32.0f },
          { 81.0f, 51.0f, 64.5f, 37.0f },
          { 101.0f, 43.5f, 58.5f, 37.0f },
          { 121.0f, 27.5f, 49.0f, 37.0f },
          { 141.0f, 10.0f, 36.0f, 37.0f },
          { 161.0f, 4.0f, 16.0f, 37.0f },
          { 170.0f, 2.0f, 6.0f, 32.0f },
      } },
    // The raft, loc 59496 (the 856-face model, in the skiff's tint column; 59494-59500 are its row, the 530-face
    // model from 59497): a flat log platform 48 wide and 140 long, model z -71..69, x -24..24 with two crossbeams
    // standing 5 out each side (model z -46 and 2, left out of the outline), the logs from model y 8 to their top
    // at 18.5, a post at model z 18. No rail. It floats flat with its logs half under: the top 6 over the water,
    // the model drawn 12.5 down.
    { "raft",
      "objects/rs_props/rs_boat_raft/props",
      6.0f,
      -12.5f,
      0.0f,
      0,
      10.0f,
      0.0f,   // no upgrade considered yet
      300.0f, // the best handling: the quickest spin and the tightest circle, ~420 across
      200.0f,
      7,
      {
          { -70.0f, 22.0f, 24.0f, 0.0f },
          { -46.0f, 22.0f, 24.0f, 0.0f },
          { -22.0f, 22.0f, 24.0f, 0.0f },
          { 2.0f, 22.0f, 24.0f, 0.0f },
          { 26.0f, 22.0f, 24.0f, 0.0f },
          { 50.0f, 22.0f, 24.0f, 0.0f },
          { 68.0f, 21.0f, 23.0f, 0.0f },
      } },
};

static f32 RsBoat_DeckY(const RsBoat* boat) {
    return sRsBoatKinds[boat->kind].deckY;
}

f32 RsBoat_TopSpeed(const RsBoat* boat) {
    const RsBoatKind* kind = &sRsBoatKinds[boat->kind];
    const f32 override = RsBoat_Tune(RS_BOAT_TUNE_FORWARD);

    if (override > 0.0f) {
        return override;
    }
    if (RsBoat_Tune(RS_BOAT_TUNE_UPGRADE) >= 0.5f && kind->topSpeedUpgrade > 0.0f) {
        return kind->topSpeedUpgrade;
    }
    return kind->topSpeed;
}

// The turn rate at full tiller for a boat moving at `speed`, binang a tick. From the kind's rest rate it rises with
// speed to (rest + at speed) at its default top speed, and in step with any faster top speed beyond that (an upgrade,
// or `fwd`), so a faster boat draws the same full-speed circle rather than a wider one: an upgrade the player cannot
// undo never handles worse. The owner's tuning, 2026-10-25 (the upgraded skiff felt right at 579 = 450 x 18 / 14).
static f32 RsBoat_TurnRate(const RsBoat* boat, f32 speed) {
    const RsBoatKind* kind = &sRsBoatKinds[boat->kind];
    const f32 top = RsBoat_TopSpeed(boat);
    const f32 rest = RsBoat_Tune(RS_BOAT_TUNE_TURN_STILL) > 0.0f ? RsBoat_Tune(RS_BOAT_TUNE_TURN_STILL)
                                                                 : kind->turnAtRest;
    const f32 atSpeed = RsBoat_Tune(RS_BOAT_TUNE_TURN_MOVING) > 0.0f ? RsBoat_Tune(RS_BOAT_TUNE_TURN_MOVING)
                                                                     : kind->turnAtSpeed;
    const f32 atTop = (rest + atSpeed) * top / kind->topSpeed;

    return rest + (atTop - rest) * MIN(fabsf(speed) / top, 1.0f);
}

const char* RsBoat_KindName(s32 kind) {
    return (kind >= 0 && kind < RS_BOAT_KIND_COUNT) ? sRsBoatKinds[kind].name : "?";
}

s32 RsBoat_KindByName(const char* name) {
    s32 i;

    for (i = 0; i < RS_BOAT_KIND_COUNT; i++) {
        if (strcmp(name, sRsBoatKinds[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

// --- the hull: collision ------------------------------------------------------------------------
//
// Built from the stations at the first Init of each kind, so the normals are computed rather than typed. At each
// station, on each side, four vertices: the outer side's foot (under the water), the rail's outer and inner top,
// and the inner wall's foot on the deck. Between two stations, on each side, the outer side, the rail's top and
// the inner wall, and the deck between the inner feet; the stern and the bow are closed the same way across.
// No bottom, which nothing stands on and the camera never sees from inside. The rail is RS_BOAT_RAIL_THICK wide
// and Link can stand on it, which is how he climbs aboard from the water (RsBoat_SwimReach) and steps off again
// (RsBoat_ClimbDelay, RsBoat_SteppedOffBoat). Wider than the drawn rail on purpose: Player looks for the ledge 10
// past the plane of the wall Link pushes into, from either side. At 10 wide the probe lands on the rail's far edge
// and misses it (it found the deck from the water, the lake from the deck); at 14 it lands 4 inside the top from
// both sides.
#define RS_BOAT_RAIL_THICK 14.0f
// A collision triangle at least this thick at its narrowest, or it is left out (RsBoat_AddTri).
#define RS_BOAT_MIN_ALTITUDE 4.0f
#define RS_BOAT_MAX_VTX (RS_BOAT_MAX_STATIONS * 8)
#define RS_BOAT_MAX_POLYS ((RS_BOAT_MAX_STATIONS - 1) * 14 + 12)
// The shore probes: the floor's edge and the gunwale's edge at every station, both sides, and the stern's middle.
#define RS_BOAT_MAX_POINTS (RS_BOAT_MAX_STATIONS * 2 + 1)
#define RS_BOAT_BOX_VTX 24

// A shore probe, boat space: the point, and the unit direction straight in from the outline there, which is the way
// a push moves the hull when what the point meets has no direction of its own.
typedef struct RsBoatProbe {
    f32 x;
    f32 z;
    f32 inX;
    f32 inZ;
} RsBoatProbe;

typedef struct RsBoatHull {
    s32 built;
    f32 sternZ;
    f32 bowZ;
    f32 halfWidth; // the widest the gunwale is
    f32 helmZ;     // where Link stands to steer
    s16 rollAmp;   // the rock's angles, binang: RS_BOAT_ROLL_DIP and RS_BOAT_PITCH_DIP at this hull's size
    s16 pitchAmp;
    s32 numKeel;
    RsBoatProbe keel[RS_BOAT_MAX_POINTS]; // the floor's edge
    s32 numOuter;
    RsBoatProbe outer[RS_BOAT_MAX_POINTS]; // the gunwale's edge
    Vec3s vtx[RS_BOAT_MAX_VTX];
    CollisionPoly polys[RS_BOAT_MAX_POLYS];
    s32 numPolys;
    CollisionHeader col;
    // The placeholder boxes (the issue allows a box until the RS model lands): the hull, a tiller post at the
    // stern so the helm is findable, and a prow at the bow so the heading reads.
    Vtx hullBox[RS_BOAT_BOX_VTX];
    Vtx tillerBox[RS_BOAT_BOX_VTX];
    Vtx prowBox[RS_BOAT_BOX_VTX];
} RsBoatHull;

static RsBoatHull sHulls[RS_BOAT_KIND_COUNT];

static SurfaceType sHullSurface[1] = {
    // wood footsteps (material 9, the bridge planks); everything else zero: a plain floor
    { { 0, 9 } },
};
// The camera reads the floor's camera data under Link, and dereferences the list without a null check
// (BgCheck_GetBgCamSettingImpl). CAM_SET_NONE is what makes it keep the scene's own setting.
static CamData sHullCamData[1] = { { CAM_SET_NONE, 0, NULL } };

s32 RsBoat_KindPolyCount(s32 kind) {
    return (kind >= 0 && kind < RS_BOAT_KIND_COUNT && sHulls[kind].built) ? sHulls[kind].numPolys : 0;
}

static void RsBoat_AddTri(RsBoatHull* hull, s32 a, s32 b, s32 c, f32 outX, f32 outY, f32 outZ) {
    CollisionPoly* poly = &hull->polys[hull->numPolys];
    Vec3s* va = &hull->vtx[a];
    Vec3s* vb = &hull->vtx[b];
    Vec3s* vc = &hull->vtx[c];
    f32 abx = (f32)(vb->x - va->x);
    f32 aby = (f32)(vb->y - va->y);
    f32 abz = (f32)(vb->z - va->z);
    f32 acx = (f32)(vc->x - va->x);
    f32 acy = (f32)(vc->y - va->y);
    f32 acz = (f32)(vc->z - va->z);
    // The engine's own convention, read off an exported scene: normal = (B - A) x (C - A).
    f32 nx = aby * acz - abz * acy;
    f32 ny = abz * acx - abx * acz;
    f32 nz = abx * acy - aby * acx;
    f32 len = sqrtf(nx * nx + ny * ny + nz * nz);

    // A sliver is left out. The engine moves the hull's collision into world space every tick and truncates each
    // vertex to whole units (DynaPoly_ExpandSRT), up to ~1.7 off in 3D; a triangle thinner than twice that can land
    // with no area, keep its unrotated normal and go in the wall list, where a flat one trips
    // BgCheck_SphVsDynaWallInBgActor's assert. The skiff's bow had them: the owner's crash on letting go of the helm.
    {
        f32 bcx = (f32)(vc->x - vb->x);
        f32 bcy = (f32)(vc->y - vb->y);
        f32 bcz = (f32)(vc->z - vb->z);
        f32 longest = MAX(MAX(sqrtf(abx * abx + aby * aby + abz * abz), sqrtf(acx * acx + acy * acy + acz * acz)),
                          sqrtf(bcx * bcx + bcy * bcy + bcz * bcz));

        if (longest < 0.5f || len / longest < RS_BOAT_MIN_ALTITUDE) {
            return;
        }
    }
    nx /= len;
    ny /= len;
    nz /= len;
    if (nx * outX + ny * outY + nz * outZ < 0.0f) {
        // wound the other way: swap B and C, which flips the normal
        s32 swap = b;

        b = c;
        c = swap;
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    poly->type = 0;
    poly->flags_vIA = COLPOLY_VTX(a, 0);
    poly->flags_vIB = COLPOLY_VTX(b, 0);
    poly->vIC = COLPOLY_VTX_INDEX(c);
    poly->normal.x = COLPOLY_SNORMAL(nx);
    poly->normal.y = COLPOLY_SNORMAL(ny);
    poly->normal.z = COLPOLY_SNORMAL(nz);
    poly->dist = (s16)lroundf(-(nx * va->x + ny * va->y + nz * va->z));
    hull->numPolys++;
}

// (a, b, c), (a, c, d), each facing `out`.
static void RsBoat_AddQuad(RsBoatHull* hull, s32 a, s32 b, s32 c, s32 d, f32 outX, f32 outY, f32 outZ) {
    RsBoat_AddTri(hull, a, b, c, outX, outY, outZ);
    RsBoat_AddTri(hull, a, c, d, outX, outY, outZ);
}

// Station i's vertices: side 0 is +x (the hull's right, looking at the bow), side 1 is -x.
#define RS_BOAT_V_FOOT 0
#define RS_BOAT_V_TOP 1
#define RS_BOAT_V_INNER_TOP 2
#define RS_BOAT_V_DECK 3
#define RS_BOAT_V(i, side, k) ((i) * 8 + (side) * 4 + (k))

static void RsBoat_SetVtx(Vtx* v, s16 x, s16 y, s16 z, s8 nx, s8 ny, s8 nz);
static void RsBoat_BuildBox(Vtx* out, s16 x0, s16 y0, s16 z0, s16 x1, s16 y1, s16 z1);

static void RsBoat_AddProbe(RsBoatProbe* probes, s32* count, f32 x, f32 z, f32 inX, f32 inZ) {
    const f32 len = sqrtf(inX * inX + inZ * inZ);
    RsBoatProbe* probe = &probes[(*count)++];

    probe->x = x;
    probe->z = z;
    probe->inX = len > 0.001f ? inX / len : 0.0f;
    probe->inZ = len > 0.001f ? inZ / len : 0.0f;
}

// One station's probes on one outline (the floor's or the gunwale's, half-width `hw`, and `prevHw` / `nextHw` at the
// stations either side). Straight in from a side is across the outline's run from the station before to the one
// after; at the stern and the bow it also leans along the hull, so a bow that meets something head-on is pushed back
// rather than sideways. Where the outline narrows to nothing there is one point, on the centreline.
static void RsBoat_AddStationProbes(RsBoatProbe* probes, s32* count, f32 z, f32 hw, f32 prevHw, f32 nextHw, f32 run,
                                    f32 endIn) {
    if (hw < 3.0f) {
        RsBoat_AddProbe(probes, count, 0.0f, z, 0.0f, endIn);
        return;
    }
    RsBoat_AddProbe(probes, count, hw, z, -run, nextHw - prevHw + endIn * run);
    RsBoat_AddProbe(probes, count, -hw, z, run, nextHw - prevHw + endIn * run);
}

static void RsBoat_BuildHull(s32 kindId) {
    const RsBoatKind* kind = &sRsBoatKinds[kindId];
    RsBoatHull* hull = &sHulls[kindId];
    const s32 n = kind->numStations;
    const f32 halfLength = (kind->stations[n - 1].z - kind->stations[0].z) * 0.5f;
    CollisionHeader* col = &hull->col;
    s32 i;
    s32 side;

    hull->sternZ = kind->stations[0].z;
    hull->bowZ = kind->stations[n - 1].z;
    hull->helmZ = hull->sternZ + RS_BOAT_HELM_FROM_STERN;
    hull->halfWidth = 0.0f;
    hull->numKeel = 0;
    hull->numOuter = 0;
    hull->numPolys = 0;

    for (i = 0; i < n; i++) {
        const RsBoatStation* st = &kind->stations[i];
        const f32 innerHw = MAX(st->outerHw - RS_BOAT_RAIL_THICK, st->outerHw * 0.5f);
        // The rail's inner edge steps in from the stern and the bow too, so the ends have a rail top of their own -
        // but never past the next station in: at the skiff's bow (112, the one before at 99) a full step crossed
        // it and folded the deck and rail top into slivers (RS_BOAT_MIN_ALTITUDE). 8 short, not 4: a 4-deep strip of
        // the raft's deck fell under the thinness limit and left a gap Link could drop through.
        const f32 innerZ = (i == 0)       ? MIN(st->z + RS_BOAT_RAIL_THICK, kind->stations[1].z - 8.0f)
                           : (i == n - 1) ? MAX(st->z - RS_BOAT_RAIL_THICK, kind->stations[n - 2].z + 8.0f)
                                          : st->z;
        const s16 railTop = (s16)lroundf(kind->deckY + st->railY);

        hull->halfWidth = MAX(hull->halfWidth, st->outerHw);
        for (side = 0; side < 2; side++) {
            const f32 sx = side == 0 ? 1.0f : -1.0f;
            Vec3s* v = &hull->vtx[RS_BOAT_V(i, side, 0)];

            v[RS_BOAT_V_FOOT].x = (s16)lroundf(sx * st->outerHw);
            v[RS_BOAT_V_FOOT].y = (s16)RS_BOAT_SIDE_FOOT_Y;
            v[RS_BOAT_V_FOOT].z = (s16)lroundf(st->z);
            v[RS_BOAT_V_TOP] = v[RS_BOAT_V_FOOT];
            v[RS_BOAT_V_TOP].y = railTop;
            v[RS_BOAT_V_INNER_TOP].x = (s16)lroundf(sx * innerHw);
            v[RS_BOAT_V_INNER_TOP].y = railTop;
            v[RS_BOAT_V_INNER_TOP].z = (s16)lroundf(innerZ);
            v[RS_BOAT_V_DECK] = v[RS_BOAT_V_INNER_TOP];
            v[RS_BOAT_V_DECK].y = (s16)lroundf(kind->deckY);
        }
    }
    for (i = 0; i < n; i++) {
        const RsBoatStation* st = &kind->stations[i];
        const RsBoatStation* prev = &kind->stations[MAX(i - 1, 0)];
        const RsBoatStation* next = &kind->stations[MIN(i + 1, n - 1)];
        // In from the stern is ahead and in from the bow is astern; the stations between have no end of their own.
        const f32 endIn = (i == 0) ? 1.0f : (i == n - 1) ? -1.0f : 0.0f;

        RsBoat_AddStationProbes(hull->keel, &hull->numKeel, st->z, st->floorHw, prev->floorHw, next->floorHw,
                                next->z - prev->z, endIn);
        RsBoat_AddStationProbes(hull->outer, &hull->numOuter, st->z, st->outerHw, prev->outerHw, next->outerHw,
                                next->z - prev->z, endIn);
    }
    // The stern is flat across: its middle is as far aft as its corners, and in from it is ahead.
    RsBoat_AddProbe(hull->keel, &hull->numKeel, 0.0f, hull->sternZ, 0.0f, 1.0f);
    RsBoat_AddProbe(hull->outer, &hull->numOuter, 0.0f, hull->sternZ, 0.0f, 1.0f);

    for (i = 0; i + 1 < n; i++) {
        for (side = 0; side < 2; side++) {
            const f32 sx = side == 0 ? 1.0f : -1.0f;

            RsBoat_AddQuad(hull, RS_BOAT_V(i, side, RS_BOAT_V_FOOT), RS_BOAT_V(i, side, RS_BOAT_V_TOP),
                           RS_BOAT_V(i + 1, side, RS_BOAT_V_TOP), RS_BOAT_V(i + 1, side, RS_BOAT_V_FOOT), sx, 0.0f,
                           0.0f);
            RsBoat_AddQuad(hull, RS_BOAT_V(i, side, RS_BOAT_V_TOP), RS_BOAT_V(i, side, RS_BOAT_V_INNER_TOP),
                           RS_BOAT_V(i + 1, side, RS_BOAT_V_INNER_TOP), RS_BOAT_V(i + 1, side, RS_BOAT_V_TOP), 0.0f,
                           1.0f, 0.0f);
            RsBoat_AddQuad(hull, RS_BOAT_V(i, side, RS_BOAT_V_INNER_TOP), RS_BOAT_V(i, side, RS_BOAT_V_DECK),
                           RS_BOAT_V(i + 1, side, RS_BOAT_V_DECK), RS_BOAT_V(i + 1, side, RS_BOAT_V_INNER_TOP), -sx,
                           0.0f, 0.0f);
        }
        RsBoat_AddQuad(hull, RS_BOAT_V(i, 0, RS_BOAT_V_DECK), RS_BOAT_V(i, 1, RS_BOAT_V_DECK),
                       RS_BOAT_V(i + 1, 1, RS_BOAT_V_DECK), RS_BOAT_V(i + 1, 0, RS_BOAT_V_DECK), 0.0f, 1.0f, 0.0f);
    }
    // The stern (station 0, facing -z) and the bow (the last, facing +z), closed across.
    for (side = 0; side < 2; side++) {
        const s32 at = side == 0 ? 0 : n - 1;
        const f32 sz = side == 0 ? -1.0f : 1.0f;

        RsBoat_AddQuad(hull, RS_BOAT_V(at, 0, RS_BOAT_V_FOOT), RS_BOAT_V(at, 0, RS_BOAT_V_TOP),
                       RS_BOAT_V(at, 1, RS_BOAT_V_TOP), RS_BOAT_V(at, 1, RS_BOAT_V_FOOT), 0.0f, 0.0f, sz);
        RsBoat_AddQuad(hull, RS_BOAT_V(at, 0, RS_BOAT_V_TOP), RS_BOAT_V(at, 1, RS_BOAT_V_TOP),
                       RS_BOAT_V(at, 1, RS_BOAT_V_INNER_TOP), RS_BOAT_V(at, 0, RS_BOAT_V_INNER_TOP), 0.0f, 1.0f,
                       0.0f);
        RsBoat_AddQuad(hull, RS_BOAT_V(at, 0, RS_BOAT_V_INNER_TOP), RS_BOAT_V(at, 1, RS_BOAT_V_INNER_TOP),
                       RS_BOAT_V(at, 1, RS_BOAT_V_DECK), RS_BOAT_V(at, 0, RS_BOAT_V_DECK), 0.0f, 0.0f, -sz);
    }

    col->minBounds.x = (s16)-lroundf(hull->halfWidth);
    col->minBounds.y = (s16)RS_BOAT_SIDE_FOOT_Y;
    col->minBounds.z = (s16)lroundf(hull->sternZ);
    col->maxBounds.x = (s16)lroundf(hull->halfWidth);
    col->maxBounds.y = (s16)0;
    col->maxBounds.z = (s16)lroundf(hull->bowZ);
    for (i = 0; i < n * 8; i++) {
        col->maxBounds.y = MAX(col->maxBounds.y, hull->vtx[i].y);
    }
    col->numVertices = (u32)(n * 8);
    col->vtxList = hull->vtx;
    col->numPolygons = (u32)hull->numPolys;
    col->polyList = hull->polys;
    col->surfaceTypeList = sHullSurface;
    col->cameraDataList = sHullCamData;
    col->numWaterBoxes = 0;
    col->waterBoxes = NULL;
    col->cameraDataListLen = (size_t)ARRAY_COUNT(sHullCamData);

    hull->rollAmp = (s16)(atan2f(RS_BOAT_ROLL_DIP, hull->halfWidth) * (32768.0f / (f32)M_PI));
    hull->pitchAmp = (s16)(atan2f(RS_BOAT_PITCH_DIP, halfLength) * (32768.0f / (f32)M_PI));

    {
        const s16 w = (s16)lroundf(hull->halfWidth);
        const s16 stern = (s16)lroundf(hull->sternZ);
        const s16 bow = (s16)lroundf(hull->bowZ);
        const s16 deck = (s16)lroundf(kind->deckY);

        RsBoat_BuildBox(hull->hullBox, (s16)-w, (s16)RS_BOAT_KEEL_Y, stern, w, deck, bow);
        // the tiller post: just inboard of the stern, behind where Link stands to steer, and low, because the
        // camera sits behind Link at the helm and a tall post fills the frame. Not on the stern itself: its back
        // face on the transom's z-fought with it.
        RsBoat_BuildBox(hull->tillerBox, -4, deck, (s16)(stern + 8), 4, (s16)(deck + 16), (s16)(stern + 14));
        // the prow: a low fin along the bow's last stretch
        RsBoat_BuildBox(hull->prowBox, -5, deck, (s16)(bow - 40), 5, (s16)(deck + 14), bow);
    }
    hull->built = 1;
}

// --- the hull: drawing --------------------------------------------------------------------------
//
// The placeholder boxes are lit by the scene's lights through per-face normals; colour is the primitive colour,
// one per part.
static void RsBoat_SetVtx(Vtx* v, s16 x, s16 y, s16 z, s8 nx, s8 ny, s8 nz) {
    v->n.ob[0] = x;
    v->n.ob[1] = y;
    v->n.ob[2] = z;
    v->n.flag = 0;
    v->n.tc[0] = 0;
    v->n.tc[1] = 0;
    v->n.n[0] = nx;
    v->n.n[1] = ny;
    v->n.n[2] = nz;
    v->n.a = 255;
}

// Six faces of four vertices, in the order the draw expects: top, bottom, +x, -x, +z, -z. Each face's four
// run round its edge, so (0,1,2),(0,2,3) covers it. Back-face culling is off for these, so the winding is free.
static void RsBoat_BuildBox(Vtx* out, s16 x0, s16 y0, s16 z0, s16 x1, s16 y1, s16 z1) {
    RsBoat_SetVtx(&out[0], x0, y1, z0, 0, 127, 0);
    RsBoat_SetVtx(&out[1], x1, y1, z0, 0, 127, 0);
    RsBoat_SetVtx(&out[2], x1, y1, z1, 0, 127, 0);
    RsBoat_SetVtx(&out[3], x0, y1, z1, 0, 127, 0);
    RsBoat_SetVtx(&out[4], x0, y0, z0, 0, -127, 0);
    RsBoat_SetVtx(&out[5], x1, y0, z0, 0, -127, 0);
    RsBoat_SetVtx(&out[6], x1, y0, z1, 0, -127, 0);
    RsBoat_SetVtx(&out[7], x0, y0, z1, 0, -127, 0);
    RsBoat_SetVtx(&out[8], x1, y0, z0, 127, 0, 0);
    RsBoat_SetVtx(&out[9], x1, y1, z0, 127, 0, 0);
    RsBoat_SetVtx(&out[10], x1, y1, z1, 127, 0, 0);
    RsBoat_SetVtx(&out[11], x1, y0, z1, 127, 0, 0);
    RsBoat_SetVtx(&out[12], x0, y0, z0, -127, 0, 0);
    RsBoat_SetVtx(&out[13], x0, y1, z0, -127, 0, 0);
    RsBoat_SetVtx(&out[14], x0, y1, z1, -127, 0, 0);
    RsBoat_SetVtx(&out[15], x0, y0, z1, -127, 0, 0);
    RsBoat_SetVtx(&out[16], x0, y0, z1, 0, 0, 127);
    RsBoat_SetVtx(&out[17], x1, y0, z1, 0, 0, 127);
    RsBoat_SetVtx(&out[18], x1, y1, z1, 0, 0, 127);
    RsBoat_SetVtx(&out[19], x0, y1, z1, 0, 0, 127);
    RsBoat_SetVtx(&out[20], x0, y0, z0, 0, 0, -127);
    RsBoat_SetVtx(&out[21], x1, y0, z0, 0, 0, -127);
    RsBoat_SetVtx(&out[22], x1, y1, z0, 0, 0, -127);
    RsBoat_SetVtx(&out[23], x0, y1, z0, 0, 0, -127);
}

// --- markers ------------------------------------------------------------------------------------

static void RsBoat_Mark(RsBoat* this, const char* event) {
    char line[200];

    snprintf(line, sizeof(line), "rs_boat event=%s x=%d y=%d z=%d yaw=%d speed=%d helm=%d blocked=%s surface=%d",
             event, (int)lroundf(this->dyna.actor.world.pos.x), (int)lroundf(this->dyna.actor.world.pos.y),
             (int)lroundf(this->dyna.actor.world.pos.z), (int)(u16)this->dyna.actor.shape.rot.y,
             (int)lroundf(this->speed * 100.0f), this->helm, RsBoat_BlockName(this->blocked),
             (int)lroundf(this->surfaceY));
    RsAgent_Marker(line);
}

const char* RsBoat_BlockName(s32 block) {
    switch (block) {
        case RS_BOAT_BLOCK_NO_WATER:
            return "no_water";
        case RS_BOAT_BLOCK_SHALLOW:
            return "shallow";
        case RS_BOAT_BLOCK_WALL:
            return "wall";
        case RS_BOAT_BLOCK_HEADROOM:
            return "headroom";
        default:
            return "none";
    }
}

// --- geometry helpers ---------------------------------------------------------------------------

// A point on the hull, in world space, for a hull at (x, z) turned to `yaw`. Local +z is the bow.
static void RsBoat_LocalToWorld(f32 x, f32 z, s16 yaw, f32 lx, f32 lz, f32* outX, f32* outZ) {
    f32 s = Math_SinS(yaw);
    f32 c = Math_CosS(yaw);

    *outX = x + lx * c + lz * s;
    *outZ = z - lx * s + lz * c;
}

void RsBoat_HelmPos(const RsBoat* boat, Vec3f* out) {
    RsBoat_LocalToWorld(boat->dyna.actor.world.pos.x, boat->dyna.actor.world.pos.z, boat->dyna.actor.shape.rot.y,
                        0.0f, sHulls[boat->kind].helmZ, &out->x, &out->z);
    out->y = boat->dyna.actor.world.pos.y + RsBoat_DeckY(boat);
}

// A waterbox's surface is open water at (x, z) unless it is buried: Lake Hylia, drained, lowers its main box
// under the lakebed rather than switching it off (Bg_Spot06_Objects), and a boat on that box sails under the
// map. Looking straight up from the surface, ground over a buried box is met from BELOW, as the underside of
// an up-facing floor; open water meets sky, or a ceiling (a bridge's underside), first. A raycast down from a
// fixed height cannot tell the two apart: start it low and it begins inside a high bank, start it high and a
// bridge reads as ground. `skip` is the boat's own collision, which sits over the water by design.
#define RS_BOAT_BURIED_LOOK 4000.0f

static s32 RsBoat_SurfaceIsOpen(PlayState* play, f32 x, f32 z, f32 surface, Actor* skip) {
    Vec3f from;
    Vec3f to;
    Vec3f hit;
    CollisionPoly* poly = NULL;
    s32 bgId = BGCHECK_SCENE;

    from.x = x;
    from.y = surface + 1.0f;
    from.z = z;
    to = from;
    to.y = surface + RS_BOAT_BURIED_LOOK;
    if (!BgCheck_EntityLineTest2(&play->colCtx, &from, &to, &hit, &poly, false, true, true, false, &bgId, skip)) {
        return 1;
    }
    // Any boat's deck over the water is not ground burying it: `boat kind` puts a new boat where the old one is still
    // in the collision this tick, and the old deck read as ground and refused the spawn.
    if (bgId != BGCHECK_SCENE) {
        // Asked of the collision itself, so a boat killed this tick (its update already cleared) still counts.
        DynaPolyActor* owner = DynaPoly_GetActor(&play->colCtx, bgId);

        if (owner != NULL && owner->actor.id == ACTOR_RS_BOAT) {
            return 1;
        }
    }
    return poly == NULL || COLPOLY_GET_NORMAL(poly->normal.y) < 0.0f;
}

// The surface at (x, z) as RsBoat_SurfaceAt finds it, and which box it is (-1: none, or Zora's Domain's own).
static s32 RsBoat_SurfaceBox(PlayState* play, f32 x, f32 z, f32* outY, Actor* skip, s32* outBox) {
    CollisionHeader* header = play->colCtx.colHeader;
    s32 found = 0;
    f32 best = 0.0f;
    u32 i;

    *outBox = -1;
    if (play->sceneNum == SCENE_ZORAS_DOMAIN) {
        // Zora's Domain has a waterbox of its own outside the list (WaterBox_GetSurface1); no boat goes there.
        WaterBox* box = NULL;
        f32 y = *outY;

        if (WaterBox_GetSurface1(play, &play->colCtx, x, z, &y, &box) && box != NULL) {
            *outY = y;
            return 1;
        }
        return 0;
    }
    // WaterBox_GetSurfaceImpl's own filters (its room, not switched off, strictly inside), but every box that
    // passes is a candidate rather than only the first: the highest open one wins.
    if (header->numWaterBoxes == 0 || header->waterBoxes == PHYSICAL_TO_VIRTUAL(gSegments[0])) {
        return 0;
    }
    for (i = 0; i < header->numWaterBoxes; i++) {
        WaterBox* box = &header->waterBoxes[i];
        u32 room = (box->properties >> 13) & 0x3F;
        f32 surface = (f32)box->ySurface;

        if ((room != (u32)play->roomCtx.curRoom.num && room != 0x3F) || (box->properties & 0x80000) != 0) {
            continue;
        }
        if (!(box->xMin < x && x < box->xMin + box->xLength && box->zMin < z && z < box->zMin + box->zLength)) {
            continue;
        }
        if ((!found || surface > best) && RsBoat_SurfaceIsOpen(play, x, z, surface, skip)) {
            best = surface;
            found = 1;
            *outBox = (s32)i;
        }
    }
    if (found) {
        *outY = best;
    }
    return found;
}

s32 RsBoat_SurfaceAt(PlayState* play, f32 x, f32 z, f32* outY, Actor* skip) {
    s32 box;

    return RsBoat_SurfaceBox(play, x, z, outY, skip, &box);
}

// A hull point is on the boat's own water: inside the box under the hull's centre (the cheap case, nearly every
// point every tick), or over another open box at the same height. Ground over the water inside the box - a bank
// overhanging it, a dock - is the floor test's to find, not this one's.
// (x, z) is inside the box the boat floats in, still switched on: once afloat in a box, the whole of it is water.
// The buried test (RsBoat_SurfaceIsOpen) only picks a box to start in - by Lake Hylia's west wall it looks up at a
// cliff's ground overhanging deep water, calls the water buried, and the boat sank to the lakebed.
static s32 RsBoat_InOwnBox(const RsBoat* this, PlayState* play, f32 x, f32 z) {
    CollisionHeader* header = play->colCtx.colHeader;
    WaterBox* own;

    if (this->waterBox < 0 || (u32)this->waterBox >= header->numWaterBoxes) {
        return 0;
    }
    own = &header->waterBoxes[this->waterBox];
    return (own->properties & 0x80000) == 0 && own->xMin < x && x < own->xMin + own->xLength && own->zMin < z &&
           z < own->zMin + own->zLength;
}

static s32 RsBoat_WaterAt(RsBoat* this, PlayState* play, f32 x, f32 z) {
    f32 y = this->surfaceY;
    s32 box;

    if (RsBoat_InOwnBox(this, play, x, z)) {
        return 1;
    }
    return RsBoat_SurfaceBox(play, x, z, &y, &this->dyna.actor, &box) &&
           fabsf(y - this->surfaceY) <= RS_BOAT_SURFACE_TOLERANCE;
}

// --- the shore ----------------------------------------------------------------------------------
//
// A hull pose is measured, not just passed or failed: every outline point in trouble says how far the hull must
// move to clear it, and which way. The deepest one is what a push follows; the sum is what "no deeper in trouble
// than before" compares.
typedef struct RsBoatContact {
    s32 reason; // RsBoatBlock of the deepest trouble, NONE when the pose fits
    f32 depth;  // how far the hull must move to clear it, world units
    f32 pushX;  // the unit direction that clears it
    f32 pushZ;
    f32 total; // every point's depth, summed
} RsBoatContact;

// While `boat contact` asks, every point in trouble is written here as it is noted (RsBoat_ContactReport).
static char* sContactReport = NULL;
static size_t sContactReportSize = 0;

static void RsBoat_Note(RsBoatContact* c, s32 reason, f32 depth, f32 dirX, f32 dirZ) {
    const f32 len = sqrtf(dirX * dirX + dirZ * dirZ);

    if (sContactReport != NULL) {
        size_t used = strlen(sContactReport);

        if (used + 1 < sContactReportSize) {
            snprintf(sContactReport + used, sContactReportSize - used, " %s:%d@%d,%d", RsBoat_BlockName(reason),
                     (int)lroundf(depth), (int)lroundf(dirX * 100.0f / MAX(len, 0.001f)),
                     (int)lroundf(dirZ * 100.0f / MAX(len, 0.001f)));
        }
    }
    c->total += depth;
    if (c->reason != RS_BOAT_BLOCK_NONE && depth <= c->depth) {
        return;
    }
    c->reason = reason;
    c->depth = depth;
    c->pushX = len > 0.001f ? dirX / len : 0.0f;
    c->pushZ = len > 0.001f ? dirZ / len : 0.0f;
}

// Ground `rise` too high under a point. On a slope its normal leans away from the land, so its level part points
// down the slope, and the run that lowers the ground by `rise` is rise * ny / |n.xz|. Flat ground (a dock, a
// shelf, a bank's top) has no downhill: the hull steps straight in from the point, (inX, inZ).
static void RsBoat_NoteGround(RsBoatContact* c, s32 reason, f32 rise, const CollisionPoly* poly, f32 inX,
                              f32 inZ) {
    f32 nx = 0.0f;
    f32 ny = 1.0f;
    f32 nz = 0.0f;
    f32 flat;

    if (poly != NULL) {
        nx = COLPOLY_GET_NORMAL(poly->normal.x);
        ny = COLPOLY_GET_NORMAL(poly->normal.y);
        nz = COLPOLY_GET_NORMAL(poly->normal.z);
    }
    flat = sqrtf(nx * nx + nz * nz);
    if (flat > 0.2f && ny > 0.1f) {
        RsBoat_Note(c, reason, CLAMP(rise * ny / flat, 1.0f, RS_BOAT_PUSH_MAX), nx, nz);
    } else {
        RsBoat_Note(c, reason, CLAMP(rise, 1.0f, RS_BOAT_STEP_OUT), inX, inZ);
    }
}

// A wall between the hull's centre (cx, cz) and an outline point: the point is behind it by its distance along
// the wall's normal, turned to face the centre's side (a line test meets a wall from either side). A wall with no
// level normal (a sloped ceiling's edge) is stepped straight in from, (inX, inZ).
static void RsBoat_NoteWall(RsBoatContact* c, Vec3f* hit, const CollisionPoly* poly, Vec3f* point, f32 cx, f32 cz,
                            f32 inX, f32 inZ) {
    f32 nx = 0.0f;
    f32 nz = 0.0f;
    f32 flat = 0.0f;

    if (poly != NULL) {
        nx = COLPOLY_GET_NORMAL(poly->normal.x);
        nz = COLPOLY_GET_NORMAL(poly->normal.z);
        flat = sqrtf(nx * nx + nz * nz);
    }
    if (flat < 0.2f) {
        RsBoat_Note(c, RS_BOAT_BLOCK_WALL, Math_Vec3f_DistXZ(hit, point) + 1.0f, inX, inZ);
        return;
    }
    nx /= flat;
    nz /= flat;
    if (nx * (cx - hit->x) + nz * (cz - hit->z) < 0.0f) {
        nx = -nx;
        nz = -nz;
    }
    RsBoat_Note(c, RS_BOAT_BLOCK_WALL, (hit->x - point->x) * nx + (hit->z - point->z) * nz + 1.0f, nx, nz);
}

// A hull at (x, z, yaw) on the boat's water. The floor's edge (the narrowest outline, where the hull sits on the
// water): over that water, over a floor deeper than the draft, with nothing lower than Link's head above the
// deck. The gunwale's edge (the widest): no ground rising into the side, and no wall between the centre and it,
// at the waterline or at Link's waist.
static void RsBoat_Evaluate(RsBoat* this, PlayState* play, f32 x, f32 z, s16 yaw, RsBoatContact* c) {
    const RsBoatHull* hull = &sHulls[this->kind];
    const f32 surface = this->surfaceY;
    const f32 deckY = RsBoat_DeckY(this);
    Actor* self = &this->dyna.actor;
    s32 i;

    c->reason = RS_BOAT_BLOCK_NONE;
    c->depth = 0.0f;
    c->pushX = 0.0f;
    c->pushZ = 0.0f;
    c->total = 0.0f;

    for (i = 0; i < hull->numKeel; i++) {
        Vec3f point;
        Vec3f from;
        Vec3f hit;
        CollisionPoly* poly = NULL;
        s32 bgId = BGCHECK_SCENE;
        f32 floorY;
        f32 inX;
        f32 inZ;

        RsBoat_LocalToWorld(x, z, yaw, hull->keel[i].x, hull->keel[i].z, &point.x, &point.z);
        RsBoat_LocalToWorld(0.0f, 0.0f, yaw, hull->keel[i].inX, hull->keel[i].inZ, &inX, &inZ);
        // Down from above Link's head on the deck, skipping the boat's own collision: a bank, a dock or a rock
        // above the water reads as a floor above the surface, which is shallower than any draft. Starting
        // lower missed a bank that rises steeply: the raycast began inside it and found the bed below.
        point.y = surface + deckY + RS_BOAT_HEADROOM;
        floorY = BgCheck_EntityRaycastFloor4(&play->colCtx, &poly, &bgId, self, &point);
        if (floorY > surface - RS_BOAT_DRAFT) {
            RsBoat_NoteGround(c, RS_BOAT_BLOCK_SHALLOW, floorY - (surface - RS_BOAT_DRAFT), poly, inX, inZ);
            continue;
        }
        if (!RsBoat_WaterAt(this, play, point.x, point.z)) {
            RsBoat_Note(c, RS_BOAT_BLOCK_NO_WATER, RS_BOAT_STEP_OUT, inX, inZ);
            continue;
        }
        // Straight up from the deck at the point: anything in the way is lower than Link's head.
        from = point;
        from.y = surface + deckY + 2.0f;
        if (BgCheck_EntityLineTest2(&play->colCtx, &from, &point, &hit, &poly, true, true, true, false, &bgId,
                                    self)) {
            RsBoat_Note(c, RS_BOAT_BLOCK_HEADROOM, RS_BOAT_STEP_OUT, inX, inZ);
        }
    }
    for (i = 0; i < hull->numOuter; i++) {
        Vec3f point;
        Vec3f center;
        Vec3f hit;
        CollisionPoly* poly = NULL;
        s32 bgId = BGCHECK_SCENE;
        f32 floorY;
        f32 inX;
        f32 inZ;

        RsBoat_LocalToWorld(x, z, yaw, hull->outer[i].x, hull->outer[i].z, &point.x, &point.z);
        RsBoat_LocalToWorld(0.0f, 0.0f, yaw, hull->outer[i].inX, hull->outer[i].inZ, &inX, &inZ);
        point.y = surface + deckY + RS_BOAT_HEADROOM;
        floorY = BgCheck_EntityRaycastFloor4(&play->colCtx, &poly, &bgId, self, &point);
        if (floorY > surface + deckY + RS_BOAT_SIDE_CLEAR) {
            RsBoat_NoteGround(c, RS_BOAT_BLOCK_SHALLOW, floorY - (surface + deckY + RS_BOAT_SIDE_CLEAR),
                              poly, inX, inZ);
            continue;
        }
        center.x = x;
        center.y = surface + RS_BOAT_PROBE_Y;
        center.z = z;
        point.y = center.y;
        if (BgCheck_EntityLineTest2(&play->colCtx, &center, &point, &hit, &poly, true, false, false, false, &bgId,
                                    self)) {
            RsBoat_NoteWall(c, &hit, poly, &point, x, z, inX, inZ);
            continue;
        }
        center.y = surface + deckY + RS_BOAT_WAIST_Y;
        point.y = center.y;
        if (BgCheck_EntityLineTest2(&play->colCtx, &center, &point, &hit, &poly, true, false, false, false, &bgId,
                                    self)) {
            RsBoat_NoteWall(c, &hit, poly, &point, x, z, inX, inZ);
        }
    }
}

// Pushes a hull at (*x, *z, yaw) out of the shore: up to RS_BOAT_PUSH_TRIES times, the deepest trouble's way,
// at most RS_BOAT_PUSH_MAX in all. 1 when it ends clear. Either way (*x, *z) is where it ended and `*total` how
// deep in trouble it still is there; `*first` is what the first push met (NONE when the pose fitted as it was).
static s32 RsBoat_Settle(RsBoat* this, PlayState* play, f32* x, f32* z, s16 yaw, s32* first, f32* total) {
    RsBoatContact c;
    f32 pushed = 0.0f;
    s32 tries;

    *first = RS_BOAT_BLOCK_NONE;
    for (tries = 0;; tries++) {
        f32 step;

        RsBoat_Evaluate(this, play, *x, *z, yaw, &c);
        *total = c.total;
        if (c.reason == RS_BOAT_BLOCK_NONE) {
            return 1;
        }
        if (tries == 0) {
            *first = c.reason;
        }
        if (tries == RS_BOAT_PUSH_TRIES || pushed >= RS_BOAT_PUSH_MAX) {
            return 0;
        }
        step = MIN(c.depth, RS_BOAT_PUSH_MAX - pushed);
        *x += c.pushX * step;
        *z += c.pushZ * step;
        pushed += step;
    }
}

s32 RsBoat_ContactReport(RsBoat* this, PlayState* play, char* out, s32 size) {
    RsBoatContact c;
    s32 written;

    written = snprintf(out, (size_t)size, "points=");
    if (written >= 0 && written + 1 < size) {
        sContactReport = out + written;
        sContactReportSize = (size_t)(size - written);
    }
    RsBoat_Evaluate(this, play, this->dyna.actor.world.pos.x, this->dyna.actor.world.pos.z,
                    this->dyna.actor.shape.rot.y, &c);
    sContactReport = NULL;
    return c.reason;
}

// A pose that fits, or is less deep in trouble than `than` (the hull's own, now): the way off a shore it is
// already touching. Not "no deeper": a depth with no slope to measure is capped (RS_BOAT_STEP_OUT), so a hull
// driving further onto a flat shelf or off the water reads the same until the next probe meets it, and an equal
// pose let it creep on at full speed.
static s32 RsBoat_Improves(RsBoat* this, PlayState* play, f32 x, f32 z, s16 yaw, f32 than) {
    RsBoatContact c;

    RsBoat_Evaluate(this, play, x, z, yaw, &c);
    return c.reason == RS_BOAT_BLOCK_NONE || c.total < than - 0.01f;
}

// --- the helm -----------------------------------------------------------------------------------

s32 RsBoat_LinkOnDeck(const RsBoat* this, const Player* player) {
    return this->dyna.bgId >= 0 && player->actor.floorBgId == (u8)this->dyna.bgId &&
           (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
}

void RsBoat_StandAtHelm(const RsBoat* this, Player* player, f32 lift) {
    Vec3f helm;

    RsBoat_HelmPos(this, &helm);
    player->actor.world.pos.x = helm.x;
    player->actor.world.pos.z = helm.z;
    player->actor.world.pos.y = (lift > 0.0f) ? helm.y + lift : player->actor.world.pos.y;
    // agenttest goto's recipe: prevPos and home.pos too, or the wall sweep clamps him back where he was, and
    // fallStartHeight so a drop onto the deck does not read as a fall.
    player->actor.prevPos = player->actor.world.pos;
    player->actor.home.pos = player->actor.world.pos;
    player->fallStartHeight = (s16)player->actor.world.pos.y;
    player->actor.shape.rot.y = this->dyna.actor.shape.rot.y;
    player->actor.world.rot.y = this->dyna.actor.shape.rot.y;
    player->yaw = this->dyna.actor.shape.rot.y;
    player->linearVelocity = 0.0f;
    player->actor.speedXZ = 0.0f;
}

s32 RsBoat_LinkCanTakeHelm(const Player* player) {
    const u32 busy = PLAYER_STATE1_HOOKSHOT_FALLING | PLAYER_STATE1_ITEM_IN_HAND | PLAYER_STATE1_INPUT_DISABLED |
                     PLAYER_STATE1_TALKING | PLAYER_STATE1_DEAD | PLAYER_STATE1_START_CHANGING_HELD_ITEM |
                     PLAYER_STATE1_READY_TO_FIRE | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_CARRYING_ACTOR |
                     PLAYER_STATE1_CHARGING_SPIN_ATTACK | PLAYER_STATE1_HANGING_OFF_LEDGE |
                     PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_JUMPING | PLAYER_STATE1_FREEFALL |
                     PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_SHIELDING |
                     PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_USING_BOOMERANG | PLAYER_STATE1_DAMAGED |
                     PLAYER_STATE1_IN_WATER | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE;

    // unk_6AD is Player's camera mode: 0 is the normal third-person view; 1 and 2 are first person, with
    // and without an item drawn. The bow's zoom is 2, and A at the tiller while it was up locked Link into it.
    // heldItemAction is what is in his hand: a drawn sword too, when A is "Put Away" and vanilla's sheathe
    // should win - the next A then takes the helm, rather than Link steering in his sword stance.
    return (player->stateFlags1 & busy) == 0 && player->unk_6AD == 0 && player->heldActor == NULL &&
           player->talkActor == NULL && player->heldItemAction == PLAYER_IA_NONE;
}

// Link could take this boat's helm with A now: on its deck, free to, and either within reach of the tiller or
// locked on to it with Z, which works from anywhere on the skiff's deck (its bow is ~200 from the tiller) and the
// sloop's stern half (RsBoat_Init's targetMode).
static s32 RsBoat_TillerReachable(const RsBoat* this, const Player* player) {
    Vec3f helm;

    if (this->helm || !RsBoat_LinkOnDeck(this, player) || !RsBoat_LinkCanTakeHelm(player)) {
        return 0;
    }
    if (player->focusActor == &this->dyna.actor) {
        return 1;
    }
    RsBoat_HelmPos(this, &helm);
    return Math_Vec3f_DistXZ(&helm, (Vec3f*)&player->actor.world.pos) < RS_BOAT_HELM_RANGE;
}

s32 RsBoat_TillerInReach(PlayState* play) {
    const Player* player = GET_PLAYER(play);
    Actor* a;

    if (Message_GetState(&play->msgCtx) != TEXT_STATE_NONE) {
        return 0;
    }
    for (a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_RS_BOAT && a->update != NULL && RsBoat_TillerReachable((const RsBoat*)a, player)) {
            return 1;
        }
    }
    return 0;
}

// The first live boat `test` holds for, or NULL.
static const RsBoat* RsBoat_Find(PlayState* play, s32 (*test)(const RsBoat*)) {
    Actor* a;

    for (a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_RS_BOAT && a->update != NULL && test((const RsBoat*)a)) {
            return (const RsBoat*)a;
        }
    }
    return NULL;
}

static s32 RsBoat_HelmHeld(const RsBoat* boat) {
    return boat->helm;
}

static s32 RsBoat_LinkWasOn(const RsBoat* boat) {
    return boat->linkAboard;
}

s32 RsBoat_LinkAtHelm(PlayState* play) {
    return RsBoat_Find(play, RsBoat_HelmHeld) != NULL;
}

s32 RsBoat_LinkWasAboard(PlayState* play) {
    return RsBoat_Find(play, RsBoat_LinkWasOn) != NULL;
}

// A swimmer reaches this much higher against a hull than against a bank. Vanilla's climb-out takes a ledge at most
// 70 over him (sAgeProperties unk_10, adult) and he floats ~46 under the surface, so ~24 over the water; the
// skiff's rail is 26 over it and the sloop's 36, plus the bob. 20 more covers both, with the sloop's raised bow.
#define RS_BOAT_SWIM_REACH 20.0f
// Pushing into a hull's inner wall this many ticks before Link hops onto its rail: 0.6 s, a run at the side on
// purpose rather than brushing past it (the far side then hops him off, RsBoat_SteppedOffBoat). The boat counts it
// itself (RsBoat_CountRailPush), because Player's own count cannot: against a hull, the wall-contact flag drops
// every other tick and each drop resets Player's ledgeClimbDelayTimer, so vanilla's 3-tick hop up a ledge under 41
// came only at angles that held contact.
#define RS_BOAT_RAIL_CLIMB_DELAY 12
#define RS_BOAT_RAIL_PUSH_GRACE 2    // ticks without contact a push survives
#define RS_BOAT_RAIL_PUSH_STICK 40.0f // stick travel that is pushing (it reads -85..85)

static s32 RsBoat_IsHullBg(PlayState* play, s32 bgId) {
    Actor* a;

    for (a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_RS_BOAT && a->update != NULL && ((const RsBoat*)a)->dyna.bgId >= 0 &&
            (u8)((const RsBoat*)a)->dyna.bgId == (u8)bgId) {
            return 1;
        }
    }
    return 0;
}

f32 RsBoat_SwimReach(PlayState* play, const Player* player) {
    return RsBoat_IsHullBg(play, player->actor.wallBgId) ? RS_BOAT_SWIM_REACH : 0.0f;
}

s32 RsBoat_ClimbDelay(PlayState* play, const Player* player, s32 vanilla) {
    if ((player->stateFlags1 & PLAYER_STATE1_IN_WATER) || !RsBoat_IsHullBg(play, player->actor.wallBgId)) {
        return vanilla;
    }
    // Never: the boat's own count hops him (RsBoat_RailHop). The timer is a u8 that stops at 100.
    return 255;
}

// One tick of Link pushing into this hull's wall from its deck: the stick held, facing the wall, in contact this
// tick or within RS_BOAT_RAIL_PUSH_GRACE of it. Read before Player's update, so from last tick's contact.
static void RsBoat_CountRailPush(RsBoat* this, PlayState* play, const Player* player) {
    const Input* input = &play->state.input[0];
    const f32 stick = sqrtf(SQ((f32)input->cur.stick_x) + SQ((f32)input->cur.stick_y));
    const s32 pushing = !this->helm && this->linkAboard && stick > RS_BOAT_RAIL_PUSH_STICK;
    const s32 touching = pushing && (player->actor.bgCheckFlags & BGCHECKFLAG_WALL) && this->dyna.bgId >= 0 &&
                         (u8)player->actor.wallBgId == (u8)this->dyna.bgId &&
                         ABS((s16)(player->actor.shape.rot.y - (s16)(player->actor.wallYaw + 0x8000))) < 0x2000;

    if (touching) {
        this->railPush++;
        this->railGrace = RS_BOAT_RAIL_PUSH_GRACE;
    } else if (pushing && this->railPush > 0 && this->railGrace > 0) {
        this->railPush++;
        this->railGrace--;
    } else {
        this->railPush = 0;
        this->railGrace = 0;
    }
}

f32 RsBoat_RailHop(PlayState* play, const Player* player) {
    Actor* a;

    for (a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != NULL; a = a->next) {
        const RsBoat* boat = (const RsBoat*)a;
        const RsBoatKind* kind;
        f32 dx;
        f32 dz;
        f32 lz;
        f32 railY;
        s32 i;

        if (a->id != ACTOR_RS_BOAT || a->update == NULL || boat->railPush < RS_BOAT_RAIL_CLIMB_DELAY) {
            continue;
        }
        // The rail's height at the station nearest Link, along the hull.
        kind = &sRsBoatKinds[boat->kind];
        dx = player->actor.world.pos.x - a->world.pos.x;
        dz = player->actor.world.pos.z - a->world.pos.z;
        lz = dx * Math_SinS(a->shape.rot.y) + dz * Math_CosS(a->shape.rot.y);
        railY = kind->stations[0].railY;
        for (i = 1; i < kind->numStations; i++) {
            if (fabsf(kind->stations[i].z - lz) < fabsf(kind->stations[i - 1].z - lz)) {
                railY = kind->stations[i].railY;
            }
        }
        return MAX(a->world.pos.y + kind->deckY + railY - player->actor.world.pos.y, 18.0f);
    }
    return 0.0f;
}

s32 RsBoat_IsBoatFloor(PlayState* play, s32 bgId) {
    return RsBoat_IsHullBg(play, bgId);
}

s32 RsBoat_SteppedOffBoat(PlayState* play, const Player* player) {
    Actor* a;

    if (!RsBoat_LinkWasAboard(play)) {
        return 0;
    }
    // Onto another boat's deck is still aboard.
    for (a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_RS_BOAT && a->update != NULL && RsBoat_LinkOnDeck((const RsBoat*)a, player)) {
            return 0;
        }
    }
    return 1;
}

// --- the steering camera --------------------------------------------------------------------------

// Where the steering camera wants to be: behind Link along the hull's heading, its eye at a locked height over
// the deck, looking at a point over the deck a little ahead of him so the bow and the water ahead fill the view.
#define RS_BOAT_CAM_AT_Y 40.0f      // the look-at point above the deck: Link's chest
#define RS_BOAT_CAM_AT_AHEAD 60.0f  // and this far ahead of him

static void RsBoat_CamTarget(const RsBoat* this, const Player* player, Vec3f* at, Vec3f* eye) {
    const f32 height = (f32)CLAMP(CVarGetInteger(CVAR_RS_BOAT_CAM_HEIGHT, RS_BOAT_CAM_HEIGHT_DEFAULT),
                                  RS_BOAT_CAM_HEIGHT_MIN, RS_BOAT_CAM_HEIGHT_MAX);
    const f32 dist = (f32)CLAMP(CVarGetInteger(CVAR_RS_BOAT_CAM_DIST, RS_BOAT_CAM_DIST_DEFAULT), RS_BOAT_CAM_DIST_MIN,
                                RS_BOAT_CAM_DIST_MAX);
    const s16 yaw = this->dyna.actor.shape.rot.y;
    const f32 s = Math_SinS(yaw);
    const f32 c = Math_CosS(yaw);
    const f32 deckY = this->dyna.actor.world.pos.y + RsBoat_DeckY(this);

    at->x = player->actor.world.pos.x + s * RS_BOAT_CAM_AT_AHEAD;
    at->y = deckY + RS_BOAT_CAM_AT_Y;
    at->z = player->actor.world.pos.z + c * RS_BOAT_CAM_AT_AHEAD;
    eye->x = player->actor.world.pos.x - s * dist;
    eye->y = deckY + height;
    eye->z = player->actor.world.pos.z - c * dist;
}

// Takes the view over, from wherever the normal camera is now, so the first ticks ease out of it. No camera is
// a quiet no: CVar off, or no sub-camera free (a cutscene holds them), and the normal camera simply stays.
static void RsBoat_CamStart(RsBoat* this, PlayState* play) {
    Camera* main;

    this->subCamId = SUBCAM_NONE;
    if (!CVarGetInteger(CVAR_RS_BOAT_CAM, 1)) {
        return;
    }
    this->subCamId = Play_CreateSubCamera(play);
    if (this->subCamId == SUBCAM_NONE) {
        return;
    }
    main = Play_GetCamera(play, CAM_ID_MAIN);
    this->camEye = main->eye;
    this->camAt = main->at;
    Play_ChangeCameraStatus(play, CAM_ID_MAIN, CAM_STAT_WAIT);
    Play_ChangeCameraStatus(play, this->subCamId, CAM_STAT_ACTIVE);
    Play_CameraSetAtEye(play, this->subCamId, &this->camAt, &this->camEye);
}

// One tick of the follow: each coordinate a fixed share of the way to its target (RsBoat_CamTarget), so a far
// camera closes fast and a near one settles, then pulled in front of any wall between Link and it.
static void RsBoat_CamUpdate(RsBoat* this, PlayState* play, const Player* player) {
    Vec3f at;
    Vec3f eye;
    f32 ease;

    if (this->subCamId == SUBCAM_NONE) {
        return;
    }
    ease = (f32)CLAMP(CVarGetInteger(CVAR_RS_BOAT_CAM_EASE, RS_BOAT_CAM_EASE_DEFAULT), RS_BOAT_CAM_EASE_MIN,
                      RS_BOAT_CAM_EASE_MAX) /
           100.0f;
    RsBoat_CamTarget(this, player, &at, &eye);
    Math_ApproachF(&this->camAt.x, at.x, ease, 400.0f);
    Math_ApproachF(&this->camAt.y, at.y, ease, 400.0f);
    Math_ApproachF(&this->camAt.z, at.z, ease, 400.0f);
    Math_ApproachF(&this->camEye.x, eye.x, ease, 400.0f);
    Math_ApproachF(&this->camEye.y, eye.y, ease, 400.0f);
    Math_ApproachF(&this->camEye.z, eye.z, ease, 400.0f);
    eye = this->camEye;
    Camera_BGCheck(Play_GetCamera(play, this->subCamId), &this->camAt, &eye);
    Play_CameraSetAtEye(play, this->subCamId, &this->camAt, &eye);
}

// Hands the view back where the steering camera left it, so the normal camera picks up from there rather than
// from where it was when the helm was taken.
static void RsBoat_CamStop(RsBoat* this, PlayState* play) {
    if (this->subCamId == SUBCAM_NONE) {
        return;
    }
    Play_CameraSetAtEye(play, CAM_ID_MAIN, &this->camAt, &this->camEye);
    Play_ChangeCameraStatus(play, CAM_ID_MAIN, CAM_STAT_ACTIVE);
    Play_ClearCamera(play, this->subCamId);
    this->subCamId = SUBCAM_NONE;
}

s32 RsBoat_SetHelm(RsBoat* this, PlayState* play, s32 on) {
    Player* player = GET_PLAYER(play);

    if (on == this->helm) {
        return 1;
    }
    if (on) {
        if (!RsBoat_LinkOnDeck(this, player)) {
            return 0;
        }
        // Stand Link at the tiller facing the bow, so the stick's left is the boat's left, and on the deck: A from
        // the rail, which is deck too (RsBoat_LinkOnDeck), would otherwise leave him in the air over the tiller.
        RsBoat_StandAtHelm(this, player, 1.0f);
        player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
        this->helm = 1;
        RsBoat_CamStart(this, play);
        RsBoat_Mark(this, "helm_on");
    } else {
        player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
        // The B that let go reaches Player this same tick (the boat updates first): without the cooldown it
        // swings the sword.
        player->textboxBtnCooldownTimer = 4;
        this->helm = 0;
        RsBoat_CamStop(this, play);
        RsBoat_Mark(this, "helm_off");
    }
    return 1;
}

// The floor's lowest drawn point over the water at this bob and rock, before any lift. The floor's edge is the keel
// probes; RsBoat_Draw rotates the model by the roll about z and then the pitch about x, so a point (x, deck, z)
// lands at x sin(roll) + (deck cos(pitch) - z sin(pitch)) cos(roll). Both signs of each are taken: the floor is
// symmetric across, and whichever end the pitch dips, the lowest point is found.
static f32 RsBoat_FloorLow(const RsBoat* this, f32 bob) {
    const RsBoatHull* hull = &sHulls[this->kind];
    const f32 sr = fabsf(Math_SinS(this->roll));
    const f32 cr = Math_CosS(this->roll);
    const f32 sp = fabsf(Math_SinS(this->pitch));
    const f32 cp = Math_CosS(this->pitch);
    f32 low = RsBoat_DeckY(this);
    s32 i;

    for (i = 0; i < hull->numKeel; i++) {
        const f32 y = -fabsf(hull->keel[i].x) * sr + (RsBoat_DeckY(this) * cp - fabsf(hull->keel[i].z) * sp) * cr;

        low = MIN(low, y);
    }
    return bob + low;
}

// --- actor --------------------------------------------------------------------------------------

void RsBoat_Init(Actor* thisx, PlayState* play) {
    RsBoat* this = (RsBoat*)thisx;

    this->kind = CLAMP(thisx->params, 0, RS_BOAT_KIND_COUNT - 1);
    if (!sHulls[this->kind].built) {
        RsBoat_BuildHull(this->kind);
    }

    Actor_SetScale(thisx, 1.0f);
    // Position and yaw: the engine's moving-platform path carries Link along and turns him with the hull.
    DynaPolyActor_Init(&this->dyna, DPM_UNK3);
    this->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, thisx, &sHulls[this->kind].col);

    thisx->shape.rot.x = 0;
    thisx->shape.rot.z = 0;
    thisx->world.rot = thisx->shape.rot;
    // Targetable from the whole deck and the water just around it: RsBoat_Update makes attention measure from
    // the tiller, and the skiff's bow is ~200 from it, inside range 240 (mode 8). The sloop's is ~320: locked on
    // from its bow half, A does not reach the tiller.
    thisx->targetMode = 8;
    thisx->uncullZoneForward = 4000.0f;
    thisx->uncullZoneScale = 300.0f;
    thisx->uncullZoneDownward = 300.0f;

    this->helm = 0;
    this->speed = 0.0f;
    this->tiller = 0.0f;
    this->yawVel = 0;
    this->timer = 0;
    this->blocked = RS_BOAT_BLOCK_NONE;
    this->blockCount = 0;
    this->touch = RS_BOAT_BLOCK_NONE;
    this->touchCount = 0;
    this->markedBlock = RS_BOAT_BLOCK_NONE;
    this->freeTicks = 0;
    this->helmPress = 0;
    this->linkAboard = 0;
    this->lift = 0.0f;
    this->floorLow = RsBoat_DeckY(this);
    this->railPush = 0;
    this->railGrace = 0;
    this->throttle = 0.0f;
    this->subCamId = SUBCAM_NONE;
    this->surfaceY = thisx->world.pos.y;
    this->onWater = RsBoat_SurfaceBox(play, thisx->world.pos.x, thisx->world.pos.z, &this->surfaceY, thisx,
                                      &this->waterBox);
    if (this->onWater) {
        thisx->world.pos.y = this->surfaceY;
    }
    RsBoat_Mark(this, "spawn");
}

void RsBoat_Destroy(Actor* thisx, PlayState* play) {
    RsBoat* this = (RsBoat*)thisx;

    if (this->helm) {
        GET_PLAYER(play)->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
        RsBoat_CamStop(this, play);
    }
    DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, this->dyna.bgId);
}

// Reads the helm's inputs: the pad, or the agent loop's override. Throttle and turn are -1..1.
//
// The stick does both (#262 point 3): how far it is pushed is the throttle, which way it points scales it ahead,
// astern or to nothing (RS_BOAT_ARC_*), and its left-right is the tiller. So up-left at the rim is full ahead and
// full left, and straight left only turns. A is left free for something else at the helm.
static void RsBoat_ReadHelm(RsBoat* this, PlayState* play, f32* throttle, f32* turn, s32* letGo) {
    Input* input = &play->state.input[0];
    const f32 stickX = (f32)input->cur.stick_x;
    const f32 stickY = (f32)input->cur.stick_y;
    const s32 steps = (s32)lroundf(RsBoat_Tune(RS_BOAT_TUNE_STEPS));
    f32 amount;

    *letGo = (input->press.button & BTN_B) != 0;
    if (gRsBoatDrive.ticks > 0) {
        gRsBoatDrive.ticks--;
        *throttle = gRsBoatDrive.throttle;
        *turn = gRsBoatDrive.turn;
        return;
    }
    amount = CLAMP((sqrtf(stickX * stickX + stickY * stickY) - RS_BOAT_STICK_DEAD) /
                       (RS_BOAT_STICK_FULL - RS_BOAT_STICK_DEAD),
                   0.0f, 1.0f);
    // Steps round up, so the lightest push past the dead zone is the first step rather than nothing.
    if (steps > 0 && amount > 0.0f) {
        amount = ceilf(amount * (f32)steps - 0.001f) / (f32)steps;
    }
    if (stickY > 0.0f) {
        *throttle = amount * CLAMP((RS_BOAT_ARC_AHEAD_END - fabsf(atan2f(stickX, stickY))) /
                                       (RS_BOAT_ARC_AHEAD_END - RS_BOAT_ARC_AHEAD_FULL),
                                   0.0f, 1.0f);
    } else if (stickY < 0.0f) {
        *throttle = -amount * CLAMP((RS_BOAT_ARC_ASTERN_END - fabsf(atan2f(stickX, -stickY))) /
                                        (RS_BOAT_ARC_ASTERN_END - RS_BOAT_ARC_ASTERN_FULL),
                                    0.0f, 1.0f);
    } else {
        *throttle = 0.0f;
    }
    // Stick left is a positive turn: yaw grows counter-clockwise seen from above, which is Link's left.
    *turn = CLAMP(-stickX / RS_BOAT_STICK_FULL, -1.0f, 1.0f);
}

// One tick's move, (nextX, nextZ, nextYaw), against the shore. Pushed out of it if it can be, keeping only the
// speed the shore leaves: a head-on stop loses its way, a graze keeps the part along the bank. If no push clears
// it in one tick, the pushed move is still taken when it leaves the hull less deep in trouble than it is now, so
// a hull that is already on the bank (put down there, or a bank that moved) works its way off over a few ticks.
// Failing that, the move alone and the turn alone are tried, and taken if they fit or leave it less deep - so a
// hull touching land can always drive off it.
static void RsBoat_Move(RsBoat* this, PlayState* play, f32 nextX, f32 nextZ, s16 nextYaw) {
    Actor* thisx = &this->dyna.actor;
    f32 x = nextX;
    f32 z = nextZ;
    f32 left;
    s32 touched;
    s32 cleared;
    RsBoatContact now;

    now.total = 0.0f;
    this->blocked = RS_BOAT_BLOCK_NONE;
    cleared = RsBoat_Settle(this, play, &x, &z, nextYaw, &touched, &left);
    if (!cleared) {
        RsBoat_Evaluate(this, play, thisx->world.pos.x, thisx->world.pos.z, thisx->shape.rot.y, &now);
    }
    // Whatever the probes say, the centre stays on the boat's water. A hull already half off it took a move that left
    // it less deep in trouble with the centre over the edge, and a boat off its water settles on the ground: in Lake
    // Hylia, 300 down on the lakebed past the main box's west edge, with Link aboard.
    if ((cleared || left < now.total - 0.01f) && RsBoat_WaterAt(this, play, x, z)) {
        if (touched != RS_BOAT_BLOCK_NONE) {
            const f32 along =
                (x - thisx->world.pos.x) * Math_SinS(nextYaw) + (z - thisx->world.pos.z) * Math_CosS(nextYaw);

            if (along * this->speed <= 0.0f) {
                this->speed = 0.0f;
            } else if (fabsf(along) < fabsf(this->speed)) {
                this->speed = along;
            }
            this->touch = touched;
            this->touchCount++;
        }
        thisx->world.pos.x = x;
        thisx->world.pos.z = z;
        thisx->shape.rot.y = nextYaw;
        // A crawl against the shore fits for a tick or two between refusals; only a run of moves re-arms.
        if (++this->freeTicks > RS_BOAT_FREE_TICKS) {
            this->markedBlock = RS_BOAT_BLOCK_NONE;
        }
        return;
    }

    {
        s32 moved = 0;

        x = thisx->world.pos.x + Math_SinS(thisx->shape.rot.y) * this->speed;
        z = thisx->world.pos.z + Math_CosS(thisx->shape.rot.y) * this->speed;
        if (this->speed != 0.0f && RsBoat_WaterAt(this, play, x, z) &&
            RsBoat_Improves(this, play, x, z, thisx->shape.rot.y, now.total)) {
            thisx->world.pos.x = x;
            thisx->world.pos.z = z;
            moved = 1;
        } else if (this->yawVel != 0 &&
                   RsBoat_Improves(this, play, thisx->world.pos.x, thisx->world.pos.z, nextYaw, now.total)) {
            thisx->shape.rot.y = nextYaw;
        }
        this->blocked = touched;
        if (!moved) {
            this->speed = 0.0f;
        }
        this->freeTicks = 0;
        this->blockCount++;
        if (touched != this->markedBlock) {
            this->markedBlock = touched;
            RsBoat_Mark(this, "blocked");
        }
    }
}

void RsBoat_Update(Actor* thisx, PlayState* play) {
    RsBoat* this = (RsBoat*)thisx;
    const RsBoatHull* hull = &sHulls[this->kind];
    Player* player = GET_PLAYER(play);
    Input* input = &play->state.input[0];
    const f32 top = RsBoat_TopSpeed(this);
    f32 rate;
    f32 throttle = 0.0f;
    f32 turn = 0.0f;
    s32 letGo = 0;
    f32 target;
    f32 bob;
    s16 nextYaw;
    s32 aDown = (input->cur.button & BTN_A) != 0;

    // No lag to make up for when drawing: DynaPoly_Setup runs between the BG and Player categories and takes the
    // hull's transform as this tick left it, so Player carries Link by this tick's move, not last tick's.
    this->timer++;
    // Read before Player's update this tick, so it is where Link stood last tick: Player asks when he leaves the
    // ground whether the floor he left was a boat (RsBoat_LinkWasAboard).
    this->linkAboard = RsBoat_LinkOnDeck(this, player);
    RsBoat_CountRailPush(this, play, player);

    // The tiller is targetable while the helm is free: the arrow and the lock-on ride over the post at head
    // height. Off at the helm, so the Z search skips it and the leash drops a lock held through the take.
    if (this->helm) {
        thisx->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    } else {
        thisx->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    }
    RsBoat_HelmPos(this, &thisx->focus.pos);
    thisx->focus.pos.y += 40.0f;
    thisx->focus.rot = thisx->shape.rot;
    // Attention reads its distance and its "in front of Link" angle off these, which Actor_UpdateAll measured
    // from the hull's centre - so walking toward the stern put the centre behind him and the tiller dropped
    // out of the Z search, while turning away from it brought it back. Measured from the tiller instead; the
    // attention search and the lock-on leash run after every actor's update. Nothing else here reads them.
    {
        Vec3f tiller;

        RsBoat_HelmPos(this, &tiller);
        thisx->xzDistToPlayer = Math_Vec3f_DistXZ(&tiller, &player->actor.world.pos);
        thisx->yDistToPlayer = player->actor.world.pos.y - tiller.y;
        thisx->xyzDistToPlayerSq = SQ(thisx->xzDistToPlayer) + SQ(thisx->yDistToPlayer);
        thisx->yawTowardsPlayer = Math_Vec3f_Yaw(&tiller, &player->actor.world.pos);
    }

    // Take the helm: a fresh A press with the tiller in reach (on deck, free to, close enough or locked on),
    // and no textbox wanting the A.
    if (!this->helm && aDown && !this->helmPress && RsBoat_TillerReachable(this, player) &&
        Message_GetState(&play->msgCtx) == TEXT_STATE_NONE) {
        RsBoat_SetHelm(this, play, 1);
    }
    this->helmPress = aDown;

    if (this->helm) {
        Vec3f helm;
        s32 knocked;

        RsBoat_ReadHelm(this, play, &throttle, &turn, &letGo);
        // Hurt, dead, or shoved off the tiller (a bomb's blast moves him even with his input off): any of
        // them lets go, as falling off the deck does.
        RsBoat_HelmPos(this, &helm);
        knocked = (player->stateFlags1 & (PLAYER_STATE1_DAMAGED | PLAYER_STATE1_DEAD)) != 0 ||
                  Math_Vec3f_DistXZ(&helm, &player->actor.world.pos) > RS_BOAT_HELM_KNOCKED;
        if (letGo || knocked || !RsBoat_LinkOnDeck(this, player)) {
            RsBoat_SetHelm(this, play, 0);
            throttle = 0.0f;
            turn = 0.0f;
        }
    }
    this->throttle = throttle;

    // Speed toward the throttle's target: braking while the stick opposes the way the boat moves, gaining toward a
    // higher target, coasting toward a lower one. The tiller eases toward the stick, and turns harder with way on.
    target = throttle > 0.0f ? throttle * top : throttle * top * RsBoat_Tune(RS_BOAT_TUNE_REVERSE);
    if ((this->speed > 0.0f && target < 0.0f) || (this->speed < 0.0f && target > 0.0f)) {
        rate = RsBoat_Tune(RS_BOAT_TUNE_BRAKE);
    } else if (fabsf(target) > fabsf(this->speed)) {
        rate = RsBoat_Tune(RS_BOAT_TUNE_ACCEL);
    } else {
        rate = RsBoat_Tune(RS_BOAT_TUNE_DRAG);
    }
    Math_StepToF(&this->speed, target, rate);
    this->tiller += (turn - this->tiller) * RS_BOAT_TILLER_EASE;
    this->yawVel = (s16)(this->tiller * RsBoat_TurnRate(this, this->speed));

    // The surface under the hull's centre. Off the water the boat does not move, and settles on whatever is
    // under its keel (beached; or Lake Hylia drained under it).
    if (this->onWater && RsBoat_InOwnBox(this, play, thisx->world.pos.x, thisx->world.pos.z)) {
        this->surfaceY = (f32)play->colCtx.colHeader->waterBoxes[this->waterBox].ySurface;
    } else {
        this->onWater = RsBoat_SurfaceBox(play, thisx->world.pos.x, thisx->world.pos.z, &this->surfaceY, thisx,
                                          &this->waterBox);
    }
    if (!this->onWater) {
        Vec3f above = thisx->world.pos;
        CollisionPoly* poly = NULL;
        s32 bgId = BGCHECK_SCENE;
        f32 groundY;

        above.y += RsBoat_DeckY(this) + RS_BOAT_HEADROOM;
        groundY = BgCheck_EntityRaycastFloor4(&play->colCtx, &poly, &bgId, thisx, &above);
        if (groundY > BGCHECK_Y_MIN) {
            thisx->world.pos.y = groundY - RS_BOAT_KEEL_Y;
        }
        this->speed = 0.0f;
    }

    nextYaw = (s16)(thisx->shape.rot.y + this->yawVel);
    if (this->onWater && (this->speed != 0.0f || this->yawVel != 0)) {
        RsBoat_Move(this, play, thisx->world.pos.x + Math_SinS(nextYaw) * this->speed,
                    thisx->world.pos.z + Math_CosS(nextYaw) * this->speed, nextYaw);
    }
    thisx->world.rot.y = thisx->shape.rot.y;
    if (this->helm) {
        RsBoat_CamUpdate(this, play, player);
    }

    // Float: the surface plus the bob, and the lift that keeps the rocking floor out of the water. Only the hull
    // moves; the water stays flat.
    this->roll = (s16)(sinf((f32)this->timer * (2.0f * (f32)M_PI / RS_BOAT_ROCK_PERIOD)) * hull->rollAmp);
    this->pitch = (s16)(cosf((f32)this->timer * (2.0f * (f32)M_PI / RS_BOAT_ROCK_PERIOD) * 0.7f) * hull->pitchAmp);
    bob = sinf((f32)this->timer * (2.0f * (f32)M_PI / RS_BOAT_BOB_PERIOD)) * RS_BOAT_BOB_AMP;
    this->lift = MAX(0.0f, RS_BOAT_FLOAT_MARGIN - RsBoat_FloorLow(this, bob));
    this->floorLow = RsBoat_FloorLow(this, bob) + this->lift;
    if (this->onWater) {
        thisx->world.pos.y = this->surfaceY + bob + this->lift;
    }
}

static void RsBoat_DrawBox(GraphicsContext* gfxCtx, Vtx* vtx, u8 r, u8 g, u8 b, u8 topR, u8 topG, u8 topB) {
    OPEN_DISPS(gfxCtx);

    gSPVertex(POLY_OPA_DISP++, (uintptr_t)vtx, RS_BOAT_BOX_VTX, 0);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, topR, topG, topB, 255);
    gSP2Triangles(POLY_OPA_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, 255);
    gSP2Triangles(POLY_OPA_DISP++, 4, 5, 6, 0, 4, 6, 7, 0);
    gSP2Triangles(POLY_OPA_DISP++, 8, 9, 10, 0, 8, 10, 11, 0);
    gSP2Triangles(POLY_OPA_DISP++, 12, 13, 14, 0, 12, 14, 15, 0);
    gSP2Triangles(POLY_OPA_DISP++, 16, 17, 18, 0, 16, 18, 19, 0);
    gSP2Triangles(POLY_OPA_DISP++, 20, 21, 22, 0, 20, 22, 23, 0);

    CLOSE_DISPS(gfxCtx);
}

// The test waterbox's surface: one flat translucent quad in world space, unlit. Every boat draws it, so two boats
// draw it twice; a test fixture can live with that.
static Vtx sTestWaterVtx[4];

static void RsBoat_DrawTestWater(GraphicsContext* gfxCtx) {
    const s16 x0 = (s16)CLAMP(gRsBoatTestWater.x0, -32000.0f, 32000.0f);
    const s16 x1 = (s16)CLAMP(gRsBoatTestWater.x1, -32000.0f, 32000.0f);
    const s16 z0 = (s16)CLAMP(gRsBoatTestWater.z0, -32000.0f, 32000.0f);
    const s16 z1 = (s16)CLAMP(gRsBoatTestWater.z1, -32000.0f, 32000.0f);
    const s16 y = (s16)CLAMP(gRsBoatTestWater.y, -32000.0f, 32000.0f);

    RsBoat_SetVtx(&sTestWaterVtx[0], x0, y, z0, 0, 127, 0);
    RsBoat_SetVtx(&sTestWaterVtx[1], x1, y, z0, 0, 127, 0);
    RsBoat_SetVtx(&sTestWaterVtx[2], x1, y, z1, 0, 127, 0);
    RsBoat_SetVtx(&sTestWaterVtx[3], x0, y, z1, 0, 127, 0);

    OPEN_DISPS(gfxCtx);

    Gfx_SetupDL_25Xlu(gfxCtx);
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_CULL_BACK | G_LIGHTING);
    gDPSetCombineMode(POLY_XLU_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 40, 96, 150, 150);
    gSPVertex(POLY_XLU_DISP++, (uintptr_t)sTestWaterVtx, 4, 0);
    gSP2Triangles(POLY_XLU_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);
    gSPSetGeometryMode(POLY_XLU_DISP++, G_CULL_BACK | G_LIGHTING);

    CLOSE_DISPS(gfxCtx);
}

void RsBoat_Draw(Actor* thisx, PlayState* play) {
    RsBoat* this = (RsBoat*)thisx;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    RsBoatHull* hull = &sHulls[this->kind];
    Gfx* model = RsBoat_ModelList(sRsBoatKinds[this->kind].list);

    OPEN_DISPS(gfxCtx);

    Gfx_SetupDL_25Opa(gfxCtx);
    // Actor_Draw has set the hull's matrix (position and yaw); the rock is draw-only, on top of it.
    Matrix_RotateZYX(this->pitch, 0, this->roll, MTXMODE_APPLY);
    if (model != NULL) {
        // The kind's model, in its own model space: its floor on the deck, centred on the collision. Its list carries
        // its own materials (lit, RS's base colours as prim colours), so the state is set up again after it.
        Matrix_Push();
        Matrix_Translate(0.0f, sRsBoatKinds[this->kind].modelY, sRsBoatKinds[this->kind].modelZ, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPDisplayList(POLY_OPA_DISP++, model);
        Matrix_Pop();
        Gfx_SetupDL_25Opa(gfxCtx);
    }
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    // PRIMITIVE * SHADE: the part's colour, lit by the scene through each face's normal.
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, 0, SHADE, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, SHADE, 0, 0, 0, 0,
                      PRIMITIVE);

    CLOSE_DISPS(gfxCtx);

    // The placeholder box only when the kind's archive is not mounted; the tiller post, as the thing Link
    // targets and steers from, unless the archive carries the kind's own helm (Sailing's helm is a separate loc).
    if (model == NULL) {
        RsBoat_DrawBox(gfxCtx, hull->hullBox, 96, 62, 34, 168, 128, 80);
        RsBoat_DrawBox(gfxCtx, hull->prowBox, 120, 80, 44, 150, 104, 60);
    }
    if (model == NULL || !sRsBoatKinds[this->kind].helmInModel) {
        RsBoat_DrawBox(gfxCtx, hull->tillerBox, 60, 38, 20, 60, 38, 20);
    }

    OPEN_DISPS(gfxCtx);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    CLOSE_DISPS(gfxCtx);

    if (gRsBoatTestWater.active) {
        RsBoat_DrawTestWater(gfxCtx);
    }
}
