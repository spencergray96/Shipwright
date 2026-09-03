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

const char* RsNpc_ResultName(int32_t result);
const char* RsNpc_ActionName(int32_t kind); // "none", "start_quest", "complete_quest", "set_world_flag"

// One line, no newline, always NUL-terminated for len > 0:
//   id=192 name=debug_giver tier=debug rules=5 rule=3 options=2 display="Debug: the giver"
// An unregistered id still renders: id=1 name=- registered=0 rules=0 rule=-1
void RsNpc_Describe(int32_t npcId, char* buf, size_t len);

// Runs an option's action through the CHECKED quest API. Returns the QuestResult
// (Quest.h) for the quest actions, or QUEST_OK for the ones that cannot fail. Never asserts.
int32_t RsNpc_RunAction(const RsDialogueOption* option);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_NPC_DIALOGUE_H
