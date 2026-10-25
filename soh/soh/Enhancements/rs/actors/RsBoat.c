/*
 * RsBoat.c - a crude steerable boat (sturdy-bassoon#256). RsBoat.h has the design: the platform, the helm,
 * the water and the shore.
 *
 * Same /W3 /WX rules as the other rs/ actors: explicit casts, no `s32 pad`, no ICHAIN.
 */

#include <math.h>
#include <stdio.h> // snprintf, for the agent-loop markers

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
// Per 20 Hz tick. Link's run is ~6 units a tick as adult, so the boat's top speed outruns him a little.
#define RS_BOAT_MAX_FORWARD 8.0f
#define RS_BOAT_MAX_REVERSE 2.5f
#define RS_BOAT_ACCEL 0.25f
#define RS_BOAT_DRAG 0.15f
#define RS_BOAT_TILLER_EASE 0.25f  // of the way to the stick's target, each tick: turning has some weight
#define RS_BOAT_TURN_STILL 110.0f  // binang a tick at full tiller, standing still
#define RS_BOAT_TURN_MOVING 300.0f // added at top speed
#define RS_BOAT_STICK_FULL 70.0f   // stick travel that reads as full tiller (the stick reads -85..85)
#define RS_BOAT_STICK_BACK -40.0f  // stick y below this reverses

// The bob and the rock (tier 1 of the issue's waves): a slow sine on the height, a slower one on the tilt.
#define RS_BOAT_BOB_AMP 1.8f
#define RS_BOAT_BOB_PERIOD 64.0f   // ticks: ~3.2 s
#define RS_BOAT_ROCK_PERIOD 101.0f // ticks: ~5 s, out of step with the bob on purpose
#define RS_BOAT_ROLL_AMP 0x01C0    // ~2.5 degrees
#define RS_BOAT_PITCH_AMP 0x00E0   // ~1.2 degrees

// The wall probe runs this far above the surface: under the deck, above any lip a shore has at the waterline.
#define RS_BOAT_PROBE_Y 8.0f
// Moves in a row that must fit before a refusal is reported again (the `blocked` marker): half a second.
#define RS_BOAT_FREE_TICKS 10
// A waterbox surface this far from the boat's own is a different body of water (a pool at another height).
#define RS_BOAT_SURFACE_TOLERANCE 2.0f

// --- the hull: collision ------------------------------------------------------------------------
//
// Built once, at the first Init, so the normals are computed rather than typed. 8 corners, 10 polys: a deck
// and four sides. No bottom, which nothing stands on and the camera never sees from inside.
static Vec3s sHullVtx[8];
static CollisionPoly sHullPolys[10];
static SurfaceType sHullSurface[1] = {
    // wood footsteps (material 9, the bridge planks); everything else zero: a plain floor
    { { 0, 9 } },
};
// The camera reads the floor's camera data under Link, and dereferences the list without a null check
// (BgCheck_GetBgCamSettingImpl). CAM_SET_NONE is what makes it keep the scene's own setting.
static CamData sHullCamData[1] = { { CAM_SET_NONE, 0, NULL } };
static CollisionHeader sHullCol;
static s32 sHullBuilt = 0;

static void RsBoat_AddQuad(s32* polyIdx, s32 a, s32 b, s32 c, s32 d, f32 outX, f32 outY, f32 outZ) {
    s32 tri[2][3] = { { a, b, c }, { a, c, d } };
    s32 t;

    for (t = 0; t < 2; t++) {
        CollisionPoly* poly = &sHullPolys[*polyIdx];
        Vec3s* va = &sHullVtx[tri[t][0]];
        Vec3s* vb = &sHullVtx[tri[t][1]];
        Vec3s* vc = &sHullVtx[tri[t][2]];
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
        s32 ib = tri[t][1];
        s32 ic = tri[t][2];

        nx /= len;
        ny /= len;
        nz /= len;
        if (nx * outX + ny * outY + nz * outZ < 0.0f) {
            // wound the other way: swap B and C, which flips the normal
            ib = tri[t][2];
            ic = tri[t][1];
            nx = -nx;
            ny = -ny;
            nz = -nz;
        }
        poly->type = 0;
        poly->flags_vIA = COLPOLY_VTX(tri[t][0], 0);
        poly->flags_vIB = COLPOLY_VTX(ib, 0);
        poly->vIC = COLPOLY_VTX_INDEX(ic);
        poly->normal.x = COLPOLY_SNORMAL(nx);
        poly->normal.y = COLPOLY_SNORMAL(ny);
        poly->normal.z = COLPOLY_SNORMAL(nz);
        poly->dist = (s16)lroundf(-(nx * va->x + ny * va->y + nz * va->z));
        (*polyIdx)++;
    }
}

