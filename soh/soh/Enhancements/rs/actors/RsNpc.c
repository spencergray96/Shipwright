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

/* Follow an edge to `head` (sturdy-bassoon#96). One helper because an edge is followed from three
 * places: an option picked with no reply, an option's reply dismissed, and a statement dismissed.
 *
 * The node GROUP is resolved HERE, on arrival, and that timing is the point: it runs after the
 * picked option's action has fired, so an option that sets a flag and then navigates lands on the
 * node that flag selects. `node=` in the marker is where the conversation landed and `head=` is the
 * edge that was followed; they differ exactly when a group fell through. */
static void RsNpc_GoToNode(RsNpc* this, PlayState* play, s32 head) {
    char line[128];
    s32 node = RsNpc_ResolveNode(this->npcId, head);

    if (node < 0) {
        /* Unreachable for a registered definition. Open the head anyway, so the renderer's
         * diagnostic box says what is wrong instead of the conversation silently doing nothing. */
        node = head;
    }
    snprintf(line, sizeof(line), "rs_dialogue npc=%d event=node rule=%d node=%d head=%d", this->npcId,
             this->ruleIndex, (int)node, (int)head);
    RsAgent_Marker(line);
    Message_ContinueTextbox(play, RS_TEXT_NODE_ID(this->npcId, node));
}

static void RsNpc_Talk(RsNpc* this, PlayState* play) {
    const RsDialogueRule* screen;
    const RsDialogueOption* option;
    char line[224];
    u8 state = Message_GetState(&play->msgCtx);
    u16 openId = play->msgCtx.textId;
    s32 slots[RS_DIALOGUE_MAX_OPTIONS];
    s32 visibleCount;
    s32 screenKind;
    s32 screenNpc = -1;
    s32 screenIndex = -1;
    s32 choice;
    s32 declared;
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
        /* WHICH SCREEN IS OPEN comes from the LIVE text id, not from a field on this actor
         * (sturdy-bassoon#96). The entry box's id carries the rule that matched and a node box's
         * carries the node, so the whole of "where am I in this conversation" is already on screen
         * and the actor has nothing to remember - which is what keeps RsNpc.h's promise that
         * nothing in this struct needs to survive a scene transition. Walk away mid-tree and
         * re-talk, and entry resolution runs again, for free. */
        screenKind = RsNpc_DecodeScreen(openId, &screenNpc, &screenIndex);
        if (screenKind == RS_SCREEN_NONE || screenNpc != this->npcId) {
            return; /* somebody else's box, or a reply box: not ours to pick from */
        }
        screen = RsNpc_Screen(this->npcId, screenKind, screenIndex);
        if (screen == NULL) {
            this->actionFunc = RsNpc_Wait;
            return;
        }
        /* `choiceIndex` counts the rows the player can SEE, so it indexes the visible list, not the
         * definition. Mapping it back through the same list the renderer laid out is the whole of
         * gating's correctness (sturdy-bassoon#96 P2): get this wrong and a menu with a hidden
         * option silently runs the action next to the one that was picked. */
        visibleCount = RsNpc_VisibleOptions(screen, slots, RS_DIALOGUE_MAX_OPTIONS);
        choice = play->msgCtx.choiceIndex;
        if (choice < 0 || choice >= visibleCount) {
            return;
        }
        declared = slots[choice];
        option = &screen->options[declared];
        /* ACTION -> REPLY -> NEXT. The action fires wherever navigation goes afterwards. */
        result = RsNpc_RunAction(option);
        /* Fields are APPENDED, never inserted: `rule= index= action= a= result=` stay in the order
         * every earlier phase's acceptance regex expects. On an entry rule with no gated options,
         * `screen_index` equals `rule` and `option` equals `index`, so those runs keep matching
         * verbatim - the new fields only start to differ where the new features are in play. */
        snprintf(line, sizeof(line),
                 "rs_dialogue npc=%d event=choice rule=%d index=%d action=%s a=%d result=%s screen=%s screen_index=%d "
                 "option=%d visible=%d next=%d",
                 this->npcId, this->ruleIndex, (int)choice, RsNpc_ActionName(option->kind), option->a,
                 Quest_ResultName(result), RsNpc_ScreenKindName(screenKind), (int)screenIndex,
                 (int)declared, (int)visibleCount, (int)option->next);
        RsAgent_Marker(line);
        if (option->reply != NULL) {
            /* SetDirectCopy, not SetDirect: a reply may carry a `{floor:N}` token
             * (sturdy-bassoon#94), and the moment it does, the string the box reads is COMPOSED
             * rather than the definition's own - so it needs storage that outlives this actor. */
            RsText_SetDirectCopy(option->reply);
            /* An option carrying BOTH a reply and a `next` opens the reply on an id that NAMES the
             * pending node, so the destination rides on the open box rather than in a field here
             * (NpcDialogueDef.h). A reply with nowhere to go afterwards keeps the plain id. */
            Message_ContinueTextbox(play, option->next != RS_DLG_NO_NEXT ? RS_TEXT_REPLY_TO_NODE(option->next)
                                                                        : RS_TEXT_DIRECT);
        } else if (option->next != RS_DLG_NO_NEXT) {
            RsNpc_GoToNode(this, play, option->next);
        } else {
            Message_CloseTextbox(play);
            this->actionFunc = RsNpc_Wait;
        }
        return;
    }

    if (state == TEXT_STATE_DONE && Message_ShouldAdvance(play)) {
        /* The other half of the reply-then-navigate pair: the box that just finished says where to
         * go next, in its own id. */
        if (openId >= RS_TEXT_REPLY_TO_NODE_BASE && openId <= RS_TEXT_REPLY_TO_NODE_END) {
            RsNpc_GoToNode(this, play, RS_TEXT_REPLY_TO_NODE_GET(openId));
            return;
        }
        /* A STATEMENT that continues (sturdy-bassoon#96 follow-up): the screen itself names where
         * to go when it is dismissed. Decoded from the live id like every other screen, so this is
         * still no state on the actor. Only a screen with no options qualifies - registration
         * refuses `next` on a choice, and a choice screen that degraded to a statement (fewer than
         * two visible options) is unreachable for the same registration reason. */
        screenKind = RsNpc_DecodeScreen(openId, &screenNpc, &screenIndex);
        if (screenKind != RS_SCREEN_NONE && screenNpc == this->npcId) {
            screen = RsNpc_Screen(this->npcId, screenKind, screenIndex);
            if (screen != NULL && screen->optionCount == 0 && screen->next != RS_DLG_NO_NEXT) {
                RsNpc_GoToNode(this, play, screen->next);
                return;
            }
        }
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
