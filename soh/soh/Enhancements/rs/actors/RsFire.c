/*
 * RsFire.c - OoT's torch fire on an RS torch (sturdy-bassoon#232). RsFire.h has the design: the row's
 * fields, the light, and why the collider is gated.
 *
 * The draw is En_Light's (z_en_light.c, params >= 0) at params 4's colours. The lighting rules are
 * Obj_Syokudai's (z_obj_syokudai.c), minus the stand, its timers and its "light all N" counter:
 *   - a Deku stick's tip in a LIT flame lights the stick (Player's unk_860 is the stick's burn timer);
 *   - a BURNING stick's tip in an UNLIT flame lights the fire;
 *   - a normal arrow through a LIT flame becomes a burning arrow, which is how adult Link carries fire
 *     before he has the fire arrows;
 *   - a fire arrow, a burning arrow or Din's Fire lights an UNLIT one.
 * Its flame is scaled with the fire, so the numbers below are Obj_Syokudai's, multiplied by how much
 * bigger this fire is than that one (RS_FIRE_SYOKUDAI_RATIO).
 *
 * Same /W3 /WX rules as the other rs/ actors: explicit casts, no `s32 pad`, no ICHAIN.
 */

#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsFire.h"
#include "RsActorParams.h"
#include "RsActors.h"
#include "global.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/Enhancements/lights/LightsBudget.h"

// En_Light params 4's scale: the fire the owner picked, and what rot.x is a percentage of.
#define RS_FIRE_BASE_SCALE 0.004f
// Obj_Syokudai's flame scale (z_obj_syokudai.c, ObjSyokudai_Draw). Its collider and stick test are
// sized for that flame, so ours scale by the ratio between the two: WHERE they sit by the visible
// flame (the size), HOW FAR they reach by the hit size.
#define RS_FIRE_SYOKUDAI_SCALE 0.0027f
// Obj_Syokudai's flame, from the flame's base (its flame is drawn 52 above the stand's origin): the
// cylinder is radius 15, height 45, from 7 below the base, so it is centred about 15 above the base;
// the stick's tip must be within 20 of a point 15 above the base. Ours keep that centre on the
// visible flame and grow around it with the hit size, so a bigger reach never floats above the fire.
#define RS_FIRE_CYL_RADIUS 15.0f
#define RS_FIRE_CYL_HEIGHT 45.0f
#define RS_FIRE_CYL_BELOW 7.0f
#define RS_FIRE_STICK_ABOVE 15.0f
#define RS_FIRE_STICK_REACH 20.0f
// What the flame takes, as Obj_Syokudai's: 0x20 a normal arrow, 0x800 a fire arrow, 0x20000 Din's.
#define RS_FIRE_DMG_ARROW 0x00000020
#define RS_FIRE_DMG_FIRE 0x00020800
#define RS_FIRE_DMG_ANY 0x00020820
// En_Light's light: radius 300, the glow 40 above the fire's base at params 4's size. En_Light keeps it
// at 40 whatever its size; ours scales it with the fire, so a small fire's halo stays on its flame.
#define RS_FIRE_LIGHT_RADIUS 300
#define RS_FIRE_GLOW_ABOVE 40.0f
// Obj_Syokudai's flame grows in over 10 ticks when it lights.
#define RS_FIRE_GROW_TICKS 10
// A burning stick: Obj_Syokudai's numbers for Player's unk_860.
#define RS_FIRE_STICK_LIGHT 210
#define RS_FIRE_STICK_KEEP 200

void RsFire_Init(Actor* thisx, PlayState* play);
void RsFire_Destroy(Actor* thisx, PlayState* play);
void RsFire_Update(Actor* thisx, PlayState* play);
void RsFire_Draw(Actor* thisx, PlayState* play);

static ColliderCylinderInit sRsFireCylinderInit = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_ON | AC_TYPE_PLAYER,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00000000, 0x00, 0x00 },
        { RS_FIRE_DMG_ANY, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON,
        OCELEM_NONE,
    },
    { 0, 0, 0, { 0, 0, 0 } }, // sized at Init, from RS_FIRE_CYL_* and the fire's size and hit size
};

