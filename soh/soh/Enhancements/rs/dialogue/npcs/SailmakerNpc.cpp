// NPC_SAILMAKER (1) - puts the player's own picture on a boat's sail (sturdy-bassoon#287). A proof of concept: the
// owner will tidy the prose and the place before release, and the plain-colour sailmaker is to be a character of
// its own.
//
// The tree: an entry statement into a menu (which boat), then a screen per boat (which sail, each offered only on a
// boat the player owns, BoatOwned), then a screen per sail. Taking a sail ARMS it (RS_DLG_ACTION_BOAT_SAIL): while
// its screen is open, a picture dropped on the game window is that sail's, converted into sails/ and drawn at once
// (RsBoatSail.h). The screen offers a picture already in sails/ (put there by hand: the sail's picture look to custom,
// or a toast saying there is none), the system's file dialog, a way back to the sail's colour, and Done, which
// disarms. Every way out of a sail's screen disarms, so a later drop is SoH's again.
//
// Spawned on Lake Hylia's south shore each time the lake loads, beside the boat RsBoatConsole.cpp puts on the water
// (both gated on `boat lakehylia`, CVAR_RS_BOAT_LAKE_HYLIA), as the boat is: on the first player update, once the
// actor context is up.

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/actors/RsActorParams.h"
#include "soh/Enhancements/rs/actors/RsActors.h"
#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/Enhancements/rs/actors/RsBoatConsole.h"
#include "soh/Enhancements/rs/boats/RsBoatSail.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/dialogue/NpcIds.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

// The nodes, by index: the menu, a screen a boat, a screen a sail.
enum {
    NODE_MENU,
    NODE_SMALL_BOATS,
    NODE_SLOOP,
    NODE_RAFT_SAIL,
    NODE_SKIFF_SAIL,
    NODE_SLOOP_SAIL,
    NODE_JIB,
    NODE_COUNT,
};

const QuestPredicate sOwnsRaft[] = { QP_BOAT_OWNED(RS_BOAT_KIND_RAFT) };
const QuestPredicate sOwnsSkiff[] = { QP_BOAT_OWNED(RS_BOAT_KIND_SKIFF) };
const QuestPredicate sOwnsSloop[] = { QP_BOAT_OWNED(RS_BOAT_KIND_SLOOP) };

const char* const kHowItWorks =
    "Pick a sail, then drop your picture on the game window, or browse for it. A square picture suits every sail; "
    "the sails folder has a template of each sail's shape to paint on.";

