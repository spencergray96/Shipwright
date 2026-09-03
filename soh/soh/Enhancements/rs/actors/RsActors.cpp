#include "RsActors.h"
#include "RsActorParams.h"
#include "RsNpc.h"
#include "RsQuestItem.h"

#include <string>

#include "soh/ActorDB.h"
#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/dialogue/NpcIds.h"
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
const char* sDirectText = nullptr;

// --- message building ---------------------------------------------------------------------------

// AutoFormat is CTRL_TWO_CHOICE-aware: CustomMessage::AutoFormatString finds '\x1B' and lays the
// choice out on the right rows itself, paginating with '^' when the body is long. It does NOT know
// CTRL_THREE_CHOICE, which is why a three-option rule is hand-laid-out with Format() and its body
// is capped to one short line at registration (NpcDialogue.cpp).
CustomMessage BuildRuleMessage(const RsDialogueRule& rule) {
    std::string text = rule.text;
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
    // exactly one.
    COND_HOOK(OnOpenText, true, RsText_OnOpenText);
}

RegisterShipInitFunc rsActorsInitFunc(RegisterRsActors);

} // namespace

extern "C" void RsText_SetDirect(const char* text) {
    sDirectText = text;
}

extern "C" void RsAgent_Marker(const char* line) {
    AgentTest_WriteMarker(line);
}
