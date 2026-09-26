#ifndef SOH_RS_STAIRS_H
#define SOH_RS_STAIRS_H

#include <stdint.h>
#include "StairDef.h"

// ============================================================================================
//  STAIRCASES: the registry, the menu, and the STOREY MOVE  (sturdy-bassoon#147)
// ============================================================================================
//
// Three jobs, in the order a player meets them:
//
//   1. REGISTRY. StairTable.cpp registers one RsStairDef per staircase at boot; registration
//      refuses a malformed one (RsStair_DefProblem) and says why, never echoing prose.
//   2. MENU. Every (staircase, row) has one menu, BUILT HERE AT REGISTRATION as an
//      RsDialogueRule, so it goes through the quest-giver's renderer - AutoFormat for two options,
//      the hand layout for three and four, the five-row box, the pixel checks under both floor
//      conventions - rather than a second renderer that would drift from it. Two storeys skip
//      straight to "Go up to the {floor:1}?"; three or four list the others and Cancel.
//   3. THE MOVE, which is the reason this is not simply an NPC. It is owned by a small controller
//      driven from an OnPlayerUpdate hook - NOT by the actor that opened the menu. The move may
//      change rooms, and Room_FinishRoomChange kills every actor outside the new room: an actor
//      running the move from its own update would destroy itself half way through. A controller
//      outside the actor list cannot be killed by a room change, and the console drives the same
//      path (`stairs go`), so the agent loop can prove the move without driving a conversation.
//
// THE MOVE IS IN PLACE, NEVER A SCENE TRANSITION. A transition reloads the scene and resets every
// actor; walk up to meet an NPC pacing the balcony and they would be gone. What the move writes,
// and why each one, is at RsStair_Teleport in Stairs.cpp.

