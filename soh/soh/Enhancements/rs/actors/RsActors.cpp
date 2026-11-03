#include "RsActors.h"
#include "RsActorParams.h"
#include "RsNpc.h"
#include "RsQuestItem.h"
#include "RsStairs.h"
#include "RsInteraction.h"
#include "RsFire.h"
#include "RsAnimProp.h"
#include "RsBoat.h"
#include "RsBalloon.h"
#include "RsItemPickup.h"

#include <array>
#include <cstdio>
#include <string>

#include "soh/ActorDB.h"
#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/dialogue/NpcIds.h"
#include "soh/Enhancements/rs/interactions/Interaction.h"
#include "soh/Enhancements/rs/items/RsItems.h"
#include "soh/Enhancements/rs/prefs/FloorText.h"
#include "soh/Enhancements/rs/prefs/RsPrefs.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestStore.h"
#include "soh/Enhancements/rs/stairs/Stairs.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "textures/parameter_static/parameter_static.h" // the ocarina staff's C-Up / C-Down icons (#290)
#include <libultraship/bridge/consolevariablebridge.h>

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

void RsStairs_Init(Actor* thisx, PlayState* play);
void RsStairs_Destroy(Actor* thisx, PlayState* play);
void RsStairs_Update(Actor* thisx, PlayState* play);

void RsInteraction_Init(Actor* thisx, PlayState* play);
void RsInteraction_Destroy(Actor* thisx, PlayState* play);
void RsInteraction_Update(Actor* thisx, PlayState* play);

void RsFire_Init(Actor* thisx, PlayState* play);
void RsFire_Destroy(Actor* thisx, PlayState* play);
void RsFire_Update(Actor* thisx, PlayState* play);
void RsFire_Draw(Actor* thisx, PlayState* play);

void RsAnimProp_Init(Actor* thisx, PlayState* play);
void RsAnimProp_Destroy(Actor* thisx, PlayState* play);
void RsAnimProp_Update(Actor* thisx, PlayState* play);
void RsAnimProp_Draw(Actor* thisx, PlayState* play);

void RsBoat_Init(Actor* thisx, PlayState* play);
void RsBoat_Destroy(Actor* thisx, PlayState* play);
void RsBoat_Update(Actor* thisx, PlayState* play);
void RsBoat_Draw(Actor* thisx, PlayState* play);

void RsBalloon_Init(Actor* thisx, PlayState* play);
void RsBalloon_Destroy(Actor* thisx, PlayState* play);
void RsBalloon_Update(Actor* thisx, PlayState* play);
void RsBalloon_Draw(Actor* thisx, PlayState* play);
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

// --- scrolling choices (sturdy-bassoon#290) ------------------------------------------------------
//
// A screen offering more than RS_DIALOGUE_WINDOW_OPTIONS shows four of them, and this says which
// four. It belongs to ONE open box - `textId` - and is put back to the top whenever a box opens,
// except when the box is this same screen re-opened by its own scroll. Text-layer state, like the
// direct-text slot above: a conversation's position is still the open box's id, and this only
// says how far down that box's list the window sits.
//
// A scroll re-lays the OPEN box in place: the engine reports the cursor pushed past an edge from its
// DRAW pass (RsText_ChoiceEdge), which only queues it, and the next frame's OnPlayDrawBegin hook
// (RsText_ApplyScroll) moves the window, re-opens and re-decodes the box on its own id, and puts the
// choice state back - before any of that frame's text is drawn. The box never leaves its choice, so
// nothing blanks (RsText_ApplyScroll says why it is not a continue, and why it is not later). The hook composes the
// shifted window, and the decoder starts the cursor on the row it should be on (RsText_ChoiceStartRow).
struct ChoiceScroll {
    uint16_t textId = 0;    // the box the rest belongs to
    int32_t total = 0;      // that screen's visible options; more than the window means it scrolls
    int32_t offset = 0;     // the visible option on the top row
    int32_t pending = 0;    // a queued scroll: -1 up, +1 down
    bool reopening = false; // the next open of `textId` is a scroll, so keep the window where it is
    bool relayout = false;  // the box opening now IS that scroll's re-layout (set in OnOpenText)
};
ChoiceScroll sScroll;

// --- choice memory: the conversation's route (sturdy-bassoon#281) ----------------------------------
//
// Within one conversation, coming BACK to a screen puts its scroll and cursor back on the option
// picked there; going down to a child again is a FRESH entry. The node graph cannot say which screen
// is a parent - cycles are legal and a node can be reached from several screens - so "parent" is
// taken from the route this conversation actually took: a STACK of the screens it walked through.
//
//   - A screen NOT on the stack is pushed: fresh, list at the top, cursor on the first row.
//   - A screen ALREADY on the stack is a return: everything above it is popped, and its scroll and
//     cursor come back - by OPTION, not by row, so gating that changed the visible list meanwhile
//     still finds it (or the nearest visible option if it is hidden now).
//
// So A -> B -> C -> A restores A and drops B and C, and picking the same option again lands on B
// fresh. A self-loop keeps its place; a node reached by a second route is fresh; a node group keys on
// the node it lands on. The one case this cannot tell apart is an author routing FORWARD to a screen
// already on the route on purpose; under this rule that is always a return.
//
// Text-layer state like sScroll, keyed by the screens' text ids (a screen's id names it - NpcDialogueDef.h),
// and nothing on an actor. Emptied whenever a CONVERSATION starts, which this hook can see for every
// speaker at once: a box opened with the message system idle (MSGMODE_NONE) starts one, while a
// continued box, a reply or a scroll re-layout opens with a box already up. The one fresh open that
// is the SAME conversation is a gift's re-open (RsNpc_Resume, GIVE_ITEM / BUY_ITEM), which asks for the
// route to be kept (RsText_KeepRouteOnNextOpen).
struct RouteStep {
    uint16_t textId; // the screen
    int32_t offset;  // its window's scroll when an option was picked there
    int32_t picked;  // the DECLARED option picked there, or -1 before any pick
};
constexpr int32_t kMaxRouteDepth = 32; // deeper than any tree here; a deeper route drops its oldest step
std::array<RouteStep, kMaxRouteDepth> sRoute;
int32_t sRouteDepth = 0;
bool sRouteKeep = false;
int32_t sStartRow = 0; // the row the cursor starts on in the box opening now (RsText_ChoiceStartRow)

