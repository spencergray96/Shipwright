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
#include "soh/Enhancements/rs/prefs/FloorText.h"
#include "soh/Enhancements/rs/prefs/RsPrefs.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestStore.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"

// For the box-geometry guard below. Declared here the same way NpcConsole.cpp does it.
extern PlayState* gPlayState;

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
// escape hatch for a caller whose line is COMPOSED rather than named (an option's reply, which may
// carry a `{floor:N}` token): the expanded line is copied here, into storage that outlives every
// actor.
//
// `sDirectCopy` was a char[128] that truncated silently. It is a std::string now (#94): a
// `{floor:N}` token is shorter than the label it expands to, so the composed line is no longer the
// length the author sees, and a sentence cut in half renders plausibly - the failure mode this
// project treats as the enemy. `sDirectText` is re-pointed on the line after every assignment, so
// a reallocation cannot leave it dangling.
const char* sDirectText = nullptr;
std::string sDirectCopy;

// --- message building ---------------------------------------------------------------------------

// --- the box's row budget ------------------------------------------------------------------------
//
// A textbox row is R_TEXT_LINE_SPACING, which Message_OpenText sets to 12 for English (NOT the 16
// z_construct initialises it to - that value never survives a textbox opening). Vanilla's 64px box
// is therefore four rows with 8px under the last one. A four-option rule needs FIVE rows - a body
// row plus one per option - so it gets a taller box.
//
// The texture step is `R_TEXTBOX_TEXHEIGHT << 1` in 5.10 fixed point, so 512 is exactly one texel
// per pixel over the 64-texel-tall background. A SMALLER value advances more slowly, i.e. stretches:
// 512 * 64/80 = 410. Message_GrowTextbox does this same division every frame of the open animation,
// which is why a stretched box is a known quantity rather than a gamble.
constexpr int16_t RS_TEXTBOX_ROWS_HEIGHT = 80;      // five rows: 8 + 4*12 + 12 = 68, rounded up
constexpr int16_t RS_TEXTBOX_ROWS_TEXHEIGHT = 410;  // 512 * 64 / 80
constexpr int16_t RS_TEXTBOX_VANILLA_HEIGHT = 64;
constexpr int16_t RS_TEXTBOX_VANILLA_TEXHEIGHT = 512;

// AutoFormat is CTRL_TWO_CHOICE-aware: CustomMessage::AutoFormatString finds '\x1B' and lays the
// choice out on the right rows itself, paginating with '^' when the body is long. It does NOT know
// CTRL_THREE_CHOICE or our CTRL_FOUR_CHOICE, which is why a three- or four-option rule is
// hand-laid-out with Format() and its body is measured against one row at registration
// (NpcDialogue.cpp).
// The rule's whole textbox source: a body, plus the choice block when it has options. Split out of
// BuildRuleMessage so the REGISTRATION-TIME pagination check below asks the exact question the
// renderer will answer, rather than a character count standing in for it.
//
// `convention` is which floor convention the option LABELS are expanded under (#94). It is a
// parameter rather than a read of the live setting because the registration-time checks below
// measure the rule under EVERY convention - a box that fits in UK and paginates in US is a bug no
// marker sees, and the caller that renders simply passes the live value.
// `slots` / `visibleCount` are the DECLARED indices of the options actually being offered
// (sturdy-bassoon#96 P2), from RsNpc_VisibleOptions. They are passed in rather than recomputed
// because the mapping between a rendered row and a definition option has to be the same list the
// actor maps `msgCtx.choiceIndex` back through - two places computing it independently is how a
// gated menu picks the wrong option.
std::string BuildScreenText(const RsDialogueRule& screen, const std::string& body, int32_t convention,
                            const int32_t* slots, int32_t visibleCount) {
    std::string text = body;
    if (visibleCount >= 2) {
        // A two-way choice sits on the last two rows of a four-row box, so its body needs one BLANK
        // row before it; a three-way fills the last three rows and a four-way (in a five-row box)
        // the last four, so neither wants the blank. Getting this wrong pushes the last option off
        // the bottom of the box, where it is still selectable and simply cannot be read - a
        // rendering bug that looks like a content bug. The two in-tree three-way messages
        // (QoL/BetterSaveMenu.cpp) use exactly this spacing.
        text += (visibleCount == 2) ? "&&" : "&";
        text += (visibleCount == 4)   ? CustomMessage::FOUR_WAY_CHOICE()
                : (visibleCount == 3) ? CustomMessage::THREE_WAY_CHOICE()
                                      : CustomMessage::TWO_WAY_CHOICE();
        text += "%g";
        for (int32_t i = 0; i < visibleCount; i++) {
            if (i > 0) {
                text += "&";
            }
            text += RsFloorText_ExpandUnder(screen.options[slots[i]].label, convention);
        }
        text += "%w";
    }
    // A visible count of ONE draws no choice block at all, so the screen degrades to a statement
    // rather than to a cursor sitting on a row that does not exist. Registration makes that
    // unreachable (the two-ungated rule, NpcDialogue.cpp) - this is the second lock on the same
    // door, because the failure it guards against is invisible in a screenshot: the box looks
    // right and the conversation does something else.
    return text;
}