const char* RsFire_LitByName(int32_t litBy) {
    switch (litBy) {
        case RS_FIRE_LIT_BY_FLAG:
            return "flag";
        case RS_FIRE_LIT_BY_STICK:
            return "stick";
        case RS_FIRE_LIT_BY_FIRE:
            return "fire";
        default:
            return "none";
    }
}

// How much bigger a fire of this size is than Obj_Syokudai's flame. Of the visible size: where its
// collider and stick test are centred. Of the hit size: how far they reach.
static float RsFire_SyokudaiRatio(float size) {
    return size * (RS_FIRE_BASE_SCALE / RS_FIRE_SYOKUDAI_SCALE);
}

static void RsFire_PlayIgnition(RsFire* this) {
    Audio_PlaySoundGeneral(NA_SE_EV_FLAME_IGNITION, &this->actor.projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// A percentage from a rot field: 0 reads as `unset`, anything else is clamped.
static int32_t RsFire_Percent(int32_t raw, int32_t unset) {
    if (raw == 0) {
        return unset;
    }
    if (raw < RS_FIRE_SCALE_MIN) {
        return RS_FIRE_SCALE_MIN;
    }
    if (raw > RS_FIRE_SCALE_MAX) {
        return RS_FIRE_SCALE_MAX;
    }
    return raw;
}

static void RsFire_Mark(RsFire* this, const char* event) {
    char line[200];

    snprintf(line, sizeof(line), "rs_fire event=%s lit=%d by=%s flag=%d size=%d hit=%d light=%d x=%d y=%d z=%d",
             event, this->lit, RsFire_LitByName(this->litBy), (int)this->flag, (int)(this->size * 100.0f + 0.5f),
             (int)(this->hitSize * 100.0f + 0.5f), this->lightNode != NULL ? 1 : 0, (int)this->actor.world.pos.x,
             (int)this->actor.world.pos.y, (int)this->actor.world.pos.z);
    RsAgent_Marker(line);
}

int32_t RsFire_DrawDistance(void) {
    int32_t distance = CVarGetInteger(CVAR_RS_FIRE_DRAW_DISTANCE, RS_FIRE_DRAW_DISTANCE);

    return CLAMP(distance, RS_FIRE_DRAW_DISTANCE_MIN, RS_FIRE_DRAW_DISTANCE_MAX);
}

int32_t RsFire_GlowOn(void) {
    return CVarGetInteger(CVAR_RS_FIRE_GLOW, RS_FIRE_GLOW_DEFAULT) != 0 ? 1 : 0;
}

// The planted probes (RsFire.h, #253): session-only, all off at launch.
static uint32_t sRsFireProbes = 0;

uint32_t RsFire_GetProbes(void) {
    return sRsFireProbes;
}

void RsFire_SetProbes(uint32_t probes) {
    sRsFireProbes = probes;
}

// The two settings (RsFire.h, #243). The light's type is all the glow is: Lights_PointGlowSetInfo and
// Lights_PointNoGlowSetInfo differ in nothing else, so switching it in place keeps the node.
void RsFire_ApplySettings(RsFire* this) {
    this->actor.uncullZoneForward = (f32)RsFire_DrawDistance();
    this->lightInfo.type = RsFire_GlowOn() ? LIGHT_POINT_GLOW : LIGHT_POINT_NOGLOW;
    if (sRsFireProbes & RS_FIRE_PROBE_NO_POINTLIGHTS) {
        this->actor.flags |= ACTOR_FLAG_IGNORE_POINTLIGHTS;
    } else {
        this->actor.flags &= ~ACTOR_FLAG_IGNORE_POINTLIGHTS;
    }
}

// Takes a node, once, if this fire wants a light and has none. A full pool returns NULL, and the fire
// burns unlit: nothing is taken from another holder (#233 is where that changes).
static void RsFire_TakeLight(RsFire* this, PlayState* play) {
    s16 x;
    s16 y;
    s16 z;

    if (!this->lightWanted || this->lightNode != NULL) {
        return;
    }
    x = (s16)this->actor.world.pos.x;
    y = (s16)(this->actor.world.pos.y + RS_FIRE_GLOW_ABOVE * this->size);
    z = (s16)this->actor.world.pos.z;
    if (RsFire_GlowOn()) {
        Lights_PointGlowSetInfo(&this->lightInfo, x, y, z, 255, 255, 180, -1);
    } else {
        Lights_PointNoGlowSetInfo(&this->lightInfo, x, y, z, 255, 255, 180, -1);
    }
    this->lightNode = LightContext_InsertLight(play, &play->lightCtx, &this->lightInfo);
    // The flicker in RsFire_Update rolls 255 and 200 by 0.5-1.0, so its mean colour sum is 455 x 0.75 (#250).
    Lights_SetShadowMeanColor(this->lightNode, &this->lightInfo, (255.0f + 200.0f) * 0.75f);
}

static void RsFire_Light(RsFire* this, PlayState* play, int32_t by) {
    this->lit = 1;
    this->litBy = by;
    this->growTicks = 0;
    if (this->flag >= 0) {
        Flags_SetWorldFlag(this->flag);
    }
    RsFire_TakeLight(this, play);
    RsFire_PlayIgnition(this);
    RsFire_Mark(this, "lit");
}

void RsFire_Init(Actor* thisx, PlayState* play) {
    RsFire* this = (RsFire*)thisx;
    int32_t percent = RsFire_Percent(thisx->home.rot.x, RS_FIRE_SCALE_DEFAULT);
    float ratio;
    float hitRatio;
    char line[128];

    this->flag = -1;
    if (RS_FIRE_PARAMS_GET_HAS_FLAG(thisx->params)) {
        if (WORLD_FLAG_IS_PLACEMENT(RS_FIRE_PARAMS_GET_FLAG(thisx->params))) {
            this->flag = (int16_t)RS_FIRE_PARAMS_GET_FLAG(thisx->params);
        } else {
            // Loud, never fatal: a named flag belongs to a quest or an NPC, and a fire setting it would
            // change their state. The fire still burns; it just remembers nothing.
            LUSLOG_ERROR("RsFire: params 0x%04X names world flag %d, outside the placement bands",
                         (u16)thisx->params, RS_FIRE_PARAMS_GET_FLAG(thisx->params));
            snprintf(line, sizeof(line), "rs_fire event=bad_flag flag=%d params=0x%04X",
                     RS_FIRE_PARAMS_GET_FLAG(thisx->params), (unsigned)(u16)thisx->params);
            RsAgent_Marker(line);
        }
    }

    this->size = (float)percent / 100.0f;
    this->hitSize = (float)RsFire_Percent(thisx->home.rot.z, percent) / 100.0f;
    this->lightWanted = RS_FIRE_PARAMS_GET_NO_LIGHT(thisx->params) ? 0 : 1;
    this->lightNode = NULL;
    this->timer = (int16_t)(Rand_ZeroOne() * 255.0f);
    this->growTicks = RS_FIRE_GROW_TICKS;
    this->litBy = RS_FIRE_LIT_BY_NONE;

    // The rot fields carried the size; the fire itself turns to the camera in its draw.
    thisx->world.rot.x = thisx->world.rot.y = thisx->world.rot.z = 0;
    thisx->shape.rot.x = thisx->shape.rot.y = thisx->shape.rot.z = 0;
    ActorShape_Init(&thisx->shape, 0.0f, NULL, 0.0f);
    Actor_SetScale(thisx, RS_FIRE_BASE_SCALE * this->size);

    ratio = RsFire_SyokudaiRatio(this->size);
    hitRatio = RsFire_SyokudaiRatio(this->hitSize);
    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, thisx, &sRsFireCylinderInit);
    // Obj_Syokudai's cylinder, sized by the hit size and centred on the visible flame's centre - which
    // is where Obj_Syokudai's own sits, 15 above its flame's base (-7 + 45 / 2).
    this->collider.dim.radius = (s16)(RS_FIRE_CYL_RADIUS * hitRatio + 0.5f);
    this->collider.dim.height = (s16)(RS_FIRE_CYL_HEIGHT * hitRatio + 0.5f);
    this->collider.dim.yShift = (s16)(RS_FIRE_STICK_ABOVE * ratio - (float)this->collider.dim.height / 2.0f);
    // The draw distance, before the first cull test (RsFire.h), and the probes. The light's type is
    // overwritten in TakeLight.
    RsFire_ApplySettings(this);

    if (this->flag >= 0 && Flags_GetWorldFlag(this->flag)) {
        // Lit once already in this save: it stays lit, without its ignition sound.
        this->lit = 1;
        this->litBy = RS_FIRE_LIT_BY_FLAG;
    } else {
        this->lit = RS_FIRE_PARAMS_GET_UNLIT(thisx->params) ? 0 : 1;
    }
    if (this->lit) {
        RsFire_TakeLight(this, play);
    }
    RsFire_Mark(this, "spawn");
}

void RsFire_Destroy(Actor* thisx, PlayState* play) {
    RsFire* this = (RsFire*)thisx;

    Collider_DestroyCylinder(play, &this->collider);
    LightContext_RemoveLight(play, &play->lightCtx, this->lightNode);
    this->lightNode = NULL;
}

// Obj_Syokudai's own conversion: params 0 is a burning arrow, and 0x800 is what it now deals - which is
// what lights the next fire it passes through.
static void RsFire_SetArrowBurning(EnArrow* arrow) {
    arrow->actor.params = 0;
    arrow->collider.info.toucher.dmgFlags = 0x800;
}

// THE ARROW'S PATH IN ONE TICK (sturdy-bassoon#235). An arrow flies 150 units a tick and is
// collision-checked once a tick, over that whole stretch; a lit fire sets it burning only when the fire
// next updates. So from close in, one tick's stretch reaches a lit fire AND an unlit one beyond it while
// the arrow is still plain, and the unlit one stays dark - vanilla's Obj_Syokudai does the same (the
// #235 run record, its vanilla control), made worse by the arrow telling only one fire a tick (below).
// Here the fire that is told walks the stretch the arrow flew this tick, in order along it: the order
// along the path, as finely as it can be known, for this one check only (RsFire_WalkStretch). The tick
// rate, En_Arrow and Obj_Syokudai are untouched.

// How far ahead of the arrow's origin its collider's tip sits: En_Arrow's quad tip is (0, +-400, 1500) in
// its model, at scale 0.01 (z_en_arrow.c, func_809B4800). The quad sweeps that tip from tick to tick.
#define RS_FIRE_ARROW_TIP 15.0f

// How much further out than the collider's radius the engine's own arrow test counts a hit, measured
// (#235 run record, the arrow-line scan): a lit hit-75 fire (radius 17) took an arrow passing 19.5-20
// from its axis. The walk adds it so a fire lights on the close path exactly where a burning arrow would
// have lit it the ordinary way.
#define RS_FIRE_PATH_PAD 3.0f

// Where along a -> b (0..1) the segment first enters this fire's hit cylinder - the collider itself,
// sized by its hit size, widened by RS_FIRE_PATH_PAD - or -1 if it never does.
static float RsFire_SegmentEnters(RsFire* fire, Vec3f* a, Vec3f* b) {
    float r = (float)fire->collider.dim.radius + RS_FIRE_PATH_PAD;
    float yLo = fire->actor.world.pos.y + (float)fire->collider.dim.yShift;
    float yHi = yLo + (float)fire->collider.dim.height;
    float ox = a->x - fire->actor.world.pos.x;
    float oz = a->z - fire->actor.world.pos.z;
    float dx = b->x - a->x;
    float dy = b->y - a->y;
    float dz = b->z - a->z;
    float qa = dx * dx + dz * dz;
    float qb = 2.0f * (ox * dx + oz * dz);
    float qc = ox * ox + oz * oz - r * r;
    float lo = 0.0f;
    float hi = 1.0f;
    float disc;
    float root;
    float t0;
    float t1;

    // Across the circle, in x/z.
    if (qa < 0.0001f) {
        if (qc > 0.0f) {
            return -1.0f;
        }
    } else {
        disc = qb * qb - 4.0f * qa * qc;
        if (disc < 0.0f) {
            return -1.0f;
        }
        root = sqrtf(disc);
        t0 = (-qb - root) / (2.0f * qa);
        t1 = (-qb + root) / (2.0f * qa);
        lo = MAX(lo, t0);
        hi = MIN(hi, t1);
    }
    // Between the cylinder's bottom and top.
    if (fabsf(dy) < 0.0001f) {
        if (a->y < yLo || a->y > yHi) {
            return -1.0f;
        }
    } else {
        t0 = (yLo - a->y) / dy;
        t1 = (yHi - a->y) / dy;
        lo = MAX(lo, MIN(t0, t1));
        hi = MIN(hi, MAX(t0, t1));
    }
    return (lo <= hi) ? lo : -1.0f;
}

// The stretch `arrow`'s collider swept this tick: its tip, from last tick's position to this one. False
// when the arrow did not move.
static s32 RsFire_ArrowStretch(EnArrow* arrow, Vec3f* a, Vec3f* b) {
    Vec3f dir;
    float len;

    Math_Vec3f_Diff(&arrow->actor.world.pos, &arrow->actor.prevPos, &dir);
    len = sqrtf(SQ(dir.x) + SQ(dir.y) + SQ(dir.z));
    if (len < 1.0f) {
        return 0;
    }
    a->x = arrow->actor.prevPos.x + dir.x / len * RS_FIRE_ARROW_TIP;
    a->y = arrow->actor.prevPos.y + dir.y / len * RS_FIRE_ARROW_TIP;
    a->z = arrow->actor.prevPos.z + dir.z / len * RS_FIRE_ARROW_TIP;
    b->x = arrow->actor.world.pos.x + dir.x / len * RS_FIRE_ARROW_TIP;
    b->y = arrow->actor.world.pos.y + dir.y / len * RS_FIRE_ARROW_TIP;
    b->z = arrow->actor.world.pos.z + dir.z / len * RS_FIRE_ARROW_TIP;
    return 1;
}

// Where along a -> b this fire sits when the stretch misses its cylinder by our arithmetic though the
// engine said it was hit (a rounding edge): the point of closest approach to its axis, 0..1.
static float RsFire_ClosestOnStretch(RsFire* fire, Vec3f* a, Vec3f* b) {
    float dx = b->x - a->x;
    float dz = b->z - a->z;
    float qa = dx * dx + dz * dz;
    float t;

    if (qa < 0.0001f) {
        return 0.0f;
    }
    t = ((fire->actor.world.pos.x - a->x) * dx + (fire->actor.world.pos.z - a->z) * dz) / qa;
    return CLAMP(t, 0.0f, 1.0f);
}

// THE ENGINE TELLS ONLY ONE FIRE. En_Arrow's collider is TOUCH_NEAREST (z_collision_check.c,
// Collider_QuadSetNearestAC): of everything its stretch touches in a tick, only the collider nearest the
// stretch's middle is hit, and the rest are cleared. So whichever fire is told walks the whole stretch
// itself, in order along it, as the arrow would have met them one at a time:
//   - a lit fire sets the arrow burning;
//   - an unlit fire lights if the arrow is burning by then - so a chain (lit, unlit A, unlit B) lights
//     both A and B, and a burning arrow lights every unlit fire on its stretch;
//   - an unlit fire met by a PLAIN arrow stops the walk: a plain arrow stops in it, as it does at an
//     unlit Obj_Syokudai, and never reaches what lies beyond.
// Afterwards the arrow is left burning if it caught, so the next fire past the stretch's end sees a
// burning arrow too. Cost: one pass over the scene's props, only on a tick an arrow touches a fire.
#define RS_FIRE_STRETCH_MAX 16

static void RsFire_WalkStretch(RsFire* told, PlayState* play, EnArrow* arrow, s32 burning) {
    RsFire* fires[RS_FIRE_STRETCH_MAX];
    float along[RS_FIRE_STRETCH_MAX];
    s32 count = 0;
    s32 plainAtStart = !burning;
    s32 haveStretch;
    s32 i;
    s32 j;
    Vec3f a;
    Vec3f b;
    Actor* other;

    haveStretch = RsFire_ArrowStretch(arrow, &a, &b);
    // The told fire is on the stretch whatever our arithmetic says: the engine hit it.
    fires[count] = told;
    along[count] = haveStretch ? RsFire_SegmentEnters(told, &a, &b) : 0.0f;
    if (along[count] < 0.0f) {
        along[count] = RsFire_ClosestOnStretch(told, &a, &b);
    }
    count++;
    if (haveStretch) {
        for (other = play->actorCtx.actorLists[ACTORCAT_PROP].head; other != NULL && count < RS_FIRE_STRETCH_MAX;
             other = other->next) {
            float t;

            if (other == &told->actor || other->id != ACTOR_RS_FIRE) {
                continue;
            }
            t = RsFire_SegmentEnters((RsFire*)other, &a, &b);
            if (t >= 0.0f) {
                fires[count] = (RsFire*)other;
                along[count] = t;
                count++;
            }
        }
    }
    // In order along the stretch (a handful at most: insertion sort).
    for (i = 1; i < count; i++) {
        RsFire* f = fires[i];
        float t = along[i];

        for (j = i - 1; j >= 0 && along[j] > t; j--) {
            fires[j + 1] = fires[j];
            along[j + 1] = along[j];
        }
        fires[j + 1] = f;
        along[j + 1] = t;
    }
    for (i = 0; i < count; i++) {
        RsFire* f = fires[i];

        if (f->lit) {
            if (!burning) {
                burning = 1;
                RsFire_Mark(f, "arrow_lit");
            }
        } else if (burning) {
            // Lit the classic way only when the told fire met an arrow already burning when it came.
            if (f != told || plainAtStart) {
                RsFire_Mark(f, "path_lit");
            }
            RsFire_Light(f, play, RS_FIRE_LIT_BY_FIRE);
        } else {
            break;
        }
    }
    if (burning && plainAtStart) {
        RsFire_SetArrowBurning(arrow);
    }
}

// A hit on the flame. An arrow - plain, burning or a fire arrow - walks its stretch (above). Anything else
// that burns (Din's Fire) lights an unlit one, as Obj_Syokudai's 0x20800.
// Returns whether the flame was hit, because Obj_Syokudai tests the stick only when it was not.
static s32 RsFire_TakeHit(RsFire* this, PlayState* play) {
    uint32_t dmgFlags;
    EnArrow* arrow;

    if (!(this->collider.base.acFlags & AC_HIT)) {
        return 0;
    }
    this->collider.base.acFlags &= ~AC_HIT;
    dmgFlags = this->collider.info.acHitInfo->toucher.dmgFlags;
    arrow = (EnArrow*)this->collider.base.ac;
    if (arrow != NULL && (arrow->actor.update == NULL || arrow->actor.id != ACTOR_EN_ARROW)) {
        arrow = NULL;
    }
    if (arrow != NULL && (dmgFlags & (RS_FIRE_DMG_ARROW | 0x800))) {
        RsFire_WalkStretch(this, play, arrow, (dmgFlags & RS_FIRE_DMG_ARROW) ? 0 : 1);
    } else if (!this->lit && (dmgFlags & RS_FIRE_DMG_FIRE)) {
        RsFire_Light(this, play, RS_FIRE_LIT_BY_FIRE);
    }
    return 1;
}

// A Deku stick's tip in the flame.
static void RsFire_TakeStick(RsFire* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    float ratio;
    float hitRatio;
    float reach;
    Vec3f d;

    if (player->heldItemAction != PLAYER_IA_DEKU_STICK) {
        return;
    }
    ratio = RsFire_SyokudaiRatio(this->size);
    hitRatio = RsFire_SyokudaiRatio(this->hitSize);
    d.x = player->meleeWeaponInfo[0].tip.x - this->actor.world.pos.x;
    d.y = player->meleeWeaponInfo[0].tip.y - (this->actor.world.pos.y + RS_FIRE_STICK_ABOVE * ratio);
    d.z = player->meleeWeaponInfo[0].tip.z - this->actor.world.pos.z;
    // Centred on the visible flame, reaching by the hit size - with no floor, as the collider has none
    // (the owner, #232: no single floor suits every flame), so a stick and an arrow agree on its size.
    reach = RS_FIRE_STICK_REACH * hitRatio;
    if (SQ(d.x) + SQ(d.y) + SQ(d.z) >= SQ(reach)) {
        return;
    }
    if (this->lit) {
        if (player->unk_860 == 0) {
            player->unk_860 = RS_FIRE_STICK_LIGHT;
            RsFire_PlayIgnition(this);
            RsFire_Mark(this, "stick_lit");
        } else if (player->unk_860 < RS_FIRE_STICK_KEEP) {
            player->unk_860 = RS_FIRE_STICK_KEEP;
        }
    } else if (player->unk_860 != 0) {
        if (player->unk_860 < RS_FIRE_STICK_KEEP) {
            player->unk_860 = RS_FIRE_STICK_KEEP;
        }
        RsFire_Light(this, play, RS_FIRE_LIT_BY_STICK);
    }
}

void RsFire_Update(Actor* thisx, PlayState* play) {
    RsFire* this = (RsFire*)thisx;
    float intensity;

    if (!RsFire_TakeHit(this, play)) {
        RsFire_TakeStick(this, play);
    }
    // The draw distance and the glow, read every tick, so a CVar changed by hand reaches every fire in
    // view; `rsfire distance|glow` reaches the culled ones too.
    RsFire_ApplySettings(this);

    if (this->lit) {
        this->timer++;
        if (this->growTicks < RS_FIRE_GROW_TICKS) {
            this->growTicks++;
        }
        if (this->lightNode != NULL) {
            // En_Light's flicker, at params 4's colour.
            intensity = Rand_ZeroOne() * 0.5f + 0.5f;
            Lights_PointSetColorAndRadius(&this->lightInfo, (u8)(255.0f * intensity), (u8)(200.0f * intensity), 0,
                                          RS_FIRE_LIGHT_RADIUS);
        }
        if (!(sRsFireProbes & RS_FIRE_PROBE_NO_SOUND)) {
            Audio_PlayActorSound2(thisx, NA_SE_EV_TORCH - SFX_FLAG);
        }
    }

    // The flame's collider, every tick it updates - and it updates only while in view, so the culling
    // (RsFire.h, the draw distance) is the range: an arrow can light any fire Link can see.
    if (!(sRsFireProbes & RS_FIRE_PROBE_NO_COLLIDER)) {
        Collider_UpdateCylinder(thisx, &this->collider);
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider.base);
    }
}

