/*
 * RsBalloon.c - a crude balloon basket (sturdy-bassoon#274). RsBalloon.h has the design: the basket, the controls,
 * flying and being left alone.
 *
 * Same /W3 /WX rules as the other rs/ actors: explicit casts, no `s32 pad`, no ICHAIN.
 */

#include <math.h>
#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsBalloon.h"
#include "RsBoat.h" // RsBoat_SurfaceAt: which waterbox is water, buried boxes left out
#include "RsVehicleShadow.h"
#include "RsActors.h"
#include "global.h"
#include "soh/OTRGlobals.h" // BTN_CUSTOM_MODIFIER1 and 2

void RsBalloon_Init(Actor* thisx, PlayState* play);
void RsBalloon_Destroy(Actor* thisx, PlayState* play);
void RsBalloon_Update(Actor* thisx, PlayState* play);
void RsBalloon_Draw(Actor* thisx, PlayState* play);

RsBalloonDrive gRsBalloonDrive = { 0, 0.0f, 0.0f, 0.0f };

// --- tuning -------------------------------------------------------------------------------------
//
// First guesses, to be felt out: a climb a little slower than Link's fall, reached in about a second (`lift`); a
// top speed a little over Link's run; a coast that carries it on for a few seconds once the stick is let go, as a
// balloon would rather than a boat; a turn slower than the skiff's. The ceiling is high enough to see Hyrule Field
// from, and well under its fog.
const RsBalloonTuneInfo gRsBalloonTune[RS_BALLOON_TUNE_COUNT] = {
    { "climb", CVAR_ENHANCEMENT("RsBalloonTuneClimb"), 3.0f, 0.1f, 30.0f },
    { "sink", CVAR_ENHANCEMENT("RsBalloonTuneSink"), 3.0f, 0.1f, 30.0f },
    { "lift", CVAR_ENHANCEMENT("RsBalloonTuneLift"), 0.15f, 0.01f, 10.0f },
    { "drift", CVAR_ENHANCEMENT("RsBalloonTuneDrift"), 0.0f, 0.0f, 10.0f },
    { "fwd", CVAR_ENHANCEMENT("RsBalloonTuneForward"), 8.0f, 0.0f, 40.0f },
    { "accel", CVAR_ENHANCEMENT("RsBalloonTuneAccel"), 0.2f, 0.01f, 5.0f },
    { "drag", CVAR_ENHANCEMENT("RsBalloonTuneDrag"), 0.06f, 0.01f, 5.0f },
    { "brake", CVAR_ENHANCEMENT("RsBalloonTuneBrake"), 0.3f, 0.01f, 5.0f },
    { "turn", CVAR_ENHANCEMENT("RsBalloonTuneTurn"), 200.0f, 0.0f, 2000.0f },
    { "ceiling", CVAR_ENHANCEMENT("RsBalloonTuneCeiling"), 1200.0f, 0.0f, 20000.0f },
    { "abandonsink", CVAR_ENHANCEMENT("RsBalloonTuneAbandonSink"), 1.0f, 0.1f, 30.0f },
};

f32 RsBalloon_Tune(s32 knob) {
    const RsBalloonTuneInfo* info = &gRsBalloonTune[knob];

    return CLAMP(CVarGetFloat(info->cvar, info->def), info->min, info->max);
}

s32 RsBalloon_AbandonMode(void) {
    return CLAMP(CVarGetInteger(CVAR_RS_BALLOON_ABANDON, RS_BALLOON_ABANDON_SINK), 0, RS_BALLOON_ABANDON_COUNT - 1);
}

#define RS_BALLOON_TILLER_EASE 0.25f // of the way to the stick's target, each tick
#define RS_BALLOON_FREE_TICKS 10     // moves in a row that must fit before a refusal is reported again
#define RS_BALLOON_INNER (RS_BALLOON_HALF_WIDTH - RS_BALLOON_RAIL_THICK)
// The ground probes and the fence sit this far inside the outer faces: a wall the basket is flush against is met
// by the fence, not stood on by the probes.
#define RS_BALLOON_PROBE_INSET 2.0f
// The probe grid across its bottom (RsBalloon_GridPoint): 5 x 5. Nine points 54 apart let a wall's top or a beam
// between them through (the #274 review).
#define RS_BALLOON_GRID 5
#define RS_BALLOON_GRID_POINTS (RS_BALLOON_GRID * RS_BALLOON_GRID)
#define RS_BALLOON_CAM_AT_Y 40.0f     // the look-at point over the floor: Link's chest
#define RS_BALLOON_CAM_AT_AHEAD 60.0f // and this far ahead of him

// The fence's heights over the bottom: just over a skimmed step, the middle of the walls, over the rail at Link's
// chest, and his head.
static const f32 sFenceY[] = { RS_BALLOON_STEP + 2.0f, 30.0f, 60.0f, RS_BALLOON_CLEAR_Y - 2.0f };

// --- the basket: collision ----------------------------------------------------------------------
//
// Sixteen corners: the outer faces' feet and tops, the rail tops' inner edges, and the inner walls' feet on the
// floor. Twenty-eight triangles: the floor, the underside, four outer walls, four rail tops, four inner walls.

#define RS_BALLOON_NUM_VTX 16
#define RS_BALLOON_MAX_POLYS 28
#define RS_BALLOON_BOX_VTX 24

#define RS_BALLOON_SURFACE_FLOOR 0
#define RS_BALLOON_SURFACE_WALL 1
static SurfaceType sBasketSurface[2] = {
    // wood footsteps (material 9, the bridge planks); everything else zero: a plain floor
    { { 0, 9 } },
    // the same with the hands-climb wall type (#179) in word 0's wall-type field (bits 21-25): from the floor the
    // inner walls are 40 tall, under adult Link's 41 for a climb, so a plain wall would be a hop, which carries him
    // 24-32 past the face: over the 14 rail and out. A hands-climb wall gets the climb that stands him on the top.
    { { (u32)WALL_TYPE_HANDS_CLIMB << 21, 9 } },
};
// What the camera passes and everything else meets (z_bgcheck.c's COLPOLY_IGNORE_CAMERA, not in a header): the
// walls and rail tops, so the normal camera behind Link in the basket is not pulled in front of the back wall.
#define RS_BALLOON_POLY_CAMERA_PASS (1 << 0)
// The camera reads the floor's camera data under Link and dereferences the list without a null check
// (BgCheck_GetBgCamSettingImpl). CAM_SET_NONE keeps the scene's own setting (the boat's lesson, RsBoat.c).
static CamData sBasketCamData[1] = { { CAM_SET_NONE, 0, NULL } };

typedef struct RsBalloonBasket {
    s32 built;
    Vec3s vtx[RS_BALLOON_NUM_VTX];
    CollisionPoly polys[RS_BALLOON_MAX_POLYS];
    s32 numPolys;
    CollisionHeader col;
    // The drawn boxes: the floor, the four walls (the front one its own colour, so the heading reads) and the
    // controls' panel on the back wall.
    Vtx floorBox[RS_BALLOON_BOX_VTX];
    Vtx wallBox[4][RS_BALLOON_BOX_VTX];
    Vtx panelBox[RS_BALLOON_BOX_VTX];
} RsBalloonBasket;

