/*
 * RsVehicleShadow.c - Link's shadow on a vehicle he stands in (sturdy-bassoon#267): the boats, and the balloon's
 * basket. RsVehicleShadow.h has the design.
 */

#include <math.h>
#include <string.h>

#include "RsVehicleShadow.h"
#include "RsBalloon.h"
#include "RsBoat.h"
#include "global.h"
#include "objects/gameplay_keep/gameplay_keep.h" // gCircleShadowTex
#include "soh/frame_interpolation.h"

// Link over a vehicle's collision, or over `src`'s when it is given: his floor is it, or was it last tick and he is off
// the ground and out of the water. Landed from a climb at the rail's very outer edge, his floor slips off the boat on
// single ticks as the rail's ease draws him in (`boat shadow trace`: floorBgId 50, bgCheckFlags 4), and the shadow
// blinked and restarted its fade-in each time (#267). Not when he stands on other ground: stepped onto a dock, his own
// shadows take over at once, and stepped onto another boat, that boat's.
static s32 RsVehicleShadow_LinkOver(PlayState* play, const Player* player, const RsVehicleShadowSource* src) {
    if (src != NULL ? (src->bgId >= 0 && player->actor.floorBgId == (u8)src->bgId)
                    : (RsBoat_IsBoatFloor(play, player->actor.floorBgId) ||
                       RsBalloon_IsBasketFloor(play, player->actor.floorBgId))) {
        return 1;
    }
    return !(player->stateFlags1 & PLAYER_STATE1_IN_WATER) && !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
           (src != NULL ? src->linkAboard : (RsBoat_LinkWasAboard(play) || RsBalloon_LinkWasAboard(play)));
}

s32 RsVehicleShadow_Mode(PlayState* play, const Actor* actor) {
    const Player* player = GET_PLAYER(play);
    const s32 mode = CVarGetInteger(CVAR_RS_VEHICLE_SHADOW, RS_VEHICLE_SHADOW_DEFAULT);

    if ((mode != RS_VEHICLE_SHADOW_CIRCLE && mode != RS_VEHICLE_SHADOW_DRAPED) || player == NULL ||
        actor != &player->actor || !RsVehicleShadow_LinkOver(play, player, NULL)) {
        return RS_VEHICLE_SHADOW_FEET;
    }
    return mode;
}

f32 RsVehicleShadow_Size(void) {
    const s32 size = CVarGetInteger(CVAR_RS_VEHICLE_SHADOW_SIZE, RS_VEHICLE_SHADOW_SIZE_DEFAULT);

    return CLAMP(size, RS_VEHICLE_SHADOW_SIZE_MIN, RS_VEHICLE_SHADOW_SIZE_MAX) / 100.0f;
}

// --- the draped shadow ----------------------------------------------------------------------------------

// Built in 32nds of a unit. In quarters, the rounding moved a copy by up to an eighth as his place on the rocking hull
// drifted from tick to tick, and on a steep face (the rim's outer bevel) that took it in and out of the wood: flicker
// standing still (the owner, 2026-10-30).
#define RS_VEHICLE_SHADOW_VTX_SCALE 32.0f
// gCircleShadowDL's half-width in its own units, so the size CVar gives the circle's radius in either mode. The
// shadow wears the circle's own texture (gCircleShadowTex, 32 x 32 IA16: a disc at alpha ~166, a two-texel rim) across
// that width, so its edge is round and it is as dark as vanilla's. Matched by eye to the circle's mode on the skiff's
// deck from above (#267): 50 drew it ~20% small.
#define RS_VEHICLE_SHADOW_CIRCLE_HALF 60.0f
#define RS_VEHICLE_SHADOW_TEX 32
// Off the drawn face along each corner's normal averaged over the faces that share it (RsVehicleDeckFace's vn), so two
// faces' copies still meet along their shared edge: along each face's own normal they parted at every convex edge and
// the wood showed through as a light seam; straight up, a steep face got almost no clearance, and as the copies slide
// with him between ticks (built relative to him: RsVehicleShadowAnchor) a slow walk pushed them into the slanted walls,
// which flickered (the owner, 2026-10-30). The decal depth mode adds its own small bias.
// Plus up to RS_VEHICLE_SHADOW_LIFT_STEEP more on an upright face, by how steep the corner's normal is: as copies slide
// along with him between ticks, a slow step off the rim (~1.5 units a tick) pushed them into the slanted walls by about
// that much, and the wall's shadow flickered (the owner, 2026-10-30).
#define RS_VEHICLE_SHADOW_LIFT 0.75f
#define RS_VEHICLE_SHADOW_LIFT_STEEP 1.25f
// Auto anchor (RsVehicleShadowAnchor): built relative to him once he moves this far a tick across the boat, on the hull
// again under the second. A walk is ~1.5 a tick, a run 6 to 9.
#define RS_VEHICLE_SHADOW_GLIDE_ON 4.0f
#define RS_VEHICLE_SHADOW_GLIDE_OFF 2.5f
// Ticks the shadow takes to fade in when it starts drawing after a tick it did not: up from a climb, onto the boat
// (the owner, 2026-10-30: it popped in).
#define RS_VEHICLE_SHADOW_FADE_IN 6
#define RS_VEHICLE_SHADOW_OVER_FEET \
    6.0f                             // a face higher than this over his feet gets none: a wall beside him, a rail he
                                     // climbs onto (until he is nearly up), the trim over the deck. The raft's drawn
                                     // logs crown a few units over its flat collision, so not less
