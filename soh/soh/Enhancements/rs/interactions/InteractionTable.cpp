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
#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/Enhancements/rs/boats/RsBoatDock.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/quest/QuestIds.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

namespace {

// --- I-0001, pier: boat dock (sturdy-bassoon#282) ------------------------------------------------------
//
// Every pier placed in the grid tool with this interaction is a dock (boats/RsBoatDock.cpp has its radius). The menu
// has three rows a kind, of which the gates show at most one: take it out (stowed, or docked at any pier: free), put
// it away (docked at this pier), or fetch it (abandoned: its fee, RsBoatDock.h). A kind the player does not own shows
// none. The fetch row shows whether or not Link can pay, with its fee in the label: hidden, a player who first came
// with no money would not learn it exists (the owner, 2026-11-02). Short of the fee, the pier says so (RsBoatDock's
// words). The two ungated rows keep the choice a choice. Placeholder prose: the owner tidies it, as the sailmaker's.

#define PIER_STR_(x) #x
#define PIER_STR(x) PIER_STR_(x)

#define PIER_TAKE_WHEN(kind)                                                                                         \
    { QP_BOAT_OWNED(kind), QP_NOT_BOAT_STATE_IS(kind, RS_BOAT_DOCK_HERE),                                            \
      QP_NOT_BOAT_STATE_IS(kind, RS_BOAT_DOCK_ABANDONED) }
const QuestPredicate sPierTakeRaft[] = PIER_TAKE_WHEN(RS_BOAT_KIND_RAFT);
const QuestPredicate sPierTakeSkiff[] = PIER_TAKE_WHEN(RS_BOAT_KIND_SKIFF);
const QuestPredicate sPierTakeSloop[] = PIER_TAKE_WHEN(RS_BOAT_KIND_SLOOP);
#undef PIER_TAKE_WHEN
const QuestPredicate sPierPutRaft[] = { QP_BOAT_STATE_IS(RS_BOAT_KIND_RAFT, RS_BOAT_DOCK_HERE) };
const QuestPredicate sPierPutSkiff[] = { QP_BOAT_STATE_IS(RS_BOAT_KIND_SKIFF, RS_BOAT_DOCK_HERE) };
const QuestPredicate sPierPutSloop[] = { QP_BOAT_STATE_IS(RS_BOAT_KIND_SLOOP, RS_BOAT_DOCK_HERE) };
const QuestPredicate sPierFetchRaft[] = { QP_BOAT_STATE_IS(RS_BOAT_KIND_RAFT, RS_BOAT_DOCK_ABANDONED) };
const QuestPredicate sPierFetchSkiff[] = { QP_BOAT_STATE_IS(RS_BOAT_KIND_SKIFF, RS_BOAT_DOCK_ABANDONED) };
const QuestPredicate sPierFetchSloop[] = { QP_BOAT_STATE_IS(RS_BOAT_KIND_SLOOP, RS_BOAT_DOCK_ABANDONED) };

const char* const kPierTaken = "She'll be at the end of the pier.";
const char* const kPierStowed = "I'll see her stowed.";
const char* const kPierFetched = "I'll have her brought round.";
const char* const kPierHow =
    "Any boat of yours that is put away, or tied up at any pier, I bring here free. One left anywhere else I fetch "
    "for a fee.";

enum { PIER_NODE_MENU, PIER_NODE_COUNT };

const RsDialogueOption sPierMenuOptions[] = {
    { "Take the raft out", RS_DLG_ACTION_BOAT_DOCK, RS_DLG_DOCK(RS_BOAT_DOCK_OP_TAKE_OUT, RS_BOAT_KIND_RAFT), kPierTaken,
      RS_DLG_NO_NEXT, sPierTakeRaft, 3 },
    { "Put the raft away", RS_DLG_ACTION_BOAT_DOCK, RS_DLG_DOCK(RS_BOAT_DOCK_OP_PUT_AWAY, RS_BOAT_KIND_RAFT), kPierStowed,
      RS_DLG_NO_NEXT, sPierPutRaft, 1 },
    { "Fetch the raft: " PIER_STR(RS_BOAT_DOCK_FEE_RAFT) " rupees", RS_DLG_ACTION_BOAT_DOCK,
      RS_DLG_DOCK(RS_BOAT_DOCK_OP_FETCH, RS_BOAT_KIND_RAFT), kPierFetched, RS_DLG_NO_NEXT, sPierFetchRaft, 1 },
    { "Take the skiff out", RS_DLG_ACTION_BOAT_DOCK, RS_DLG_DOCK(RS_BOAT_DOCK_OP_TAKE_OUT, RS_BOAT_KIND_SKIFF),
      kPierTaken, RS_DLG_NO_NEXT, sPierTakeSkiff, 3 },
    { "Put the skiff away", RS_DLG_ACTION_BOAT_DOCK, RS_DLG_DOCK(RS_BOAT_DOCK_OP_PUT_AWAY, RS_BOAT_KIND_SKIFF),
      kPierStowed, RS_DLG_NO_NEXT, sPierPutSkiff, 1 },
    { "Fetch the skiff: " PIER_STR(RS_BOAT_DOCK_FEE_SKIFF) " rupees", RS_DLG_ACTION_BOAT_DOCK,
      RS_DLG_DOCK(RS_BOAT_DOCK_OP_FETCH, RS_BOAT_KIND_SKIFF), kPierFetched, RS_DLG_NO_NEXT, sPierFetchSkiff, 1 },
    { "Take the sloop out", RS_DLG_ACTION_BOAT_DOCK, RS_DLG_DOCK(RS_BOAT_DOCK_OP_TAKE_OUT, RS_BOAT_KIND_SLOOP),
      kPierTaken, RS_DLG_NO_NEXT, sPierTakeSloop, 3 },
    { "Put the sloop away", RS_DLG_ACTION_BOAT_DOCK, RS_DLG_DOCK(RS_BOAT_DOCK_OP_PUT_AWAY, RS_BOAT_KIND_SLOOP),
      kPierStowed, RS_DLG_NO_NEXT, sPierPutSloop, 1 },
    { "Fetch the sloop: " PIER_STR(RS_BOAT_DOCK_FEE_SLOOP) " rupees", RS_DLG_ACTION_BOAT_DOCK,
      RS_DLG_DOCK(RS_BOAT_DOCK_OP_FETCH, RS_BOAT_KIND_SLOOP), kPierFetched, RS_DLG_NO_NEXT, sPierFetchSloop, 1 },
    { "How does this work?", RS_DLG_ACTION_NONE, 0, kPierHow, PIER_NODE_MENU },
    { "Never mind", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};

#undef PIER_STR
#undef PIER_STR_

const RsDialogueNode sPierNodes[PIER_NODE_COUNT] = {
    { nullptr, 0, "Which boat?", sPierMenuOptions, (int32_t)(sizeof(sPierMenuOptions) / sizeof(sPierMenuOptions[0])),
      RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
};

const QuestPredicate sPierNoBoat[] = {
    QP_NOT_BOAT_OWNED(RS_BOAT_KIND_RAFT),
    QP_NOT_BOAT_OWNED(RS_BOAT_KIND_SKIFF),
    QP_NOT_BOAT_OWNED(RS_BOAT_KIND_SLOOP),
};
const RsDialogueRule sPierRules[] = {
    { sPierNoBoat, 3, "You've no boat to keep here.", nullptr, 0, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    { nullptr, 0, "Boats in and out.", nullptr, 0, RS_DLG_NO_MISSING, PIER_NODE_MENU },
};

const RsInteractionDef sPier = {
    RS_INTERACTION(1), QUEST_TIER_PROD, "pier", "Pier: boat dock", sPierRules, 2, sPierNodes, PIER_NODE_COUNT,
};

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
    RsInteraction_Register(&sPier);
}

RegisterShipInitFunc interactionTableInitFunc(RegisterInteractionTable);

} // namespace
