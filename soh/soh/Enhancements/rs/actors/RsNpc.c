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
 *
 * A CONVERSATION, AS STATES (`actionFunc`):
 *
 *     Wait --talk accepted--> Talk --the way out (RsNpc_Leave)--> Wait
 *                              |  ^
 *            a gift or a sale  |  |  re-opened with no A press, on the option's `next`
 *            was promised      v  |
 *                             Give --> GiveDone --> Resume
 *
 *   Wait    resolves the entry rule every frame and offers talk. On acceptance the conversation begins:
 *           the notice queue is emptied and the entry screen's speaker gets the floor (RsSpeaker).
 *   Talk    knows which screen is open ONLY from the open box's text id - the actor stores no position.
 *           On a choice it maps the cursor (plus the scroll offset) back to the declared option,
 *           remembers the pick for choice memory, and runs ACTION -> REPLY -> NEXT. On a finished box
 *           it follows a reply's or a statement's `next`, or takes the way out.
 *   Leave   the one way out: each queued journal notice as a box of its own (dismissing one comes back
 *           here), then the floor is given back, then a promised item (Give) or the close (RsNpc_End).
 *   Give, GiveDone, Resume   GIVE_ITEM and BUY_ITEM's hand-over, after the box has closed (below).
 *
 * The plain-English walkthrough of all of this, with the text hook's half, is docs/reference/
 * QUEST_SYSTEM.md "A conversation, start to finish" in sturdy-bassoon.
 */

#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsNpc.h"
#include "RsActorParams.h"
#include "RsActors.h"
#include "RsQuestItem.h"
#include "RsSpeaker.h"
#include "global.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/interactions/Interaction.h"
#include "soh/Enhancements/rs/quest/Quest.h"

#define RS_NPC_FLAGS (ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY | ACTOR_FLAG_UPDATE_CULLING_DISABLED)

// Talk range. Generous on purpose: the agent test loop reaches an NPC by teleporting near it, and
// a tight range turns "the rule table is wrong" and "I stood 5 units too far away" into the same
// symptom.
#define RS_NPC_TALK_RANGE 110.0f

// How far Link may be from an NPC handing him an item (sturdy-bassoon#281) - while it offers, and when
// the conversation re-opens afterwards - before it withdraws and ends instead. He cannot walk during
// the get-item ceremony, so in play he is where the conversation left him, inside talk range; this
// stops an offer Player keeps declining (Link mid-air, carrying something) from being taken, or a
// re-open from pulling him back into a conversation, from across the room.
#define RS_NPC_RESUME_RANGE 300.0f

void RsNpc_Init(Actor* thisx, PlayState* play);
void RsNpc_Destroy(Actor* thisx, PlayState* play);
void RsNpc_Update(Actor* thisx, PlayState* play);
void RsNpc_Draw(Actor* thisx, PlayState* play);

static void RsNpc_Wait(RsNpc* this, PlayState* play);
static void RsNpc_Talk(RsNpc* this, PlayState* play);
static void RsNpc_Give(RsNpc* this, PlayState* play);
static void RsNpc_GiveDone(RsNpc* this, PlayState* play);
static void RsNpc_Resume(RsNpc* this, PlayState* play);

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
// inferring it from a screenshot. Outside agent mode the line reaches the engine log only
// (sturdy-bassoon#97).
static void RsNpc_Mark(RsNpc* this, const char* event) {
    char line[128];

    snprintf(line, sizeof(line), "rs_dialogue npc=%d event=%s rule=%d", this->npcId, event, this->ruleIndex);
    RsAgent_Marker(line);
}

/* Hands the floor to whoever speaks the box this conversation just opened on `textId`, if it names one
 * of this character's screens (#281). Every other box - a notice, a reply - says nothing about its
 * speaker in its id, so its caller hands the floor itself or leaves it where it is. */
static void RsNpc_HandToScreen(RsNpc* this, PlayState* play, u16 textId, const char* on) {
    s32 screenNpc = -1;
    s32 screenIndex = -1;
    const s32 kind = RsNpc_DecodeScreen(textId, &screenNpc, &screenIndex);
    const RsDialogueRule* screen = screenNpc == this->npcId ? RsNpc_Screen(this->npcId, kind, screenIndex) : NULL;

    RsSpeaker_Hand(play, this->ruleIndex, screen != NULL ? screen->speaker : RS_DLG_SPEAKER_DEFAULT, on);
}

/* Who answers an option: its own reply speaker, or whoever spoke the screen it was picked on. */
static s32 RsNpc_ReplySpeaker(const RsDialogueRule* screen, const RsDialogueOption* option) {
    return option->replySpeaker != RS_DLG_SPEAKER_DEFAULT ? option->replySpeaker : screen->speaker;
}

