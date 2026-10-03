/*
 * RsInteraction.c - the invisible trigger over an interactive prop (sturdy-bassoon#183).
 *
 * The interactive-props ADR's "trigger over baked" shape: no draw function, no collider, no
 * gravity, no movement. The prop it stands over is drawn by the map's baked archive list and
 * collides through the scene's static collision, and neither knows this actor exists. All it does
 * is offer a check and run the interaction's dialogue - the same screens, gates, flag effects and
 * trees as a character (RsNpc.c), with two differences:
 *
 *   - THE SPEAKER IS THIS ACTOR, NOT THE TEXT ID. A character's entry id carries its NpcId; an
 *     interaction's ids carry only which screen is open (InteractionIds.h), and the OnOpenText hook
 *     asks Player which actor it is talking to, then reads that actor's params. Two interactive
 *     props side by side cannot render each other's line, because Player talks to exactly one.
 *   - NO DIRECT-TEXT SLOT, ever. A reply opens on an id naming (screen, option), and the hook
 *     composes it from the speaker's own definition. The #117 prototype wrote the shared slot every
 *     tick and the last actor to update won.
 *
 * Update is kept to what the bench measured for a null-draw trigger: an offer, and a check of the
 * talk request. The focus is set once at Init - nothing moves it.
 *
 * Same /W3 /WX rules as RsNpc.c: no `s32 pad`, no ICHAIN.
 */

#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsInteraction.h"
#include "RsActorParams.h"
#include "RsActors.h"
#include "global.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/interactions/Interaction.h"
#include "soh/Enhancements/rs/quest/Quest.h"

// The y window Link must be inside to check it: a prop on the storey he stands on, not one on the
// floor above or below. The x/z range comes from the placement (rot.z).
#define RS_INTERACTION_TALK_Y 60.0f
// A placement that carries no range (a hand-written row, rot.z of 0) still works at a step's reach.
#define RS_INTERACTION_DEFAULT_RANGE 60.0f

void RsInteraction_Init(Actor* thisx, PlayState* play);
void RsInteraction_Destroy(Actor* thisx, PlayState* play);
void RsInteraction_Update(Actor* thisx, PlayState* play);

static void RsInteraction_Wait(RsInteraction* this, PlayState* play);
static void RsInteraction_Talk(RsInteraction* this, PlayState* play);

static void RsInteraction_Mark(RsInteraction* this, const char* event) {
    char line[160];

    snprintf(line, sizeof(line), "rs_interaction id=%d event=%s checks=%d rule=%d code=%d x=%d y=%d z=%d",
             this->interactionId, event, this->checks, this->ruleIndex,
             RsInteraction_GetDef(this->interactionId) != NULL ? 1 : 0, (int)this->actor.world.pos.x,
             (int)this->actor.world.pos.y, (int)this->actor.world.pos.z);
    RsAgent_Marker(line);
}

void RsInteraction_Init(Actor* thisx, PlayState* play) {
    RsInteraction* this = (RsInteraction*)thisx;
    char line[128];

    this->interactionId = RS_INTERACTION_PARAMS_GET_ID(thisx->params);
    this->ruleIndex = -1;
    this->checks = 0;
    this->range = thisx->home.rot.z > 0 ? (float)thisx->home.rot.z : RS_INTERACTION_DEFAULT_RANGE;

    // Loud, never fatal (RsNpc's rule): a placement naming id 0 still stands there and says the
    // placeholder. An id with no code yet is NOT a fault - it is how every interaction starts - so it
    // is not reported here; `interaction status` lists it.
    if (!RS_INTERACTION_ID_IS_VALID(this->interactionId)) {
        LUSLOG_ERROR("RsInteraction: params 0x%04X names no interaction", (u16)thisx->params);
        snprintf(line, sizeof(line), "rs_interaction id=%d event=bad_placement params=0x%04X", this->interactionId,
                 (unsigned)(u16)thisx->params);
        RsAgent_Marker(line);
    }

    ActorShape_Init(&thisx->shape, 0.0f, NULL, 0.0f);
    // The rot fields carried the focus height and the range; the actor itself faces nowhere.
    thisx->world.rot.x = thisx->world.rot.z = 0;
    thisx->shape.rot.x = thisx->shape.rot.z = 0;
    thisx->targetMode = 0;
    thisx->textId = RS_TEXT_INTERACTION_ENTRY;
    // Once: nothing moves this actor, so the attention arrow's anchor never changes.
    thisx->focus.pos = thisx->world.pos;
    thisx->focus.pos.y += (float)thisx->home.rot.x;
    thisx->focus.rot = thisx->world.rot;

    this->actionFunc = RsInteraction_Wait;
}

void RsInteraction_Destroy(Actor* thisx, PlayState* play) {
}

static void RsInteraction_Wait(RsInteraction* this, PlayState* play) {
    if (Actor_ProcessTalkRequest(&this->actor, play)) {
        // The same gates the hook resolves when it renders the entry box, in the same frame
        // (Player opened the box earlier this tick), so the two agree on the rule.
        this->ruleIndex = RsDialogue_ResolveRule(RsInteraction_GetDef(this->interactionId));
        this->checks++;
        RsInteraction_Mark(this, "open");
        this->actionFunc = RsInteraction_Talk;
        return;
    }
    Actor_OfferTalkExchange(&this->actor, play, this->range, RS_INTERACTION_TALK_Y, EXCH_ITEM_NONE);
}

static void RsInteraction_End(RsInteraction* this) {
    RsInteraction_Mark(this, "close");
    this->actionFunc = RsInteraction_Wait;
}