// QUICKTEXT_ENABLE on EVERY page, not only the first (sturdy-bassoon#96). The control code is per
// TEXTBOX PAGE: Message_DrawText honours it only at the start of the page being drawn, and each page
// is decoded on its own - so the one byte at the front of a message makes page 1 instant and leaves
// page 2 onwards typing out. That stayed invisible until a reply got long enough to paginate, which
// #96's fixture does on purpose, and then it is two failures at once: a player reads information at
// drama speed, and the agent loop's A press on a still-typing page finishes the page instead of
// turning it, so a driven conversation silently falls one press behind and stays there.
//
// The byte is re-emitted after every page break AutoFormat inserted. CustomMessage::AutoFormat(ItemID)
// repeats its item icon on every page with the same Replace, which is the precedent. Only the
// AutoFormat path needs it: Format() - the three- and four-option layout - never paginates, and the
// registration-time measurements build their own messages, so what they search for is unchanged.
void QuickTextEveryPage(CustomMessage& msg) {
    msg.Replace(CustomMessage::WAIT_FOR_INPUT(), CustomMessage::WAIT_FOR_INPUT() + std::string("\x08"));
}

CustomMessage BuildScreenMessage(const RsDialogueRule& screen, const int32_t* slots, int32_t visibleCount) {
    // The composed body (D26): the screen's own text, plus its missing-steps clause when it carries
    // one. Composed HERE rather than stored anywhere, from the global stores, at open time - which
    // is what keeps the entry textbox stateless and therefore safe with two NPCs in talk range.
    // The console prints this same function's output, so the two surfaces cannot drift.
    // RsNpc_ComposeRuleText has already expanded the BODY under the live convention; the labels are
    // expanded by BuildScreenText, which is handed the same one.
    const std::string text =
        BuildScreenText(screen, RsNpc_ComposeRuleText(screen), RsPrefs_GetFloorConvention(), slots, visibleCount);
    // QUICKTEXT_ENABLE (control code 08). Two reasons, and the second is the load-bearing one:
    // a quest-giver's line is information, not drama, and - because the agent test loop advances a
    // textbox with a fixed-duration button injection - text that is still typing swallows the
    // press, since TEXT_STATE_CHOICE is only reached once msgMode is MSGMODE_TEXT_DONE. Instant
    // text makes the conversation deterministic to drive unattended.
    CustomMessage msg(std::string("\x08") + text);
    if (visibleCount >= 3) {
        msg.Format();
    } else {
        msg.AutoFormat();
        QuickTextEveryPage(msg); // a statement's body can paginate - a missing-steps list grows
    }
    return msg;
}