// A screen opening (not a scroll's re-layout): push it, or return to it. On a return, set where the
// window sits (sScroll.offset) and where the cursor starts (sStartRow) from what was picked there.
// `slots` / `visibleCount` are the options on offer now, the same list the renderer lays out.
void RouteArrive(uint16_t id, const int32_t* slots, int32_t visibleCount) {
    int32_t at = -1;
    for (int32_t i = 0; i < sRouteDepth; i++) {
        if (sRoute[i].textId == id) {
            at = i;
            break;
        }
    }
    char line[128];
    if (at < 0) {
        if (sRouteDepth == kMaxRouteDepth) {
            for (int32_t i = 1; i < kMaxRouteDepth; i++) {
                sRoute[i - 1] = sRoute[i];
            }
            sRouteDepth--;
        }
        sRoute[sRouteDepth++] = { id, 0, -1 };
        std::snprintf(line, sizeof(line), "rs_text event=route id=0x%04X depth=%d arrive=fresh", id, sRouteDepth);
        AgentTest_WriteMarker(line);
        return;
    }
    sRouteDepth = at + 1; // a return: everything entered after it is popped
    const RouteStep& step = sRoute[at];
    int32_t visible = -1;
    if (step.picked >= 0) {
        // By option: where the picked one sits in today's list. Hidden now -> the first option after
        // it that is still offered, else the last one.
        for (int32_t i = 0; i < visibleCount && visible < 0; i++) {
            if (slots[i] >= step.picked) {
                visible = i;
            }
        }
        if (visible < 0 && visibleCount > 0) {
            visible = visibleCount - 1;
        }
    }
    int32_t offset = 0;
    int32_t row = 0;
    if (visible >= 0) {
        // The window where it was, moved only as far as needed to keep the option in it.
        const int32_t lastOffset =
            visibleCount > RS_DIALOGUE_WINDOW_OPTIONS ? visibleCount - RS_DIALOGUE_WINDOW_OPTIONS : 0;
        offset = step.offset;
        if (visible < offset) {
            offset = visible;
        } else if (visible >= offset + RS_DIALOGUE_WINDOW_OPTIONS) {
            offset = visible - (RS_DIALOGUE_WINDOW_OPTIONS - 1);
        }
        offset = offset < 0 ? 0 : (offset > lastOffset ? lastOffset : offset);
        row = visible - offset;
    }
    sScroll.offset = offset;
    sStartRow = row;
    std::snprintf(line, sizeof(line),
                  "rs_text event=route id=0x%04X depth=%d arrive=return picked=%d visible=%d offset=%d row=%d", id,
                  sRouteDepth, step.picked, visible, offset, row);
    AgentTest_WriteMarker(line);
}


constexpr size_t kOptionRowPixels = 216 - 32; // a choice row is indented 32px