void RsNpc_Init(Actor* thisx, PlayState* play) {
    RsNpc* this = (RsNpc*)thisx;

    this->npcId = RS_NPC_PARAMS_GET_ID(thisx->params);
    this->ruleIndex = -1;
    this->giving = NULL;

    // Loud, but never fatal and never an assert. An unregistered character or a params word with a
    // reserved bit set is a mistake worth shouting about - and killing the actor would hide it,
    // while asserting would hang the agent loop on a Debug build. So it stands there, and every
    // conversation with it says what is wrong (RsActors.cpp renders the diagnostic).
    if (RS_NPC_PARAMS_GET_RSVD(thisx->params) != 0) {
        LUSLOG_ERROR("RsNpc: params 0x%04X has reserved bits set (npc %d)", (u16)thisx->params, this->npcId);
    }
    // Before the registered check below: every character has registered by the time a placement
    // spawns, so this is when a speaker naming an unregistered character can be refused (#281).
    RsNpc_CheckSpeakers();
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

    RsSpeaker_ActorGone(play, thisx); /* never leave Player or the camera on a freed speaker (#281) */
    Collider_DestroyCylinder(play, &this->collider);
}

// The idle state. The rule is re-resolved EVERY FRAME from the global stores - it is presentation
// rebuilt from state, never state of its own - and the text id it produces carries both the
// character and the rule, so two of these standing next to each other cannot render each other's
// line. Note Player updates before most actors and reads `textId` from the previous frame, so a
// state change and a talk request landing on the same frame speaks one frame late; the next frame
// corrects it, and nothing in the rule table depends on the difference.
static void RsNpc_Wait(RsNpc* this, PlayState* play) {
    /* The id Player has just opened a box with, if it accepted the offer this frame: Player updates first,
     * so it read last frame's id, not the one about to be written (#281 hands the floor by it). */
    const u16 offered = this->actor.textId;

    this->ruleIndex = RsNpc_ResolveRule(this->npcId);
    this->actor.textId = RS_TEXT_NPC_ID(this->npcId, this->ruleIndex >= 0 ? this->ruleIndex : 0);

    if (Actor_ProcessTalkRequest(&this->actor, play)) {
        // A new conversation: whatever notices an earlier one left queued - it closed from outside,
        // say - are not this one's to show (NpcDialogue.h).
        RsDialogue_NoticeReset();
        this->giving = NULL;
        RsNpc_Mark(this, "open");
        /* The floor, from the first line (#281): a conversation whose entry screen is someone else's
         * opens the camera straight on them, because the talk camera has not framed anyone yet. */
        RsSpeaker_Begin(&this->actor, this->npcId);
        RsNpc_HandToScreen(this, play, offered, "entry");
        this->actionFunc = RsNpc_Talk;
        return;
    }
    Actor_OfferTalk(&this->actor, play, RS_NPC_TALK_RANGE);
}

/* Back to idle, the conversation over: the one place that says so, whichever way it ended. */
static void RsNpc_End(RsNpc* this) {
    this->giving = NULL;
    RsSpeaker_End();
    RsNpc_Mark(this, "close");
    this->actionFunc = RsNpc_Wait;
}

/* The next JOURNAL NOTICE, if one is queued (sturdy-bassoon#281), as this conversation's next box. Asked
 * wherever the conversation would otherwise end, so the queue is shown before it does - Majora's Mask
 * holds back "closing" until its notebook queue is empty in the same way. A notice box's id names no
 * screen, so dismissing it comes back through the same way out and asks again. 1 when a box was opened. */
static s32 RsNpc_ShowNotice(RsNpc* this, PlayState* play) {
    char line[128];
    s32 questId;
    s32 kind;

    if (!RsDialogue_NoticeTake(&questId, &kind)) {
        return 0;
    }
    snprintf(line, sizeof(line), "rs_dialogue npc=%d event=notice rule=%d quest=%d kind=%s", this->npcId,
             this->ruleIndex, (int)questId, Quest_NoticeKindName(kind));
    RsAgent_Marker(line);
    Message_ContinueTextbox(play, RS_TEXT_NOTICE_ID(questId, kind));
    return 1;
}

/* Every way out of a conversation comes through here (#281), so nothing it owes the player can be
 * skipped: first the journal notices its options queued, one box each; then an item one of them
 * promised (GIVE_ITEM); then the close. `closeBox` is whether the box is ours to close - a choice
 * picked with nowhere to go - rather than one closing by itself, as a dismissed statement does. */