// The box geometry this message wants. Called for EVERY textbox the game opens, ours or not, so a
// taller box can never outlive the conversation that asked for it.
//
// Both pairs are written, and the live pair is the one that matters. On the normal path
// Message_GrowTextbox overwrites HEIGHT/TEXHEIGHT from the targets on frame 0, so setting the live
// pair is a harmless no-op - but an option's REPLY goes through Message_ContinueTextbox, which sets
// MSGMODE_TEXT_CONTINUING and never calls Message_GrowTextbox at all. Setting only the targets there
// would leave the reply rendering inside the four-option box's taller frame.
void RsText_ApplyBoxGeometry(bool tall) {
    R_TEXTBOX_HEIGHT_TARGET = tall ? RS_TEXTBOX_ROWS_HEIGHT : RS_TEXTBOX_VANILLA_HEIGHT;
    R_TEXTBOX_TEXHEIGHT_TARGET = tall ? RS_TEXTBOX_ROWS_TEXHEIGHT : RS_TEXTBOX_VANILLA_TEXHEIGHT;
    // Not while the open animation is mid-flight: the mid-text language switch re-enters
    // Message_OpenText from the draw path, and clobbering the live pair there would show one frame
    // of a full-size box in the middle of it growing.
    if (gPlayState == nullptr || gPlayState->msgCtx.msgMode != MSGMODE_TEXT_BOX_GROWING) {
        R_TEXTBOX_HEIGHT = tall ? RS_TEXTBOX_ROWS_HEIGHT : RS_TEXTBOX_VANILLA_HEIGHT;
        R_TEXTBOX_TEXHEIGHT = tall ? RS_TEXTBOX_ROWS_TEXHEIGHT : RS_TEXTBOX_VANILLA_TEXHEIGHT;
    }
}

CustomMessage BuildPlainMessage(const char* text) {
    CustomMessage msg(std::string("\x08") + text);
    msg.AutoFormat();
    QuickTextEveryPage(msg); // a reply or a pickup line can paginate
    return msg;
}

// The unfiltered OnOpenText bucket, which GameInteractor_ExecuteOnOpenText runs before the per-id
// and filter buckets. Everything outside our band is left alone.
void RsText_OnOpenText(uint16_t* textId, bool* loadFromMessageTable) {
    const uint16_t id = *textId;

    // FIRST, unconditionally, for every textbox in the game and before any early return: put the box
    // back to vanilla. The four-option box is the only thing that ever makes it taller, and this is
    // what guarantees the taller frame cannot outlive that one message - including onto the reply
    // box, onto the next NPC, or onto a vanilla conversation. Only a rule proven to have four
    // options turns it back on, at the bottom of this function.
    RsText_ApplyBoxGeometry(false);

    // The direct-text slot, and the reply boxes that CONTINUE to a node when they are dismissed
    // (#96 P1). Both render identically - the difference is entirely in what the actor does when
    // the box closes, which it reads back off this same id.
    if (id == RS_TEXT_DIRECT || (id >= RS_TEXT_REPLY_TO_NODE_BASE && id <= RS_TEXT_REPLY_TO_NODE_END)) {
        // Never a literal-prose fallback: an empty slot means an actor opened this id without
        // setting the text, which is a bug that must be visible rather than a blank box.
        CustomMessage msg = BuildPlainMessage(sDirectText != nullptr ? sDirectText : "<no direct text set>");
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }

    // A quest item's pickup line (#99), for BOTH pickup styles. Composed from the definition when the
    // box opens, from nothing but the id - which is the whole reason it has an id band: in the get-item
    // style this box is opened by PLAYER, from the GetItemEntry, about a second after the item actor
    // accepted and killed itself. That is too long to trust the one-slot direct pointer still holds
    // this item's line, and the held-item draw function reads the same id to find its texture.
    if (RS_TEXT_IS_ITEM(id)) {
        const int32_t questId = RS_TEXT_ITEM_GET_QUEST(id);
        const int32_t step = RS_TEXT_ITEM_GET_STEP(id);
        CustomMessage msg = BuildPlainMessage(Quest_ComposePickupText(questId, step).c_str());
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        // Which of the two lines was shown, as a marker: a screenshot shows the words, and this is
        // what proves they came from the definition rather than from the template beside it.
        char line[112];
        std::snprintf(line, sizeof(line), "rs_item quest=%d step=%d event=text source=%s", questId, step,
                      Quest_PickupSourceName(questId, step));
        AgentTest_WriteMarker(line);
        return;
    }

    int32_t npcId = 0;
    int32_t index = 0;
    const int32_t kind = RsNpc_DecodeScreen(id, &npcId, &index);
    if (kind == RS_SCREEN_NONE) {
        return;
    }
    const RsNpcDef* def = RsNpc_GetDef(npcId);

    // A diagnostic, never silence and never a plausible-looking wrong line. Init already shouted
    // into the engine log; this is the half the player and the screenshot see.
    if (def == nullptr) {
        CustomMessage msg = BuildPlainMessage(("<unregistered npc " + std::to_string(npcId) + ">").c_str());
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }
    const RsDialogueRule* screen = RsNpc_Screen(npcId, kind, index);
    if (screen == nullptr) {
        CustomMessage msg =
            BuildPlainMessage(("<npc " + std::to_string(npcId) + " has no " + RsNpc_ScreenKindName(kind) + " " +
                               std::to_string(index) + ">")
                                  .c_str());
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }

    // The options actually on offer right now (#96 P2). The renderer lays out exactly these, and
    // the actor maps the cursor row back through the same list.
    int32_t slots[RS_DIALOGUE_MAX_OPTIONS];
    const int32_t visibleCount = RsNpc_VisibleOptions(screen, slots, RS_DIALOGUE_MAX_OPTIONS);

    // The one place the box grows, and it keys off the VISIBLE count, not the declared one: a
    // four-option screen showing three needs the four-row box, and a three-option screen can never
    // need the five-row one. Registration has already proven this screen renders inside whichever
    // it gets.
    //
    // Navigating from a three-option node to a four-option one sizes correctly because
    // Message_ContinueTextbox calls Message_OpenText, so this hook fires on a continued box too,
    // and RsText_ApplyBoxGeometry writes the LIVE height pair rather than only the targets -
    // precisely because ContinueTextbox never calls Message_GrowTextbox. It snaps rather than
    // animates, which mid-conversation is arguably better. Do not regress it.
    if (visibleCount == 4) {
        RsText_ApplyBoxGeometry(true);
    }

    CustomMessage msg = BuildScreenMessage(*screen, slots, visibleCount);
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
            "RS quest item (touch or get-item)",
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
    // Expanded here, which is what makes this the door a C caller's composed line goes through: an
    // option's reply is authored prose that may name a storey, and its caller is not C++ (#94).
    // Expansion is also why the copy is not optional - the expanded string is built here and nothing
    // else owns it.
    sDirectCopy = RsFloorText_Compose(text);
    sDirectText = sDirectCopy.c_str();
}