void RsFire_Draw(Actor* thisx, PlayState* play) {
    RsFire* this = (RsFire*)thisx;
    float grow;

    if (!this->lit || (sRsFireProbes & RS_FIRE_PROBE_NO_FLAME)) {
        return;
    }
    grow = (this->growTicks < RS_FIRE_GROW_TICKS) ? ((float)this->growTicks / (float)RS_FIRE_GROW_TICKS) : 1.0f;

    OPEN_DISPS(play->state.gfxCtx);

    // The same setup list as Gfx_SetupDL_25Xlu, written inline: a Debug build's open/close markers around
    // that call were two of a fire's 25 translucent words (sturdy-bassoon#247).
    POLY_XLU_DISP = Gfx_SetupDL(POLY_XLU_DISP, SETUPDL_25);
    // En_Light's two scrolling layers (params >= 0), with its prim and env colours for index 4.
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 0, 32, 64, 1, 0, (this->timer * -20) & 511, 32,
                                             128, 0, 0, 0, -20));
    gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 200, 0, 255);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, 0);

    // Billboarded: the actor's own matrix carries no turn, so this one faces the camera.
    Matrix_RotateY((f32)((s16)(Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) + 0x8000) * (M_PI / 32768.0)),
                   MTXMODE_APPLY);
    Matrix_Scale(grow, grow, grow, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffFire1DL);

    CLOSE_DISPS(play->state.gfxCtx);
}
