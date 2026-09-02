#ifndef SOH_RS_QUEST_STORE_H
#define SOH_RS_QUEST_STORE_H

#include <stdint.h>
#include "QuestIds.h"

// The `quests` SaveManager section (sturdy-bassoon#58 decision D1): a fixed array indexed by
// QuestId, one {status, stepMask} entry per quest. Sits beside worldFlags (#54) as a project
// global rather than a gSaveContext member - no vanilla struct is touched, no save format is
// broken, older builds warn-and-skip the unknown section, and a save without it reads all-zero.
//
// This is the STORE layer only: raw, bounds-checked reads and writes of the persisted bytes. It
// knows nothing about quest definitions. The definition-aware API (ordered-step enforcement,
// tier check at registration, COMPLETE-before-rewards completion, debugwipe) is Quest.h and sits
// on top of these calls; nothing below should ever grow a rule about what a quest *means*.
//
// Every accessor bounds-checks and SHOUTS on a bad quest id / status / step: SPDLOG_ERROR plus a
// debug assert, then a safe no-op or zero return. Silent-failure limits are this project's stated
// enemy (ENGINE_BUDGETS.md).

// Serialized as a u8. NEVER reorder or renumber - these values are in save files.
typedef enum QuestStatus {
    QUEST_STATUS_NOT_STARTED = 0,
    QUEST_STATUS_IN_PROGRESS = 1,
    QUEST_STATUS_COMPLETE = 2,
    QUEST_STATUS_COUNT, // validation sentinel, not a status
} QuestStatus;

// One persisted entry. Layout change => section version bump (see QuestStore.cpp).
typedef struct QuestSaveEntry {
    uint8_t status;    // QuestStatus
    uint32_t stepMask; // bit N = step N set
} QuestSaveEntry;

#ifdef __cplusplus
extern "C" {
#endif

// Status. Out of range: logged + debug assert; Get returns QUEST_STATUS_NOT_STARTED, Set is a
// no-op. Set also validates `status` < QUEST_STATUS_COUNT.
int32_t QuestStore_GetStatus(int32_t questId);
void QuestStore_SetStatus(int32_t questId, int32_t status);

// Whole step mask.
uint32_t QuestStore_GetStepMask(int32_t questId);
void QuestStore_SetStepMask(int32_t questId, uint32_t stepMask);

// Single step, `step` in [0, QUEST_STEP_MAX). Out of range on either argument: logged + debug
// assert; IsStepSet returns 0, Set/Clear are no-ops.
int32_t QuestStore_IsStepSet(int32_t questId, int32_t step);
void QuestStore_SetStep(int32_t questId, int32_t step);
void QuestStore_ClearStep(int32_t questId, int32_t step);

// Zero one entry (status NOT_STARTED, no steps).
void QuestStore_Reset(int32_t questId);

// Number of entries that are not all-zero. Diagnostics/tests only.
int32_t QuestStore_CountTouched(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_QUEST_STORE_H