extern "C" void RsAgent_Marker(const char* line) {
    AgentTest_WriteMarker(line);
}

extern "C" int32_t RsText_ChoiceWouldPaginate(const RsDialogueRule* screen, int32_t visibleCount) {
    // Asked at REGISTRATION, and it asks the renderer rather than guessing. A two-option body that
    // wraps to two lines pushes AutoFormatString past four lines, and its answer to that is
    // `"^&&\x1B"` - a page break BEFORE the choice. The box then renders perfectly and behaves
    // differently: the first A press turns the page instead of choosing. That is not a cosmetic
    // bug, and no marker sees it - P4 shipped exactly this once and it took a screenshot to find.
    //
    // A character cap would be a guess: the real budget is 216 PIXELS in a variable-width font, so
    // 32 characters of one string fits and 33 of another does not. Formatting the actual message
    // and looking for the '^' AutoFormatString would have inserted is the exact question.
    if (screen == nullptr || visibleCount != 2 || screen->text == nullptr) {
        return 0; // a statement paginates harmlessly; a three- or four-option box goes through
                  // Format(), which never paginates, and is covered by RsText_BodyWouldWrap instead
    }
    // This FORMATS the screen, so it reads every label. The validator only calls it after each
    // label has been proven non-NULL, and this is the second lock on that door: the definitions
    // most likely to reach a validator are the malformed ones, and a crash while deciding that a
    // definition is bad looks exactly like a hang.
    if (screen->options == nullptr || screen->optionCount < visibleCount) {
        return 0;
    }
    for (int32_t i = 0; i < screen->optionCount; i++) {
        if (screen->options[i].label == nullptr) {
            return 0;
        }
    }
    // The FIRST `visibleCount` options stand in for whichever ones gating leaves showing. That is
    // faithful, not a shortcut: every label has already been proven to fit its 184-pixel row, so
    // the choice block is exactly `visibleCount` rows whichever options are in it, and what this
    // measurement is really asking about is the BODY.
    int32_t slots[RS_DIALOGUE_MAX_OPTIONS];
    for (int32_t i = 0; i < visibleCount; i++) {
        slots[i] = i;
    }
    // EVERY convention, not the live one (#94). The measurement is against real pixel widths, and
    // "ground floor" and "first floor" are not the same width - so a body that fits beside a choice
    // under UK can paginate under US, and the player whose file says US gets a first A press that
    // turns the page instead of picking. Nothing at runtime would report it. The registration gate
    // is the only place that can see both, so it looks at both and refuses the widest.
    for (int32_t convention = 0; convention < RS_FLOOR_CONVENTION_COUNT; convention++) {
        const std::string body = RsFloorText_ExpandUnder(screen->text, convention);
        CustomMessage msg(std::string("\x08") + BuildScreenText(*screen, body, convention, slots, visibleCount));
        msg.AutoFormat();
        // Look for the BOX BREAK, not for a '^'. AutoFormatString's last act is to replace every '^'
        // it inserted with WAIT_FOR_INPUT's control byte, so the character the author writes does
        // not survive the pass that decides where the breaks go. Searching the source convention
        // instead of the compiled one is a check that always passes - which is what the first
        // version of this did.
        if (msg.GetEnglish(MF_RAW).find(CustomMessage::WAIT_FOR_INPUT()) != std::string::npos) {
            return 1;
        }
    }
    return 0;
}