static RsBalloonBasket sBasket;

static void RsBalloon_SetVtx(Vtx* v, s16 x, s16 y, s16 z, s8 nx, s8 ny, s8 nz) {
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

// Six faces of four vertices, in the order the draw expects: top, bottom, +x, -x, +z, -z (RsBoat_BuildBox's).
static void RsBalloon_BuildBox(Vtx* out, s16 x0, s16 y0, s16 z0, s16 x1, s16 y1, s16 z1) {
    RsBalloon_SetVtx(&out[0], x0, y1, z0, 0, 127, 0);
    RsBalloon_SetVtx(&out[1], x1, y1, z0, 0, 127, 0);
    RsBalloon_SetVtx(&out[2], x1, y1, z1, 0, 127, 0);
    RsBalloon_SetVtx(&out[3], x0, y1, z1, 0, 127, 0);
    RsBalloon_SetVtx(&out[4], x0, y0, z0, 0, -127, 0);
    RsBalloon_SetVtx(&out[5], x1, y0, z0, 0, -127, 0);
    RsBalloon_SetVtx(&out[6], x1, y0, z1, 0, -127, 0);
    RsBalloon_SetVtx(&out[7], x0, y0, z1, 0, -127, 0);
    RsBalloon_SetVtx(&out[8], x1, y0, z0, 127, 0, 0);
    RsBalloon_SetVtx(&out[9], x1, y1, z0, 127, 0, 0);
    RsBalloon_SetVtx(&out[10], x1, y1, z1, 127, 0, 0);
    RsBalloon_SetVtx(&out[11], x1, y0, z1, 127, 0, 0);
    RsBalloon_SetVtx(&out[12], x0, y0, z0, -127, 0, 0);
    RsBalloon_SetVtx(&out[13], x0, y1, z0, -127, 0, 0);
    RsBalloon_SetVtx(&out[14], x0, y1, z1, -127, 0, 0);
    RsBalloon_SetVtx(&out[15], x0, y0, z1, -127, 0, 0);
    RsBalloon_SetVtx(&out[16], x0, y0, z1, 0, 0, 127);
    RsBalloon_SetVtx(&out[17], x1, y0, z1, 0, 0, 127);
    RsBalloon_SetVtx(&out[18], x1, y1, z1, 0, 0, 127);
    RsBalloon_SetVtx(&out[19], x0, y1, z1, 0, 0, 127);
    RsBalloon_SetVtx(&out[20], x0, y0, z0, 0, 0, -127);
    RsBalloon_SetVtx(&out[21], x1, y0, z0, 0, 0, -127);
    RsBalloon_SetVtx(&out[22], x1, y1, z0, 0, 0, -127);
    RsBalloon_SetVtx(&out[23], x0, y1, z0, 0, 0, -127);
}

// One triangle, wound to face `out` (the engine's convention, read off an exported scene: normal = (B - A) x (C - A)).
// The basket has no slivers to leave out (ENGINE_BUDGETS.md's thin-triangle crash): its narrowest face is 14.
static void RsBalloon_AddTri(s32 a, s32 b, s32 c, f32 outX, f32 outY, f32 outZ, s32 surface, u16 flags) {
    CollisionPoly* poly = &sBasket.polys[sBasket.numPolys];
    const Vec3s* va = &sBasket.vtx[a];
    const Vec3s* vb = &sBasket.vtx[b];
    const Vec3s* vc = &sBasket.vtx[c];
    const f32 abx = (f32)(vb->x - va->x);
    const f32 aby = (f32)(vb->y - va->y);
    const f32 abz = (f32)(vb->z - va->z);
    const f32 acx = (f32)(vc->x - va->x);
    const f32 acy = (f32)(vc->y - va->y);
    const f32 acz = (f32)(vc->z - va->z);
    f32 nx = aby * acz - abz * acy;
    f32 ny = abz * acx - abx * acz;
    f32 nz = abx * acy - aby * acx;
    const f32 len = sqrtf(nx * nx + ny * ny + nz * nz);

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
    poly->type = (u16)surface;
    poly->flags_vIA = COLPOLY_VTX(a, flags);
    poly->flags_vIB = COLPOLY_VTX(b, 0);
    poly->vIC = COLPOLY_VTX_INDEX(c);
    poly->normal.x = COLPOLY_SNORMAL(nx);
    poly->normal.y = COLPOLY_SNORMAL(ny);
    poly->normal.z = COLPOLY_SNORMAL(nz);
    poly->dist = (s16)lroundf(-(nx * va->x + ny * va->y + nz * va->z));
    sBasket.numPolys++;
}

static void RsBalloon_AddQuad(s32 a, s32 b, s32 c, s32 d, f32 outX, f32 outY, f32 outZ, s32 surface, u16 flags) {
    RsBalloon_AddTri(a, b, c, outX, outY, outZ, surface, flags);
    RsBalloon_AddTri(a, c, d, outX, outY, outZ, surface, flags);
}

// Corner k of a ring, k = 0..3 going round: (-,-), (+,-), (+,+), (-,+) in (x, z).
static const s8 sCornerX[4] = { -1, 1, 1, -1 };
static const s8 sCornerZ[4] = { -1, -1, 1, 1 };
// Side k runs from corner k to corner k+1; its outward normal in (x, z).
static const s8 sSideX[4] = { 0, 1, 0, -1 };
static const s8 sSideZ[4] = { -1, 0, 1, 0 };

#define RS_BALLOON_V_FOOT 0   // the outer faces' feet, on the bottom
#define RS_BALLOON_V_TOP 4    // the outer faces' tops
#define RS_BALLOON_V_INNER 8  // the rail tops' inner edges
#define RS_BALLOON_V_FLOOR 12 // the inner walls' feet, on the floor

static void RsBalloon_BuildBasket(void) {
    const s16 h = (s16)RS_BALLOON_HALF_WIDTH;
    const s16 in = (s16)RS_BALLOON_INNER;
    const s16 floorY = (s16)RS_BALLOON_FLOOR_Y;
    const s16 topY = (s16)RS_BALLOON_TOP_Y;
    CollisionHeader* col = &sBasket.col;
    s32 k;

    for (k = 0; k < 4; k++) {
        Vec3s* v;

        v = &sBasket.vtx[RS_BALLOON_V_FOOT + k];
        v->x = (s16)(sCornerX[k] * h);
        v->y = 0;
        v->z = (s16)(sCornerZ[k] * h);
        v = &sBasket.vtx[RS_BALLOON_V_TOP + k];
        v->x = (s16)(sCornerX[k] * h);
        v->y = topY;
        v->z = (s16)(sCornerZ[k] * h);
        v = &sBasket.vtx[RS_BALLOON_V_INNER + k];
        v->x = (s16)(sCornerX[k] * in);
        v->y = topY;
        v->z = (s16)(sCornerZ[k] * in);
        v = &sBasket.vtx[RS_BALLOON_V_FLOOR + k];
        v->x = (s16)(sCornerX[k] * in);
        v->y = floorY;
        v->z = (s16)(sCornerZ[k] * in);
    }

    sBasket.numPolys = 0;
    RsBalloon_AddQuad(RS_BALLOON_V_FLOOR + 0, RS_BALLOON_V_FLOOR + 1, RS_BALLOON_V_FLOOR + 2, RS_BALLOON_V_FLOOR + 3, 0.0f,
                      1.0f, 0.0f, RS_BALLOON_SURFACE_FLOOR, 0);
    // The underside: Link under a hovering basket meets it as a ceiling.
    RsBalloon_AddQuad(RS_BALLOON_V_FOOT + 0, RS_BALLOON_V_FOOT + 1, RS_BALLOON_V_FOOT + 2, RS_BALLOON_V_FOOT + 3, 0.0f,
                      -1.0f, 0.0f, RS_BALLOON_SURFACE_FLOOR, RS_BALLOON_POLY_CAMERA_PASS);
    for (k = 0; k < 4; k++) {
        const s32 n = (k + 1) % 4;

        RsBalloon_AddQuad(RS_BALLOON_V_FOOT + k, RS_BALLOON_V_FOOT + n, RS_BALLOON_V_TOP + n, RS_BALLOON_V_TOP + k,
                          (f32)sSideX[k], 0.0f, (f32)sSideZ[k], RS_BALLOON_SURFACE_WALL, RS_BALLOON_POLY_CAMERA_PASS);
        RsBalloon_AddQuad(RS_BALLOON_V_TOP + k, RS_BALLOON_V_TOP + n, RS_BALLOON_V_INNER + n, RS_BALLOON_V_INNER + k,
                          0.0f, 1.0f, 0.0f, RS_BALLOON_SURFACE_FLOOR, RS_BALLOON_POLY_CAMERA_PASS);
        RsBalloon_AddQuad(RS_BALLOON_V_FLOOR + k, RS_BALLOON_V_FLOOR + n, RS_BALLOON_V_INNER + n,
                          RS_BALLOON_V_INNER + k, (f32)-sSideX[k], 0.0f, (f32)-sSideZ[k], RS_BALLOON_SURFACE_WALL,
                          RS_BALLOON_POLY_CAMERA_PASS);
    }

    col->minBounds.x = (s16)-h;
    col->minBounds.y = 0;
    col->minBounds.z = (s16)-h;
    col->maxBounds.x = h;
    col->maxBounds.y = topY;
    col->maxBounds.z = h;
    col->numVertices = RS_BALLOON_NUM_VTX;
    col->vtxList = sBasket.vtx;
    col->numPolygons = (u32)sBasket.numPolys;
    col->polyList = sBasket.polys;
    col->surfaceTypeList = sBasketSurface;
    col->cameraDataList = sBasketCamData;
    col->numWaterBoxes = 0;
    col->waterBoxes = NULL;
    col->cameraDataListLen = (size_t)ARRAY_COUNT(sBasketCamData);

    RsBalloon_BuildBox(sBasket.floorBox, (s16)-h, 0, (s16)-h, h, floorY, h);
    RsBalloon_BuildBox(sBasket.wallBox[0], (s16)-h, floorY, (s16)-h, h, topY, (s16)-in); // back
    RsBalloon_BuildBox(sBasket.wallBox[1], in, floorY, (s16)-in, h, topY, in);           // right (+x)
    RsBalloon_BuildBox(sBasket.wallBox[2], (s16)-h, floorY, in, h, topY, h);             // front
    RsBalloon_BuildBox(sBasket.wallBox[3], (s16)-h, floorY, (s16)-in, (s16)-in, topY, in); // left (-x)
    // The panel: on the back wall's inner face but 2 inboard of it, so the two do not z-fight (the boat's tiller post
    // lesson), and standing proud of the rail so it reads from behind.
    RsBalloon_BuildBox(sBasket.panelBox, -12, (s16)(topY - 18), (s16)(-in + 2), 12, (s16)(topY + 8), (s16)(-in + 7));
    sBasket.built = 1;
}

// --- markers ------------------------------------------------------------------------------------

static void RsBalloon_Mark(RsBalloon* this, const char* event) {
    char line[240];

    snprintf(line, sizeof(line),
             "rs_balloon event=%s x=%d y=%d z=%d yaw=%d vy=%d speed=%d controls=%d landed=%d on_water=%d blocked=%s "
             "ground=%d height=%d",
             event, (int)lroundf(this->dyna.actor.world.pos.x), (int)lroundf(this->dyna.actor.world.pos.y),
             (int)lroundf(this->dyna.actor.world.pos.z), (int)(u16)this->dyna.actor.shape.rot.y,
             (int)lroundf(this->vy * 100.0f), (int)lroundf(this->speed * 100.0f), this->controls, this->landed,
             this->onWater, RsBalloon_BlockName(this->blocked), (int)lroundf(this->groundY),
             (int)lroundf(this->alt - this->groundY));
    RsAgent_Marker(line);
}

const char* RsBalloon_BlockName(s32 block) {
    switch (block) {
        case RS_BALLOON_BLOCK_WALL:
            return "wall";
        case RS_BALLOON_BLOCK_GROUND:
            return "ground";
        case RS_BALLOON_BLOCK_OVERHEAD:
            return "overhead";
        case RS_BALLOON_BLOCK_CEILING:
            return "ceiling";
        default:
            return "none";
    }
}

const char* RsBalloon_AbandonName(s32 mode) {
    switch (mode) {
        case RS_BALLOON_ABANDON_SINK:
            return "sink";
        case RS_BALLOON_ABANDON_HANG:
            return "hang";
        case RS_BALLOON_ABANDON_HOME:
            return "home";
        default:
            return "?";
    }
}

// --- geometry -----------------------------------------------------------------------------------

// A point on the basket, in world space, for a basket at (x, z) turned to `yaw`. Local +z is ahead.
static void RsBalloon_LocalToWorld(f32 x, f32 z, s16 yaw, f32 lx, f32 lz, f32* outX, f32* outZ) {
    const f32 s = Math_SinS(yaw);
    const f32 c = Math_CosS(yaw);

    *outX = x + lx * c + lz * s;
    *outZ = z - lx * s + lz * c;
}

void RsBalloon_StandPos(const RsBalloon* this, Vec3f* out) {
    const Actor* a = &this->dyna.actor;

    RsBalloon_LocalToWorld(a->world.pos.x, a->world.pos.z, a->shape.rot.y, 0.0f, RS_BALLOON_STAND_Z, &out->x, &out->z);
    out->y = a->world.pos.y + RS_BALLOON_FLOOR_Y;
}

// Probe point i (0..RS_BALLOON_GRID_POINTS-1) of the grid across the basket's bottom, in world xz, for a basket at
// (x, z) turned to `yaw`: RS_BALLOON_GRID x RS_BALLOON_GRID points from corner to corner, ~27 apart. The ground and
// the climb's overhead lines both use it; something narrower than that between two points is missed.
static void RsBalloon_GridPoint(f32 x, f32 z, s16 yaw, s32 i, f32* outX, f32* outZ) {
    const f32 a = RS_BALLOON_HALF_WIDTH - RS_BALLOON_PROBE_INSET;
    const f32 step = 2.0f * a / (f32)(RS_BALLOON_GRID - 1);

    RsBalloon_LocalToWorld(x, z, yaw, -a + (f32)(i % RS_BALLOON_GRID) * step, -a + (f32)(i / RS_BALLOON_GRID) * step,
                           outX, outZ);
}

f32 RsBalloon_GroundAt(PlayState* play, Actor* self, f32 x, f32 z, s16 yaw, f32 alt, s32* outWater) {
    f32 ground = BGCHECK_Y_MIN;
    f32 surface = alt;
    s32 water = 0;
    s32 i;

    for (i = 0; i < RS_BALLOON_GRID_POINTS; i++) {
        Vec3f from;
        CollisionPoly* poly = NULL;
        s32 bgId = BGCHECK_SCENE;
        f32 y;

        RsBalloon_GridPoint(x, z, yaw, i, &from.x, &from.z);
        from.y = alt + RS_BALLOON_CLEAR_Y;
        y = BgCheck_EntityRaycastFloor4(&play->colCtx, &poly, &bgId, self, &from);
        ground = MAX(ground, y);
    }
    // Open water under its centre higher than the floor: it floats on the surface, as the boat does.
    if (RsBoat_SurfaceAt(play, x, z, &surface, self) && surface > ground) {
        ground = surface;
        water = 1;
    }
    if (outWater != NULL) {
        *outWater = water;
    }
    return ground;
}

// The fence round the basket at (x, z, yaw) with its bottom at `alt`: its edge, corner to side's middle and on round,
// and its two diagonals, at each of sFenceY. The first line that meets anything (wall, floor or ceiling, either
// face; the basket's own collision is skipped) is a hit: 1, and the poly's normal in `*outNormal`.
static s32 RsBalloon_FenceHit(RsBalloon* this, PlayState* play, f32 x, f32 z, s16 yaw, f32 alt, Vec3f* outNormal) {
    static const s8 sRingX[8] = { -1, 0, 1, 1, 1, 0, -1, -1 };
    static const s8 sRingZ[8] = { -1, -1, -1, 0, 1, 1, 1, 0 };
    const f32 a = RS_BALLOON_HALF_WIDTH - RS_BALLOON_PROBE_INSET;
    f32 ringX[8];
    f32 ringZ[8];
    s32 h;
    s32 i;

    for (i = 0; i < 8; i++) {
        RsBalloon_LocalToWorld(x, z, yaw, (f32)sRingX[i] * a, (f32)sRingZ[i] * a, &ringX[i], &ringZ[i]);
    }
    for (h = 0; h < (s32)ARRAY_COUNT(sFenceY); h++) {
        const f32 y = alt + sFenceY[h];

        for (i = 0; i < 10; i++) {
            // 0..7 the edge, 8 and 9 the diagonals (corner 0 to 2, corner 1 to 3: ring points 0-4 and 2-6)
            const s32 from = i < 8 ? i : (i - 8) * 2;
            const s32 to = i < 8 ? (i + 1) % 8 : (i - 8) * 2 + 4;
            Vec3f pa = { ringX[from], y, ringZ[from] };
            Vec3f pb = { ringX[to], y, ringZ[to] };
            Vec3f hit;
            CollisionPoly* poly = NULL;
            s32 bgId = BGCHECK_SCENE;

            if (BgCheck_EntityLineTest2(&play->colCtx, &pa, &pb, &hit, &poly, true, true, true, false, &bgId,
                                        &this->dyna.actor)) {
                outNormal->x = poly != NULL ? COLPOLY_GET_NORMAL(poly->normal.x) : 0.0f;
                outNormal->y = poly != NULL ? COLPOLY_GET_NORMAL(poly->normal.y) : 0.0f;
                outNormal->z = poly != NULL ? COLPOLY_GET_NORMAL(poly->normal.z) : 0.0f;
                return 1;
            }
        }
    }
    return 0;
}

// Whether the basket fits at (x, z, yaw) with its bottom at `*ioAlt`: the ground under it rises by no more than a
// step (then it is skimmed: `*ioAlt` rises onto it), and the fence at that height meets nothing. 0 and the reason
// otherwise, with the normal of what the fence met.
static s32 RsBalloon_Fits(RsBalloon* this, PlayState* play, f32 x, f32 z, s16 yaw, f32* ioAlt, s32* outBlock,
                          Vec3f* outNormal) {
    f32 alt = *ioAlt;
    const f32 ground = RsBalloon_GroundAt(play, &this->dyna.actor, x, z, yaw, alt, NULL);

    outNormal->x = outNormal->y = outNormal->z = 0.0f;
    if (ground > alt) {
        if (ground - alt > RS_BALLOON_STEP) {
            *outBlock = RS_BALLOON_BLOCK_GROUND;
            return 0;
        }
        alt = ground;
    }
    if (RsBalloon_FenceHit(this, play, x, z, yaw, alt, outNormal)) {
        *outBlock = RS_BALLOON_BLOCK_WALL;
        return 0;
    }
    *ioAlt = alt;
    return 1;
}

// A refusal is reported once a stop (the `blocked` marker), and re-armed after RS_BALLOON_FREE_TICKS free moves: a
// basket wedged between a wall and a wall top flips between `wall` and `ground` every tick, and reported each change.
static void RsBalloon_NoteBlock(RsBalloon* this, s32 block) {
    this->blocked = block;
    this->blockCount++;
    this->freeTicks = 0;
    if (this->markedBlock == RS_BALLOON_BLOCK_NONE) {
        this->markedBlock = block;
        RsBalloon_Mark(this, "blocked");
    }
}

static void RsBalloon_NoteFree(RsBalloon* this) {
    if (++this->freeTicks > RS_BALLOON_FREE_TICKS) {
        this->markedBlock = RS_BALLOON_BLOCK_NONE;
    }
}

// One tick's move and turn, (nextX, nextZ, nextYaw). Tried whole; then, if the fence met a wall, slid along it once
// (the part of the move into the wall's normal taken out, and the speed cut to what is left of it ahead); then the
// turn alone and the move alone; failing all of them it stops.
static void RsBalloon_Move(RsBalloon* this, PlayState* play, f32 nextX, f32 nextZ, s16 nextYaw) {
    Actor* thisx = &this->dyna.actor;
    const f32 x = thisx->world.pos.x;
    const f32 z = thisx->world.pos.z;
    f32 alt = this->alt;
    s32 block = RS_BALLOON_BLOCK_NONE;
    s32 ignored;
    Vec3f normal;
    Vec3f unused;

    if (RsBalloon_Fits(this, play, nextX, nextZ, nextYaw, &alt, &block, &normal)) {
        thisx->world.pos.x = nextX;
        thisx->world.pos.z = nextZ;
        thisx->shape.rot.y = nextYaw;
        this->alt = alt;
        this->blocked = RS_BALLOON_BLOCK_NONE;
        RsBalloon_NoteFree(this);
        return;
    }
    // A basket already in something - put down across a wall or a wall's top, or risen into an overhang between the
    // climb's lines - would refuse every move, because whatever it is in is met wherever it goes. It takes the move
    // whole instead, skimming ground up to a step as any move does but never popped up onto a wall's top, so it can
    // always be flown out.
    if (RsBalloon_GroundAt(play, thisx, x, z, thisx->shape.rot.y, this->alt, NULL) - this->alt > RS_BALLOON_STEP ||
        RsBalloon_FenceHit(this, play, x, z, thisx->shape.rot.y, this->alt, &unused)) {
        const f32 ground = RsBalloon_GroundAt(play, thisx, nextX, nextZ, nextYaw, this->alt, NULL);

        thisx->world.pos.x = nextX;
        thisx->world.pos.z = nextZ;
        thisx->shape.rot.y = nextYaw;
        if (ground > this->alt && ground - this->alt <= RS_BALLOON_STEP) {
            this->alt = ground;
        }
        RsBalloon_NoteBlock(this, block);
        return;
    }
    if (block == RS_BALLOON_BLOCK_WALL && (normal.x != 0.0f || normal.z != 0.0f)) {
        const f32 len = sqrtf(normal.x * normal.x + normal.z * normal.z);
        const f32 nx = normal.x / len;
        const f32 nz = normal.z / len;
        const f32 dx = nextX - x;
        const f32 dz = nextZ - z;
        const f32 into = dx * nx + dz * nz;
        const f32 sx = x + dx - into * nx;
        const f32 sz = z + dz - into * nz;

        alt = this->alt;
        if (RsBalloon_Fits(this, play, sx, sz, nextYaw, &alt, &ignored, &unused)) {
            const f32 along = (sx - x) * Math_SinS(nextYaw) + (sz - z) * Math_CosS(nextYaw);

            thisx->world.pos.x = sx;
            thisx->world.pos.z = sz;
            thisx->shape.rot.y = nextYaw;
            this->alt = alt;
            this->speed = CLAMP(along, 0.0f, this->speed);
            RsBalloon_NoteBlock(this, block);
            return;
        }
    }
    alt = this->alt;
    if (nextYaw != thisx->shape.rot.y && RsBalloon_Fits(this, play, x, z, nextYaw, &alt, &ignored, &unused)) {
        thisx->shape.rot.y = nextYaw;
        this->alt = alt;
        this->speed = 0.0f;
        RsBalloon_NoteBlock(this, block);
        return;
    }
    alt = this->alt;
    if (this->speed > 0.0f) {
        const f32 mx = x + Math_SinS(thisx->shape.rot.y) * this->speed;
        const f32 mz = z + Math_CosS(thisx->shape.rot.y) * this->speed;

        if (RsBalloon_Fits(this, play, mx, mz, thisx->shape.rot.y, &alt, &ignored, &unused)) {
            thisx->world.pos.x = mx;
            thisx->world.pos.z = mz;
            this->alt = alt;
            RsBalloon_NoteBlock(this, block);
            return;
        }
    }
    this->speed = 0.0f;
    RsBalloon_NoteBlock(this, block);
}

// The highest point a climb from `alt` to `nextAlt` may reach: lines straight up over the probe grid from Link's head
// height, stopping a unit under the first thing they meet. `nextAlt` when nothing is in the way.
static f32 RsBalloon_Overhead(RsBalloon* this, PlayState* play, f32 alt, f32 nextAlt, s32* outHit) {
    const Actor* thisx = &this->dyna.actor;
    f32 top = nextAlt;
    s32 i;

    *outHit = 0;
    for (i = 0; i < RS_BALLOON_GRID_POINTS; i++) {
        Vec3f pa;
        Vec3f pb;
        Vec3f hit;
        CollisionPoly* poly = NULL;
        s32 bgId = BGCHECK_SCENE;

        RsBalloon_GridPoint(thisx->world.pos.x, thisx->world.pos.z, thisx->shape.rot.y, i, &pa.x, &pa.z);
        pb.x = pa.x;
        pb.z = pa.z;
        pa.y = alt + RS_BALLOON_CLEAR_Y - 1.0f;
        pb.y = nextAlt + RS_BALLOON_CLEAR_Y + 1.0f;
        if (BgCheck_EntityLineTest2(&play->colCtx, &pa, &pb, &hit, &poly, true, true, true, false, &bgId,
                                    &this->dyna.actor)) {
            top = MIN(top, hit.y - RS_BALLOON_CLEAR_Y - 1.0f);
            *outHit = 1;
        }
    }
    return MAX(top, alt);
}

// --- Link and the controls ----------------------------------------------------------------------

s32 RsBalloon_LinkAboard(const RsBalloon* this, const Player* player) {
    return this->dyna.bgId >= 0 && player->actor.floorBgId == (u8)this->dyna.bgId &&
           (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
}

void RsBalloon_StandAtControls(const RsBalloon* this, Player* player, f32 lift) {
    Vec3f stand;

    RsBalloon_StandPos(this, &stand);
    stand.y += lift;
    RsSteer_StandLinkAt(player, &stand, this->dyna.actor.shape.rot.y);
}

static s32 RsBalloon_ControlsReachable(const RsBalloon* this, const Player* player) {
    Vec3f stand;

    if (this->controls || !RsBalloon_LinkAboard(this, player) || !RsSteer_LinkCanTake(player)) {
        return 0;
    }
    RsBalloon_StandPos(this, &stand);
    return Math_Vec3f_DistXZ(&stand, (Vec3f*)&player->actor.world.pos) < RS_BALLOON_CONTROLS_RANGE;
}

s32 RsBalloon_ControlsInReach(PlayState* play) {
    const Player* player = GET_PLAYER(play);
    Actor* a;

    if (Message_GetState(&play->msgCtx) != TEXT_STATE_NONE) {
        return 0;
    }
    for (a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_RS_BALLOON && a->update != NULL && RsBalloon_ControlsReachable((const RsBalloon*)a, player)) {
            return 1;
        }
    }
    return 0;
}

s32 RsBalloon_LinkAtControls(PlayState* play) {
    Actor* a;

    for (a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_RS_BALLOON && a->update != NULL && ((const RsBalloon*)a)->controls) {
            return 1;
        }
    }
    return 0;
}

