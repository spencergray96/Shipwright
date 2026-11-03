#ifndef SOH_RS_BOAT_DOCK_H
#define SOH_RS_BOAT_DOCK_H

// Boat docks (sturdy-bassoon#282): where the player's own boats are, and the piers that hand them out. The design is
// sturdy-bassoon docs/decisions/2026-10-30-boat-ownership-and-fit-out.md ("Docks"); the words are CONTEXT.md's.
//
// A BOAT'S HP (#284, RsBoatHealth.h) is the pier's business too: it repairs a damaged boat that is stowed or docked
// at any pier, to full, for RsBoatHealth_RepairFee, and will not take out a WRECKED one (capsized, 0 HP) until then.
//
// EVERY OWNED BOAT IS IN ONE OF TWO PLACES, saved per kind in `rsBoats` (RsBoatFit.cpp writes the section, this file
// its keys):
//   - STOWED: in no particular place. Any pier takes it out, free. A newly owned boat starts stowed, and so does every
//     boat of a save from before this landed (no `out` key).
//   - OUT: in the world, at a scene, x, z and heading. The boat is spawned there whenever its scene loads, so a boat
//     left by an island is still by the island after a scene change or a reload. An out boat inside a dock's radius
//     is DOCKED there; anywhere else it is ABANDONED, and fetching it to a pier costs a fee.
// No pier is ever saved: stowed and docked boats are free at every pier alike.
//
// A DOCK is a pier and its water. The pier is a prop placed in the grid tool with a dock interaction attached; its
// trigger actor (Rs_Interaction) is the dock, and its row here (kDocks) gives the radius. The MOORING, where a boat
// taken out appears, is in front of the pier along its facing (the trigger's rot.y, the placement's turn), far enough
// out for the kind's beam, with the boat parallel to the shore. Docks are found among the loaded actors: one in another
// scene is known only through the boats' saved `docked`, and in a multi-room scene a pier in an unloaded room does not
// count, so a boat beside it reads abandoned until its room loads (the docks' test map is one room).
//
// OVERLAPPING DOCKS are allowed and never refused. A boat inside several belongs to the one whose mooring is nearest,
// the earliest-spawned pier on a tie. Taking a boat out stows any other of the player's boats lying on the mooring,
// whichever pier it belongs to, and nothing else: one of theirs elsewhere in the pier's water stays where it is, so a
// pier can have several docked. Only a boat that is not the player's blocks the mooring (`mooring_blocked`, nothing
// changes), so two hulls are never spawned inside each other. A boat already inside this pier's ring is not taken out
// or fetched again: `already_here`, though it may belong to a neighbour.
//
// SPAWNS ARE GUARDED against the DynaPoly pools: a boat whose most collision would not fit is not spawned, with a
// `rs_boat event=refused reason=dyna_pool` marker, and stays where the save has it. Boats already up count at their
// most too, since a facility fitted later grows a hull with no spawn to check it. Two sloops used to assert
// (ENGINE_BUDGETS.md).
//
// Console: `boat where|docks|stow|place|takeout|putaway|fetch` (RsBoatDock_ConsoleRun). The pier's dialogue runs the same
// operations through RS_DLG_ACTION_BOAT_DOCK, and gates its options on BoatStateIs (QuestPredicate.h).

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where a kind's boat is: QuestPredicate.h's BoatStateIs operand `b`, and `boat where`'s `state=`. Appended, never
// reordered (definitions carry the numbers).
typedef enum RsBoatDockState {
    RS_BOAT_DOCK_STOWED,    // in no place; any pier takes it out free
    RS_BOAT_DOCK_DOCKED,    // out, inside a dock (as of when it was last in a loaded scene)
    RS_BOAT_DOCK_ABANDONED, // out, outside every dock, or where there is no water to put it now
    RS_BOAT_DOCK_HERE,      // docked at the pier Link is talking to (a test, never a stored state): implies DOCKED
    RS_BOAT_DOCK_DAMAGED,   // its HP is under its most, wherever it is (a test, as HERE; boats/RsBoatHealth.h, #284)
    RS_BOAT_DOCK_WRECKED,   // its HP is 0: it capsized and has not been repaired (a test, as HERE; always stowed)
    RS_BOAT_DOCK_STATE_COUNT,
} RsBoatDockState;

// What a pier does for one kind: RS_DLG_ACTION_BOAT_DOCK's op, and the console's verbs.
typedef enum RsBoatDockOp {
    RS_BOAT_DOCK_OP_TAKE_OUT, // a stowed or docked boat to this pier's mooring, free
    RS_BOAT_DOCK_OP_PUT_AWAY, // the boat moored here to stowed
    RS_BOAT_DOCK_OP_FETCH,    // an abandoned boat to this pier's mooring, for its fee
    RS_BOAT_DOCK_OP_REPAIR,   // a damaged boat, stowed or docked at any pier, to full HP, for its repair fee (#284)
    RS_BOAT_DOCK_OP_COUNT,
} RsBoatDockOp;