// Does the BODY of a hand-laid-out choice (three or four options) need more than one row?
//
// This replaces a 24-character cap whose own comment admitted it stood in for a 216-PIXEL budget in
// a variable-width font. The cap was the last guess of its kind in the dialogue layer, and it was
// wrong in the expensive direction as well as the cheap one: it refused "What can I help you with,
// Link?" - 30 characters, comfortably inside 216px - which is the exact sentence #59 was filed to
// make possible.
//
// Asked of the renderer, like its two-option sibling: AutoFormat the body ALONE and see whether the
// formatter put a break in it. Measuring the body alone is faithful because AutoFormatString's first
// NextLineLength starts at offset 0 with the same 216px budget the real message's first row gets,
// and the body IS that row - a three- or four-way indents every row EXCEPT the body's
// (z_message_PAL.c, choiceNum indent hack), so the body has the full width.
extern "C" int32_t RsText_BodyWouldWrap(const RsDialogueRule* screen, int32_t visibleCount) {
    if (screen == nullptr || screen->text == nullptr || visibleCount < 3) {
        return 0; // only the hand-laid-out counts spend the rows this asks about
    }
    // Under every convention, for the reason RsText_ChoiceWouldPaginate gives (#94).
    for (int32_t convention = 0; convention < RS_FLOOR_CONVENTION_COUNT; convention++) {
        CustomMessage msg(std::string("\x08") + RsFloorText_ExpandUnder(screen->text, convention));
        msg.AutoFormat();
        const std::string formatted = msg.GetEnglish(MF_RAW);
        if (formatted.find(CustomMessage::NEWLINE()) != std::string::npos ||
            formatted.find(CustomMessage::WAIT_FOR_INPUT()) != std::string::npos) {
            return 1;
        }
    }
    return 0;
}

// Does an option LABEL run off the right edge of its row?
//
// Nothing measured these before, at any option count - a gap that only showed up when #59's review
// went looking. It matters more now: four options is four more chances, and a label that overflows
// does it silently, off the edge of the box, exactly like the third option P3 pushed off the bottom.
//
// The budget is 184, not 216: an option row is indented 32px (the choiceNum hack in
// Message_DrawText). Asked of the renderer's own pixel table, not of a character count - which is
// the whole point, since 24 characters of "Wwwwww" and of "iiiiii" are not the same row.
extern "C" int32_t RsText_LabelWouldOverflow(const char* label) {
    if (label == nullptr) {
        return 0; // a NULL label is CheckOption's refusal to make, and it runs first
    }
    // Under every convention (#94): "second floor" is wider than "first floor", so a label that
    // fits one way can run off the row the other way - and it does it silently, off the edge of the
    // box, which is the failure this check was added for in the first place.
    for (int32_t convention = 0; convention < RS_FLOOR_CONVENTION_COUNT; convention++) {
        if (!CustomMessage::LineFitsInPixels(RsFloorText_ExpandUnder(label, convention), 216 - 32)) {
            return 1;
        }
    }
    return 0;
}