s32 RsBalloon_IsBasketFloor(PlayState* play, s32 bgId) {
    Actor* a;

    for (a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_RS_BALLOON && a->update != NULL && ((const RsBalloon*)a)->dyna.bgId >= 0 &&
            (u8)((const RsBalloon*)a)->dyna.bgId == (u8)bgId) {
            return 1;
        }
    }
    return 0;
}

s32 RsBalloon_LinkWasAboard(PlayState* play) {
    Actor* a;

    for (a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_RS_BALLOON && a->update != NULL && ((const RsBalloon*)a)->linkAboard) {
            return 1;
        }
    }
    return 0;
}

// --- Link's shadow (#267) -----------------------------------------------------------------------------------------

// The placeholder basket's faces for Link's shadow (RsVehicleShadow.h): every box's six quads, two triangles each,
// each wound to face the way its vertices' normal says (the boxes are drawn without culling, so their winding is not
// otherwise kept). Built once; a basket model drawn from an archive would read its deck from there instead.
#define RS_BALLOON_BOXES 6
#define RS_BALLOON_DECK_TRIS (RS_BALLOON_BOXES * 6 * 2)

static const RsVehicleDeck* RsBalloon_Deck(void) {
    static const RsVehicleDeck* sDeck;
    static f32 sTris[RS_BALLOON_DECK_TRIS][3][3];
    const Vtx* boxes[RS_BALLOON_BOXES] = { sBasket.floorBox,   sBasket.wallBox[0], sBasket.wallBox[1],
                                           sBasket.wallBox[2], sBasket.wallBox[3], sBasket.panelBox };
    s32 count = 0;
    s32 b;
    s32 q;

    if (sDeck != NULL || !sBasket.built) {
        return sDeck;
    }
    for (b = 0; b < RS_BALLOON_BOXES; b++) {
        for (q = 0; q < 6; q++) {
            const Vtx* v = &boxes[b][q * 4];
            static const u8 kTri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
            s32 t;

            for (t = 0; t < 2; t++) {
                f32(*tri)[3] = sTris[count++];
                f32 u[3];
                f32 w[3];
                s32 k;

                for (k = 0; k < 3; k++) {
                    tri[k][0] = v[kTri[t][k]].n.ob[0];
                    tri[k][1] = v[kTri[t][k]].n.ob[1];
                    tri[k][2] = v[kTri[t][k]].n.ob[2];
                }
                for (k = 0; k < 3; k++) {
                    u[k] = tri[1][k] - tri[0][k];
                    w[k] = tri[2][k] - tri[0][k];
                }
                // (b - a) x (c - a) against the quad's normal: wound the other way, swap b and c.
                if ((u[1] * w[2] - u[2] * w[1]) * v[0].n.n[0] + (u[2] * w[0] - u[0] * w[2]) * v[0].n.n[1] +
                        (u[0] * w[1] - u[1] * w[0]) * v[0].n.n[2] <
                    0.0f) {
                    for (k = 0; k < 3; k++) {
                        const f32 swap = tri[1][k];

                        tri[1][k] = tri[2][k];
                        tri[2][k] = swap;
                    }
                }
            }
        }
    }
    sDeck = RsVehicleDeck_FromTris("rs_balloon/placeholder", (const f32(*)[3][3])sTris, count);
    return sDeck;
}