static void RsNpc_Leave(RsNpc* this, PlayState* play, s32 closeBox) {
    if (RsNpc_ShowNotice(this, play)) {
        return;
    }
    /* The box closes now, so the floor goes with it - a gift's re-open begins a new lending (#281). */
    RsSpeaker_End();
    if (closeBox) {
        Message_CloseTextbox(play);
    }
    if (this->giving != NULL) {
        this->actionFunc = RsNpc_Give;
        return;
    }
    RsNpc_End(this);
}

/* Follow an edge to `head` (sturdy-bassoon#96). One helper because an edge is followed from three
 * places: an option picked with no reply, an option's reply dismissed, and a statement dismissed.
 *
 * The node GROUP is resolved HERE, on arrival, and that timing is the point: it runs after the
 * picked option's action has fired, so an option that sets a flag and then navigates lands on the
 * node that flag selects. `node=` in the marker is where the conversation landed and `head=` is the
 * edge that was followed; they differ exactly when a group fell through. */
static s32 RsNpc_LandOn(RsNpc* this, s32 head) {
    s32 node = RsNpc_ResolveNode(this->npcId, head);

    if (node < 0) {
        /* Unreachable for a registered definition. Open the head anyway, so the renderer's
         * diagnostic box says what is wrong instead of the conversation silently doing nothing. */
        node = head;
    }
    return node;
}

static void RsNpc_MarkNode(RsNpc* this, s32 node, s32 head) {
    char line[128];

    snprintf(line, sizeof(line), "rs_dialogue npc=%d event=node rule=%d node=%d head=%d", this->npcId,
             this->ruleIndex, (int)node, (int)head);
    RsAgent_Marker(line);
}

static void RsNpc_GoToNode(RsNpc* this, PlayState* play, s32 head) {
    s32 node = RsNpc_LandOn(this, head);

    RsNpc_MarkNode(this, node, head);
    Message_ContinueTextbox(play, RS_TEXT_NODE_ID(this->npcId, node));
    RsNpc_HandToScreen(this, play, RS_TEXT_NODE_ID(this->npcId, node), "node");
}

