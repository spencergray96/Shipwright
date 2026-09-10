#ifndef SOH_RS_ACTORS_H
#define SOH_RS_ACTORS_H

#include <stdint.h>

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

// Hands one string to the NEXT textbox opened with RS_TEXT_DIRECT (an option's reply, an item
// pickup). A one-slot pointer, and safe as one because it is a PARAMETER, not shared state: it is
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
// naming one - a quest item saying what it was, built from the quest's step label - because the
// actor holding the buffer is killed while its own textbox is still on screen and its instance
// memory goes straight back to the arena. The expansion is `{floor:N}` against the save file's
// floor convention (sturdy-bassoon#94): the moment a string can carry a token it STOPS being a
// definition string and becomes a composed one, so an option's reply moved here from
// RsText_SetDirect. Getting that wrong presents as a dangling pointer, not as a text bug.
//
// No length cap: the storage is a std::string. It used to truncate at 128 bytes, which stopped
// being defensible when a 9-byte token started expanding to a 12-byte label.
void RsText_SetDirectCopy(const char* text);

// Writes one agent-loop marker, or nothing at all outside agent mode. This is what makes an
// in-game conversation ASSERTABLE - a screenshot shows a textbox, a marker names the rule that
// produced it and what picking an option actually returned.
void RsAgent_Marker(const char* line);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_ACTORS_H