static void RsInteraction_GoToNode(RsInteraction* this, PlayState* play, s32 head) {
    char line[128];
    s32 node = RsDialogue_ResolveNode(RsInteraction_GetDef(this->interactionId), head);

    if (node < 0) {
        node = head; // unreachable for a registered definition; the hook's diagnostic says so
    }
    snprintf(line, sizeof(line), "rs_interaction id=%d event=node node=%d head=%d", this->interactionId, (int)node,
             (int)head);
    RsAgent_Marker(line);
    Message_ContinueTextbox(play, RS_TEXT_INTERACTION_NODE_ID(node));
}

// Which screen is open, from the live text id: the entry box is the rule this check resolved, a
// node id names its node, and a reply id names the screen its option was on. `slot` is what a reply
// on this screen would carry.
static const RsDialogueRule* RsInteraction_OpenScreen(RsInteraction* this, u16 openId, s32* slot) {
    const RsInteractionDef* def = RsInteraction_GetDef(this->interactionId);

    if (openId == RS_TEXT_INTERACTION_ENTRY) {
        *slot = this->ruleIndex;
        return RsDialogue_Screen(def, RS_SCREEN_RULE, this->ruleIndex);
    }
    if (RS_TEXT_IS_INTERACTION_NODE(openId)) {
        *slot = RS_DIALOGUE_MAX_RULES + RS_TEXT_INTERACTION_GET_NODE(openId);
        return RsDialogue_Screen(def, RS_SCREEN_NODE, RS_TEXT_INTERACTION_GET_NODE(openId));
    }
    return NULL;
}

// The screen a reply slot names: a rule (0..31) or a node (32..63).
static const RsDialogueRule* RsInteraction_SlotScreen(const RsInteractionDef* def, s32 slot) {
    return slot < RS_DIALOGUE_MAX_RULES ? RsDialogue_Screen(def, RS_SCREEN_RULE, slot)
                                        : RsDialogue_Screen(def, RS_SCREEN_NODE, slot - RS_DIALOGUE_MAX_RULES);
}

static void RsInteraction_Talk(RsInteraction* this, PlayState* play) {
    const RsInteractionDef* def = RsInteraction_GetDef(this->interactionId);
    const RsDialogueRule* screen;
    const RsDialogueOption* option;
    char line[224];
    u8 state = Message_GetState(&play->msgCtx);
    u16 openId = play->msgCtx.textId;
    s32 slots[RS_DIALOGUE_MAX_OPTIONS];
    s32 visibleCount;
    s32 slot = -1;
    s32 choice;
    s32 declared;
    s32 result;

    // Closed from outside - a transition, damage, another box: back to waiting (RsNpc's rule).
    if (state == TEXT_STATE_NONE) {
        RsInteraction_End(this);
        return;
    }

    if (state == TEXT_STATE_CHOICE) {
        if (!Message_ShouldAdvance(play)) {
            return;
        }
        screen = RsInteraction_OpenScreen(this, openId, &slot);
        if (screen == NULL) {
            return; // a reply box, or not ours
        }
        // The cursor counts VISIBLE rows; map it back through the list the renderer laid out.
        visibleCount = RsNpc_VisibleOptions(screen, slots, RS_DIALOGUE_MAX_OPTIONS);
        choice = play->msgCtx.choiceIndex;
        if (choice < 0 || choice >= visibleCount) {
            return;
        }
        declared = slots[choice];
        option = &screen->options[declared];
        result = RsNpc_RunAction(option);
        snprintf(line, sizeof(line),
                 "rs_interaction id=%d event=choice slot=%d index=%d option=%d action=%s a=%d result=%s next=%d",
                 this->interactionId, (int)slot, (int)choice, (int)declared, RsNpc_ActionName(option->kind),
                 option->a, Quest_ResultName(result), (int)option->next);
        RsAgent_Marker(line);
        if (option->reply != NULL) {
            // The reply's id names this screen and this option, so the hook composes it from the
            // definition; DONE on that id then follows the option's `next`.
            Message_ContinueTextbox(play, RS_TEXT_INTERACTION_REPLY_ID(slot, declared));
        } else if (option->next != RS_DLG_NO_NEXT) {
            RsInteraction_GoToNode(this, play, option->next);
        } else {
            Message_CloseTextbox(play);
            RsInteraction_End(this);
        }
        return;
    }

    if (state == TEXT_STATE_DONE && Message_ShouldAdvance(play)) {
        if (RS_TEXT_IS_INTERACTION_REPLY(openId)) {
            screen = RsInteraction_SlotScreen(def, RS_TEXT_INTERACTION_GET_SLOT(openId));
            declared = RS_TEXT_INTERACTION_GET_OPTION(openId);
            if (screen != NULL && declared < screen->optionCount && screen->options[declared].next != RS_DLG_NO_NEXT) {
                RsInteraction_GoToNode(this, play, screen->options[declared].next);
                return;
            }
        } else {
            // A statement that continues: the screen names where to go when it is dismissed.
            screen = RsInteraction_OpenScreen(this, openId, &slot);
            if (screen != NULL && screen->optionCount == 0 && screen->next != RS_DLG_NO_NEXT) {
                RsInteraction_GoToNode(this, play, screen->next);
                return;
            }
        }
        RsInteraction_End(this);
    }
}

void RsInteraction_Update(Actor* thisx, PlayState* play) {
    RsInteraction* this = (RsInteraction*)thisx;

    this->actionFunc(this, play);
}