const RsDialogueOption sMenuOptions[] = {
    { "Raft or skiff", RS_DLG_ACTION_NONE, 0, nullptr, NODE_SMALL_BOATS },
    { "The sloop", RS_DLG_ACTION_NONE, 0, nullptr, NODE_SLOOP },
    { "How does it work?", RS_DLG_ACTION_NONE, 0, kHowItWorks, NODE_MENU },
    { "Never mind", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};

// A sail is offered only on a boat the player owns: two ungated rows keep the choice a choice either way.
const RsDialogueOption sSmallBoatOptions[] = {
    { "The raft's sail", RS_DLG_ACTION_BOAT_SAIL, RS_DLG_SAIL(RS_BOAT_SAIL_RAFT, RS_BOAT_SAIL_OP_ARM), nullptr,
      NODE_RAFT_SAIL, sOwnsRaft, 1 },
    { "The skiff's sail", RS_DLG_ACTION_BOAT_SAIL, RS_DLG_SAIL(RS_BOAT_SAIL_SKIFF, RS_BOAT_SAIL_OP_ARM), nullptr,
      NODE_SKIFF_SAIL, sOwnsSkiff, 1 },
    { "Back", RS_DLG_ACTION_NONE, 0, nullptr, NODE_MENU },
    { "Never mind", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};

const RsDialogueOption sSloopOptions[] = {
    { "The mainsail", RS_DLG_ACTION_BOAT_SAIL, RS_DLG_SAIL(RS_BOAT_SAIL_SLOOP, RS_BOAT_SAIL_OP_ARM), nullptr,
      NODE_SLOOP_SAIL, sOwnsSloop, 1 },
    { "The jib", RS_DLG_ACTION_BOAT_SAIL, RS_DLG_SAIL(RS_BOAT_SAIL_SLOOP_JIB, RS_BOAT_SAIL_OP_ARM), nullptr, NODE_JIB,
      sOwnsSloop, 1 },
    { "Back", RS_DLG_ACTION_NONE, 0, nullptr, NODE_MENU },
    { "Never mind", RS_DLG_ACTION_NONE, 0, nullptr, RS_DLG_NO_NEXT },
};

// One sail's screen: the drop is armed while it is open.
#define SAIL_OPTIONS(sail)                                                                                         \
    {                                                                                                              \
        { "Use my picture", RS_DLG_ACTION_BOAT_SAIL, RS_DLG_SAIL(sail, RS_BOAT_SAIL_OP_USE), nullptr,              \
          RS_DLG_NO_NEXT },                                                                                        \
            { "Browse files", RS_DLG_ACTION_BOAT_SAIL, RS_DLG_SAIL(sail, RS_BOAT_SAIL_OP_BROWSE),                  \
              "Pick it in the window that opens.", RS_DLG_NO_NEXT },                                               \
            { "Back to its colour", RS_DLG_ACTION_BOAT_SAIL, RS_DLG_SAIL(sail, RS_BOAT_SAIL_OP_OFF),               \
              "Just its colour, then.", RS_DLG_NO_NEXT },                                                          \
            { "Done", RS_DLG_ACTION_BOAT_SAIL, RS_DLG_SAIL(sail, RS_BOAT_SAIL_OP_DONE), nullptr, RS_DLG_NO_NEXT }, \
    }
const RsDialogueOption sRaftSailOptions[] = SAIL_OPTIONS(RS_BOAT_SAIL_RAFT);
const RsDialogueOption sSkiffSailOptions[] = SAIL_OPTIONS(RS_BOAT_SAIL_SKIFF);
const RsDialogueOption sSloopSailOptions[] = SAIL_OPTIONS(RS_BOAT_SAIL_SLOOP);
const RsDialogueOption sJibOptions[] = SAIL_OPTIONS(RS_BOAT_SAIL_SLOOP_JIB);
#undef SAIL_OPTIONS

const char* const kDropIt = "Drop your picture now.";

const RsDialogueNode sNodes[NODE_COUNT] = {
    { nullptr, 0, "Which boat's sail?", sMenuOptions, 4, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    { nullptr, 0, "Which sail?", sSmallBoatOptions, 4, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    { nullptr, 0, "Which sail?", sSloopOptions, 4, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    { nullptr, 0, kDropIt, sRaftSailOptions, 4, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    { nullptr, 0, kDropIt, sSkiffSailOptions, 4, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    { nullptr, 0, kDropIt, sSloopSailOptions, 4, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
    { nullptr, 0, kDropIt, sJibOptions, 4, RS_DLG_NO_MISSING, RS_DLG_NO_NEXT },
};

const RsDialogueRule sRules[] = {
    { nullptr, 0, "Sails made to order. Your own picture on one, even.", nullptr, 0, RS_DLG_NO_MISSING, NODE_MENU },
};

const RsNpcDef sSailmaker = {
    NPC_SAILMAKER, QUEST_TIER_PROD, "sailmaker", "The Sailmaker", sRules, 1, sNodes, NODE_COUNT,
};

// On the narrow beach due south of where the lake's boat lies (-875, 6000), facing it across the water. Measured in
// game with the lake raised (`boat probe`: dry ground from z 6760 to 6800, its floor about -1290; the cliff past it).
constexpr float kSailmakerX = -875.0f;
constexpr float kSailmakerY = -1270.0f;
constexpr float kSailmakerZ = 6785.0f;
constexpr int16_t kSailmakerYaw = (int16_t)0x8000;

bool sPending = false;

void OnSceneInitSailmaker(int16_t sceneNum) {
    sPending = sceneNum == SCENE_LAKE_HYLIA && CVarGetInteger(CVAR_RS_BOAT_LAKE_HYLIA, 1) != 0;
}

void OnPlayerUpdateSailmaker() {
    if (!sPending || gPlayState == nullptr) {
        return;
    }
    sPending = false;
    if (gPlayState->sceneNum == SCENE_LAKE_HYLIA) {
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_RS_NPC, kSailmakerX, kSailmakerY, kSailmakerZ, 0,
                    kSailmakerYaw, 0, RS_NPC_PARAMS(NPC_SAILMAKER));
    }
}

// RsNpc_Register is idempotent for the same pointer, which is what makes a ShipInit "*" re-run safe.
void RegisterSailmakerNpc() {
    RsNpc_Register(&sSailmaker);
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        COND_HOOK(OnSceneInit, true, OnSceneInitSailmaker);
        COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateSailmaker);
    }
}

RegisterShipInitFunc sailmakerNpcInitFunc(RegisterSailmakerNpc);

} // namespace