static void RsBoat_BuildHull(void) {
    const s16 w = (s16)RS_BOAT_HALF_WIDTH;
    const s16 l = (s16)RS_BOAT_HALF_LENGTH;
    const s16 lo = (s16)RS_BOAT_SIDE_FOOT_Y;
    const s16 hi = (s16)RS_BOAT_DECK_Y;
    const Vec3s corners[8] = {
        { (s16)-w, lo, (s16)-l }, { w, lo, (s16)-l }, { w, lo, l }, { (s16)-w, lo, l },
        { (s16)-w, hi, (s16)-l }, { w, hi, (s16)-l }, { w, hi, l }, { (s16)-w, hi, l },
    };
    s32 polyIdx = 0;
    s32 i;

    for (i = 0; i < 8; i++) {
        sHullVtx[i] = corners[i];
    }
    RsBoat_AddQuad(&polyIdx, 4, 5, 6, 7, 0.0f, 1.0f, 0.0f);  // deck
    RsBoat_AddQuad(&polyIdx, 1, 2, 6, 5, 1.0f, 0.0f, 0.0f);  // +x side
    RsBoat_AddQuad(&polyIdx, 0, 4, 7, 3, -1.0f, 0.0f, 0.0f); // -x side
    RsBoat_AddQuad(&polyIdx, 2, 3, 7, 6, 0.0f, 0.0f, 1.0f);  // bow
    RsBoat_AddQuad(&polyIdx, 0, 1, 5, 4, 0.0f, 0.0f, -1.0f); // stern

    sHullCol.minBounds.x = (s16)-w;
    sHullCol.minBounds.y = lo;
    sHullCol.minBounds.z = (s16)-l;
    sHullCol.maxBounds.x = w;
    sHullCol.maxBounds.y = hi;
    sHullCol.maxBounds.z = l;
    sHullCol.numVertices = (u32)ARRAY_COUNT(sHullVtx);
    sHullCol.vtxList = sHullVtx;
    sHullCol.numPolygons = (u32)ARRAY_COUNT(sHullPolys);
    sHullCol.polyList = sHullPolys;
    sHullCol.surfaceTypeList = sHullSurface;
    sHullCol.cameraDataList = sHullCamData;
    sHullCol.numWaterBoxes = 0;
    sHullCol.waterBoxes = NULL;
    sHullCol.cameraDataListLen = (size_t)ARRAY_COUNT(sHullCamData);
}

// --- the hull: drawing --------------------------------------------------------------------------
//
// Placeholder boxes (the issue allows a box until the RS model lands): the hull, a tiller post at the stern so
// the helm is findable, and a prow at the bow so the heading reads. Lit by the scene's lights through
// per-face normals; colour is the primitive colour, one per part.
#define RS_BOAT_BOX_VTX 24
static Vtx sHullBoxVtx[RS_BOAT_BOX_VTX];
static Vtx sTillerBoxVtx[RS_BOAT_BOX_VTX];
static Vtx sProwBoxVtx[RS_BOAT_BOX_VTX];

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

static void RsBoat_BuildMesh(void) {
    const s16 w = (s16)RS_BOAT_HALF_WIDTH;
    const s16 l = (s16)RS_BOAT_HALF_LENGTH;
    const s16 deck = (s16)RS_BOAT_DECK_Y;

    RsBoat_BuildBox(sHullBoxVtx, (s16)-w, (s16)RS_BOAT_KEEL_Y, (s16)-l, w, deck, l);
    // the tiller post: on the stern edge behind where Link stands to steer, and low, because the camera sits
    // behind Link at the helm and a tall post fills the frame
    RsBoat_BuildBox(sTillerBoxVtx, -4, deck, (s16)-l, 4, (s16)(deck + 16), (s16)(-l + 6));
    // the prow: a low fin along the bow's last stretch
    RsBoat_BuildBox(sProwBoxVtx, -5, deck, (s16)(l - 40), 5, (s16)(deck + 14), l);
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
                        0.0f, RS_BOAT_HELM_Z, &out->x, &out->z);
    out->y = boat->dyna.actor.world.pos.y + RS_BOAT_DECK_Y;
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
    return poly == NULL || COLPOLY_GET_NORMAL(poly->normal.y) < 0.0f;
}

s32 RsBoat_SurfaceAt(PlayState* play, f32 x, f32 z, f32* outY, Actor* skip) {
    CollisionHeader* header = play->colCtx.colHeader;
    s32 found = 0;
    f32 best = 0.0f;
    u32 i;

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
        }
    }
    if (found) {
        *outY = best;
    }
    return found;
}

