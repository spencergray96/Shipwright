// THE CODE TABLE (sturdy-bassoon#183): one row per interaction that has dialogue, keyed by the id
// the grid tool minted. An interaction with no row says the placeholder ("Nothing interesting
// happens.") - which is every interaction until an agent writes its row here.
//
// The grid tool reads this file, and only this file, for its "code status" column: every
// `RS_INTERACTION(<n>)` outside a comment is an id that has code (src/shared/interactions.ts,
// INTERACTION_CODE_TABLE). So:
//   - spell the id with RS_INTERACTION(n), in the definition's id field, and nowhere else outside a
//     comment - a stray one would mark an interaction as having code it does not have;
//   - one definition per interaction, however many placements share it: the placements share the
//     id, the dialogue is written once.
//
// Writing a row: the definition is an RsNpcDef (Interaction.h) - copy a character's from
// rs/dialogue/npcs/ and the soh-add-quest skill's rules all apply: screens, gated options, flag
// effects, trees, `{floor:N}` for any storey. `tier` is the id's band (RS_INTERACTION_ID_TIER), and
// a production interaction may set only production flags. Remembered state is a permanent world
// flag set by a choice (WorldFlagIds.h), the first pass's only kind.
#include "Interaction.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/quest/QuestIds.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

namespace {

// --- I-32512, debug crate (a test fixture) -------------------------------------------------------
//
// The #183 acceptance fixture: a choice whose option sets a permanent flag and replies, and a first
// rule that reads the flag, so one prop proves the screen, the reply routing, the flag effect and
// the remembered state through the talking actor. Its neighbour in the run, I-32513, deliberately
// has no row here: it is the placeholder case.

const QuestPredicate sDebugCrateOpenedWhen[] = {
    QP_WORLD_FLAG_SET(WORLD_FLAG_DEBUG_INTERACTION_CRATE),
};
const RsDialogueOption sDebugCrateOptions[] = {
    { "Prise it open", RS_DLG_ACTION_SET_WORLD_FLAG, WORLD_FLAG_DEBUG_INTERACTION_CRATE,
      "Nothing inside but straw.", RS_DLG_NO_NEXT },
    { "Leave it", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};
const RsDialogueRule sDebugCrateRules[] = {
    { sDebugCrateOpenedWhen, 1, "The lid is off. Only straw.", nullptr, 0, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    { nullptr, 0, "A crate, nailed shut.", sDebugCrateOptions, 2, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
};

const RsInteractionDef sDebugCrate = {
    RS_INTERACTION(32512), QUEST_TIER_DEBUG, "debug_crate", "Debug: a crate", sDebugCrateRules, 2,
};

void RegisterInteractionTable() {
    RsInteraction_Register(&sDebugCrate);
}

RegisterShipInitFunc interactionTableInitFunc(RegisterInteractionTable);

} // namespace
