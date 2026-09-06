#include "RsActors.h"
#include "RsActorParams.h"
#include "RsNpc.h"
#include "RsQuestItem.h"

#include <cstdio>
#include <string>

#include "soh/ActorDB.h"
#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/dialogue/NpcIds.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestStore.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"

void RsNpc_Init(Actor* thisx, PlayState* play);
void RsNpc_Destroy(Actor* thisx, PlayState* play);
void RsNpc_Update(Actor* thisx, PlayState* play);
void RsNpc_Draw(Actor* thisx, PlayState* play);

void RsQuestItem_Init(Actor* thisx, PlayState* play);
void RsQuestItem_Destroy(Actor* thisx, PlayState* play);
void RsQuestItem_Update(Actor* thisx, PlayState* play);
void RsQuestItem_Draw(Actor* thisx, PlayState* play);
}

// The C++ half of the mod's actors (sturdy-bassoon#58 P3 / #64):
//   - registering them with ActorDB at boot (D6: never in the 428-overlay tree), and
//   - rendering their textboxes, because CustomMessageManager is C++.

namespace {

// --- the one-slot direct-text pointer ------------------------------------------------------------
//
// See RsActors.h: a parameter passed through a global, not shared state. The ENTRY textbox never
// touches it - its id carries the npc and the rule.
//
// It is read LATER than it looks. Message_OpenText is re-entered from the draw path on a mid-text
// language switch (z_message_PAL.c), so the pointer has to stay valid for as long as the box is
// open - which is why RsActors.h says it must always be a definition string. `sDirectCopy` is the
// escape hatch for the one caller that needs a COMPOSED line (a quest item naming what it was):
// the sentence is copied here, into storage that outlives every actor, because the actor that
// built it is killed while its own textbox is still on screen.
const char* sDirectText = nullptr;
char sDirectCopy[128];

// --- message building ---------------------------------------------------------------------------

// AutoFormat is CTRL_TWO_CHOICE-aware: CustomMessage::AutoFormatString finds '\x1B' and lays the
// choice out on the right rows itself, paginating with '^' when the body is long. It does NOT know
// CTRL_THREE_CHOICE, which is why a three-option rule is hand-laid-out with Format() and its body
// is capped to one short line at registration (NpcDialogue.cpp).
// The rule's whole textbox source: a body, plus the choice block when it has options. Split out of
// BuildRuleMessage so the REGISTRATION-TIME pagination check below asks the exact question the
// renderer will answer, rather than a character count standing in for it.
std::string BuildRuleText(const RsDialogueRule& rule, const std::string& body) {
    std::string text = body;
    if (rule.optionCount >= 2) {
        // A two-way choice sits on the last two rows of the box and a three-way on the last three,
        // so the body needs one blank row before the first and none before the second. Getting this
        // wrong pushes the last option off the bottom of the box, where it is still selectable and
        // simply cannot be read - so it is a rendering bug that looks like a content bug. The two
        // in-tree three-way messages (QoL/BetterSaveMenu.cpp) use exactly this spacing.
        text += (rule.optionCount == 3) ? "&" : "&&";
        text += (rule.optionCount == 3) ? CustomMessage::THREE_WAY_CHOICE() : CustomMessage::TWO_WAY_CHOICE();
        text += "%g";
        for (int32_t i = 0; i < rule.optionCount; i++) {
            if (i > 0) {
                text += "&";
            }
            text += rule.options[i].label;
        }
        text += "%w";
    }
    return text;
}

CustomMessage BuildRuleMessage(const RsDialogueRule& rule) {
    // The composed body (D26): the rule's own text, plus its missing-steps clause when it carries
    // one. Composed HERE rather than stored anywhere, from the global stores, at open time - which
    // is what keeps the entry textbox stateless and therefore safe with two NPCs in talk range.
    // The console prints this same function's output, so the two surfaces cannot drift.
    const std::string text = BuildRuleText(rule, RsNpc_ComposeRuleText(rule));
    // QUICKTEXT_ENABLE (control code 08). Two reasons, and the second is the load-bearing one:
    // a quest-giver's line is information, not drama, and - because the agent test loop advances a
    // textbox with a fixed-duration button injection - text that is still typing swallows the
    // press, since TEXT_STATE_CHOICE is only reached once msgMode is MSGMODE_TEXT_DONE. Instant
    // text makes the conversation deterministic to drive unattended.
    CustomMessage msg(std::string("\x08") + text);
    if (rule.optionCount == 3) {
        msg.Format();
    } else {
        msg.AutoFormat();
    }
    return msg;
}

CustomMessage BuildPlainMessage(const char* text) {
    CustomMessage msg(std::string("\x08") + text);
    msg.AutoFormat();
    return msg;
}

// The unfiltered OnOpenText bucket, which GameInteractor_ExecuteOnOpenText runs before the per-id
// and filter buckets. Everything outside our band is left alone.
void RsText_OnOpenText(uint16_t* textId, bool* loadFromMessageTable) {
    const uint16_t id = *textId;

    if (id == RS_TEXT_DIRECT) {
        // Never a literal-prose fallback: an empty slot means an actor opened this id without
        // setting the text, which is a bug that must be visible rather than a blank box.
        CustomMessage msg = BuildPlainMessage(sDirectText != nullptr ? sDirectText : "<no direct text set>");
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }
    if (id < RS_TEXT_NPC_BASE || id > RS_TEXT_NPC_END) {
        return;
    }

    const int32_t npcId = RS_TEXT_NPC_GET_ID(id);
    const int32_t ruleIndex = RS_TEXT_NPC_GET_RULE(id);
    const RsNpcDef* def = RsNpc_GetDef(npcId);

    // A diagnostic, never silence and never a plausible-looking wrong line. Init already shouted
    // into the engine log; this is the half the player and the screenshot see.
    if (def == nullptr) {
        CustomMessage msg = BuildPlainMessage(("<unregistered npc " + std::to_string(npcId) + ">").c_str());
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }
    if (ruleIndex < 0 || ruleIndex >= def->ruleCount) {
        CustomMessage msg = BuildPlainMessage(
            ("<npc " + std::to_string(npcId) + " has no rule " + std::to_string(ruleIndex) + ">").c_str());
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }

    CustomMessage msg = BuildRuleMessage(def->rules[ruleIndex]);
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

// --- already-collected items do not spawn (P4) --------------------------------------------------
//
// The last piece of the item lifecycle, and the mirror image of the pitfall the actor itself
// honours. RsQuestItem_Init writes NOTHING, because a flag set on SPAWN lets a player leave the
// zone without the item and be locked out of it forever; this is the other direction, and it is
// safe precisely because it only READS: an item whose step is already set has nothing left to give,
// so it is never built.
//
// Answering false makes Actor_Init/Actor_UpdateAll call Actor_Kill instead of init (z_actor.c),
// which is vanilla's own path: Actor_Spawn has already zeroed the instance, Actor_Kill only nulls
// update/draw, and the delete path's Destroy runs Collider_DestroyCylinder on a collider that was
// never initialised - a no-op chain in z_collision_check.c. Nothing here may assert: this runs
// inside scene load, and a Debug assert there hangs the agent loop with no window to read.
//
// The order of the three reads is the whole of the safety argument. Quest_GetDef is the only quiet
// lookup (Quest_IsStepSet goes through Resolve, which asserts on an unregistered id and again on a
// step past stepCount), so it comes first; the step is then range-checked against the DEFINITION
// before the raw store is asked. A params word naming a quest or step this build does not have is
// deliberately left ALONE - the actor spawns and RsQuestItem_Init shouts about it, which is a
// mistake you can see rather than an item that silently never appears.
void RsQuestItem_ShouldInit(void* actorRef, bool* should) {
    Actor* actor = static_cast<Actor*>(actorRef);
    const int32_t questId = RS_ITEM_PARAMS_GET_QUEST(actor->params);
    const int32_t step = RS_ITEM_PARAMS_GET_STEP(actor->params);

    if (RS_ITEM_PARAMS_GET_RSVD(actor->params) != 0) {
        return; // a params word this build cannot read: let Init say so
    }
    const QuestDef* def = Quest_GetDef(questId); // NULL for an invalid OR unregistered id; quiet
    if (def == nullptr || step < 0 || step >= def->stepCount) {
        return;
    }
    if (!QuestStore_IsStepSet(questId, step)) {
        return;
    }
    *should = false;

    char line[96];
    std::snprintf(line, sizeof(line), "rs_item quest=%d step=%d event=suppressed", questId, step);
    AgentTest_WriteMarker(line);
}

// --- ActorDB registration -----------------------------------------------------------------------
//
// ShipInit "*" functions re-run on preset apply and config load, and ActorDB::AddEntry asserts on
// both a duplicate name and an already-valid slot - so the guard is not optional. IvanCoop.cpp
// (the only other AddEntry caller in the tree) uses the same file-static bool.
bool sAddedToActorDB = false;

void RegisterRsActors() {
    if (!sAddedToActorDB) {
        ActorDBInit npc = {
            "Rs_Npc",
            "RS quest-giver NPC",
            ACTOR_RS_NPC,
            ACTORCAT_NPC,
            (u32)(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY | ACTOR_FLAG_UPDATE_CULLING_DISABLED),
            OBJECT_GAMEPLAY_KEEP,
            sizeof(RsNpc),
            (ActorFunc)RsNpc_Init,
            (ActorFunc)RsNpc_Destroy,
            (ActorFunc)RsNpc_Update,
            (ActorFunc)RsNpc_Draw,
            nullptr,
        };
        ActorDB::Instance->AddEntry(npc);

        ActorDBInit item = {
            "Rs_QuestItem",
            "RS quest item (touch to collect)",
            ACTOR_RS_QUEST_ITEM,
            ACTORCAT_PROP,
            (u32)(ACTOR_FLAG_UPDATE_CULLING_DISABLED),
            OBJECT_GAMEPLAY_KEEP,
            sizeof(RsQuestItem),
            (ActorFunc)RsQuestItem_Init,
            (ActorFunc)RsQuestItem_Destroy,
            (ActorFunc)RsQuestItem_Update,
            (ActorFunc)RsQuestItem_Draw,
            nullptr,
        };
        ActorDB::Instance->AddEntry(item);

        sAddedToActorDB = true;
    }

    // COND_HOOK unregisters its previous hook before registering, so a ShipInit re-run leaves
    // exactly one. COND_ID_HOOK does the same for the per-actor-id bucket, and the only other
    // ShouldActorInit registration in the tree is Anchor's, on ACTOR_PLAYER - a different bucket.
    COND_HOOK(OnOpenText, true, RsText_OnOpenText);
    COND_ID_HOOK(ShouldActorInit, ACTOR_RS_QUEST_ITEM, true, RsQuestItem_ShouldInit);
}

RegisterShipInitFunc rsActorsInitFunc(RegisterRsActors);

} // namespace

extern "C" void RsText_SetDirect(const char* text) {
    sDirectText = text;
}

extern "C" void RsText_SetDirectCopy(const char* text) {
    if (text == nullptr) {
        sDirectText = nullptr;
        return;
    }
    std::snprintf(sDirectCopy, sizeof(sDirectCopy), "%s", text);
    sDirectText = sDirectCopy;
}

extern "C" void RsAgent_Marker(const char* line) {
    AgentTest_WriteMarker(line);
}

extern "C" int32_t RsText_ChoiceWouldPaginate(const RsDialogueRule* rule) {
    // Asked at REGISTRATION, and it asks the renderer rather than guessing. A two-option body that
    // wraps to two lines pushes AutoFormatString past four lines, and its answer to that is
    // `"^&&\x1B"` - a page break BEFORE the choice. The box then renders perfectly and behaves
    // differently: the first A press turns the page instead of choosing. That is not a cosmetic
    // bug, and no marker sees it - P4 shipped exactly this once and it took a screenshot to find.
    //
    // A character cap would be a guess: the real budget is 216 PIXELS in a variable-width font, so
    // 32 characters of one string fits and 33 of another does not. Formatting the actual message
    // and looking for the '^' AutoFormatString would have inserted is the exact question.
    if (rule == nullptr || rule->optionCount != 2 || rule->text == nullptr) {
        return 0; // a statement paginates harmlessly; a three-option box goes through Format(),
                  // which never paginates and is covered by its own one-line cap instead
    }
    // This FORMATS the rule, so it reads every label. The validator only calls it after each label
    // has been proven non-NULL, and this is the second lock on that door: the definitions most
    // likely to reach a validator are the malformed ones, and a crash while deciding that a
    // definition is bad looks exactly like a hang.
    if (rule->options == nullptr) {
        return 0;
    }
    for (int32_t i = 0; i < rule->optionCount; i++) {
        if (rule->options[i].label == nullptr) {
            return 0;
        }
    }
    CustomMessage msg(std::string("\x08") + BuildRuleText(*rule, rule->text));
    msg.AutoFormat();
    // Look for the BOX BREAK, not for a '^'. AutoFormatString's last act is to replace every '^' it
    // inserted with WAIT_FOR_INPUT's control byte, so the character the author writes does not
    // survive the pass that decides where the breaks go. Searching the source convention instead of
    // the compiled one is a check that always passes - which is what the first version of this did.
    return msg.GetEnglish(MF_RAW).find(CustomMessage::WAIT_FOR_INPUT()) != std::string::npos ? 1 : 0;
}
