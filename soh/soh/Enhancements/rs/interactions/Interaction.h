#ifndef SOH_RS_INTERACTION_H
#define SOH_RS_INTERACTION_H

// The interaction registry (sturdy-bassoon#183): a prop's dialogue, keyed by the id the grid tool
// minted. The definitions are RsNpcDef - the same screens, gates, flag effects and trees a character
// has (NpcDialogueDef.h) - with `id` holding the INTERACTION id and `tier` its band's tier, and
// `displayName` what a console line calls the prop. They are held to the same body validation as a
// character (RsDialogue_BodyProblem); only the id space is this file's own.
//
// The rows live in InteractionTable.cpp, which is the file the grid tool reads for "has code".

#include <stddef.h>
#include <stdint.h>
#include "InteractionIds.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"

typedef RsNpcDef RsInteractionDef;

typedef enum RsInteractionResult {
    RS_INTERACTION_OK = 0,
    RS_INTERACTION_ERR_BAD_DEF = 1,
    RS_INTERACTION_ERR_DUPLICATE = 2,
} RsInteractionResult;

#ifdef __cplusplus
extern "C" {
#endif

// Validates and registers. Idempotent for the same pointer (ShipInit re-runs); a different
// definition for an owned id is a duplicate. Refuses an id outside 15 bits, a tier that does not
// match the id's band, and anything RsDialogue_BodyProblem refuses - loudly, as RsNpc_Register does.
int32_t RsInteraction_Register(const RsInteractionDef* def);

// The same checks with no log and no assert, for a console probe. 0 clean; 1 with the reason.
int32_t RsInteraction_DefProblem(const RsInteractionDef* def, char* buf, size_t len);

// NULL for an id with no code yet - the placeholder case - or an invalid one. Never asserts.
const RsInteractionDef* RsInteraction_GetDef(int32_t id);
int32_t RsInteraction_RegisteredCount(void);
// The n-th registered definition in id order, for listing; NULL past the end.
const RsInteractionDef* RsInteraction_DefAt(int32_t n);

// The screen a reply slot names (InteractionIds.h): a rule for 0..31, a node for 32..63. NULL for
// a NULL definition or a slot it does not have.
const RsDialogueRule* RsInteraction_SlotScreen(const RsInteractionDef* def, int32_t slot);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_INTERACTION_H