#define RS_VEHICLE_SHADOW_DROP 10.0f // a face this far under the deck he is over (or his lower foot) gets none, fading
#define RS_VEHICLE_SHADOW_DROP_FROM 2.0f // from this far under it: a bevel just under the trim's top is not lightened
// How far round him to look for the deck he is over, two rings, when no drawn deck is under him: the raft's helm stands
// over a gap between two logs, and the deck map has nothing over the slanted inner walls, which a walk along the trim's
// inner edge crosses (3 alone missed, and the shadow flickered between drawn and held back).
#define RS_VEHICLE_SHADOW_GAP 3.0f
#define RS_VEHICLE_SHADOW_GAP_FAR 6.0f
#define RS_VEHICLE_SHADOW_MAX_VTX 512 // a tick's clipped faces' corners, at most: past it the rest are left out
#define RS_VEHICLE_SHADOW_MAX_POLYS 128
#define RS_VEHICLE_SHADOW_POLY 10 // a triangle clipped by the square's four sides has at most 7 corners; cut between
                                  // two heights as well (the drop's), at most 9
#define RS_VEHICLE_SHADOW_CORNER 6 // a clipped corner: its place and its normal

RsVehicleShadowStats gRsVehicleShadow = { .feetOver = -999.0f, .lowestDrawn = 999.0f };
// The tick's corners are built here and copied into the frame's graph arena, which then holds only those used.
static Vtx sShadowScratch[RS_VEHICLE_SHADOW_MAX_VTX];
RsVehicleShadowTraceRow gRsVehicleShadowTrace[RS_VEHICLE_SHADOW_TRACE];
s32 gRsVehicleShadowTraceNext;
// His place last tick (model space), for the auto anchor's speed, and whether it glides.
static f32 sShadowLastX;
static f32 sShadowLastZ;
static u32 sShadowLastFrame;
static s32 sShadowGlide;
// How far in it has faded (0..1), and the tick it last drew.
static f32 sShadowAppear;
static u32 sShadowDrawnFrame;
// His height (model space) the last tick his floor was really this vehicle, for a tick it is only last tick's
// (RsVehicleShadow_LinkOver): his floor slips because he has started to fall off the rail's edge, 3 to 7.5 units in
// one tick before the boat sets him back, and at that height the rim's shadow was cut as over his feet.
static f32 sShadowFloorY;
// The vehicle all of the above belongs to: model space is its own, so another vehicle starts afresh.
static const Actor* sShadowOwner;

void RsVehicleShadow_StatsReset(void) {
    gRsVehicleShadow.drawn = gRsVehicleShadow.held = 0;
    gRsVehicleShadow.lowestDrawn = 999.0f;
}

