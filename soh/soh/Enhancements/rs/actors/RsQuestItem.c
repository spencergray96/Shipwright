/*
 * RsQuestItem.c - the touch-to-collect quest item actor (sturdy-bassoon#58 P3 / #64, D16).
 *
 * Same C-under-/W3-/WX departures from vanilla idiom as RsNpc.c: no `s32 pad`, no ICHAIN, and an
 * explicit (Gfx*) on a gameplay_keep display list. Like RsNpc.c it contains no reference to
 * sceneNum and must not grow one.
 */

#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsQuestItem.h"
#include "RsActorParams.h"
#include "RsActors.h"
#include "global.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"

#define RS_ITEM_FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED)

// Collection radius, in world units, measured horizontally from the actor. Generous for the same
// reason RsNpc's talk range is: the agent loop arrives by teleport, and a tight radius makes "the
// step did not get set" and "I landed 6 units short" the same symptom.
#define RS_ITEM_COLLECT_RANGE 55.0f

// The pickup line. One string for every item in P3 - the three real items and their own wording
// arrive in P4 with the Cook's Assistant definitions.
static const char* sPickupText = "You picked something up.&The quest journal will&remember it.";
static const char* sBadParamsText = "This item does not know&which quest it belongs to.";

void RsQuestItem_Init(Actor* thisx, PlayState* play);
void RsQuestItem_Destroy(Actor* thisx, PlayState* play);
void RsQuestItem_Update(Actor* thisx, PlayState* play);
void RsQuestItem_Draw(Actor* thisx, PlayState* play);

static void RsQuestItem_Wait(RsQuestItem* this, PlayState* play);
static void RsQuestItem_Collected(RsQuestItem* this, PlayState* play);

static ColliderCylinderInit sCylinderInit = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_NONE,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_2,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00000000, 0x00, 0x00 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_NONE,
        OCELEM_ON,
    },
    { 18, 30, 0, { 0, 0, 0 } },
};

void RsQuestItem_Init(Actor* thisx, PlayState* play) {
    RsQuestItem* this = (RsQuestItem*)thisx;
    const QuestDef* def;

    this->questId = RS_ITEM_PARAMS_GET_QUEST(thisx->params);
    this->step = RS_ITEM_PARAMS_GET_STEP(thisx->params);
    this->valid = 0;

    def = Quest_GetDef(this->questId); // NULL for an invalid OR unregistered id; never asserts
    if (RS_ITEM_PARAMS_GET_RSVD(thisx->params) != 0) {
        LUSLOG_ERROR("RsQuestItem: params 0x%04X has reserved bits set", (u16)thisx->params);
    } else if (def == NULL) {
        LUSLOG_ERROR("RsQuestItem: quest %d has no definition in this build", this->questId);
    } else if (this->step < 0 || this->step >= def->stepCount) {
        LUSLOG_ERROR("RsQuestItem: quest %d has no step %d (stepCount %d)", this->questId, this->step, def->stepCount);
    } else {
        this->valid = 1;
    }

    // NOTHING IS WRITTEN HERE. The pitfall this actor exists to respect is "flags are set on
    // collection, never on spawn": a spawn that sets the step lets the player leave the zone
    // without the item and be locked out of it forever.

    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, &this->actor, &sCylinderInit);
    ActorShape_Init(&thisx->shape, 0.0f, ActorShadow_DrawCircle, 9.0f);
    Actor_SetScale(thisx, 0.02f);

    thisx->uncullZoneDownward = 1200.0f;
    thisx->uncullZoneScale = 200.0f;
    thisx->gravity = -1.5f;

    this->actionFunc = RsQuestItem_Wait;
}

void RsQuestItem_Destroy(Actor* thisx, PlayState* play) {
    RsQuestItem* this = (RsQuestItem*)thisx;

    Collider_DestroyCylinder(play, &this->collider);
}

static void RsQuestItem_Wait(RsQuestItem* this, PlayState* play) {
    char line[128];
    Player* player = GET_PLAYER(play);
    s32 result;

    // An ACTORCAT_PROP actor keeps updating while Link is in a textbox (D_80116068[ACTORCAT_PROP]
    // does not list PLAYER_STATE1_TALKING), so without this gate an item lying near a quest-giver
    // would open its own textbox in the middle of a conversation - reassigning msgCtx->talkActor
    // and stepping on the reply the NPC is showing. Wait until the screen is clear.
    if (Message_GetState(&play->msgCtx) != TEXT_STATE_NONE ||
        (player->stateFlags1 & (PLAYER_STATE1_TALKING | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_CUTSCENE)) != 0) {
        return;
    }
    if (this->actor.xzDistToPlayer > RS_ITEM_COLLECT_RANGE || fabsf(this->actor.yDistToPlayer) > 60.0f) {
        return;
    }

    if (this->valid) {
        // Check-then-write, the P1 rule: an ordered-quest violation is bug class inside
        // Quest_SetStep (log + debug assert), and a tripped assert hangs the agent loop. The check
        // proves the same refusal without it, and a refusal is reported, not swallowed.
        result = Quest_CheckSetStep(this->questId, this->step);
        if (result == QUEST_OK) {
            result = Quest_SetStep(this->questId, this->step);
        }
        RsText_SetDirect(sPickupText);
    } else {
        result = QUEST_ERR_BAD_DEF;
        RsText_SetDirect(sBadParamsText);
    }

    snprintf(line, sizeof(line), "rs_item quest=%d step=%d event=collect result=%s", this->questId, this->step,
             Quest_ResultName(result));
    RsAgent_Marker(line);

    Audio_PlayActorSound2(&this->actor, NA_SE_SY_GET_ITEM);
    Message_StartTextbox(play, RS_TEXT_DIRECT, &this->actor);
    this->collider.base.ocFlags1 &= ~OC1_ON;
    this->actionFunc = RsQuestItem_Collected;
}

// Hidden and inert, but still alive: killing an actor that owns the OPEN textbox would leave
// msgCtx->talkActor dangling. Wait for the box to go away, then go.
static void RsQuestItem_Collected(RsQuestItem* this, PlayState* play) {
    u8 state = Message_GetState(&play->msgCtx);

    if (state == TEXT_STATE_NONE || state == TEXT_STATE_CLOSING) {
        Actor_Kill(&this->actor);
    }
}

void RsQuestItem_Update(Actor* thisx, PlayState* play) {
    RsQuestItem* this = (RsQuestItem*)thisx;

    this->actionFunc(this, play);

    Actor_MoveXZGravity(thisx);
    Actor_UpdateBgCheckInfo(play, thisx, 5.0f, 20.0f, 0.0f, 0x1D);

    if (this->actionFunc == RsQuestItem_Wait) {
        Collider_UpdateCylinder(thisx, &this->collider);
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider.base);
    }
}

void RsQuestItem_Draw(Actor* thisx, PlayState* play) {
    RsQuestItem* this = (RsQuestItem*)thisx;

    if (this->actionFunc != RsQuestItem_Wait) {
        return; // collected: nothing to draw while the pickup textbox finishes
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_MODELVIEW | G_MTX_LOAD);
    // gameplay_keep again, so no scene needs an object list entry (see the note in RsNpc_Draw).
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gHeartPieceInteriorDL);

    CLOSE_DISPS(play->state.gfxCtx);
}
