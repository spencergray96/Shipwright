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

// 1 when this rule's textbox would put its CHOICE on a second page. The dialogue validator calls it
// at registration and refuses the definition, because a paginated choice changes what a
// conversation does - the first A press turns the page instead of picking an option - while looking
// completely correct. It lives here, with the renderer, so the check is the renderer's own
// formatting run rather than a character count guessing at a 216-pixel budget.
int32_t RsText_ChoiceWouldPaginate(const struct RsDialogueRule* rule);

// Hands one string to the NEXT textbox opened with RS_TEXT_DIRECT (an option's reply, an item
// pickup). A one-slot pointer, and safe as one because it is a PARAMETER, not shared state: it is
// set on the line above the Message_StartTextbox / Message_ContinueTextbox call, with nothing
// running in between. The ENTRY textbox does not use it at all - its text id carries the npc and
// the rule (NpcDialogueDef.h), which is what makes two NPCs in talk range at once safe.
//
// `text` must outlive the textbox, so it is always a definition string, never a local buffer.
// That is not belt and braces: Message_OpenText is re-entered from the DRAW path on a mid-text
// language switch, so the pointer is dereferenced again long after the call that set it.
void RsText_SetDirect(const char* text);

// The same slot, but the string is COPIED into storage that outlives every actor. For the one
// caller that has to compose its line rather than name one - a quest item saying what it was, built
// from the quest's step label - because the actor holding the buffer is killed while its own
// textbox is still on screen, and its instance memory goes straight back to the arena.
// Truncates silently at 128 bytes; a pickup line that long is a content bug, not a runtime one.
void RsText_SetDirectCopy(const char* text);

// Writes one agent-loop marker, or nothing at all outside agent mode. This is what makes an
// in-game conversation ASSERTABLE - a screenshot shows a textbox, a marker names the rule that
// produced it and what picking an option actually returned.
void RsAgent_Marker(const char* line);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_ACTORS_H