// The drawn deck's height at model (x, z), bilinear between the knots, and how much drawn deck is there: the weight of
// the knots that have a face (0 off the hull, 1 well inside it).
static f32 RsVehicleShadow_DeckHeightAt(const RsVehicleDeckHeights* map, f32 x, f32 z, f32* present) {
    const f32 fx = (x - map->x0) / RS_VEHICLE_DECK_STEP;
    const f32 fz = (z - map->z0) / RS_VEHICLE_DECK_STEP;
    const s32 i = (s32)floorf(fx);
    const s32 j = (s32)floorf(fz);
    const f32 tx = fx - i;
    const f32 tz = fz - j;
    f32 sum = 0.0f;
    f32 weight = 0.0f;
    s32 k;

    for (k = 0; k < 4; k++) {
        const s32 ki = i + (k & 1);
        const s32 kj = j + (k >> 1);
        const f32 wk = ((k & 1) ? tx : 1.0f - tx) * ((k >> 1) ? tz : 1.0f - tz);
        u8 cell;

        if (wk <= 0.0f || ki < 0 || kj < 0 || ki >= map->w || kj >= map->h) {
            continue;
        }
        cell = map->cells[kj * map->w + ki];
        if (cell != RS_VEHICLE_DECK_NONE) {
            sum += wk * (map->base + cell * 0.5f);
            weight += wk;
        }
    }
    *present = weight;
    return (weight > 0.0f) ? sum / weight : 0.0f;
}

static void RsVehicleShadow_Trace(PlayState* play, const Player* player, f32 x, f32 y, f32 z, f32 deck, f32 under,
                                  f32 strength, s32 tris) {
    RsVehicleShadowTraceRow* row = &gRsVehicleShadowTrace[gRsVehicleShadowTraceNext];

    row->frame = play->state.frames;
    row->x = x;
    row->y = y;
    row->z = z;
    row->deck = deck;
    row->under = under;
    row->strength = strength;
    row->speed = player->linearVelocity;
    row->tris = (s16)tris;
    row->climbing = (player->stateFlags1 & (PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE)) != 0;
    row->glide = (u8)gRsVehicleShadow.glide;
    gRsVehicleShadowTraceNext = (gRsVehicleShadowTraceNext + 1) % RS_VEHICLE_SHADOW_TRACE;
}

// Clips the polygon `in` (n corners, each its place then its normal) to the side of the plane `axis` = `edge` (axis 0
// is x, 1 is y, 2 is z) where the axis is at least the edge (`keepAbove`) or at most it, into `out`; returns its
// corners.
static s32 RsVehicleShadow_ClipPoly(f32 (*in)[RS_VEHICLE_SHADOW_CORNER], s32 n, f32 (*out)[RS_VEHICLE_SHADOW_CORNER],
                                    s32 axis, f32 edge, s32 keepAbove) {
    s32 count = 0;
    s32 i;

    for (i = 0; i < n; i++) {
        const f32* a = in[i];
        const f32* b = in[(i + 1) % n];
        const f32 da = keepAbove ? a[axis] - edge : edge - a[axis];
        const f32 db = keepAbove ? b[axis] - edge : edge - b[axis];

        s32 c;

        if (da >= 0.0f) {
            for (c = 0; c < RS_VEHICLE_SHADOW_CORNER; c++) {
                out[count][c] = a[c];
            }
            count++;
        }
        if ((da >= 0.0f) != (db >= 0.0f)) {
            const f32 t = da / (da - db);

            for (c = 0; c < RS_VEHICLE_SHADOW_CORNER; c++) {
                out[count][c] = a[c] + (b[c] - a[c]) * t;
            }
            count++;
        }
    }
    return count;
}