static f32 RsBalloon_CamSetting(const char* cvar, s32 def, s32 min, s32 max) {
    return (f32)CLAMP(CVarGetInteger(cvar, def), min, max);
}

static void RsBalloon_CamTarget(const RsBalloon* this, const Player* player, Vec3f* at, Vec3f* eye) {
    const f32 height = RsBalloon_CamSetting(CVAR_RS_BALLOON_CAM_HEIGHT, RS_BALLOON_CAM_HEIGHT_DEFAULT,
                                            RS_BALLOON_CAM_HEIGHT_MIN, RS_BALLOON_CAM_HEIGHT_MAX);
    const f32 dist = RsBalloon_CamSetting(CVAR_RS_BALLOON_CAM_DIST, RS_BALLOON_CAM_DIST_DEFAULT,
                                          RS_BALLOON_CAM_DIST_MIN, RS_BALLOON_CAM_DIST_MAX);
    const s16 yaw = this->dyna.actor.shape.rot.y;
    const f32 s = Math_SinS(yaw);
    const f32 c = Math_CosS(yaw);
    const f32 floorY = this->dyna.actor.world.pos.y + RS_BALLOON_FLOOR_Y;

    at->x = player->actor.world.pos.x + s * RS_BALLOON_CAM_AT_AHEAD;
    at->y = floorY + RS_BALLOON_CAM_AT_Y;
    at->z = player->actor.world.pos.z + c * RS_BALLOON_CAM_AT_AHEAD;
    eye->x = player->actor.world.pos.x - s * dist;
    eye->y = floorY + height;
    eye->z = player->actor.world.pos.z - c * dist;
}