#ifdef __cplusplus
extern "C" {
#endif

// What a staircase call returns. Every refusal is OUTCOME class: a return code and a marker, never
// an assert - the agent loop walks these paths, and a Debug assert there hangs it.
typedef enum RsStairResult {
    RS_STAIR_OK = 0,
    RS_STAIR_ERR_BAD_ID = 1,      // out of range, or no staircase registered under it
    RS_STAIR_ERR_BAD_ROW = 2,     // not a row of that staircase
    RS_STAIR_ERR_BUSY = 3,        // a move is already in flight
    RS_STAIR_ERR_NO_PLAY = 4,     // no scene, or Link does not exist yet
    RS_STAIR_ERR_WRONG_SCENE = 5, // the staircase's landings are in a different scene
    RS_STAIR_ERR_BAD_ROOM = 6,    // the landing names a room this scene does not have
    RS_STAIR_RESULT_COUNT,
} RsStairResult;

const char* RsStair_ResultName(int32_t result); // "ok", "busy", "wrong_scene", ...

// Registration. Idempotent for the same pointer, because ShipInit "*" functions re-run on preset
// apply and config load; a DIFFERENT definition under a registered id is refused. Returns 0 on
// success, otherwise the RsStairProblem the definition failed on.
int32_t RsStair_Register(const RsStairDef* def);

// The validator on its own - "would this register?" with no write, no log and no assert. Returns
// RS_STAIR_PROBLEM_NONE or the first problem; `where` is the row it was found on, or -1.
typedef enum RsStairProblem {
    RS_STAIR_PROBLEM_NONE = 0,
    RS_STAIR_PROBLEM_NULL_DEF,
    RS_STAIR_PROBLEM_BAD_ID,
    RS_STAIR_PROBLEM_BAD_NAME,        // NULL, empty, or carries whitespace, '%', '#' or '"'
    RS_STAIR_PROBLEM_ROW_COUNT,       // fewer than 2 rows (nothing to move between) or more than 4
    RS_STAIR_PROBLEM_NULL_LANDINGS,   // a nonzero count over a NULL array
    RS_STAIR_PROBLEM_BAD_STOREY,      // outside 0..9 - `{floor:N}` takes one digit
    RS_STAIR_PROBLEM_STOREY_ORDER,    // not strictly ascending (a repeat is the same storey twice)
    RS_STAIR_PROBLEM_BAD_ROOM,        // negative or past 255 - a room index is a u8 in the engine
    RS_STAIR_PROBLEM_MENU_OVERFLOWS,  // the built menu would not render: a label wider than its row,
                                      // a body that wraps or a choice that paginates, under SOME
                                      // floor convention
    RS_STAIR_PROBLEM_ID_TAKEN,        // a different definition already holds this id
    RS_STAIR_PROBLEM_COUNT,
} RsStairProblem;

int32_t RsStair_DefProblem(const RsStairDef* def, int32_t* where);
const char* RsStair_ProblemName(int32_t problem); // "row_count", "storey_order", ...

// Lookups. Quiet: NULL / 0 / -1 for an id or row that does not exist, so a surface can ask about
// anything a console was handed.
const RsStairDef* RsStair_GetDef(int32_t stairId);
int32_t RsStair_IsRegistered(int32_t stairId);
const RsStairLanding* RsStair_GetLanding(int32_t stairId, int32_t row);

// The row of this staircase whose landing is on Link's storey right now: the nearest by height,
// and only within half a storey. -1 when he is on none of them. What the console uses as "from";
// the actor never needs it, because every placement states its own row.
int32_t RsStair_RowNearestPlayer(int32_t stairId);

// The menu. `RsStair_Screen` is the screen the renderer lays out for (staircase, row); NULL for a
// pair that does not exist. `RsStair_MenuDestination` maps a picked row of that box back to the
// row it moves to, or -1 for Cancel and -2 for an index the box does not have.
const RsDialogueRule* RsStair_Screen(int32_t stairId, int32_t row);
int32_t RsStair_MenuDestination(int32_t stairId, int32_t row, int32_t choiceIndex);

// THE MOVE. Arms the controller: fade out, put Link down on `toRow`'s landing, change rooms if the
// landing is in another one, point a void-out at where he landed, fade back in, give him back.
// `fromRow` is only reported (-1 = unknown). `source` is a short token for the markers ("menu",
// "console") and must be a string literal.
//
// FREEZES LINK for the duration, through Player's cutscene action with NO cutscene actor: a
// csActor would make him turn to face it every frame, which overwrites the landing's facing.
int32_t RsStair_BeginMove(int32_t stairId, int32_t fromRow, int32_t toRow, const char* source);

// 1 while a move is in flight. Staircases stop offering to talk while it is, so a second menu
// cannot open on top of the first move.
int32_t RsStair_IsMoving(void);

// The fade, in game ticks each way (20 per second). 0 is a HARD CUT - the default - where the move
// happens on the first tick and nothing is drawn over the screen, except across a room change,
// which is held black until the new room is the one drawn. An override lives in a CVar, so a human
// can compare the two without a rebuild; `stairs fade <n>` sets it and `stairs fade default`
// clears it. Clamped to [0, RS_STAIR_MAX_FADE_TICKS].
#define RS_STAIR_MAX_FADE_TICKS 40
int32_t RsStair_GetFadeTicks(void);
void RsStair_SetFadeTicks(int32_t ticks);
void RsStair_ClearFadeTicks(void);
int32_t RsStair_FadeTicksOverridden(void); // 1 when the CVar is set, 0 when the build default applies

#ifdef __cplusplus
}

#include <string>

// C++ only, for the console.
struct RsStairStatus {
    bool moving;
    const char* phase; // "idle", "fade_out", "wait_room", "settle", "fade_in"
    int32_t stairId;
    int32_t fromRow;
    int32_t toRow;
    int32_t ticks;      // ticks since the move began
    int32_t fadeTicks;  // this move's fade length
    const char* source; // "menu" / "console"; "" when idle
    std::string last;   // the last move's `landed` line, or its refusal / abort
};
RsStairStatus RsStair_GetStatus();

// Every registered id, ascending.
int32_t RsStair_ListIds(int32_t* out, int32_t max);

// The menu as the player would read it RIGHT NOW: the body and each option label expanded under
// the live floor convention. The console prints this, the textbox renders the same screen, and the
// labels come from one place, so the two cannot disagree.
std::string RsStair_ComposeBody(int32_t stairId, int32_t row);
std::string RsStair_ComposeLabel(int32_t stairId, int32_t row, int32_t index);
#endif

#endif // SOH_RS_STAIRS_H