// Whether a hull at (x, z, yaw) on a surface at `surface` fits: every edge point (the corners and the middle of
// each side) over water at that height, over a floor deeper than the draft, and with no wall between it and the
// hull's centre.
static s32 RsBoat_Fits(RsBoat* this, PlayState* play, f32 x, f32 z, s16 yaw, f32 surface) {
    static const f32 kPoints[8][2] = {
        { -RS_BOAT_HALF_WIDTH, RS_BOAT_HALF_LENGTH }, { RS_BOAT_HALF_WIDTH, RS_BOAT_HALF_LENGTH },
        { -RS_BOAT_HALF_WIDTH, -RS_BOAT_HALF_LENGTH }, { RS_BOAT_HALF_WIDTH, -RS_BOAT_HALF_LENGTH },
        { 0.0f, RS_BOAT_HALF_LENGTH }, { 0.0f, -RS_BOAT_HALF_LENGTH },
        { -RS_BOAT_HALF_WIDTH, 0.0f }, { RS_BOAT_HALF_WIDTH, 0.0f },
    };
    s32 i;

    for (i = 0; i < ARRAY_COUNT(kPoints); i++) {
        Vec3f point;
        Vec3f center;
        Vec3f hit;
        CollisionPoly* poly = NULL;
        s32 bgId = BGCHECK_SCENE;
        f32 pointSurface = surface;
        f32 floorY;

        RsBoat_LocalToWorld(x, z, yaw, kPoints[i][0], kPoints[i][1], &point.x, &point.z);
        if (!RsBoat_SurfaceAt(play, point.x, point.z, &pointSurface, &this->dyna.actor) ||
            fabsf(pointSurface - surface) > RS_BOAT_SURFACE_TOLERANCE) {
            this->blocked = RS_BOAT_BLOCK_NO_WATER;
            return 0;
        }
        // Down from above Link's head on the deck, skipping the boat's own collision: a bank, a dock or a rock
        // above the water reads as a floor above the surface, which is shallower than any draft. Starting
        // lower missed a bank that rises steeply: the raycast began inside it and found the bed below.
        point.y = surface + RS_BOAT_DECK_Y + RS_BOAT_HEADROOM;
        floorY = BgCheck_EntityRaycastFloor4(&play->colCtx, &poly, &bgId, &this->dyna.actor, &point);
        if (floorY > surface - RS_BOAT_DRAFT) {
            this->blocked = RS_BOAT_BLOCK_SHALLOW;
            return 0;
        }
        center.x = x;
        center.y = surface + RS_BOAT_PROBE_Y;
        center.z = z;
        point.y = center.y;
        if (BgCheck_EntityLineTest2(&play->colCtx, &center, &point, &hit, &poly, true, false, false, false, &bgId,
                                    &this->dyna.actor)) {
            this->blocked = RS_BOAT_BLOCK_WALL;
            return 0;
        }
        // Again at Link's waist on the deck: a wall that starts above the waterline (a dock's edge, a rail).
        center.y = surface + RS_BOAT_DECK_Y + 30.0f;
        point.y = center.y;
        if (BgCheck_EntityLineTest2(&play->colCtx, &center, &point, &hit, &poly, true, false, false, false, &bgId,
                                    &this->dyna.actor)) {
            this->blocked = RS_BOAT_BLOCK_WALL;
            return 0;
        }
        // Straight up from the deck at the point: anything in the way is lower than Link's head. Ground facing
        // up is land rising through the deck's height, which is the shore, not an overhang.
        center.x = point.x;
        center.z = point.z;
        center.y = surface + RS_BOAT_DECK_Y + 2.0f;
        point.y = surface + RS_BOAT_DECK_Y + RS_BOAT_HEADROOM;
        if (BgCheck_EntityLineTest2(&play->colCtx, &center, &point, &hit, &poly, true, true, true, false, &bgId,
                                    &this->dyna.actor)) {
            this->blocked = (poly != NULL && COLPOLY_GET_NORMAL(poly->normal.y) > 0.5f) ? RS_BOAT_BLOCK_SHALLOW
                                                                                       : RS_BOAT_BLOCK_HEADROOM;
            return 0;
        }
    }
    return 1;
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
// locked on to it with Z, which works from anywhere on the deck (the bow is ~200 from the tiller).
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
    const f32 deckY = this->dyna.actor.world.pos.y + RS_BOAT_DECK_Y;

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
        // Stand Link at the tiller facing the bow, so the stick's left is the boat's left.
        RsBoat_StandAtHelm(this, player, 0.0f);
        player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
        this->helm = 1;
        this->throttleArmed = 0;
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

// --- actor --------------------------------------------------------------------------------------

void RsBoat_Init(Actor* thisx, PlayState* play) {
    RsBoat* this = (RsBoat*)thisx;

    if (!sHullBuilt) {
        RsBoat_BuildHull();
        RsBoat_BuildMesh();
        sHullBuilt = 1;
    }

    Actor_SetScale(thisx, 1.0f);
    // Position and yaw: the engine's moving-platform path carries Link along and turns him with the hull.
    DynaPolyActor_Init(&this->dyna, DPM_UNK3);
    this->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, thisx, &sHullCol);

    thisx->shape.rot.x = 0;
    thisx->shape.rot.z = 0;
    thisx->world.rot = thisx->shape.rot;
    // Targetable from the whole deck and the water just around it: RsBoat_Update makes attention measure from
    // the tiller, and the bow is ~220 from it, inside range 240 (mode 8).
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
    this->markedBlock = RS_BOAT_BLOCK_NONE;
    this->freeTicks = 0;
    this->helmPress = 0;
    this->throttleArmed = 0;
    this->subCamId = SUBCAM_NONE;
    this->surfaceY = thisx->world.pos.y;
    this->onWater = RsBoat_SurfaceAt(play, thisx->world.pos.x, thisx->world.pos.z, &this->surfaceY, thisx);
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
static void RsBoat_ReadHelm(RsBoat* this, PlayState* play, f32* throttle, f32* turn, s32* letGo) {
    Input* input = &play->state.input[0];
    f32 stickX = (f32)input->cur.stick_x;
    f32 stickY = (f32)input->cur.stick_y;

    *letGo = (input->press.button & BTN_B) != 0;
    if (gRsBoatDrive.ticks > 0) {
        gRsBoatDrive.ticks--;
        *throttle = gRsBoatDrive.throttle;
        *turn = gRsBoatDrive.turn;
        return;
    }
    if (!(input->cur.button & BTN_A)) {
        this->throttleArmed = 1;
    }
    // The A that took the helm is still down: it is not throttle until it has been let go once.
    if ((input->cur.button & BTN_A) && this->throttleArmed) {
        *throttle = 1.0f;
    } else if (stickY < RS_BOAT_STICK_BACK) {
        *throttle = -1.0f;
    } else {
        *throttle = 0.0f;
    }
    // Stick left is a positive turn: yaw grows counter-clockwise seen from above, which is Link's left.
    *turn = CLAMP(-stickX / RS_BOAT_STICK_FULL, -1.0f, 1.0f);
}

void RsBoat_Update(Actor* thisx, PlayState* play) {
    RsBoat* this = (RsBoat*)thisx;
    Player* player = GET_PLAYER(play);
    Input* input = &play->state.input[0];
    f32 throttle = 0.0f;
    f32 turn = 0.0f;
    s32 letGo = 0;
    f32 target;
    f32 bob;
    f32 nextX;
    f32 nextZ;
    s16 nextYaw;
    s32 aDown = (input->cur.button & BTN_A) != 0;

    // No lag to make up for when drawing: DynaPoly_Setup runs between the BG and Player categories and takes the
    // hull's transform as this tick left it, so Player carries Link by this tick's move, not last tick's.
    this->timer++;

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

    // Speed toward the throttle's target; the tiller eases toward the stick, and turns harder with way on.
    target = throttle > 0.0f ? throttle * RS_BOAT_MAX_FORWARD : throttle * RS_BOAT_MAX_REVERSE;
    Math_StepToF(&this->speed, target, (fabsf(target) > fabsf(this->speed)) ? RS_BOAT_ACCEL : RS_BOAT_DRAG);
    this->tiller += (turn - this->tiller) * RS_BOAT_TILLER_EASE;
    this->yawVel = (s16)(this->tiller * (RS_BOAT_TURN_STILL +
                                         RS_BOAT_TURN_MOVING * fabsf(this->speed) / RS_BOAT_MAX_FORWARD));

    // The surface under the hull's centre. Off the water the boat does not move, and settles on whatever is
    // under its keel (beached; or Lake Hylia drained under it).
    this->onWater = RsBoat_SurfaceAt(play, thisx->world.pos.x, thisx->world.pos.z, &this->surfaceY, thisx);
    if (!this->onWater) {
        Vec3f above = thisx->world.pos;
        CollisionPoly* poly = NULL;
        s32 bgId = BGCHECK_SCENE;
        f32 groundY;

        above.y += RS_BOAT_DECK_Y + RS_BOAT_HEADROOM;
        groundY = BgCheck_EntityRaycastFloor4(&play->colCtx, &poly, &bgId, thisx, &above);
        if (groundY > BGCHECK_Y_MIN) {
            thisx->world.pos.y = groundY - RS_BOAT_KEEL_Y;
        }
        this->speed = 0.0f;
    }

    nextYaw = (s16)(thisx->shape.rot.y + this->yawVel);
    nextX = thisx->world.pos.x + Math_SinS(nextYaw) * this->speed;
    nextZ = thisx->world.pos.z + Math_CosS(nextYaw) * this->speed;

    if (this->onWater && (this->speed != 0.0f || this->yawVel != 0)) {
        this->blocked = RS_BOAT_BLOCK_NONE;
        if (RsBoat_Fits(this, play, nextX, nextZ, nextYaw, this->surfaceY)) {
            thisx->world.pos.x = nextX;
            thisx->world.pos.z = nextZ;
            thisx->shape.rot.y = nextYaw;
            // A crawl against the shore fits for a tick or two between refusals; only a run of moves re-arms.
            if (++this->freeTicks > RS_BOAT_FREE_TICKS) {
                this->markedBlock = RS_BOAT_BLOCK_NONE;
            }
        } else {
            s32 reason = this->blocked;

            // Slide: the turn alone, then the move alone, before stopping dead.
            if (this->yawVel != 0 && RsBoat_Fits(this, play, thisx->world.pos.x, thisx->world.pos.z, nextYaw,
                                                 this->surfaceY)) {
                thisx->shape.rot.y = nextYaw;
            } else {
                nextX = thisx->world.pos.x + Math_SinS(thisx->shape.rot.y) * this->speed;
                nextZ = thisx->world.pos.z + Math_CosS(thisx->shape.rot.y) * this->speed;
                if (RsBoat_Fits(this, play, nextX, nextZ, thisx->shape.rot.y, this->surfaceY)) {
                    thisx->world.pos.x = nextX;
                    thisx->world.pos.z = nextZ;
                }
            }
            this->blocked = reason;
            this->speed = 0.0f;
            this->freeTicks = 0;
            this->blockCount++;
            if (reason != this->markedBlock) {
                this->markedBlock = reason;
                RsBoat_Mark(this, "blocked");
            }
        }
    }
    thisx->world.rot.y = thisx->shape.rot.y;
    if (this->helm) {
        RsBoat_CamUpdate(this, play, player);
    }

    // Float: the surface plus the bob. Only the hull moves; the water stays flat.
    bob = sinf((f32)this->timer * (2.0f * (f32)M_PI / RS_BOAT_BOB_PERIOD)) * RS_BOAT_BOB_AMP;
    if (this->onWater) {
        thisx->world.pos.y = this->surfaceY + bob;
    }
    this->roll = (s16)(sinf((f32)this->timer * (2.0f * (f32)M_PI / RS_BOAT_ROCK_PERIOD)) * RS_BOAT_ROLL_AMP);
    this->pitch = (s16)(cosf((f32)this->timer * (2.0f * (f32)M_PI / RS_BOAT_ROCK_PERIOD) * 0.7f) * RS_BOAT_PITCH_AMP);
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
    Gfx* model = RsBoat_ModelList();

    OPEN_DISPS(gfxCtx);

    Gfx_SetupDL_25Opa(gfxCtx);
    // Actor_Draw has set the hull's matrix (position and yaw); the rock is draw-only, on top of it.
    Matrix_RotateZYX(this->pitch, 0, this->roll, MTXMODE_APPLY);
    if (model != NULL) {
        // The skiff, in its own model space: its floor on the deck, centred on the collision. Its list carries
        // its own materials (lit, RS's base colours as prim colours), so the state is set up again after it.
        Matrix_Push();
        Matrix_Translate(0.0f, RS_BOAT_DECK_Y, RS_BOAT_MODEL_Z, MTXMODE_APPLY);
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

    // The placeholder box only when the skiff's archive is not mounted; the tiller post always, as the thing
    // Link targets and steers from (the skiff has no tiller of its own: Sailing's helm is a separate loc).
    if (model == NULL) {
        RsBoat_DrawBox(gfxCtx, sHullBoxVtx, 96, 62, 34, 168, 128, 80);
        RsBoat_DrawBox(gfxCtx, sProwBoxVtx, 120, 80, 44, 150, 104, 60);
    }
    RsBoat_DrawBox(gfxCtx, sTillerBoxVtx, 60, 38, 20, 60, 38, 20);

    OPEN_DISPS(gfxCtx);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    CLOSE_DISPS(gfxCtx);

    if (gRsBoatTestWater.active) {
        RsBoat_DrawTestWater(gfxCtx);
    }
}