s32 RsBalloon_SetControls(RsBalloon* this, PlayState* play, s32 on) {
    Player* player = GET_PLAYER(play);

    if (on == this->controls) {
        return 1;
    }
    if (on) {
        if (!RsBalloon_LinkAboard(this, player)) {
            return 0;
        }
        // Facing ahead, so the stick's left is the basket's left, and on the floor: A from a rail top, which is the
        // basket too (RsBalloon_LinkAboard), would otherwise leave him in the air over the stand point.
        RsBalloon_StandAtControls(this, player, 1.0f);
        RsSteer_Take(player);
        this->controls = 1;
        RsSteerCam_Start(&this->cam, play, CVarGetInteger(CVAR_RS_BALLOON_CAM, 1));
        RsBalloon_Mark(this, "controls_on");
    } else {
        RsSteer_LetGo(player);
        this->controls = 0;
        RsSteerCam_Stop(&this->cam, play);
        RsBalloon_Mark(this, "controls_off");
    }
    return 1;
}

// The controls' inputs: the pad, or the agent loop's override. `ahead` and `back` 0..1, `turn` -1..1 (+ left),
// `lift` -1..1 (+ climbs).
static void RsBalloon_ReadControls(PlayState* play, f32* ahead, f32* back, f32* turn, f32* lift, s32* letGo) {
    const Input* input = &play->state.input[0];
    s32 up;
    s32 down;

    *letGo = (input->press.button & BTN_B) != 0;
    if (gRsBalloonDrive.ticks > 0) {
        gRsBalloonDrive.ticks--;
        *ahead = MAX(gRsBalloonDrive.throttle, 0.0f);
        *back = MAX(-gRsBalloonDrive.throttle, 0.0f);
        *turn = gRsBalloonDrive.turn;
        *lift = gRsBalloonDrive.lift;
        return;
    }
    RsSteer_ReadStick(input, ahead, back, turn);
    up = (input->cur.button & (BTN_CUP | BTN_CUSTOM_MODIFIER1)) != 0;
    down = (input->cur.button & (BTN_CDOWN | BTN_CUSTOM_MODIFIER2)) != 0;
    *lift = (up && !down) ? 1.0f : (down && !up) ? -1.0f : 0.0f;
}

