/*
 * RsAnimProp.c - one flipbook placement, drawn from its own pose lists (sturdy-bassoon#204 C4a). RsAnimProp.h
 * has the design: the row, the clock, the lights and the cull.
 *
 * Same /W3 /WX rules as the other rs/ actors: explicit casts, no `s32 pad`, no ICHAIN.
 */

#include <math.h>
#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsAnimProp.h"
#include "RsActorParams.h"
#include "RsActors.h"
#include "global.h"

void RsAnimProp_Init(Actor* thisx, PlayState* play);
void RsAnimProp_Destroy(Actor* thisx, PlayState* play);
void RsAnimProp_Update(Actor* thisx, PlayState* play);
void RsAnimProp_Draw(Actor* thisx, PlayState* play);

// The room's hilite origin (z_room.c's D_801270A0): the room sets its specular up from it before its lists.
static Vec3f sRsAnimPropHiliteOrigin = { 0.0f, 0.0f, 0.0f };

int32_t RsAnimProp_Loops(const ArchivePropsAnimCopy* copy) {
    return copy->frameStep >= 1 && copy->frameStep <= copy->poseCount;
}

// RS's DynamicObject (THE CLOCK, RsAnimProp.h): from pose 0 cycle 0, each pose its length; past the last, back
// frameStep poses and round again; a step out of range ends the play on rest. `cycle` counts from the play's
// own start, so a looping copy's random start is added by the caller.
int32_t RsAnimProp_PoseAt(const ArchivePropsAnimCopy* copy, int64_t cycle) {
    int64_t total = 0;
    int64_t loop = 0;
    int32_t from;
    int32_t k;

    if (cycle < 0) {
        cycle = 0;
    }
    for (k = 0; k < copy->poseCount; k++) {
        total += copy->cycles[k];
    }
    if (cycle >= total) {
        if (!RsAnimProp_Loops(copy)) {
            return copy->poseCount;
        }
        from = copy->poseCount - copy->frameStep;
        for (k = from; k < copy->poseCount; k++) {
            loop += copy->cycles[k];
        }
        cycle = (cycle - total) % loop;
    } else {
        from = 0;
    }
    for (k = from; k < copy->poseCount; k++) {
        if (cycle < copy->cycles[k]) {
            return k;
        }
        cycle -= copy->cycles[k];
    }
    return copy->poseCount - 1; // not reached: the cycles add up
}

// Where the copy's play stands now, in client cycles: a looping one from the clock's zero plus its start, so
// copies keep their phase against each other and a pinned clock holds every pose; a play-once one from its Init.
static int64_t RsAnimProp_Cycle(const RsAnimProp* this) {
    const double now = RsAnimProp_ClockSeconds();
    int64_t start = 0;
    int32_t k;

    if (!RsAnimProp_Loops(this->copy)) {
        return (int64_t)floor((now - this->initClock) * 50.0);
    }
    for (k = 0; k < this->copy->startPose; k++) {
        start += this->copy->cycles[k];
    }
    return (int64_t)floor(now * 50.0) + start + this->copy->startCycle;
}

static void RsAnimProp_Marker(RsAnimProp* this, const char* event) {
    char line[224];
    snprintf(line, sizeof(line), "rs_animprop event=%s copy=%d list=%d state=%s poses=%d x=%d y=%d z=%d", event,
             (int)this->copyIndex, (int)this->list, ArchiveProps_AnimMissName(this->miss),
             this->copy != NULL ? (int)this->copy->poseCount : 0, (int)lroundf(this->actor.world.pos.x),
             (int)lroundf(this->actor.world.pos.y), (int)lroundf(this->actor.world.pos.z));
    RsAgent_Marker(line);
}

// The copy's lists from ArchiveProps, again whenever they were released since (a new bake group, `staticbake
// reset`): a stale pointer is never drawn.
static void RsAnimProp_Fetch(RsAnimProp* this, PlayState* play) {
    s32 miss = ARCHIVE_PROPS_ANIM_OK;

    this->generation = ArchiveProps_Generation();
    this->copy = ArchiveProps_AnimCopy(play, this->actor.room, this->list, this->copyIndex, this->check, &miss);
    this->miss = (int16_t)miss;
}

void RsAnimProp_Init(Actor* thisx, PlayState* play) {
    RsAnimProp* this = (RsAnimProp*)thisx;

    this->copyIndex = (int16_t)RS_ANIMPROP_PARAMS_GET_COPY(thisx->params);
    this->list = thisx->home.rot.x;
    this->check = thisx->home.rot.z;
    this->pose = -1;
    this->changes = 0;
    this->draws = 0;
    this->initClock = RsAnimProp_ClockSeconds();
    // The lists are in world space: nothing in the actor's own transform may move them (RsAnimProp.h).
    thisx->shape.rot.x = 0;
    thisx->shape.rot.z = 0;
    thisx->uncullZoneForward = (f32)RS_ANIMPROP_DRAW_DISTANCE;
    RsAnimProp_Fetch(this, play);
    RsAnimProp_Marker(this, "spawn");
}

void RsAnimProp_Destroy(Actor* thisx, PlayState* play) {
}

// Nothing to do a tick: the clock is read when the copy draws. An actor needs an Update (Actor_UpdateAll
// deletes one without), and it is called only while the copy is in its cull volume.
void RsAnimProp_Update(Actor* thisx, PlayState* play) {
}

void RsAnimProp_Draw(Actor* thisx, PlayState* play) {
    RsAnimProp* this = (RsAnimProp*)thisx;
    const int32_t forced = RsAnimProp_ForcedPose();
    Gfx* opa;
    Gfx* xlu;
    int32_t pose;

    RsAnimProp_FrameTick(play);
    if (this->generation != ArchiveProps_Generation()) {
        RsAnimProp_Fetch(this, play);
    }
    if (this->copy == NULL) {
        return;
    }
    if (forced == RS_ANIMPROP_FORCE_REST) {
        pose = this->copy->poseCount;
    } else if (forced >= 0) {
        pose = forced < this->copy->poseCount ? forced : this->copy->poseCount - 1;
    } else {
        pose = RsAnimProp_PoseAt(this->copy, RsAnimProp_Cycle(this));
    }
    if (this->pose >= 0 && pose != this->pose) {
        this->changes++;
    }
    this->pose = (int16_t)pose;
    this->draws++;
    opa = this->copy->lists[pose][ARCHIVE_PROPS_PASS_OPA];
    xlu = ArchiveProps_XluSubmitted() ? this->copy->lists[pose][ARCHIVE_PROPS_PASS_XLU] : NULL;

    OPEN_DISPS(play->state.gfxCtx);

    // Exactly the room's state before its archive lists (z_room.c, func_80095AB4): the hilite, the room's
    // setup list and the identity modelview, in each pass. The lists set their own materials.
    if (opa != NULL) {
        func_800342EC(&sRsAnimPropHiliteOrigin, play);
        func_80093C80(play);
        gSPMatrix(POLY_OPA_DISP++, &gMtxClear, G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPDisplayList(POLY_OPA_DISP++, opa);
    }
    if (xlu != NULL) {
        func_8003435C(&sRsAnimPropHiliteOrigin, play);
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gSPMatrix(POLY_XLU_DISP++, &gMtxClear, G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPDisplayList(POLY_XLU_DISP++, xlu);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