static void RsNpc_Talk(RsNpc* this, PlayState* play) {
    const RsDialogueRule* screen;
    const RsDialogueOption* option;
    char line[224];
    u8 state = Message_GetState(&play->msgCtx);
    u16 openId = play->msgCtx.textId;
    s32 slots[RS_DIALOGUE_MAX_OPTIONS];
    s32 visibleCount;
    s32 scroll;
    s32 screenKind;
    s32 screenNpc = -1;
    s32 screenIndex = -1;
    s32 choice;
    s32 declared;
    s32 result;
    const char* reply = NULL; /* the option's reply, or a refusal's words in its place (#286) */

    // Anything that closes the box from outside the conversation - a scene transition, damage,
    // another actor opening a textbox - leaves no other way back. Without this the actor sits in
    // Talk forever and is permanently unresponsive, which reads as "the rule table broke".
    // ovl_En_Ms has exactly this hole; copying it wholesale would have inherited it.
    if (state == TEXT_STATE_NONE) {
        this->giving = NULL; /* a gift promised in a conversation that never reached its way out */
        RsSpeaker_End();
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
         * option silently runs the action next to the one that was picked.
         *
         * A list longer than the box scrolls (sturdy-bassoon#290): the cursor's row is counted from
         * the top of the WINDOW, so the window's offset is added to reach the visible list. */
        visibleCount = RsNpc_VisibleOptions(screen, slots, RS_DIALOGUE_MAX_OPTIONS);
        scroll = RsText_ChoiceScroll();
        choice = play->msgCtx.choiceIndex + scroll;
        if (choice < 0 || choice >= visibleCount) {
            return;
        }
        declared = slots[choice];
        option = &screen->options[declared];
        /* Remembered for a return to this screen (sturdy-bassoon#281), before anything moves on. */
        RsText_RememberChoice(declared);
        /* ACTION -> REPLY -> NEXT. The action fires wherever navigation goes afterwards. */
        RsInteraction_ClearActionWords();
        result = RsNpc_RunAction(option);
        reply = option->reply;
        /* A refused items action says why in place of the reply (RsItems_SayRefusal, #286): the reply is written
         * for it having happened. Everything else - the speaker, `next` - is the reply's. */
        {
            s32 instead = 0;
            const char* says = RsInteraction_ActionWords(&instead);
            if (says != NULL && instead) {
                reply = says;
            }
        }
        /* A GIVE_ITEM or BUY_ITEM whose checks pass promises an item. The promise is all that is kept:
         * the step is set, and a sale's price taken, when Link takes the item after the box closes
         * (RsNpc_Give). */
        if (RsNpc_OptionItem(option) >= 0 && result == QUEST_OK) {
            this->giving = option;
        }
        /* Fields are APPENDED, never inserted: `rule= index= action= a= result=` stay in the order
         * every earlier phase's acceptance regex expects. On an entry rule with no gated options,
         * `screen_index` equals `rule` and `option` equals `index`, so those runs keep matching
         * verbatim - the new fields only start to differ where the new features are in play. */
        snprintf(line, sizeof(line),
                 "rs_dialogue npc=%d event=choice rule=%d index=%d action=%s a=%d result=%s screen=%s screen_index=%d "
                 "option=%d visible=%d next=%d scroll=%d",
                 this->npcId, this->ruleIndex, (int)choice, RsNpc_ActionName(option->kind), option->a,
                 Quest_ResultName(result), RsNpc_ScreenKindName(screenKind), (int)screenIndex,
                 (int)declared, (int)visibleCount, (int)option->next, (int)scroll);
        RsAgent_Marker(line);
        if (this->giving != NULL) {
            /* The item comes before `next`: the reply, then the way out, which hands it over and only
             * then follows the edge (RsNpc_Resume). So the reply opens on the plain direct id, and
             * dismissing it leaves rather than navigating. */
            if (reply != NULL) {
                RsText_SetDirectCopy(reply);
                Message_ContinueTextbox(play, RS_TEXT_DIRECT);
                RsSpeaker_Hand(play, this->ruleIndex, RsNpc_ReplySpeaker(screen, option), "reply");
            } else {
                RsNpc_Leave(this, play, 1);
            }
            return;
        }
        if (reply != NULL) {
            /* SetDirectCopy, not SetDirect: a reply may carry a `{floor:N}` token
             * (sturdy-bassoon#94), and the moment it does, the string the box reads is COMPOSED
             * rather than the definition's own - so it needs storage that outlives this actor. */
            RsText_SetDirectCopy(reply);
            /* An option carrying BOTH a reply and a `next` opens the reply on an id that NAMES the
             * pending node, so the destination rides on the open box rather than in a field here
             * (NpcDialogueDef.h). A reply with nowhere to go afterwards keeps the plain id. */
            Message_ContinueTextbox(play, option->next != RS_DLG_NO_NEXT ? RS_TEXT_REPLY_TO_NODE(option->next)
                                                                        : RS_TEXT_DIRECT);
            /* A reply's id names where it goes, not who said it, so the floor is handed here (#281). */
            RsSpeaker_Hand(play, this->ruleIndex, RsNpc_ReplySpeaker(screen, option), "reply");
        } else if (option->next != RS_DLG_NO_NEXT) {
            RsNpc_GoToNode(this, play, option->next);
        } else {
            RsNpc_Leave(this, play, 1);
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
        RsNpc_Leave(this, play, 0);
    }
}

/* --- GIVE_ITEM and BUY_ITEM: handing an item over (sturdy-bassoon#281) -----------------------------
 *
 * Majora's Mask's sequence, which is OoT's EnDs and EnAni with one camera line: close -> offer -> wait
 * for the parent -> reset -> wait out Player's box -> re-open. The offer is a quest item's own get-item
 * entry (RsQuestItem_OfferFrom), so the sprite, the ceremony and the pickup line are the ones picking
 * that item off the floor shows; nothing enters the inventory (D16).
 *
 * Offered every frame from here, at Link's own distance plus one, so he takes it wherever he stands
 * once Player is free of the conversation - Majora's Mask's OFFER_ITEM range. Player accepts on its own
 * update (it updates before an NPC does), and this actor becomes the item's `parent`. */
static void RsNpc_Give(RsNpc* this, PlayState* play) {
    char line[160];
    s32 result;
    s32 item = RsNpc_OptionItem(this->giving);

    if (Actor_HasParent(&this->actor, play)) {
        /* Handed over: clear it, as Majora's Mask's SET_COLLECTIBLE does after every OFFER_ITEM, so a
         * later give from this actor is a new acceptance rather than this one seen again. */
        this->actor.parent = NULL;
        /* Written on acceptance, check-then-write, as a get-item quest item sets its own step - and a
         * sale is paid for here, in the same call, so it is never half done. `price=` is what that took. */
        result = RsNpc_SetGivenStep(this->giving);
        snprintf(line, sizeof(line), "rs_dialogue npc=%d event=give rule=%d quest=%d step=%d result=%s price=%d",
                 this->npcId, this->ruleIndex, (int)RS_DLG_ITEM_GET_QUEST(item), (int)RS_DLG_ITEM_GET_STEP(item),
                 Quest_ResultName(result), result == QUEST_OK ? (int)RsNpc_OptionPrice(this->giving) : 0);
        RsAgent_Marker(line);
        this->actionFunc = RsNpc_GiveDone;
        return;
    }
    /* Not from across the map: Player declines while Link is mid-air or carrying something, and he can
     * walk off meanwhile. Past the re-open range the offer is withdrawn; the step is still unset, so
     * talking again offers it again. */
    if (this->actor.xzDistToPlayer > RS_NPC_RESUME_RANGE) {
        RsNpc_End(this);
        return;
    }
    RsQuestItem_OfferFrom(&this->actor, play, RS_DLG_ITEM_GET_QUEST(item), RS_DLG_ITEM_GET_STEP(item),
                          this->actor.xzDistToPlayer + 1.0f, fabsf(this->actor.yDistToPlayer) + 1.0f);
}

/* Player's now: the animation, the item camera, the pickup box, putting the item away. Wait all of it
 * out, then carry on where the option said. With nowhere to go the conversation still re-opens if the
 * gift queued a journal notice, to show just that; otherwise it ends. */
static void RsNpc_GiveDone(RsNpc* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (Message_GetState(&play->msgCtx) != TEXT_STATE_NONE ||
        (player->stateFlags1 & (PLAYER_STATE1_TALKING | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_CUTSCENE)) != 0) {
        return;
    }
    if (this->giving->next == RS_DLG_NO_NEXT && !RsDialogue_NoticePeek(NULL, NULL)) {
        RsNpc_End(this);
        return;
    }
    this->actionFunc = RsNpc_Resume;
    RsNpc_Resume(this, play);
}

/* Re-open the conversation on the option's `next`, with no A press: ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED,
 * vanilla's own talk-without-A, which Player takes on its next update as though A had been pressed
 * (Majora's Mask's AUTOTALK is the same flag). The box opens on the node's own id, so Talk picks the
 * conversation up from it exactly as if it had been continued there, and the node group resolves now,
 * after the step was set - so a node gated on having the item is the one that speaks.
 *
 * The queue is NOT emptied here, as a fresh conversation's is: it holds the gift's UPDATED notice,
 * which this conversation shows at its own way out. With no `next`, it opens on that notice instead. */
static void RsNpc_Resume(RsNpc* this, PlayState* play) {
    char line[128];
    s32 head = this->giving->next;
    s32 node = -1;
    s32 questId = 0;
    s32 kind = 0;

    if (head != RS_DLG_NO_NEXT) {
        node = RsNpc_LandOn(this, head);
        this->actor.textId = RS_TEXT_NODE_ID(this->npcId, node);
    } else {
        RsDialogue_NoticePeek(&questId, &kind); /* GiveDone came here only with one queued */
        this->actor.textId = RS_TEXT_NOTICE_ID(questId, kind);
    }
    if (Actor_ProcessTalkRequest(&this->actor, play)) {
        this->actor.flags &= ~ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED;
        this->giving = NULL;
        RsSpeaker_Begin(&this->actor, this->npcId);
        if (node >= 0) {
            RsNpc_MarkNode(this, node, head);
            RsNpc_HandToScreen(this, play, RS_TEXT_NODE_ID(this->npcId, node), "node");
        } else {
            RsDialogue_NoticeTake(&questId, &kind);
            snprintf(line, sizeof(line), "rs_dialogue npc=%d event=notice rule=%d quest=%d kind=%s", this->npcId,
                     this->ruleIndex, (int)questId, Quest_NoticeKindName(kind));
            RsAgent_Marker(line);
        }
        this->actionFunc = RsNpc_Talk;
        return;
    }
    if (this->actor.xzDistToPlayer > RS_NPC_RESUME_RANGE) {
        this->actor.flags &= ~ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED;
        RsText_KeepRouteOnNextOpen(0);
        RsNpc_End(this);
        return;
    }
    this->actor.flags |= ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED;
    /* The box this opens continues the conversation, so its route is kept (sturdy-bassoon#281). */
    RsText_KeepRouteOnNextOpen(1);
    /* Both ranges from where Link stands. Actor_OfferTalk would use one radius for both, and a Link a
     * step up from the NPC but close to it would fail the height test every frame. */
    Actor_OfferTalkExchange(&this->actor, play, this->actor.xzDistToPlayer + 1.0f,
                            fabsf(this->actor.yDistToPlayer) + 1.0f, EXCH_ITEM_NONE);
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