// --- actor --------------------------------------------------------------------------------------

void RsBalloon_Init(Actor* thisx, PlayState* play) {
    RsBalloon* this = (RsBalloon*)thisx;

    if (!sBasket.built) {
        RsBalloon_BuildBasket();
    }
    (void)RsBalloon_Deck(); // Link's shadow's deck, built now rather than as he first climbs in
    Actor_SetScale(thisx, 1.0f);
    // Position and yaw: the engine's moving-platform path carries Link up, down and along, and turns him with it.
    DynaPolyActor_Init(&this->dyna, DPM_UNK3);
    this->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, thisx, &sBasket.col);

    thisx->shape.rot.x = 0;
    thisx->shape.rot.z = 0;
    thisx->world.rot = thisx->shape.rot;
    thisx->uncullZoneForward = 6000.0f;
    thisx->uncullZoneScale = 300.0f;
    thisx->uncullZoneDownward = 3000.0f;

    this->controls = 0;
    this->vy = 0.0f;
    this->speed = 0.0f;
    this->tiller = 0.0f;
    this->yawVel = 0;
    this->blocked = RS_BALLOON_BLOCK_NONE;
    this->blockCount = 0;
    this->markedBlock = RS_BALLOON_BLOCK_NONE;
    this->freeTicks = 0;
    this->press = 0;
    this->linkAboard = 0;
    this->awayTicks = 0;
    this->abandoned = 0;
    this->throttle = 0.0f;
    this->lift = 0.0f;
    this->cam.subCamId = SUBCAM_NONE;
    this->alt = thisx->world.pos.y;
    this->groundY = RsBalloon_GroundAt(play, thisx, thisx->world.pos.x, thisx->world.pos.z, thisx->shape.rot.y,
                                       this->alt, &this->onWater);
    // Put down on (or a hair over) the ground: it stands on it. Put down higher, it hovers there (the issue's
    // stage 1, `balloon spawn ... <height>`).
    this->landed = this->groundY > BGCHECK_Y_MIN && this->alt - this->groundY < 1.0f;
    if (this->landed) {
        this->alt = this->groundY;
    }
    // Where `ceiling` counts from and `abandon home` puts it back: the ground under the spot it was put down at.
    this->launchY = this->groundY > BGCHECK_Y_MIN ? this->groundY : this->alt;
    thisx->world.pos.y = roundf(this->alt);
    RsBalloon_Mark(this, "spawn");
}