// --- the scroll arrows (#290) --------------------------------------------------------------------
//
// Drawn as ICONS at a fixed column, not as text, so they do not move with the length of a label. The
// box is 256 wide from x=34 and its rows end at 65 + 216 = 281: the right gutter is 9px against the
// left's 31, too narrow for a 12px icon on its own. So the arrow sits at 274..286 - mostly in the
// gutter - and a label on a scrolling screen stops 10px short of the row's end, leaving a 3px gap.
//
// The icons are the ocarina staff's C-Up and C-Down (parameter_static, gOcarinaBtnIcon*Tex: 16x16 IA8),
// in the staff's own yellow - vanilla's look for a C button inside a textbox.
constexpr size_t kScrollArrowColumn = 10; // what a scrolling label gives up at the right of its row
constexpr int32_t kScrollArrowInset = 16; // the box's right edge to the arrow's left
constexpr int32_t kScrollArrowSize = 12;  // drawn at the cursor's size: 16 texels at the text scale

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
// 512 * 64/76 = 431. Message_GrowTextbox does this same division every frame of the open
// animation, which is why a stretched box is a known quantity rather than a gamble.
//
// 76, not more: the text starts 8px below the box top whatever the height, so every pixel over
// 8 + 5*12 + 8 lands UNDER the last row. Until 2026-10-31 the box was 80 ("68, rounded up"), which
// left 8 above the text and 12 below it, and the owner saw the bottom-heavy box.
constexpr int16_t RS_TEXTBOX_ROWS_HEIGHT = 76;      // five rows: 8 + 5*12 + 8, as vanilla's 64 is 8 + 4*12 + 8
constexpr int16_t RS_TEXTBOX_ROWS_TEXHEIGHT = 431;  // 512 * 64 / 76
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
// `offset` (#290) is the first VISIBLE option on the top row. A screen with more visible options
// than the window shows four of them, from there. Its scroll arrows are not text: the engine draws
// them over the box (RsText_DrawScrollArrows).
std::string BuildScreenText(const RsDialogueRule& screen, const std::string& body, int32_t convention,
                            const int32_t* slots, int32_t visibleCount, int32_t offset) {
    std::string text = body;
    const int32_t shown = visibleCount > RS_DIALOGUE_WINDOW_OPTIONS ? RS_DIALOGUE_WINDOW_OPTIONS : visibleCount;
    if (shown >= 2) {
        // A two-way choice sits on the last two rows of a four-row box, so its body needs one BLANK
        // row before it; a three-way fills the last three rows and a four-way (in a five-row box)
        // the last four, so neither wants the blank. Getting this wrong pushes the last option off
        // the bottom of the box, where it is still selectable and simply cannot be read - a
        // rendering bug that looks like a content bug. The two in-tree three-way messages
        // (QoL/BetterSaveMenu.cpp) use exactly this spacing.
        text += (shown == 2) ? "&&" : "&";
        text += (shown == 4)   ? CustomMessage::FOUR_WAY_CHOICE()
                : (shown == 3) ? CustomMessage::THREE_WAY_CHOICE()
                               : CustomMessage::TWO_WAY_CHOICE();
        text += "%g";
        for (int32_t i = 0; i < shown; i++) {
            if (i > 0) {
                text += "&";
            }
            text += RsFloorText_ExpandUnder(screen.options[slots[offset + i]].label, convention);
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

CustomMessage BuildScreenMessage(const RsDialogueRule& screen, const int32_t* slots, int32_t visibleCount,
                                 int32_t offset) {
    // The composed body (D26): the screen's own text, plus its missing-steps clause when it carries
    // one. Composed HERE rather than stored anywhere, from the global stores, at open time - which
    // is what keeps the entry textbox stateless and therefore safe with two NPCs in talk range.
    // The console prints this same function's output, so the two surfaces cannot drift.
    // RsNpc_ComposeRuleText has already expanded the BODY under the live convention; the labels are
    // expanded by BuildScreenText, which is handed the same one.
    const std::string text = BuildScreenText(screen, RsNpc_ComposeRuleText(screen), RsPrefs_GetFloorConvention(),
                                             slots, visibleCount, offset);
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

// Renders one SCREEN - a body plus up to sixteen options, four of them in the window - into the open textbox. Shared by the
// quest-giver's rules and nodes and by a staircase's menu (#147), which is the point of a staircase
// menu being an RsDialogueRule: one renderer, one set of layout rules, one box-geometry switch.
void LoadScreen(const RsDialogueRule& screen, bool* loadFromMessageTable) {
    // The options actually on offer right now (#96 P2). The renderer lays out exactly these, and
    // the actor maps the cursor row back through the same list.
    int32_t slots[RS_DIALOGUE_MAX_OPTIONS];
    const int32_t visibleCount = RsNpc_VisibleOptions(&screen, slots, RS_DIALOGUE_MAX_OPTIONS);

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
    //
    // A screen with more visible options than the window scrolls (#290): it is the four-row box, its
    // window sits where sScroll says - clamped, in case gating shortened the list since it scrolled -
    // and the engine is told it may scroll.
    // ...and, unless this is that scroll's own re-layout, where this conversation's route says the
    // window and cursor belong (#281).
    if (!sScroll.relayout) {
        RouteArrive(sScroll.textId, slots, visibleCount);
    }
    sScroll.total = visibleCount;
    const int32_t lastOffset = visibleCount > RS_DIALOGUE_WINDOW_OPTIONS ? visibleCount - RS_DIALOGUE_WINDOW_OPTIONS : 0;
    sScroll.offset = sScroll.offset < 0 ? 0 : (sScroll.offset > lastOffset ? lastOffset : sScroll.offset);
    if (visibleCount >= RS_DIALOGUE_WINDOW_OPTIONS) {
        RsText_ApplyBoxGeometry(true);
    }

    CustomMessage msg = BuildScreenMessage(screen, slots, visibleCount, sScroll.offset);
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

// An interaction's box (sturdy-bassoon#183). The text id says which screen; WHOSE screen is the actor
// Link is talking to, read from Player - never from msgCtx->talkActor, which on a first box is not
// yet this box's (InteractionIds.h), and never from a shared slot. The interaction id is that
// actor's params. Every box writes a marker naming the id it rendered, which is what lets a run
// assert that two props in range each spoke their own line.
void LoadInteractionText(uint16_t id, bool* loadFromMessageTable) {
    // Player only, never msgCtx.talkActor, on a continued box too: Player holds its talk actor until
    // the conversation ends (z_player.c clears it only once its own ACTOR_FLAG_TALK drops), so every
    // box of one talk finds it there. msgCtx.talkActor is a pointer nobody can trust at hook time:
    // only Message_StartOcarina clears it, so it can name a killed actor, and on a scene's first box
    // it has never been written at all - each play state is fresh, unzeroed arena memory. A planted
    // read crashed on that box (sturdy-bassoon#214).
    Actor* speaker = nullptr;
    const char* via = "none";
    if (gPlayState != nullptr) {
        Player* player = GET_PLAYER(gPlayState);
        if (player != nullptr && player->talkActor != nullptr && player->talkActor->id == ACTOR_RS_INTERACTION) {
            speaker = player->talkActor;
            via = "player";
        }
    }
    const int32_t interactionId = speaker != nullptr ? RS_INTERACTION_PARAMS_GET_ID(speaker->params) : 0;
    const RsInteractionDef* def = RsInteraction_GetDef(interactionId);

    const char* screenWord = "entry";
    int32_t index = -1;
    const RsDialogueRule* screen = nullptr;
    std::string reply;
    bool haveReply = false;
    if (id == RS_TEXT_INTERACTION_ENTRY) {
        index = RsDialogue_ResolveRule(def);
        screen = RsDialogue_Screen(def, RS_SCREEN_RULE, index);
    } else if (RS_TEXT_IS_INTERACTION_NODE(id)) {
        screenWord = "node";
        index = RS_TEXT_INTERACTION_GET_NODE(id);
        screen = RsDialogue_Screen(def, RS_SCREEN_NODE, index);
    } else if (RS_TEXT_IS_INTERACTION_REPLY(id)) {
        screenWord = "reply";
        const int32_t slot = RS_TEXT_INTERACTION_GET_SLOT(id);
        const int32_t option = RS_TEXT_INTERACTION_GET_OPTION(id);
        index = slot * RS_DIALOGUE_MAX_OPTIONS + option;
        const RsDialogueRule* on = RsInteraction_SlotScreen(def, slot);
        if (on != nullptr && option < on->optionCount && on->options[option].reply != nullptr) {
            reply = RsNpc_ComposeOptionReply(on->options[option]);
            haveReply = true;
        }
    }

    char line[160];
    if (def == nullptr && speaker != nullptr && id == RS_TEXT_INTERACTION_ENTRY) {
        // No code yet - or an id this build has no row for. The ADR's placeholder, never a refusal.
        std::snprintf(line, sizeof(line), "rs_interaction id=%d event=text screen=placeholder index=-1 via=%s",
                      interactionId, via);
        AgentTest_WriteMarker(line);
        CustomMessage msg = BuildPlainMessage(RS_INTERACTION_PLACEHOLDER_TEXT);
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }
    std::snprintf(line, sizeof(line), "rs_interaction id=%d event=text screen=%s index=%d via=%s", interactionId,
                  screenWord, index, via);
    AgentTest_WriteMarker(line);
    if (haveReply) {
        CustomMessage msg = BuildPlainMessage(reply.c_str());
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }
    if (screen == nullptr) {
        // A diagnostic, never silence (RsNpc's rule): no talking trigger, or a screen the
        // definition does not have.
        const std::string what = speaker == nullptr ? std::string("<no interaction is talking>")
                                                    : "<interaction " + std::to_string(interactionId) + " has no " +
                                                          screenWord + " " + std::to_string(index) + ">";
        CustomMessage msg = BuildPlainMessage(what.c_str());
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }
    LoadScreen(*screen, loadFromMessageTable);
}

// A staircase's menu (#147). The text id says which ROW; WHICH STAIRCASE is the placement Link is
// talking to, read from Player exactly as LoadInteractionText reads its speaker - 8,192 staircases
// cannot ride in a 16-bit text id (StairDef.h, sturdy-bassoon#214). The row is the id's, not the
// actor's, so the text id stays the one authority on which screen opened. Every box writes a marker
// naming the staircase it resolved, which is what lets a run check it against the `event=open` the
// placement wrote a moment earlier.
void LoadStairText(uint16_t id, bool* loadFromMessageTable) {
    // Player only, never msgCtx.talkActor: a staircase menu is always a first box Link opened by
    // talking, and msgCtx.talkActor cannot be trusted at hook time (LoadInteractionText says why).
    // Reading it there crashed a planted build (#214).
    Actor* speaker = nullptr;
    const char* via = "none";
    if (gPlayState != nullptr) {
        Player* player = GET_PLAYER(gPlayState);
        if (player != nullptr && player->talkActor != nullptr && player->talkActor->id == ACTOR_RS_STAIRS) {
            speaker = player->talkActor;
            via = "player";
        }
    }
    const int32_t row = RS_TEXT_STAIR_GET_ROW(id);
    const int32_t stairId = speaker != nullptr ? RS_STAIR_PARAMS_GET_ID(speaker->params) : -1;
    // The menu's order is read as the box opens (decision 22, #173 F3), and latched so the choice is
    // read back against the screen shown here. `list=`/`lead=` name it: the marker the order settings
    // are proved by.
    const int32_t order = RsStair_LatchMenuOrder();
    const RsDialogueRule* screen = speaker != nullptr ? RsStair_Screen(stairId, row, order) : nullptr;

    char line[160];
    std::snprintf(line, sizeof(line), "rs_stairs stair=%d event=text row=%d options=%d via=%s list=%s lead=%s",
                  stairId, row, screen != nullptr ? static_cast<int>(screen->optionCount) : 0, via,
                  RsStair_MenuListName(RS_STAIR_MENU_ORDER_LIST(order)),
                  RsStair_MenuLeadName(RS_STAIR_MENU_ORDER_LEAD(order)));
    AgentTest_WriteMarker(line);

    if (screen == nullptr) {
        // No staircase is talking, or a placement naming a staircase or row this build does not have.
        // Init has already said the second in the log; this is the half the player and a screenshot see.
        const std::string what =
            speaker == nullptr ? std::string("<no staircase is talking>")
                               : "<staircase " + std::to_string(stairId) + " has no row " + std::to_string(row) + ">";
        CustomMessage msg = BuildPlainMessage(what.c_str());
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }
    LoadScreen(*screen, loadFromMessageTable);
}

// The unfiltered OnOpenText bucket, which GameInteractor_ExecuteOnOpenText runs before the per-id
// and filter buckets. Everything outside our band is left alone.
//
// ONE hook for every rs/ textbox, staircase menus included (#147), and that is not tidiness: hooks
// on one event run in hash-bucket order (MODDING_HOOKS.md), and the first thing this one does is
// put the box back to vanilla size. A second OnOpenText hook that grew the box for a four-line
// staircase menu could run before this one and be undone by it.
void RsText_OnOpenText(uint16_t* textId, bool* loadFromMessageTable) {
    const uint16_t id = *textId;

    // FIRST, unconditionally, for every textbox in the game and before any early return: put the box
    // back to vanilla. The four-option box is the only thing that ever makes it taller, and this is
    // what guarantees the taller frame cannot outlive that one message - including onto the reply
    // box, onto the next NPC, or onto a vanilla conversation. Only a rule proven to have four
    // options turns it back on, at the bottom of this function.
    RsText_ApplyBoxGeometry(false);

    // The scroll window belongs to one box (#290): back to the top for every box that opens, ours or
    // not, unless this is a scrolling screen re-opened by its own scroll. `total` stays 0 unless
    // LoadScreen finds a screen that scrolls, so the engine's edge call answers no for anything else.
    sScroll.relayout = sScroll.reopening && id == sScroll.textId;
    if (!sScroll.relayout) {
        sScroll.offset = 0;
        sStartRow = 0;
        // A CONVERSATION starts here when the message system is idle (#281): the route is emptied,
        // unless this is a gift re-opening the same conversation. A quest item's pickup box opens idle
        // too, and it is not a conversation: in a gift it comes BETWEEN the conversation and its
        // re-open, and counting it as a start emptied the route the re-open had asked to keep.
        if (gPlayState != nullptr && gPlayState->msgCtx.msgMode == MSGMODE_NONE && !RS_TEXT_IS_ITEM(id)) {
            if (!sRouteKeep) {
                sRouteDepth = 0;
            }
            sRouteKeep = false;
        }
    }
    sScroll.reopening = false;
    sScroll.pending = 0;
    sScroll.total = 0;
    sScroll.textId = id;

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

    // A key item's get-item box (#286), opened by Player from the entry after whatever offered it has gone (the quest
    // item's reason for an id band).
    if (RS_TEXT_IS_KEY_ITEM(id)) {
        std::string text = "<no such item>";
        if (const RsKeyItemDef* key = RsKeyItems_Def(RS_TEXT_KEY_ITEM_GET(id))) {
            text = key->pickupText != nullptr ? key->pickupText : std::string("You got a ") + key->name + "!";
        }
        CustomMessage msg = BuildPlainMessage(text.c_str());
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }

    // A journal notice (#281): the id carries the quest and the kind, so the box is composed from the
    // definition with no slot and no speaker - either speaker opens the same id.
    if (RS_TEXT_IS_NOTICE(id)) {
        CustomMessage msg = BuildPlainMessage(
            Quest_ComposeJournalNotice(RS_TEXT_NOTICE_GET_QUEST(id), RS_TEXT_NOTICE_GET_KIND(id)).c_str());
        msg.LoadIntoFont();
        *loadFromMessageTable = false;
        return;
    }

    // A staircase's menu (#147): the id carries the row the placement stands on, the talking actor
    // which staircase (#214).
    if (RS_TEXT_IS_STAIR(id)) {
        LoadStairText(id, loadFromMessageTable);
        return;
    }

    // An interaction's screen (#183): the id names which screen, the talking actor names whose.
    if (RS_TEXT_IS_INTERACTION(id)) {
        LoadInteractionText(id, loadFromMessageTable);
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

    LoadScreen(*screen, loadFromMessageTable);
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

// --- scrolling: the update half (#290) -----------------------------------------------------------
//
// The engine found the cursor pushed past an edge during its draw pass and RsText_ChoiceEdge queued
// it. Here, at the start of the next frame's draw (OnPlayDrawBegin), the window moves and the open box
// is re-laid IN PLACE. Only while that box is still open on its choice: a queued scroll for a box that
// has since closed or moved on is dropped - including one whose box an A press on this same tick has
// already moved on, since the actors update before the draw.
//
// AT THE START OF A DRAW, NOT AFTER ONE. Decoding loads each character's glyph into the font's texture
// slots, which a frame's display list points at rather than copies. The second cut applied the scroll
// from OnGameFrameUpdate, which GameState_Update runs after the draw and before the frame is rendered:
// that frame then drew the OLD rows' layout with the NEW rows' glyphs in its slots, held for a whole
// tick by frame interpolation. Rows of the same shape ("Topic 3" -> "Topic 4") share almost every
// glyph and hid it; a long label moving through the window ghosted. Decoding before the text is
// drawn keeps every frame's layout and glyphs from the same decode - which is also where the engine's
// own mid-text language switch re-decodes.
//
// IN PLACE, NOT A CONTINUE. The first cut called Message_ContinueTextbox, the call a node takes, and
// the box blinked on every scroll: Message_OpenText zeroes the box's alpha and its draw position, and
// a continued box then sits in MSGMODE_TEXT_CONTINUING, drawing no text, until it decodes. A node can
// afford that; a scroll is pressed many times in a row. This is the engine's own mid-text language
// switch instead (Message_DrawMain, `#region SOH [NTSC]`): open and decode the same id - which runs the
// text hook, so the shifted window is what gets decoded - then restore what the box was doing. It
// stays in MSGMODE_TEXT_DONE on its four-way choice throughout, so the next draw shows the new rows
// and the cursor where it belongs.
void RsText_ApplyScroll() {
    if (sScroll.pending == 0) {
        return;
    }
    const int32_t dir = sScroll.pending;
    sScroll.pending = 0;
    if (gPlayState == nullptr || gPlayState->msgCtx.textId != sScroll.textId ||
        Message_GetState(&gPlayState->msgCtx) != TEXT_STATE_CHOICE) {
        return;
    }
    MessageContext* msgCtx = &gPlayState->msgCtx;
    const uint8_t mode = msgCtx->msgMode;
    const uint8_t endType = msgCtx->textboxEndType;
    const int16_t alpha = msgCtx->textboxColorAlphaCurrent;

    sScroll.offset += dir;
    sScroll.reopening = true;
    char line[96];
    std::snprintf(line, sizeof(line), "rs_text event=scroll id=0x%04X offset=%d total=%d dir=%d", sScroll.textId,
                  sScroll.offset, sScroll.total, dir);
    AgentTest_WriteMarker(line);

    Message_OpenText(gPlayState, sScroll.textId);
    Message_Decode(gPlayState);
    msgCtx->textDrawPos = msgCtx->decodedTextLen; // all of it, now: quick text, and no typing to wait for
    msgCtx->msgMode = mode;
    msgCtx->textboxEndType = endType;
    msgCtx->textboxColorAlphaCurrent = alpha;
    // Down leaves the cursor on the bottom row, on the option that just scrolled in; up, on the top.
    msgCtx->choiceIndex = dir > 0 ? RS_DIALOGUE_WINDOW_OPTIONS - 1 : 0;
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

        // No draw function: the shaft is the visible thing (RsStairs.h). ActorDB and Actor_DrawAll
        // both take a null draw - the actor is still projected, so it can still be targeted.
        // No ACTOR_FLAG_ATTENTION_ENABLED here: RsStairs_Update sets it each tick, only while Link is
        // on the placement's own storey (#192).
        ActorDBInit stairs = {
            "Rs_Stairs",
            "RS staircase (menu-driven storey move)",
            ACTOR_RS_STAIRS,
            ACTORCAT_PROP,
            (u32)(ACTOR_FLAG_FRIENDLY | ACTOR_FLAG_UPDATE_CULLING_DISABLED),
            OBJECT_GAMEPLAY_KEEP,
            sizeof(RsStairs),
            (ActorFunc)RsStairs_Init,
            (ActorFunc)RsStairs_Destroy,
            (ActorFunc)RsStairs_Update,
            nullptr,
            nullptr,
        };
        ActorDB::Instance->AddEntry(stairs);

        // No draw function, ON PURPOSE (#183): the prop is drawn by its map's baked list and this
        // actor only supplies the check. A null draw is what Actor_DrawAll skips outright - 0.20 us
        // a tick against 1.15 us for a draw function that draws nothing (#117 stage C). Not
        // UPDATE_CULLING_DISABLED either: an offer only matters within a step of Link.
        ActorDBInit interaction = {
            "Rs_Interaction",
            "RS interaction trigger over a baked prop",
            ACTOR_RS_INTERACTION,
            ACTORCAT_PROP,
            (u32)(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY),
            OBJECT_GAMEPLAY_KEEP,
            sizeof(RsInteraction),
            (ActorFunc)RsInteraction_Init,
            (ActorFunc)RsInteraction_Destroy,
            (ActorFunc)RsInteraction_Update,
            nullptr,
            nullptr,
        };
        ActorDB::Instance->AddEntry(interaction);

        // OoT's torch fire on an RS torch (#232). Flags 0, as En_Light: a culled fire neither updates nor
        // draws, and its flame collider registers only near Link (RsFire.h says why).
        ActorDBInit fire = {
            "Rs_Fire",
            "RS torch fire (sized, lit by stick or arrow)",
            ACTOR_RS_FIRE,
            ACTORCAT_PROP,
            0,
            OBJECT_GAMEPLAY_KEEP,
            sizeof(RsFire),
            (ActorFunc)RsFire_Init,
            (ActorFunc)RsFire_Destroy,
            (ActorFunc)RsFire_Update,
            (ActorFunc)RsFire_Draw,
            nullptr,
        };
        ActorDB::Instance->AddEntry(fire);

        // A flipbook prop (#204 C4a): one a placement, drawing its own pose lists. IGNORE_POINTLIGHTS binds the
        // scene's lights as the room's are bound, so it is lit as its baked twin is; otherwise flags 0, so a
        // copy out of view neither updates nor draws (RsAnimProp.h).
        ActorDBInit animProp = {
            "Rs_AnimProp",
            "RS flipbook prop (its own pose lists, on RS's clock)",
            ACTOR_RS_ANIMPROP,
            ACTORCAT_PROP,
            (u32)(ACTOR_FLAG_IGNORE_POINTLIGHTS),
            OBJECT_GAMEPLAY_KEEP,
            sizeof(RsAnimProp),
            (ActorFunc)RsAnimProp_Init,
            (ActorFunc)RsAnimProp_Destroy,
            (ActorFunc)RsAnimProp_Update,
            (ActorFunc)RsAnimProp_Draw,
            nullptr,
        };
        ActorDB::Instance->AddEntry(animProp);

        // The sailing prototype (sturdy-bassoon#256): a moving platform Link stands on and steers (RsBoat.h).
        // ACTORCAT_BG so it updates before Player, which is what lets it take the stick away from Link on the tick
        // A takes the helm.
        ActorDBInit boat = {
            "Rs_Boat",
            "RS boat (sailing prototype: board, take the helm, steer)",
            ACTOR_RS_BOAT,
            ACTORCAT_BG,
            // FRIENDLY: the tiller is targeted like a sign. ATTENTION_ENABLED is set by RsBoat_Update while
            // the helm is free, not here.
            (u32)(ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED | ACTOR_FLAG_FRIENDLY),
            OBJECT_GAMEPLAY_KEEP,
            sizeof(RsBoat),
            (ActorFunc)RsBoat_Init,
            (ActorFunc)RsBoat_Destroy,
            (ActorFunc)RsBoat_Update,
            (ActorFunc)RsBoat_Draw,
            nullptr,
        };
        ActorDB::Instance->AddEntry(boat);

        // The balloon prototype (sturdy-bassoon#274): a basket Link climbs into, lifts off, flies and lands
        // (RsBalloon.h). ACTORCAT_BG for the boat's reason: it takes the pad from Link before Player updates.
        ActorDBInit balloon = {
            "Rs_Balloon",
            "RS balloon (prototype: climb in, take the controls, fly)",
            ACTOR_RS_BALLOON,
            ACTORCAT_BG,
            (u32)(ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED),
            OBJECT_GAMEPLAY_KEEP,
            sizeof(RsBalloon),
            (ActorFunc)RsBalloon_Init,
            (ActorFunc)RsBalloon_Destroy,
            (ActorFunc)RsBalloon_Update,
            (ActorFunc)RsBalloon_Draw,
            nullptr,
        };
        ActorDB::Instance->AddEntry(balloon);

        // The inventory's actor (sturdy-bassoon#286): a counted or key-item pickup, or the icon over Link's head
        // when a count goes up (RsItemPickup.h). ACTORCAT_PROP, the quest item's, so it keeps updating under a
        // textbox - a float spawned by a dialogue gain bounces while the reply shows.
        ActorDBInit pickup = {
            "Rs_ItemPickup",
            "RS inventory item (counted or key; a pickup, or the float over Link's head)",
            ACTOR_RS_ITEM_PICKUP,
            ACTORCAT_PROP,
            (u32)(ACTOR_FLAG_UPDATE_CULLING_DISABLED),
            OBJECT_GAMEPLAY_KEEP,
            sizeof(RsItemPickup),
            (ActorFunc)RsItemPickup_Init,
            (ActorFunc)RsItemPickup_Destroy,
            (ActorFunc)RsItemPickup_Update,
            (ActorFunc)RsItemPickup_Draw,
            nullptr,
        };
        ActorDB::Instance->AddEntry(pickup);

        sAddedToActorDB = true;
    }

    // COND_HOOK unregisters its previous hook before registering, so a ShipInit re-run leaves
    // exactly one. COND_ID_HOOK does the same for the per-actor-id bucket, and the only other
    // ShouldActorInit registration in the tree is Anchor's, on ACTOR_PLAYER - a different bucket.
    COND_HOOK(OnOpenText, true, RsText_OnOpenText);
    COND_HOOK(OnPlayDrawBegin, true, RsText_ApplyScroll);
    COND_ID_HOOK(ShouldActorInit, ACTOR_RS_QUEST_ITEM, true, RsQuestItem_ShouldInit);
    COND_ID_HOOK(ShouldActorInit, ACTOR_RS_ITEM_PICKUP, true, RsItemPickup_ShouldInit);
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
        CustomMessage msg(std::string("\x08") + BuildScreenText(*screen, body, convention, slots, visibleCount, 0));
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
        if (!CustomMessage::LineFitsInPixels(RsFloorText_ExpandUnder(label, convention), kOptionRowPixels)) {
            return 1;
        }
    }
    return 0;
}

// The same question on a screen that can SCROLL (#290), whose top and bottom rows end in an arrow.
extern "C" int32_t RsText_ScrollLabelWouldOverflow(const char* label) {
    if (label == nullptr) {
        return 0;
    }
    // The row less the arrow's column (kScrollArrowColumn says where the arrow sits). Under every
    // convention for the reason RsText_LabelWouldOverflow gives.
    for (int32_t convention = 0; convention < RS_FLOOR_CONVENTION_COUNT; convention++) {
        if (!CustomMessage::LineFitsInPixels(RsFloorText_ExpandUnder(label, convention),
                                             kOptionRowPixels - kScrollArrowColumn)) {
            return 1;
        }
    }
    return 0;
}

// The open box is the one sScroll describes. Read from the engine's own msgCtx, so a call made while
// some other box is open - a vanilla NPC's, a sign's - is never answered about ours.
static bool ScrollIsForOpenBox() {
    return gPlayState != nullptr && gPlayState->msgCtx.textId == sScroll.textId;
}

// --- scrolling: the engine's three questions (#290) ---------------------------------------------

extern "C" int32_t RsText_ChoiceEdge(int32_t dir) {
    if (!ScrollIsForOpenBox() || sScroll.total <= RS_DIALOGUE_WINDOW_OPTIONS) {
        return 0;
    }
    if (sScroll.pending != 0) {
        // One already queued for this frame's update. Yes only if it goes the same way, so a push the
        // other way is clamped silently rather than sounding for a scroll that will not happen.
        return sScroll.pending == dir ? 1 : 0;
    }
    if (dir > 0 && sScroll.offset + RS_DIALOGUE_WINDOW_OPTIONS < sScroll.total) {
        sScroll.pending = 1;
        return 1;
    }
    if (dir < 0 && sScroll.offset > 0) {
        sScroll.pending = -1;
        return 1;
    }
    return 0; // the top or bottom of the whole list: vanilla's clamp, no wrap
}

// The arrows, over the open box (#290): an up arrow beside the top row while options are hidden above,
// a down arrow beside the bottom row while options are hidden below. Called by the engine while a
// four-way choice is up (Message_DrawMain, after the cursor), so they share the box's frame and its
// letterbox clamp. Draw runs once a tick, so the animation steps at 20Hz, as the cursor's colour does.
extern "C" void RsText_DrawScrollArrows(PlayState* play, Gfx** p) {
    if (!ScrollIsForOpenBox() || sScroll.total <= RS_DIALOGUE_WINDOW_OPTIONS) {
        return;
    }
    const bool up = sScroll.offset > 0;
    const bool down = sScroll.offset + RS_DIALOGUE_WINDOW_OPTIONS < sScroll.total;
    if (!up && !down) {
        return;
    }
    // FADE (the default) is a linear dip to about 40% alpha and back over a second and a half; BOUNCE
    // a linear up-and-down of half the arrow's height (+-3px, in quarter pixels, a texture
    // rectangle's unit) over a second; NONE holds still.
    int32_t bounce = 0;
    uint8_t alpha = 255;
    const int32_t tick = static_cast<int32_t>(play->gameplayFrames);
    switch (CVarGetInteger(CVAR_RS_SCROLL_ARROW_ANIM, RS_SCROLL_ARROW_FADE)) {
        case RS_SCROLL_ARROW_BOUNCE: {
            const int32_t phase = tick % 20;
            const int32_t tri = phase < 10 ? phase : 20 - phase; // 0..10..0
            bounce = tri * 24 / 10 - 12;                         // -12..12 quarter pixels
            break;
        }
        case RS_SCROLL_ARROW_NONE:
            break;
        default: { // RS_SCROLL_ARROW_FADE, the owner's pick (2026-11-01)
            const int32_t phase = tick % 30;
            const int32_t tri = phase < 15 ? phase : 30 - phase; // 0..15..0
            alpha = static_cast<uint8_t>(255 - tri * 150 / 15);
            break;
        }
    }

    Gfx* gfx = *p;
    gDPPipeSync(gfx++);
    gDPSetCombineLERP(gfx++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetPrimColor(gfx++, 0, 0, 255, 255, 50, alpha); // the ocarina staff's C-button yellow
    gDPSetEnvColor(gfx++, 10, 10, 10, 0);
    const int32_t x = R_TEXTBOX_X + R_TEXTBOX_WIDTH - kScrollArrowInset;
    // 16 texels over 12 pixels, in the 5.10 step a texture rectangle takes.
    const int32_t step = (16 << 10) / kScrollArrowSize;
    for (int32_t i = 0; i < 2; i++) {
        const bool isUp = i == 0;
        if (isUp ? !up : !down) {
            continue;
        }
        const int32_t row = isUp ? 0 : RS_DIALOGUE_WINDOW_OPTIONS - 1;
        const int32_t y = (R_TEXT_CHOICE_YPOS(row) << 2) + (isUp ? -bounce : bounce);
        gDPLoadTextureBlock(gfx++, isUp ? gOcarinaBtnIconCUpTex : gOcarinaBtnIconCDownTex, G_IM_FMT_IA, G_IM_SIZ_8b,
                            16, 16, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
        gSPTextureRectangle(gfx++, x << 2, y, (x + kScrollArrowSize) << 2, y + (kScrollArrowSize << 2),
                            G_TX_RENDERTILE, 0, 0, step, step);
    }
    *p = gfx;
}

extern "C" int32_t RsText_ChoiceScroll(void) {
    return ScrollIsForOpenBox() ? sScroll.offset : 0;
}

extern "C" int32_t RsText_ChoiceStartRow(void) {
    return ScrollIsForOpenBox() ? sStartRow : 0;
}

extern "C" void RsText_RememberChoice(int32_t declared) {
    // The screen picked on is the open box, which is the top of the route.
    if (sRouteDepth > 0 && ScrollIsForOpenBox() && sRoute[sRouteDepth - 1].textId == sScroll.textId) {
        sRoute[sRouteDepth - 1].picked = declared;
        sRoute[sRouteDepth - 1].offset = sScroll.offset;
    }
}

extern "C" void RsText_KeepRouteOnNextOpen(int32_t keep) {
    sRouteKeep = keep != 0;
}
