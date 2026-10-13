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
// En_Light's light: radius 300, the glow 40 above the fire's base at params 4's size.
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
    { 15, 45, -7, { 0, 0, 0 } }, // resized at Init
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

// How much bigger this fire's visible flame is than Obj_Syokudai's: where its collider and stick
// test are centred.
static float RsFire_Ratio(RsFire* this) {
    return this->size * (RS_FIRE_BASE_SCALE / RS_FIRE_SYOKUDAI_SCALE);
}

// The same for its hit size: how far they reach.
static float RsFire_HitRatio(RsFire* this) {
    return this->hitSize * (RS_FIRE_BASE_SCALE / RS_FIRE_SYOKUDAI_SCALE);
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

// Takes a node, once, if this fire wants a light and has none. A full pool returns NULL, and the fire
// burns unlit: nothing is taken from another holder (#233 is where that changes).
static void RsFire_TakeLight(RsFire* this, PlayState* play) {
    if (!this->lightWanted || this->lightNode != NULL) {
        return;
    }
    Lights_PointGlowSetInfo(&this->lightInfo, (s16)this->actor.world.pos.x,
                            (s16)(this->actor.world.pos.y + RS_FIRE_GLOW_ABOVE * this->size),
                            (s16)this->actor.world.pos.z, 255, 255, 180, -1);
    this->lightNode = LightContext_InsertLight(play, &play->lightCtx, &this->lightInfo);
}

static void RsFire_Light(RsFire* this, PlayState* play, int32_t by) {
    this->lit = 1;
    this->litBy = by;
    this->growTicks = 0;
    if (this->flag >= 0) {
        Flags_SetWorldFlag(this->flag);
    }
    RsFire_TakeLight(this, play);
    Audio_PlaySoundGeneral(NA_SE_EV_FLAME_IGNITION, &this->actor.projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
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

    ratio = RsFire_Ratio(this);
    hitRatio = RsFire_HitRatio(this);
    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, thisx, &sRsFireCylinderInit);
    // Obj_Syokudai's cylinder, sized by the hit size and centred on the visible flame's centre - which
    // is where Obj_Syokudai's own sits, 15 above its flame's base (-7 + 45 / 2).
    this->collider.dim.radius = (s16)(RS_FIRE_CYL_RADIUS * hitRatio + 0.5f);
    this->collider.dim.height = (s16)(RS_FIRE_CYL_HEIGHT * hitRatio + 0.5f);
    this->collider.dim.yShift = (s16)(RS_FIRE_STICK_ABOVE * ratio - (float)this->collider.dim.height / 2.0f);

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

// A hit on the flame: a normal arrow through a lit fire leaves burning; fire lights an unlit one.
static void RsFire_TakeHit(RsFire* this, PlayState* play) {
    uint32_t dmgFlags;
    EnArrow* arrow;

    if (!(this->collider.base.acFlags & AC_HIT)) {
        return;
    }
    this->collider.base.acFlags &= ~AC_HIT;
    dmgFlags = this->collider.info.acHitInfo->toucher.dmgFlags;
    if (this->lit) {
        if (dmgFlags & RS_FIRE_DMG_ARROW) {
            // Obj_Syokudai's own conversion: params 0 is a burning arrow, and 0x800 is what it now
            // deals - which is what lights the next fire it passes through.
            arrow = (EnArrow*)this->collider.base.ac;
            if (arrow != NULL && arrow->actor.update != NULL && arrow->actor.id == ACTOR_EN_ARROW) {
                arrow->actor.params = 0;
                arrow->collider.info.toucher.dmgFlags = 0x800;
                RsFire_Mark(this, "arrow_lit");
            }
        }
    } else if (dmgFlags & RS_FIRE_DMG_FIRE) {
        RsFire_Light(this, play, RS_FIRE_LIT_BY_FIRE);
    }
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
    ratio = RsFire_Ratio(this);
    hitRatio = RsFire_HitRatio(this);
    d.x = player->meleeWeaponInfo[0].tip.x - this->actor.world.pos.x;
    d.y = player->meleeWeaponInfo[0].tip.y - (this->actor.world.pos.y + RS_FIRE_STICK_ABOVE * ratio);
    d.z = player->meleeWeaponInfo[0].tip.z - this->actor.world.pos.z;
    // Centred on the visible flame; the reach grows with the hit size, and never below Obj_Syokudai's.
    reach = RS_FIRE_STICK_REACH * (hitRatio > 1.0f ? hitRatio : 1.0f);
    if (SQ(d.x) + SQ(d.y) + SQ(d.z) >= SQ(reach)) {
        return;
    }
    if (this->lit) {
        if (player->unk_860 == 0) {
            player->unk_860 = RS_FIRE_STICK_LIGHT;
            Audio_PlaySoundGeneral(NA_SE_EV_FLAME_IGNITION, &this->actor.projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
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

    RsFire_TakeHit(this, play);
    RsFire_TakeStick(this, play);

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
        Audio_PlayActorSound2(thisx, NA_SE_EV_TORCH - SFX_FLAG);
    }

    // The flame's collider, only while Link is near enough to reach it with a stick or an arrow.
    if (thisx->xyzDistToPlayerSq < SQ(RS_FIRE_AC_RANGE)) {
        Collider_UpdateCylinder(thisx, &this->collider);
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider.base);
    }
}

void RsFire_Draw(Actor* thisx, PlayState* play) {
    RsFire* this = (RsFire*)thisx;
    float grow;

    if (!this->lit) {
        return;
    }
    grow = (this->growTicks < RS_FIRE_GROW_TICKS) ? ((float)this->growTicks / (float)RS_FIRE_GROW_TICKS) : 1.0f;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
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
