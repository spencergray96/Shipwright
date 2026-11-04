#ifndef SOH_RS_ACTORS_H
#define SOH_RS_ACTORS_H

#include <stdint.h>
#include "soh/cvar_prefixes.h"

// The C-callable surface RsActors.cpp gives the two C actors: message rendering and the agent-loop
// marker. Kept small on purpose - everything else an actor needs is already extern "C" on
// Quest.h / NpcDialogue.h.

struct RsDialogueRule;

#ifdef __cplusplus
extern "C" {
#endif

// 1 when this screen's textbox would put its CHOICE on a second page at `visibleCount` options. The
// dialogue validator calls it at registration and refuses the definition, because a paginated
// choice changes what a conversation does - the first A press turns the page instead of picking an
// option - while looking completely correct. It lives here, with the renderer, so the check is the
// renderer's own formatting run rather than a character count guessing at a 216-pixel budget.
//
// `visibleCount` is a parameter rather than `screen->optionCount` because gating (sturdy-bassoon#96
// P2) makes the rendered count a RUNTIME fact: a four-option screen with two gated options presents
// 2, 3 or 4, and 2 is the only one of those that goes through the AutoFormat path this measures.
// Registration sweeps every count the screen can reach.
int32_t RsText_ChoiceWouldPaginate(const struct RsDialogueRule* screen, int32_t visibleCount);

// 1 when the BODY of a hand-laid-out choice (three or four options) needs a second row, which the
// hand layout has no room for - the option rows are already spoken for. Replaced a 24-character cap
// that stood in for a 216-pixel budget; same reason as above, same technique.
//
// It measures the body ALONE, so `visibleCount` only selects whether the question is asked at all -
// but it is taken as a parameter for the same reason its sibling does, so a caller sweeping counts
// reads the same at both call sites.
int32_t RsText_BodyWouldWrap(const struct RsDialogueRule* screen, int32_t visibleCount);

// 1 when an option label is wider than its row. An option row is indented 32px, so the budget is
// 184, not the 216 a full-width row gets - a difference no character count can express.
int32_t RsText_LabelWouldOverflow(const char* label);

// The same, for a screen that can SCROLL (sturdy-bassoon#290): its top and bottom rows end in an
// arrow, so a label there shares the 184 with a space and the arrow glyph.
int32_t RsText_ScrollLabelWouldOverflow(const char* label);

// --- scrolling choices (sturdy-bassoon#290) ----------------------------------------------------
//
// A screen offering more than RS_DIALOGUE_WINDOW_OPTIONS shows four at a time. The OFFSET - which
// visible option sits on the top row - is text-layer state for the open box, like the direct-text
// slot: set to 0 whenever a screen opens, and kept only across a scroll's own re-open of that same
// screen. Nothing about it lives on an actor.

// Called by the ENGINE (Message_HandleChoiceSelection, z_message_PAL.c) when the cursor is pushed
// past the top (dir -1) or bottom (dir +1) row. 1 when the open box is a scrolling screen with
// options hidden that way: the scroll is queued, and the engine plays the cursor sound instead of
// clamping silently. 0 for every other box in the game, which keeps vanilla's clamp. Runs in the
// draw pass, so it only queues; the box is re-laid at the start of the next frame's draw.
int32_t RsText_ChoiceEdge(int32_t dir);
// The scroll arrows' animation, a developer setting: `npc arrows fade|bounce|none` sets it (fade is
// the default). Read
// every draw by RsText_DrawScrollArrows, which the engine calls with the box's PlayState and display
// list (declared there, in z_message_PAL.c, because this header names no engine types).
// HOW LONG A TALK WAITS FOR ITS FIRST BOX (sturdy-bassoon#286's harness found it). Actor_ProcessTalkRequest says
// the talk has started, but the box can open a few frames later: Link mid-swing finishes the swing first. A talk
// actor that read "no box" in those frames as "closed from outside" went back to waiting, the box then opened
// with nobody listening, and the player's pick did nothing. So a talk's first box gets this many ticks (2 s) to
// appear; after that box has been seen, "no box" means closed, as it always did. RsNpc, RsInteraction, RsStairs.
#define RS_TALK_OPEN_WAIT 40

#define CVAR_RS_SCROLL_ARROW_ANIM CVAR_DEVELOPER_TOOLS("RsScrollArrowAnim")
#define RS_SCROLL_ARROW_BOUNCE 0
#define RS_SCROLL_ARROW_FADE 1
#define RS_SCROLL_ARROW_NONE 2
// The open box's scroll offset: what an actor adds to msgCtx.choiceIndex to get the index into the
// screen's VISIBLE options. 0 for any box that is not scrolled.
int32_t RsText_ChoiceScroll(void);

// --- choice memory (sturdy-bassoon#281) ----------------------------------------------------------
//
// Coming back to a screen within one conversation restores its scroll and cursor; going down to a
// child again is fresh. RsActors.cpp's route stack says how "back" is told from "down".

// Called by the ENGINE when a two-, three- or four-choice box finishes decoding: the row the cursor
// starts on. 0 for every box that is not one of ours, and for ours unless the route restored a pick.
int32_t RsText_ChoiceStartRow(void);
// An actor picked `declared` on the open screen: remembered with the window's scroll, for a return.
void RsText_RememberChoice(int32_t declared);
// The next box that starts a conversation continues this one instead (a gift's re-open), so the
// route is kept. 0 withdraws the request (the re-open was given up).
void RsText_KeepRouteOnNextOpen(int32_t keep);

// Hands one string to the NEXT textbox opened with RS_TEXT_DIRECT (an option's reply, or the notice
// from an item whose params this build cannot read). A one-slot pointer, and safe as one because it
// is a PARAMETER, not shared state: it is
// set on the line above the Message_StartTextbox / Message_ContinueTextbox call, with nothing
// running in between. The ENTRY textbox does not use it at all - its text id carries the npc and
// the rule (NpcDialogueDef.h), which is what makes two NPCs in talk range at once safe.
//
// `text` must outlive the textbox, so it is always a definition string, never a local buffer.
// That is not belt and braces: Message_OpenText is re-entered from the DRAW path on a mid-text
// language switch, so the pointer is dereferenced again long after the call that set it.
//
// It also does NOT expand `{floor:N}` (sturdy-bassoon#94), so it is only for a string that cannot
// carry one - in practice a fixed diagnostic. Authored prose goes through RsText_SetDirectCopy.
void RsText_SetDirect(const char* text);

// The same slot, but the string is EXPANDED and COPIED into storage that outlives every actor.
//
// Two jobs, and they are the same job. The copy is for a caller that composes its line rather than
// naming one, because the buffer it built the line in does not outlive the textbox. The expansion is
// `{floor:N}` against the save file's
// floor convention (sturdy-bassoon#94): the moment a string can carry a token it STOPS being a
// definition string and becomes a composed one, so an option's reply moved here from
// RsText_SetDirect. Getting that wrong presents as a dangling pointer, not as a text bug.
//
// No length cap: the storage is a std::string. It used to truncate at 128 bytes, which stopped
// being defensible when a 9-byte token started expanding to a 12-byte label.
void RsText_SetDirectCopy(const char* text);

// Writes one agent-loop marker; outside agent mode the line goes to the engine log only
// (sturdy-bassoon#97). This is what makes an in-game conversation ASSERTABLE - a screenshot shows a
// textbox, a marker names the rule that produced it and what picking an option actually returned.
void RsAgent_Marker(const char* line);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_ACTORS_H