// Link's shadow over this vehicle in RS_VEHICLE_SHADOW_DRAPED, cast onto the drawn hull itself: every face of it that
// does not point down (RsVehicleDeck's faces) under the shadow's square, clipped to the square and drawn again wearing
// the circle's texture laid on from straight above. So it lies on the trim to its very edge, over the covered bow's
// crown and its edges, and down the slanted inner walls as he climbs them, where a height-field grid stepped and
// notched (the owner, 2026-10-30). Each corner is as dark as its depth under the deck he is over allows, and none over
// his feet. Built on the hull at a walk, or relative to him at a run with his offset as the matrix so frame
// interpolation glides it between ticks (RsVehicleShadowAnchor). Drawn after the hull in the hull's own matrix
// (Actor_Draw's), so it rocks and bobs with the drawn deck. Mid-climb his feet are last tick's draw (Player sets them
// as it draws), so on a moving hull the shadow trails them by one tick of the hull's motion. A face hidden behind
// another (the deck under the trim, under the lid) stays hidden: it is depth-tested. Its own render state, unfogged:
// the vanilla shadows' (setup 0x2C) turns fog on, which overwrites the shade alpha the fades ride on. Black near the
// camera is the same either way.
void RsVehicleShadow_Draw(PlayState* play, const RsVehicleShadowSource* src) {
    static u8 sInterpKey;
    Actor* vehicle = src->vehicle;
    const RsVehicleDeckHeights* map;
    const RsVehicleDeckFace* faces;
    const Player* player = GET_PLAYER(play);
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    s16 polyStart[RS_VEHICLE_SHADOW_MAX_POLYS];
    u8 polyCount[RS_VEHICLE_SHADOW_MAX_POLYS];
    MtxF hullMtx;
    MtxF toBoat;
    Vec3f link;
    Vtx* vtx;
    f32 mx;
    f32 my;
    f32 mz;
    f32 myHigh;
    f32 present;
    f32 deck;
    f32 radius;
    f32 strength;
    f32 under;
    f32 ax;
    f32 az;
    const s32 anchor = CVarGetInteger(CVAR_RS_VEHICLE_SHADOW_ANCHOR, RS_VEHICLE_SHADOW_ANCHOR_AUTO);
    s32 glide;
    s32 numFaces;
    s32 numVtx = 0;
    s32 numPolys = 0;
    s32 f;
    s32 i;

    if (player == NULL || src->bgId < 0 || src->deck == NULL || !RsVehicleShadow_LinkOver(play, player, src) ||
        player->actor.shape.shadowDraw == NULL ||
        RsVehicleShadow_Mode(play, &player->actor) != RS_VEHICLE_SHADOW_DRAPED) {
        // Not drawn while he was aboard last tick: traced (tris -2) with his floor's bgId as `deck`, his bgCheckFlags
        // as `under` and the mode as `str`, to tell a lost floor from a mode.
        if (player != NULL && src->linkAboard) {
            RsVehicleShadow_Trace(play, player, 0.0f, 0.0f, 0.0f, (f32)player->actor.floorBgId,
                                  (f32)player->actor.bgCheckFlags, (f32)RsVehicleShadow_Mode(play, &player->actor), -2);
        }
        return;
    }
    map = &src->deck->map;
    faces = src->deck->faces;
    numFaces = src->deck->numFaces;
    // Link in the vehicle's space: the inverse of Actor_Draw's matrix for it (scale 1).
    SkinMatrix_SetTranslateRotateYXZScale(&hullMtx, 1.0f, 1.0f, 1.0f, vehicle->shape.rot.x, vehicle->shape.rot.y,
                                          vehicle->shape.rot.z, vehicle->world.pos.x,
                                          vehicle->world.pos.y + vehicle->shape.yOffset, vehicle->world.pos.z);
    if (SkinMatrix_Invert(&hullMtx, &toBoat) != 0) {
        return;
    }
    // Where he is as drawn. Mid-climb Player has already put him on the ledge and the climb lifts his body to it over
    // the next ticks, so his feet as last drawn stand in, and the shadow stays with the foot still on the deck (the
    // owner, 2026-10-30: it hopped onto the trim as the climb began). His lower foot reads the deck he is over and the
    // fades; anything over his higher foot is a wall.
    SkinMatrix_Vec3fMtxFMultXYZ(&toBoat, (Vec3f*)&player->actor.world.pos, &link);
    my = myHigh = link.y - src->modelY;
    if (player->stateFlags1 & (PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE)) {
        Vec3f feet[2];

        SkinMatrix_Vec3fMtxFMultXYZ(&toBoat, (Vec3f*)&player->actor.shape.feetPos[FOOT_LEFT], &feet[0]);
        SkinMatrix_Vec3fMtxFMultXYZ(&toBoat, (Vec3f*)&player->actor.shape.feetPos[FOOT_RIGHT], &feet[1]);
        link.x = (feet[0].x + feet[1].x) * 0.5f;
        link.z = (feet[0].z + feet[1].z) * 0.5f;
        my = MIN(feet[0].y, feet[1].y) - src->modelY;
        myHigh = MAX(feet[0].y, feet[1].y) - src->modelY;
    }
    mx = link.x;
    mz = link.z - src->modelZ;
    if (sShadowOwner != vehicle) {
        sShadowOwner = vehicle;
        sShadowLastFrame = sShadowDrawnFrame = 0;
        sShadowGlide = 0;
    }
    if (player->actor.floorBgId == (u8)src->bgId) {
        sShadowFloorY = my;
    } else if (sShadowDrawnFrame + 1 == (u32)play->state.frames) {
        my = myHigh = MAX(my, sShadowFloorY);
    }
    {
        const f32 moved = (sShadowLastFrame + 1 == (u32)play->state.frames)
                              ? sqrtf(SQ(mx - sShadowLastX) + SQ(mz - sShadowLastZ))
                              : 0.0f;

        if (moved >= RS_VEHICLE_SHADOW_GLIDE_ON) {
            sShadowGlide = 1;
        } else if (moved < RS_VEHICLE_SHADOW_GLIDE_OFF) {
            sShadowGlide = 0;
        }
        sShadowLastX = mx;
        sShadowLastZ = mz;
        sShadowLastFrame = play->state.frames;
        glide = (anchor == RS_VEHICLE_SHADOW_ANCHOR_LINK) || (anchor == RS_VEHICLE_SHADOW_ANCHOR_AUTO && sShadowGlide);
    }

    gRsVehicleShadow.frame = play->state.frames;
    gRsVehicleShadow.kind = src->kind;
    gRsVehicleShadow.feetOver = -999.0f;
    gRsVehicleShadow.lit = gRsVehicleShadow.tris = gRsVehicleShadow.faces = gRsVehicleShadow.dropped = 0;
    radius = player->actor.shape.shadowScale * player->actor.scale.x * RsVehicleShadow_Size() *
             RS_VEHICLE_SHADOW_CIRCLE_HALF;
    gRsVehicleShadow.radius = radius;

    // The deck he is over: of the drawn deck under his middle and on the two rings round it, the one nearest
    // his lower foot - the deck beside a log he stands between (the raft's helm), the deck a foot is still on as he
    // climbs onto the trim. None (half off the hull, climbing its side), or his feet under it (climbing up onto it):
    // no shadow until he is up.
    deck = RsVehicleShadow_DeckHeightAt(map, mx, mz, &present);
    {
        f32 best = (present >= 0.5f) ? fabsf(deck - my) : -1.0f;

        for (i = 0; i < 16; i++) {
            const f32 ring = (i < 8) ? RS_VEHICLE_SHADOW_GAP : RS_VEHICLE_SHADOW_GAP_FAR;
            f32 there;
            const f32 h = RsVehicleShadow_DeckHeightAt(map, mx + ring * Math_SinS(i * 0x2000),
                                                       mz + ring * Math_CosS(i * 0x2000), &there);

            if (there >= 0.5f && (best < 0.0f || fabsf(h - my) < best)) {
                best = fabsf(h - my);
                deck = h;
                present = there;
            }
        }
    }
    if (present < 0.5f || my < deck - RS_VEHICLE_SHADOW_OVER_FEET) {
        gRsVehicleShadow.held++;
        RsVehicleShadow_Trace(play, player, mx, my, mz, deck, 0.0f, 0.0f, -1);
        return;
    }
    gRsVehicleShadow.feetOver = my - deck;
    gRsVehicleShadow.drawn++;
    gRsVehicleShadow.lowestDrawn = MIN(gRsVehicleShadow.lowestDrawn, my - deck);
    // Vanilla's fade with height (ActorShadow_Draw), for a jump; and what lies further under him than the deck he is
    // over, or his lower foot, fades out under it: the deck seen from the rail.
    strength = player->actor.shape.shadowAlpha * (1.0f - CLAMP(my - deck, 0.0f, 150.0f) / 350.0f);
    if (sShadowDrawnFrame + 1 != (u32)play->state.frames) {
        sShadowAppear = 0.0f;
    }
    sShadowAppear = MIN(sShadowAppear + 1.0f / RS_VEHICLE_SHADOW_FADE_IN, 1.0f);
    sShadowDrawnFrame = play->state.frames;
    strength *= sShadowAppear;
    under = MIN(my, deck);
    // Not eased from tick to tick: easing it (against a wall flicker the slide turned out to cause) left the deck under
    // his feet lightened for ~5 ticks after he stepped down off the rim, the shadow seeming to lag behind him (the
    // owner, 2026-10-30). The `boat shadow trace` of that step shows deck and reference moving smoothly unaided.

    // The faces under the square, clipped to it, each a polygon of corners relative to the anchor: him, or the model's
    // origin (CVAR_RS_VEHICLE_SHADOW_ANCHOR).
    ax = glide ? mx : 0.0f;
    az = glide ? mz : 0.0f;
    gRsVehicleShadow.glide = glide;
    for (f = 0; f < numFaces && numPolys < RS_VEHICLE_SHADOW_MAX_POLYS; f++) {
        const RsVehicleDeckFace* face = &faces[f];
        f32 polyA[RS_VEHICLE_SHADOW_POLY][RS_VEHICLE_SHADOW_CORNER];
        f32 polyB[RS_VEHICLE_SHADOW_POLY][RS_VEHICLE_SHADOW_CORNER];
        s32 n;

        if (face->hi[0] < mx - radius || face->lo[0] > mx + radius || face->hi[1] < mz - radius ||
            face->lo[1] > mz + radius) {
            continue;
        }
        for (i = 0; i < 3; i++) {
            polyA[i][0] = face->v[i][0];
            polyA[i][1] = face->v[i][1];
            polyA[i][2] = face->v[i][2];
            polyA[i][3] = face->vn[i][0];
            polyA[i][4] = face->vn[i][1];
            polyA[i][5] = face->vn[i][2];
        }
        n = RsVehicleShadow_ClipPoly(polyA, 3, polyB, 0, mx - radius, 1);
        n = RsVehicleShadow_ClipPoly(polyB, n, polyA, 0, mx + radius, 0);
        n = RsVehicleShadow_ClipPoly(polyA, n, polyB, 2, mz - radius, 1);
        n = RsVehicleShadow_ClipPoly(polyB, n, polyA, 2, mz + radius, 0);
        if (n < 3) {
            continue;
        }
        // Cut where the fade under the deck bends, so each piece's corners carry it. Shaded from its corners alone, a
        // tall face (the basket's walls, 40 high) drew the fade stretched over its whole height, a streak far down the
        // wall under the rim he stood on (the owner, 2026-10-30). Not at his feet's height: that is a step, and left to
        // the corners it softens.
        {
            const f32 cuts[2] = { under - RS_VEHICLE_SHADOW_DROP_FROM, under - RS_VEHICLE_SHADOW_DROP };
            f32 pieces[2][RS_VEHICLE_SHADOW_POLY][RS_VEHICLE_SHADOW_CORNER];
            f32(*poly[3])[RS_VEHICLE_SHADOW_CORNER];
            s32 polyN[3];
            s32 numPieces = 0;
            s32 faceLit = 0;
            s32 p;

            for (p = 0; p < 2; p++) {
                const s32 above = RsVehicleShadow_ClipPoly(polyA, n, pieces[p], 1, cuts[p], 1);

                if (above >= 3) {
                    poly[numPieces] = pieces[p];
                    polyN[numPieces++] = above;
                }
                n = RsVehicleShadow_ClipPoly(polyA, n, polyB, 1, cuts[p], 0);
                memcpy(polyA, polyB, sizeof(polyA[0]) * n);
            }
            if (n >= 3) {
                poly[numPieces] = polyA;
                polyN[numPieces++] = n;
            }
            for (p = 0; p < numPieces; p++) {
                const s32 pn = polyN[p];
                s32 shaded = 0;

                if (numPolys >= RS_VEHICLE_SHADOW_MAX_POLYS || numVtx + pn > RS_VEHICLE_SHADOW_MAX_VTX) {
                    gRsVehicleShadow.dropped++;
                    continue;
                }
                for (i = 0; i < pn; i++) {
                    const f32* c = poly[p][i];
                    Vtx* v = &sShadowScratch[numVtx + i];
                    const f32 h = c[1];
                    const f32 nLen = sqrtf(SQ(c[3]) + SQ(c[4]) + SQ(c[5]));
                    const f32 lift =
                        (nLen > 0.0f)
                            ? (RS_VEHICLE_SHADOW_LIFT + RS_VEHICLE_SHADOW_LIFT_STEEP * (1.0f - fabsf(c[4]) / nLen)) /
                                  nLen
                            : 0.0f;
                    f32 a = strength;

                    if (h > myHigh + RS_VEHICLE_SHADOW_OVER_FEET) {
                        a = 0.0f;
                    } else if (under - h > RS_VEHICLE_SHADOW_DROP_FROM) {
                        a *= CLAMP_MIN(1.0f - (under - h - RS_VEHICLE_SHADOW_DROP_FROM) /
                                                  (RS_VEHICLE_SHADOW_DROP - RS_VEHICLE_SHADOW_DROP_FROM),
                                       0.0f);
                    }
                    v->v.ob[0] = (s16)lroundf((c[0] + c[3] * lift - ax) * RS_VEHICLE_SHADOW_VTX_SCALE);
                    v->v.ob[1] = (s16)lroundf((h + c[4] * lift) * RS_VEHICLE_SHADOW_VTX_SCALE);
                    v->v.ob[2] = (s16)lroundf((c[2] + c[5] * lift - az) * RS_VEHICLE_SHADOW_VTX_SCALE);
                    v->v.flag = 0;
                    // The texture laid on from above across the square, texels in s10.5.
                    v->v.tc[0] = (s16)lroundf(((c[0] - mx) / (2.0f * radius) + 0.5f) * RS_VEHICLE_SHADOW_TEX * 32);
                    v->v.tc[1] = (s16)lroundf(((c[2] - mz) / (2.0f * radius) + 0.5f) * RS_VEHICLE_SHADOW_TEX * 32);
                    v->v.cn[0] = v->v.cn[1] = v->v.cn[2] = 0;
                    v->v.cn[3] = (u8)CLAMP(a, 0.0f, 255.0f);
                    shaded += (v->v.cn[3] > 0);
                }
                if (shaded == 0) {
                    continue; // nothing to draw: the next piece's corners go over these
                }
                faceLit += shaded;
                polyStart[numPolys] = (s16)numVtx;
                polyCount[numPolys] = (u8)pn;
                numPolys++;
                numVtx += pn;
            }
            if (faceLit > 0) {
                gRsVehicleShadow.faces++;
                gRsVehicleShadow.lit += faceLit;
            }
        }
    }
    if (numPolys == 0) {
        RsVehicleShadow_Trace(play, player, mx, my, mz, deck, under, strength, 0);
        return;
    }
    vtx = Graph_Alloc(gfxCtx, sizeof(Vtx) * numVtx);
    memcpy(vtx, sShadowScratch, sizeof(Vtx) * numVtx);

    OPEN_DISPS(gfxCtx);

    // A child per anchor, so a switch draws its first tick as it is rather than blended from the other's.
    FrameInterpolation_RecordOpenChild(&sInterpKey, glide);
    Matrix_Push();
    Matrix_Translate(ax, src->modelY, az + src->modelZ, MTXMODE_APPLY);
    Matrix_Scale(1.0f / RS_VEHICLE_SHADOW_VTX_SCALE, 1.0f / RS_VEHICLE_SHADOW_VTX_SCALE,
                 1.0f / RS_VEHICLE_SHADOW_VTX_SCALE, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    Matrix_Pop();
    FrameInterpolation_RecordCloseChild();

    gDPPipeSync(POLY_OPA_DISP++);
    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetOtherMode(POLY_OPA_DISP++,
                    G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_ZB_XLU_DECAL | G_RM_ZB_XLU_DECAL2);
    gSPLoadGeometryMode(POLY_OPA_DISP++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gDPLoadTextureBlock(POLY_OPA_DISP++, gCircleShadowTex, G_IM_FMT_IA, G_IM_SIZ_16b, RS_VEHICLE_SHADOW_TEX,
                        RS_VEHICLE_SHADOW_TEX, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                        G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    // Black, the disc's alpha times the vertex's.
    gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, 0, TEXEL0, 0, SHADE, 0, 0, 0, 0, 0, TEXEL0, 0, SHADE, 0);

    // Polygons a load at a time, as many as fit the vertex cache, each a fan from its first corner.
    for (f = 0; f < numPolys;) {
        const s32 first = f;
        s32 count = 0;

        while (f < numPolys && count + polyCount[f] <= 32) {
            count += polyCount[f++];
        }
        gSPVertex(POLY_OPA_DISP++, (uintptr_t)&vtx[polyStart[first]], count, 0);
        for (i = first; i < f; i++) {
            const s32 base = polyStart[i] - polyStart[first];
            s32 k;

            for (k = 1; k + 2 < polyCount[i]; k += 2) {
                gSP2Triangles(POLY_OPA_DISP++, base, base + k, base + k + 1, 0, base, base + k + 1, base + k + 2, 0);
            }
            if (k + 1 < polyCount[i]) {
                gSP1Triangle(POLY_OPA_DISP++, base, base + k, base + k + 1, 0);
            }
            gRsVehicleShadow.tris += polyCount[i] - 2;
        }
    }
    // As RsBoat_Draw leaves things for the actors after it.
    gSPTexture(POLY_OPA_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);

    CLOSE_DISPS(gfxCtx);
    RsVehicleShadow_Trace(play, player, mx, my, mz, deck, under, strength, gRsVehicleShadow.tris);
}
