/*
 * RsNpc.c - the quest-giver NPC actor (sturdy-bassoon#58 P3 / #64).
 *
 * Idiomatic C (D5), so vanilla-derived code copies cleanly; modelled on ovl_En_Ms's talk loop and
 * z_en_a_keep.c's gameplay_keep-only draw. Two DELIBERATE departures from vanilla idiom, both
 * because files under soh/soh get the target-wide /W3 /WX while soh/src gets /w:
 *   - no `s32 pad` locals (C4101, level 3, fatal here)
 *   - no ICHAIN init chain (OFFSETOF is a size_t going into an 11-bit bitfield); the two fields
 *     vanilla would set that way are assigned directly
 * A third: a gameplay_keep display list is declared `const char[]`, so it needs an explicit
 * `(Gfx*)`. z_en_a_keep.c omits the cast and only compiles because of /w.
 *
 * THIS FILE CONTAINS NO REFERENCE TO sceneNum, AND MUST NOT GROW ONE. Many vanilla actors carry a
 * hardcoded switch(sceneNum) in CanSpawn/Init that kills them outside recognised vanilla scenes
 * (docs/reference/SCENE_CREATION.md), which is why so few vanilla NPCs work in a custom scene at
 * all. More to the point, the global quest and world-flag stores exist precisely so an NPC never
 * has to ask which scene it is standing in (D21/D22): one character in two scenes is ONE
 * character, and branching on the scene is how vanilla ends up with two.
 */

#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsNpc.h"
#include "RsActorParams.h"
#include "RsActors.h"
#include "global.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/quest/Quest.h"

#define RS_NPC_FLAGS (ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY | ACTOR_FLAG_UPDATE_CULLING_DISABLED)

// Talk range. Generous on purpose: the agent test loop reaches an NPC by teleporting near it, and
// a tight range turns "the rule table is wrong" and "I stood 5 units too far away" into the same
// symptom.
#define RS_NPC_TALK_RANGE 110.0f

void RsNpc_Init(Actor* thisx, PlayState* play);
void RsNpc_Destroy(Actor* thisx, PlayState* play);
void RsNpc_Update(Actor* thisx, PlayState* play);
void RsNpc_Draw(Actor* thisx, PlayState* play);

static void RsNpc_Wait(RsNpc* this, PlayState* play);
static void RsNpc_Talk(RsNpc* this, PlayState* play);

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
    { 25, 60, 0, { 0, 0, 0 } },
};

// One line per conversation event, so a run can assert what the rule table did rather than
// inferring it from a screenshot. Silent outside agent mode.
static void RsNpc_Mark(RsNpc* this, const char* event) {
    char line[128];

    snprintf(line, sizeof(line), "rs_dialogue npc=%d event=%s rule=%d", this->npcId, event, this->ruleIndex);
    RsAgent_Marker(line);
}

void RsNpc_Init(Actor* thisx, PlayState* play) {
    RsNpc* this = (RsNpc*)thisx;

    this->npcId = RS_NPC_PARAMS_GET_ID(thisx->params);
    this->ruleIndex = -1;

    // Loud, but never fatal and never an assert. An unregistered character or a params word with a
    // reserved bit set is a mistake worth shouting about - and killing the actor would hide it,
    // while asserting would hang the agent loop on a Debug build. So it stands there, and every
    // conversation with it says what is wrong (RsActors.cpp renders the diagnostic).
    if (RS_NPC_PARAMS_GET_RSVD(thisx->params) != 0) {
        LUSLOG_ERROR("RsNpc: params 0x%04X has reserved bits set (npc %d)", (u16)thisx->params, this->npcId);
    }
    if (!RsNpc_IsRegistered(this->npcId)) {
        LUSLOG_ERROR("RsNpc: npc id %d has no definition in this build", this->npcId);
    }

    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, &this->actor, &sCylinderInit);
    ActorShape_Init(&thisx->shape, 0.0f, ActorShadow_DrawCircle, 14.0f);
    Actor_SetScale(thisx, 0.01f);

    thisx->targetMode = 2;
    thisx->targetArrowOffset = 500.0f;
    thisx->colChkInfo.mass = MASS_IMMOVABLE;
    thisx->uncullZoneDownward = 1200.0f;
    thisx->uncullZoneScale = 200.0f;
    // Settle onto whatever floor is under the authored position rather than trusting the authored
    // y. Hand-placed entries (D24) carry a y a human guessed; a terrain scene's ground is baked
    // geometry nobody can read off a coordinate.
    thisx->gravity = -1.5f;

    this->actionFunc = RsNpc_Wait;
}