void RsBalloon_Destroy(Actor* thisx, PlayState* play) {
    RsBalloon* this = (RsBalloon*)thisx;

    if (this->controls) {
        RsSteer_LetGo(GET_PLAYER(play));
        RsSteerCam_Stop(&this->cam, play);
    }
    DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, this->dyna.bgId);
}

// Left alone for long enough (RsBalloon.h, LEFT ALONE): once per time Link leaves it, and again if `balloon abandon`
// picks another mode meanwhile. `home` puts it back where it was put down, landed, wherever it is; `sink` and
// `hang` only start in the air.
static void RsBalloon_LeftAlone(RsBalloon* this, s32 mode) {
    Actor* thisx = &this->dyna.actor;

    if (mode == RS_BALLOON_ABANDON_HOME) {
        this->abandoned = mode + 1;
        if (Math_Vec3f_DistXZ(&thisx->world.pos, &thisx->home.pos) > 1.0f || !this->landed) {
            thisx->world.pos.x = thisx->home.pos.x;
            thisx->world.pos.z = thisx->home.pos.z;
            thisx->shape.rot.y = thisx->home.rot.y;
            this->alt = this->launchY;
            this->landed = 1;
            this->vy = 0.0f;
            this->speed = 0.0f;
            this->tiller = 0.0f;
            thisx->world.pos.y = roundf(this->alt);
            RsBalloon_Mark(this, "home");
        }
    } else if (!this->landed) {
        this->abandoned = mode + 1;
        RsBalloon_Mark(this, "abandoned");
    }
}