typedef enum RsBoatDockResult {
    RS_BOAT_DOCK_OK,
    RS_BOAT_DOCK_NOT_OWNED,       // the player does not own that kind
    RS_BOAT_DOCK_NO_PIER,         // the actor is not a dock's pier (or there is none)
    RS_BOAT_DOCK_ALREADY_HERE,    // take out or fetch: it is moored here already
    RS_BOAT_DOCK_NOT_HERE,        // put away: it is not moored here
    RS_BOAT_DOCK_IS_ABANDONED,    // take out: it is abandoned, so it is fetched
    RS_BOAT_DOCK_NOT_ABANDONED,   // fetch: it is not abandoned, so it is taken out free
    RS_BOAT_DOCK_MOORING_BLOCKED, // another boat lies on the mooring
    RS_BOAT_DOCK_DYNA_POOL,       // the scene's DynaPoly pools cannot take the hull
    RS_BOAT_DOCK_NO_WATER,        // the mooring is not on water
    RS_BOAT_DOCK_CANT_AFFORD,     // fetch or repair: fewer rupees than the fee
    RS_BOAT_DOCK_IS_WRECKED,      // take out or fetch: it capsized, and is mended first (#284)
    RS_BOAT_DOCK_NOT_DAMAGED,     // repair: it is at full HP
    RS_BOAT_DOCK_RESULT_COUNT,
} RsBoatDockResult;

// 1 when the loaded scene's DynaPoly pools can take one more boat of the kind at its most collision (every registered
// actor counted, a killed one too until it is deleted). Every boat spawn checks it: the docks' and `boat spawn`'s.
int32_t RsBoatDock_SpawnFits(int32_t kind);

// The kind's state (RS_BOAT_DOCK_STOWED, _DOCKED or _ABANDONED), or -1 for a kind the player does not own.
int32_t RsBoatDock_State(int32_t kind);
// BoatStateIs: 1 when the owned kind is in `state`. RS_BOAT_DOCK_HERE asks about the pier Link is talking to.
int32_t RsBoatDock_StateIs(int32_t kind, int32_t state);
const char* RsBoatDock_StateName(int32_t state); // "stowed", "docked", "abandoned", "here", "?"
const char* RsBoatDock_ResultName(int32_t result);
const char* RsBoatDock_OpName(int32_t op);

// What fetching an abandoned boat of the kind costs, in rupees: a placeholder per kind (the owner's 2026-11-02 pick),
// to scale with the fit-out once real upgrade costs exist. Defines, because the pier's labels and gates spell them
// (InteractionTable.cpp).
#define RS_BOAT_DOCK_FEE_SKIFF 10
#define RS_BOAT_DOCK_FEE_SLOOP 20
#define RS_BOAT_DOCK_FEE_RAFT 5
int32_t RsBoatDock_Fee(int32_t kind);

// Runs one pier operation for the kind at `pier` (an Rs_Interaction actor with a dock row; may be NULL, which is
// RS_BOAT_DOCK_NO_PIER). Checks everything before changing anything: a refusal changes nothing and charges nothing.
// Writes an `rs_boat event=<op>` marker either way.
struct Actor;
int32_t RsBoatDock_Run(int32_t op, int32_t kind, struct Actor* pier);
// The pier Link is talking to (Player's talk actor, when it is a dock's pier), or NULL. The dialogue action's pier.
struct Actor* RsBoatDock_TalkPier(void);
// RS_DLG_ACTION_BOAT_DOCK: RsBoatDock_Run at RsBoatDock_TalkPier, and what the pier says about it
// (RsInteraction_ActionSays): why, in place of the reply, for a refusal ("Your skiff's already here."); after the
// reply, the boats it put away to make room. A toast only for a refusal its gates should have hidden (a bug).
int32_t RsBoatDock_DialogueAction(int32_t op, int32_t kind);

// Puts the kind back to stowed wherever it is, killing its boat if it is in the loaded scene: a capsized boat gone under,
// or wrecked in another scene (#284).
void RsBoatDock_Stow(int32_t kind);

// The `rsBoats` keys of one kind (RsBoatFit.cpp's section calls these inside each kind's object), and the reset every
// new game and load starts from (everything stowed).
void RsBoatDock_SaveKind(int32_t kind);
void RsBoatDock_LoadKind(int32_t kind);
void RsBoatDock_Reset(void);

#ifdef __cplusplus
}

#include <string>
#include <vector>

// The dock's console verbs, for RsBoatConsole_Run: returns -1 when args[0] is not one of them.
int32_t RsBoatDock_ConsoleRun(const std::vector<std::string>& args, std::vector<std::string>& lines);

// The grammar, for `boat`'s usage line and help text.
#define RS_BOAT_DOCK_GRAMMAR                                                                                        \
    "where | docks | stow <kind>|all | place <kind> <x> <z> [<yaw>] | takeout <kind> [<dock>] | putaway <kind> "          \
    "[<dock>] | fetch <kind> [<dock>] | repair <kind> [<dock>]"
#endif

#endif // SOH_RS_BOAT_DOCK_H