void RsNpc_Destroy(Actor* thisx, PlayState* play) {
    RsNpc* this = (RsNpc*)thisx;

    Collider_DestroyCylinder(play, &this->collider);
}

// The idle state. The rule is re-resolved EVERY FRAME from the global stores - it is presentation
// rebuilt from state, never state of its own - and the text id it produces carries both the
// character and the rule, so two of these standing next to each other cannot render each other's
// line. Note Player updates before most actors and reads `textId` from the previous frame, so a
// state change and a talk request landing on the same frame speaks one frame late; the next frame
// corrects it, and nothing in the rule table depends on the difference.
static void RsNpc_Wait(RsNpc* this, PlayState* play) {
    this->ruleIndex = RsNpc_ResolveRule(this->npcId);
    this->actor.textId = RS_TEXT_NPC_ID(this->npcId, this->ruleIndex >= 0 ? this->ruleIndex : 0);

    if (Actor_ProcessTalkRequest(&this->actor, play)) {
        RsNpc_Mark(this, "open");
        this->actionFunc = RsNpc_Talk;
        return;
    }
    Actor_OfferTalk(&this->actor, play, RS_NPC_TALK_RANGE);
}

static void RsNpc_Talk(RsNpc* this, PlayState* play) {
    const RsNpcDef* def;
    const RsDialogueRule* rule;
    const RsDialogueOption* option;
    char line[160];
    u8 state = Message_GetState(&play->msgCtx);
    s32 choice;
    s32 result;

    // Anything that closes the box from outside the conversation - a scene transition, damage,
    // another actor opening a textbox - leaves no other way back. Without this the actor sits in
    // Talk forever and is permanently unresponsive, which reads as "the rule table broke".
    // ovl_En_Ms has exactly this hole; copying it wholesale would have inherited it.
    if (state == TEXT_STATE_NONE) {
        this->actionFunc = RsNpc_Wait;
        return;
    }

    if (state == TEXT_STATE_CHOICE) {
        if (!Message_ShouldAdvance(play)) {
            return;
        }
        def = RsNpc_GetDef(this->npcId);
        if (def == NULL || this->ruleIndex < 0 || this->ruleIndex >= def->ruleCount) {
            this->actionFunc = RsNpc_Wait;
            return;
        }
        rule = &def->rules[this->ruleIndex];
        choice = play->msgCtx.choiceIndex;
        if (choice < 0 || choice >= rule->optionCount) {
            return;
        }
        option = &rule->options[choice];
        result = RsNpc_RunAction(option);
        snprintf(line, sizeof(line), "rs_dialogue npc=%d event=choice rule=%d index=%d action=%s a=%d result=%s",
                 this->npcId, this->ruleIndex, (int)choice, RsNpc_ActionName(option->kind), option->a,
                 Quest_ResultName(result));
        RsAgent_Marker(line);
        if (option->reply != NULL) {
            RsText_SetDirect(option->reply);
            Message_ContinueTextbox(play, RS_TEXT_DIRECT);
        } else {
            Message_CloseTextbox(play);
            this->actionFunc = RsNpc_Wait;
        }
        return;
    }

    if (state == TEXT_STATE_DONE && Message_ShouldAdvance(play)) {
        RsNpc_Mark(this, "close");
        this->actionFunc = RsNpc_Wait;
    }
}

void RsNpc_Update(Actor* thisx, PlayState* play) {
    RsNpc* this = (RsNpc*)thisx;

    this->actionFunc(this, play);

    Actor_MoveXZGravity(thisx);
    Actor_UpdateBgCheckInfo(play, thisx, 5.0f, 40.0f, 0.0f, 0x1D);

    Actor_SetFocus(thisx, 30.0f);
    Collider_UpdateCylinder(thisx, &this->collider);
    CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider.base);
}

void RsNpc_Draw(Actor* thisx, PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_MODELVIEW | G_MTX_LOAD);
    // A gameplay_keep display list, so this actor needs no object list entry in any scene -
    // gameplay_keep is spawned at bank index 0 by Object_InitBank before any actor spawns. That
    // matters on the setup-actor path specifically: Actor_SpawnEntry sets gMapLoading, which
    // suppresses the "fall back to bank 0" branch, so an actor whose object is absent silently
    // fails to spawn. Reusing an existing model is also the D21 model axis working as intended.
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gSignRectangularDL);

    CLOSE_DISPS(play->state.gfxCtx);
}