void RsBalloon_Update(Actor* thisx, PlayState* play) {
    RsBalloon* this = (RsBalloon*)thisx;
    Player* player = GET_PLAYER(play);
    const Input* input = &play->state.input[0];
    const s32 aDown = (input->cur.button & BTN_A) != 0;
    const s32 mode = RsBalloon_AbandonMode();
    f32 ahead = 0.0f;
    f32 back = 0.0f;
    f32 turn = 0.0f;
    f32 lift = 0.0f;
    s32 letGo = 0;
    f32 vyTarget;
    f32 top;
    s16 nextYaw;
    s32 touchedDown = 0;

    // Read before Player's update this tick, so it is where Link stood last tick.
    this->linkAboard = RsBalloon_LinkAboard(this, player);
    if (this->linkAboard) {
        this->awayTicks = 0;
        this->abandoned = 0;
    } else if (this->awayTicks < 1000000) { // saturates: only `balloon list` reads the count past 2 s
        this->awayTicks++;
    }

    // Take the controls: a fresh A press with them in reach, and no textbox wanting the A.
    if (!this->controls && aDown && !this->press && RsBalloon_ControlsReachable(this, player) &&
        Message_GetState(&play->msgCtx) == TEXT_STATE_NONE) {
        RsBalloon_SetControls(this, play, 1);
    }
    this->press = aDown;

    if (this->controls) {
        Vec3f stand;

        RsBalloon_ReadControls(play, &ahead, &back, &turn, &lift, &letGo);
        RsBalloon_StandPos(this, &stand);
        if (letGo || RsSteer_Knocked(player, &stand, RS_BALLOON_KNOCKED) || !RsBalloon_LinkAboard(this, player)) {
            RsBalloon_SetControls(this, play, 0);
            ahead = back = turn = lift = 0.0f;
        }
    }
    this->throttle = ahead > 0.0f ? ahead : -back;
    this->lift = lift;

    if (!this->controls && this->abandoned != mode + 1 && this->awayTicks >= RS_BALLOON_ABANDON_TICKS) {
        RsBalloon_LeftAlone(this, mode);
    }

    // The vertical speed, stepped toward what is asked: the buttons at the controls, `abandonsink` once left alone
    // to sink, otherwise a hover (or the idle drift).
    if (this->controls) {
        vyTarget = lift > 0.0f   ? lift * RsBalloon_Tune(RS_BALLOON_TUNE_CLIMB)
                   : lift < 0.0f ? lift * RsBalloon_Tune(RS_BALLOON_TUNE_SINK)
                                 : -RsBalloon_Tune(RS_BALLOON_TUNE_DRIFT);
    } else if (this->abandoned && mode == RS_BALLOON_ABANDON_SINK) {
        vyTarget = -RsBalloon_Tune(RS_BALLOON_TUNE_ABANDON_SINK);
    } else if (this->abandoned && mode == RS_BALLOON_ABANDON_HANG) {
        vyTarget = 0.0f;
    } else {
        vyTarget = -RsBalloon_Tune(RS_BALLOON_TUNE_DRIFT);
    }
    if (this->landed && vyTarget > 0.0f) {
        this->landed = 0;
        RsBalloon_Mark(this, "liftoff");
    }
    if (this->landed) {
        this->vy = 0.0f;
    } else {
        Math_StepToF(&this->vy, vyTarget, RsBalloon_Tune(RS_BALLOON_TUNE_LIFT));
    }

    // Ahead only, by how far the stick is pushed; pulled back it brakes; let go it coasts. On the ground it neither
    // moves nor turns.
    top = ahead * RsBalloon_Tune(RS_BALLOON_TUNE_FORWARD);
    if (this->landed) {
        this->speed = 0.0f;
    } else if (back > 0.0f) {
        Math_StepToF(&this->speed, 0.0f, RsBalloon_Tune(RS_BALLOON_TUNE_BRAKE) * back);
    } else if (top > this->speed) {
        Math_StepToF(&this->speed, top, RsBalloon_Tune(RS_BALLOON_TUNE_ACCEL));
    } else {
        Math_StepToF(&this->speed, top, RsBalloon_Tune(RS_BALLOON_TUNE_DRAG));
    }
    this->tiller += ((this->landed ? 0.0f : turn) - this->tiller) * RS_BALLOON_TILLER_EASE;
    this->yawVel = this->landed ? 0 : (s16)(this->tiller * RsBalloon_Tune(RS_BALLOON_TUNE_TURN));

    // Up or down, against the ground under it, what is overhead and the ceiling.
    this->groundY =
        RsBalloon_GroundAt(play, thisx, thisx->world.pos.x, thisx->world.pos.z, thisx->shape.rot.y, this->alt,
                           &this->onWater);
    if (this->landed) {
        // It follows the ground it stands on within a step (a floor that moves, or the water's surface); ground that
        // falls away further leaves it hovering where it was.
        if (fabsf(this->groundY - this->alt) <= RS_BALLOON_STEP) {
            this->alt = this->groundY;
        } else if (this->groundY < this->alt) {
            this->landed = 0;
        }
    } else {
        f32 next = this->alt + this->vy;

        if (this->vy > 0.0f) {
            const f32 cap = this->launchY + RsBalloon_Tune(RS_BALLOON_TUNE_CEILING);
            s32 hit;

            next = RsBalloon_Overhead(this, play, this->alt, next, &hit);
            if (hit) {
                this->vy = 0.0f;
                RsBalloon_NoteBlock(this, RS_BALLOON_BLOCK_OVERHEAD);
            }
            if (next > cap) {
                next = this->alt > cap ? this->alt : cap;
                this->vy = 0.0f;
                RsBalloon_NoteBlock(this, RS_BALLOON_BLOCK_CEILING);
            }
        }
        // Ground more than a step over its bottom is something it is in (a wall's top across a corner), not ground it
        // stands on: it is not lifted onto it, and flies out (RsBalloon_Move).
        if (next <= this->groundY && this->groundY - this->alt <= RS_BALLOON_STEP) {
            next = this->groundY;
            if (this->vy <= 0.0f) {
                this->landed = 1;
                this->vy = 0.0f;
                this->speed = 0.0f;
                this->tiller = 0.0f;
                this->yawVel = 0;
                touchedDown = 1;
            }
        }
        this->alt = next;
    }

    nextYaw = (s16)(thisx->shape.rot.y + this->yawVel);
    if (!this->landed && (this->speed > 0.0f || this->yawVel != 0)) {
        RsBalloon_Move(this, play, thisx->world.pos.x + Math_SinS(nextYaw) * this->speed,
                       thisx->world.pos.z + Math_CosS(nextYaw) * this->speed, nextYaw);
    }
    // Whole units, so the collision the engine truncates is the drawn basket exactly (RsBalloon.h, THE BASKET).
    thisx->world.pos.y = roundf(this->alt);
    thisx->world.rot.y = thisx->shape.rot.y;
    if (touchedDown) {
        RsBalloon_Mark(this, "landed");
    }

    if (this->controls && this->cam.subCamId != SUBCAM_NONE) {
        Vec3f at;
        Vec3f eye;

        RsBalloon_CamTarget(this, player, &at, &eye);
        RsSteerCam_Update(&this->cam, play, &at, &eye,
                          RsBalloon_CamSetting(CVAR_RS_BALLOON_CAM_EASE, RS_BALLOON_CAM_EASE_DEFAULT,
                                               RS_BALLOON_CAM_EASE_MIN, RS_BALLOON_CAM_EASE_MAX) /
                              100.0f);
    }
}

// --- drawing ------------------------------------------------------------------------------------

static void RsBalloon_DrawBox(GraphicsContext* gfxCtx, Vtx* vtx, u8 r, u8 g, u8 b, u8 topR, u8 topG, u8 topB) {
    OPEN_DISPS(gfxCtx);

    gSPVertex(POLY_OPA_DISP++, (uintptr_t)vtx, RS_BALLOON_BOX_VTX, 0);
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

void RsBalloon_Draw(Actor* thisx, PlayState* play) {
    RsBalloon* this = (RsBalloon*)thisx;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    s32 k;

    OPEN_DISPS(gfxCtx);

    Gfx_SetupDL_25Opa(gfxCtx);
    // Actor_Draw has set the basket's matrix: its position and yaw, as its collision has them.
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    // PRIMITIVE * SHADE: the part's colour, lit by the scene through each face's normal.
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, 0, SHADE, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, SHADE, 0, 0, 0, 0,
                      PRIMITIVE);

    CLOSE_DISPS(gfxCtx);

    // Wicker browns; the front wall reddened so the heading reads; the panel dark with a burner-orange top.
    RsBalloon_DrawBox(gfxCtx, sBasket.floorBox, 96, 66, 36, 120, 86, 48);
    for (k = 0; k < 4; k++) {
        if (k == 2) {
            RsBalloon_DrawBox(gfxCtx, sBasket.wallBox[k], 150, 64, 44, 190, 96, 64);
        } else {
            RsBalloon_DrawBox(gfxCtx, sBasket.wallBox[k], 150, 110, 60, 190, 150, 90);
        }
    }
    RsBalloon_DrawBox(gfxCtx, sBasket.panelBox, 50, 50, 60, 230, 110, 30);

    OPEN_DISPS(gfxCtx);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    CLOSE_DISPS(gfxCtx);

    // Last: its own render state, after the basket it lies on.
    {
        const RsVehicleShadowSource shadow = {
            thisx, this->dyna.bgId, this->linkAboard, RsBalloon_Deck(), 0.0f, 0.0f, RS_VEHICLE_SHADOW_KIND_BASKET,
        };

        RsVehicleShadow_Draw(play, &shadow);
    }
}
