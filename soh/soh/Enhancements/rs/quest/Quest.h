#ifndef SOH_RS_QUEST_H
#define SOH_RS_QUEST_H

#include <stddef.h>
#include <stdint.h>
#include "QuestIds.h"
#include "QuestDef.h"
#include "QuestStore.h"

// The definition-aware quest API (sturdy-bassoon#58 P1): the static registry of QuestDefs and the
// rules for moving a quest through NOT_STARTED -> IN_PROGRESS -> COMPLETE. Sits on top of the raw
// QuestStore_* accessors, which stay rule-free. C++ core behind an extern "C" shim (D5), the
// pattern WorldFlags.h and QuestStore.h already use, so C actors call these directly.
//
// Loudness policy, in two classes:
//   BUG class    - invalid id, unregistered id, invalid step, ORDER VIOLATION (D2), bad definition,
//                  duplicate registration: SPDLOG_ERROR + debug assert + the write is refused.
//   OUTCOME class - prerequisites unmet, wrong status, steps incomplete, already complete: a return
//                  code only. These are normal gameplay answers ("not yet", "already done"), and
//                  QUEST_ALREADY_COMPLETE is precisely the D12 re-entrant no-op that makes rewards
//                  idempotent across a save/load.
// The Quest_Check* calls answer "would this write be accepted?" WITHOUT writing or asserting, so a
// console surface can prove a refusal on a Debug build without tripping the assert (the P0 probe
// rule). Every write is Check-then-store, so the two can never disagree.

typedef enum QuestResult {
    QUEST_OK = 0,
    QUEST_ALREADY_COMPLETE = 1, // outcome: D12 no-op, nothing dispatched
    QUEST_ERR_PREREQ_UNMET = 2, // outcome
    QUEST_ERR_WRONG_STATUS = 3, // outcome: e.g. start when already started, step on a COMPLETE quest
    QUEST_ERR_STEPS_INCOMPLETE = 4, // outcome: Quest_Complete before every step is set
    QUEST_ERR_ORDER_VIOLATION = 5,  // bug: out-of-order step on an `ordered` quest (D2)
    QUEST_ERR_INVALID_ID = 6,       // bug
    QUEST_ERR_NOT_REGISTERED = 7,   // bug: id has no definition in this build
    QUEST_ERR_INVALID_STEP = 8,     // bug: step >= stepCount (or outside [0, QUEST_STEP_MAX))
    QUEST_ERR_BAD_DEF = 9,          // bug: definition failed validation at registration
    QUEST_ERR_DUPLICATE = 10,       // bug: a different definition already owns this id
    QUEST_RESULT_COUNT,
} QuestResult;

#ifdef __cplusplus
extern "C" {
#endif

// --- registry ---------------------------------------------------------------------------------

// Validates and registers a definition. Idempotent for the SAME pointer (ShipInit "*" functions
// re-run on preset apply and config drop); a DIFFERENT definition for an already-owned id is
// QUEST_ERR_DUPLICATE. Validation refuses (QUEST_ERR_BAD_DEF): an invalid id; a tier that does not
// match the id's band; a NULL name/title or one containing a space or '%'; stepCount outside
// [1, QUEST_STEP_MAX]; a NULL list with a nonzero count; an unknown predicate or reward kind; a
// rupee amount outside s16; a world-flag reward outside the store or in the other tier's band
// (a debug quest must not set a flag debugwipe cannot clear).
int32_t Quest_Register(const QuestDef* def);
const QuestDef* Quest_GetDef(int32_t questId); // NULL if unregistered or invalid (no assert)
int32_t Quest_IsRegistered(int32_t questId);   // 0 for an invalid id (no assert)
int32_t Quest_RegisteredCount(void);

// --- read half (never writes) -----------------------------------------------------------------

// Thin over QuestStore, but through the registry, so an unregistered id shouts here rather than
// silently reading a slot no definition owns. Return the store's zero values on a bad id.
int32_t Quest_GetStatus(int32_t questId);
uint32_t Quest_GetStepMask(int32_t questId);
int32_t Quest_IsStepSet(int32_t questId, int32_t step);
int32_t Quest_AllStepsSet(int32_t questId);

// D8: every `requirements` predicate true AND (`prereqFn` NULL or nonzero). 0 on a bad id.
int32_t Quest_PrereqsMet(int32_t questId);
// What a quest-giver asks (P3): NOT_STARTED and prerequisites met.
int32_t Quest_IsAvailable(int32_t questId);

// The QuestResult the corresponding write WOULD return. No write, no assert.
int32_t Quest_CheckStart(int32_t questId);
int32_t Quest_CheckSetStep(int32_t questId, int32_t step);
int32_t Quest_CheckClearStep(int32_t questId, int32_t step);
int32_t Quest_CheckComplete(int32_t questId);

// --- write half -------------------------------------------------------------------------------

// NOT_STARTED -> IN_PROGRESS if prerequisites are met.
int32_t Quest_Start(int32_t questId);
// Status is not gated (an item may be collected before the quest starts, as in RS); a COMPLETE
// quest refuses with QUEST_ERR_WRONG_STATUS because its steps are frozen. On an `ordered` quest an
// out-of-order set/clear is QUEST_ERR_ORDER_VIOLATION: logged, debug-asserted, refused (D2).
int32_t Quest_SetStep(int32_t questId, int32_t step);
int32_t Quest_ClearStep(int32_t questId, int32_t step);
// IN_PROGRESS with every step set -> COMPLETE, then rewards, then onComplete (D12 order).
int32_t Quest_Complete(int32_t questId);
// Sets every step and completes from any status. Same completion path, same idempotency: a second
// call is QUEST_ALREADY_COMPLETE and dispatches nothing.
int32_t Quest_ForceComplete(int32_t questId);
// Back to NOT_STARTED with no steps. Console tooling; nothing in gameplay should call it.
int32_t Quest_Reset(int32_t questId);
// D7: zero every quest entry in the debug band (registered or not - a stale slot counts) and clear
// every world flag in the debug band. Never touches a production entry. Out-params may be NULL.
void Quest_DebugWipe(int32_t* questsWiped, int32_t* flagsCleared);

// --- rendering (the one read API both console surfaces print - D18) --------------------------

const char* Quest_ResultName(int32_t result);   // "ok", "order_violation", ...
const char* Quest_StatusName(int32_t status);   // "not_started", "in_progress", "complete"
const char* Quest_TierName(int32_t tier);       // "prod", "debug"
const char* Quest_RewardKindName(int32_t kind); // "world_flag", "rupees"

// One line, no newline, always NUL-terminated for len > 0:
//   id=48 name=debug_smoke tier=debug status=in_progress steps=0x00000003/3 ordered=0 available=0 prereqs=met
// An unregistered id still renders (store-level fields only): id=0 name=- registered=0 ...
void Quest_Describe(int32_t questId, char* buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_QUEST_H
