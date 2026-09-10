#ifndef SOH_RS_NPC_DIALOGUE_H
#define SOH_RS_NPC_DIALOGUE_H

#include <stddef.h>
#include <stdint.h>
#include "NpcIds.h"
#include "NpcDialogueDef.h"

// The NPC dialogue registry (sturdy-bassoon#58 P3 / #64): the static table of RsNpcDefs and the
// first-match-wins resolution over one NPC's rules. C++ core behind an extern "C" shim (D5), the
// pattern Quest.h and WorldFlags.h already use, so the C actors call these directly.
//
// Everything here is a READ. A rule table never writes; picking an option is what writes, and that
// happens in the actor through the checked Quest_* API. Resolution is therefore safe to call from
// a console command, from a draw path, or every frame from an actor's idle state - and it is,
// because the ONLY place presentation lives is the global stores (D22): an NPC rebuilds what it
// says from them on every read, so nothing about a conversation is allowed to sit in the actor
// struct across a scene transition, where it would silently die.

typedef enum RsNpcResult {
    RS_NPC_OK = 0,
    RS_NPC_ERR_INVALID_ID = 1,
    RS_NPC_ERR_NOT_REGISTERED = 2,
    RS_NPC_ERR_BAD_DEF = 3,
    RS_NPC_ERR_DUPLICATE = 4,
    RS_NPC_RESULT_COUNT,
} RsNpcResult;

#ifdef __cplusplus
extern "C" {
#endif

// Validates and registers a definition. Idempotent for the SAME pointer (ShipInit "*" functions
// re-run on preset apply and config load); a DIFFERENT definition for an already-owned id is
// RS_NPC_ERR_DUPLICATE. See RsNpc_DefProblem for what validation refuses.
int32_t RsNpc_Register(const RsNpcDef* def);

// The same validation with NO log and NO assert: 0 when the definition is clean, 1 otherwise with
// the reason written into `buf` (always NUL-terminated for len > 0). This is Quest_DefProblem's
// rule applied to a second registry - it lets a console probe prove the gate refuses a bad
// definition without tripping the Debug assert that would hang the agent loop. The message reports
// the kind and the index of the problem and NEVER echoes the offending string, so a '%' or a '"'
// in a bad definition cannot reach a console sink through its own error message.
int32_t RsNpc_DefProblem(const RsNpcDef* def, char* buf, size_t len);

const RsNpcDef* RsNpc_GetDef(int32_t npcId); // NULL if unregistered or invalid; never asserts
int32_t RsNpc_IsRegistered(int32_t npcId);
int32_t RsNpc_RegisteredCount(void);

// D11: the index of the FIRST rule whose every `when` predicate is true. Returns -1 for an invalid
// or unregistered id, and -1 if somehow nothing matched - which registration makes unreachable by
// requiring the last rule to be unconditional. Never asserts and never writes.
int32_t RsNpc_ResolveRule(int32_t npcId);

// 1 when every predicate in rule `ruleIndex` is true. Out-of-range answers 0 quietly.
int32_t RsNpc_RuleMatches(int32_t npcId, int32_t ruleIndex);

// --- screens and navigation (sturdy-bassoon#96) -------------------------------------------------
//
// A SCREEN is a body plus up to four options. A rule and a node are the same struct; what differs
// is how you arrive at one - a rule by MATCHING, a node by FOLLOWING an option's `next`. These
// three calls are what let the renderer, the actor and the console all speak about "the screen
// that is open" without any of them knowing which kind it is.

// Which screen a text id names. Returns the RsScreenKind and writes the npc id and the rule/node
// index; RS_SCREEN_NONE for an id outside our bands (and then the out params are untouched).
// A pure decode: it does NOT check that the npc is registered or that the index exists.
int32_t RsNpc_DecodeScreen(uint16_t textId, int32_t* npcId, int32_t* index);

// The screen itself, or NULL for an unregistered npc, an unknown kind, or an index the definition
// does not have. Never asserts - this is read while a textbox is opening and from a console.
const RsDialogueRule* RsNpc_Screen(int32_t npcId, int32_t kind, int32_t index);

// 1 when every predicate in the option's `when` list is true, so the option is OFFERED right now.
// An ungated option (whenCount 0) is always 1. A NULL option answers 0.
int32_t RsNpc_OptionVisible(const RsDialogueOption* option);

// The DECLARED indices of the options a screen is offering right now, in order, written into `out`
// (which must hold at least RS_DIALOGUE_MAX_OPTIONS). Returns how many.
//
// This is the ONE mapping between what the player sees and what the definition says, and every
// surface goes through it: the renderer lays out exactly these labels, `msgCtx.choiceIndex` is an
// index INTO THIS LIST rather than into the definition, and the actor maps it back here before it
// runs an action. Two places computing it independently is how a gated menu picks the wrong option.
int32_t RsNpc_VisibleOptions(const RsDialogueRule* screen, int32_t* out, int32_t max);

// How many of a screen's options are UNGATED, i.e. the smallest visible count it can ever present.
// Registration requires 2 on any screen with options; see NpcDialogue.cpp for why that is the
// invariant rather than "no gate combination yields exactly one".
int32_t RsNpc_UngatedOptionCount(const RsDialogueRule* screen);

const char* RsNpc_ResultName(int32_t result);
const char* RsNpc_ActionName(int32_t kind); // "none", "start_quest", "complete_quest", "set_world_flag"
// "rule" or "node" for an RsScreenKind. One spelling, because it reaches three sinks that must
// agree: a registration refusal names the array an author has to go and look in, `npc tree` keys
// its `screen[…]`/`edge[…]` lines on it, and the actor's choice marker reports which kind of screen
// the pick came from.
const char* RsNpc_ScreenKindName(int32_t kind);

// 1 when node `nodeIndex` is reachable from some entry rule by following `next` edges. Registration
// refuses a definition with an unreachable node, so this is 1 for every node of a REGISTERED def -
// which is the point: `npc tree` prints it, so the graph walk is asserted from a console rather
// than inferred from whether a screen ever showed up in play.
int32_t RsNpc_NodeReachable(int32_t npcId, int32_t nodeIndex);

// One line, no newline, always NUL-terminated for len > 0:
//   id=192 name=debug_giver tier=debug rules=5 rule=3 options=2 display="Debug: the giver" visible=2 nodes=0
// An unregistered id still renders: id=1 name=- registered=0 rules=0 rule=-1
// Fields added by a later phase are APPENDED, never inserted, so earlier acceptance regexes hold.
void RsNpc_Describe(int32_t npcId, char* buf, size_t len);

// Runs an option's action through the CHECKED quest API. Returns the QuestResult
// (Quest.h) for the quest actions, or QUEST_OK for the ones that cannot fail. Never asserts.
int32_t RsNpc_RunAction(const RsDialogueOption* option);

#ifdef __cplusplus
}

