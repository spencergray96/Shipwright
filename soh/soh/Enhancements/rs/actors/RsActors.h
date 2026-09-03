#ifndef SOH_RS_ACTORS_H
#define SOH_RS_ACTORS_H

#include <stdint.h>

// The C-callable surface RsActors.cpp gives the two C actors: message rendering and the agent-loop
// marker. Kept small on purpose - everything else an actor needs is already extern "C" on
// Quest.h / NpcDialogue.h.

#ifdef __cplusplus
extern "C" {
#endif

// Hands one string to the NEXT textbox opened with RS_TEXT_DIRECT (an option's reply, an item
// pickup). A one-slot pointer, and safe as one because it is a PARAMETER, not shared state: it is
// set on the line above the Message_StartTextbox / Message_ContinueTextbox call, with nothing
// running in between. The ENTRY textbox does not use it at all - its text id carries the npc and
// the rule (NpcDialogueDef.h), which is what makes two NPCs in talk range at once safe.
//
// `text` must outlive the textbox, so it is always a definition string, never a local buffer.
void RsText_SetDirect(const char* text);

// Writes one agent-loop marker, or nothing at all outside agent mode. This is what makes an
// in-game conversation ASSERTABLE - a screenshot shows a textbox, a marker names the rule that
// produced it and what picking an option actually returned.
void RsAgent_Marker(const char* line);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_ACTORS_H