#include <string>

// The rule body as a PLAYER sees it: `rule->text`, plus the missing-steps clause when the rule
// carries one (D26 - NpcDialogueDef.h says why the model grew for it). One implementation, three
// sinks: the textbox renderer (RsActors.cpp), `npc dump` and `npc resolve`. That is D18's rule
// applied to composition - if the console printed the raw text and the box showed the composed one,
// the console would be validating something the player never sees.
//
// C++ only, and deliberately returning std::string rather than filling a caller's buffer. A body
// plus up to QUEST_STEP_MAX labels has no useful fixed bound, and a truncating snprintf would cut a
// sentence in half and render plausibly - the failure mode this project treats as the enemy. No C
// actor needs this: the entry textbox is built by the C++ OnOpenText hook, and the one string a C
// actor hands over directly (an item's pickup line) is composed there from Quest_StepLabel.
//
// Reads the live stores, so two calls a frame apart can legitimately differ - that IS the point.
//
// It also EXPANDS `{floor:N}` against the save file's floor convention (sturdy-bassoon#94,
// prefs/FloorText.h), over the whole composed body - so the clause's step labels are expanded too.
// Composition, not storage: switching save slots changes what this returns with nothing telling it
// to, which is the same property that makes two NPCs in talk range safe.
std::string RsNpc_ComposeRuleText(const RsDialogueRule& rule);

// The same expansion for the two strings a rule's OPTIONS carry. They exist so the console prints
// what the player reads (D18) rather than the raw definition string: the textbox renderer expands
// labels itself while laying the choice out, and a reply is expanded by RsText_SetDirectCopy on
// the way to the box. "" for a NULL reply, which is how a rule says "close without one".
std::string RsNpc_ComposeOptionLabel(const RsDialogueOption& option);
std::string RsNpc_ComposeOptionReply(const RsDialogueOption& option);

// Just the list part - "an egg and a pot of flour", or "" when nothing is missing or the rule
// carries no clause. What `npc dump` prints as `missing="…"`, so a run can assert the composition
// without matching the whole body.
std::string RsNpc_MissingList(const RsDialogueRule& rule);
#endif

#endif // SOH_RS_NPC_DIALOGUE_H
