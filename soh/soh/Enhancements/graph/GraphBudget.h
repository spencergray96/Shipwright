#ifndef SOH_GRAPH_BUDGET_H
#define SOH_GRAPH_BUDGET_H

#include "z64.h"

// The frame's display-list arenas and the overflow fail-safe (sturdy-bassoon#247). The sizes are in
// z64.h (GfxPool), the code in `soh/src/code/graph.c` and Actor_DrawAll (`z_actor.c`); the console
// surface is GraphConsole.{h,cpp} beside this file.
//
// What went wrong: OoT writes every display list by bumping a raw pointer, and only Graph_Update looks
// at the arenas, after the frame is written. OoT then skips submitting a frame that overflowed; SoH
// still hands the work list to the interpreter every frame (it also drives audio and the window), so
// a translucent overrun - which wrote over the overlay list it branches into - looped the interpreter
// forever. About 160 Rs_Fire torches in view did it: 25 translucent words each in Debug.
//
// The fail-safe, in two layers:
//   - the guard: Actor_DrawAll asks Graph_ActorDrawBegin before each actor. With less than
//     GRAPH_ACTOR_RESERVE_WORDS left in the opaque or the translucent arena, the actor is not drawn
//     that frame (warned once a session). The player is never skipped. While there is room this changes
//     nothing at all;
//   - the backstop: anything that still overruns (one draw bigger than the reserve, or a writer outside
//     the actor loop) lands in its array's guard band (GFXPOOL_GUARD_WORDS), and Graph_Update cuts the
//     overrun arena out of the chain: its first word becomes a branch to the next list, so the frame
//     draws without it and the interpreter is always handed a valid list. Warned once a session.
//
// The reserve must cover the largest single actor draw plus everything written into the two arenas after
// the actor loop (effects, particles, the rest of Play_Draw), so a frame the guard stops still finishes
// inside its arena. Both are measured, and `graph status` reports them.
#define GRAPH_ACTOR_RESERVE_WORDS 0x400

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GraphArena {
    GRAPH_ARENA_OPA,
    GRAPH_ARENA_XLU,
    GRAPH_ARENA_OVERLAY,
    GRAPH_ARENA_COUNT,
} GraphArena;

typedef struct GraphArenaStats {
    s32 words;    // the arena's size in force (its GFXPOOL_* size unless capped for a test)
    s32 physical; // the array's size, guard band included
    s32 used;     // words used in the last complete frame: head and tail together
    s32 peak;     // most used in one frame since the last `graph reset`
    s32 maxActor; // largest single actor draw into this arena since the reset (Actor_Draw's words)
    s16 maxActorId;
    s32 afterActors; // words written after the actor loop's last draw, last frame
    s32 afterPeak;   // most of those since the reset
    s32 markers;     // Debug's open/close-disp markers in the last frame (always 0 in Release)
} GraphArenaStats;

typedef struct GraphStats {
    u32 frame;          // the frame the per-frame figures describe
    s32 failsafe;       // 1 unless switched off for a planted control
    s32 actorsDrawn;    // actors drawn last frame
    s32 actorsSkipped;  // actors the guard skipped last frame
    u32 skippedTotal;   // since the reset
    u32 skippedFrames;  // frames with any actor skipped, since the reset
    u32 repairedFrames; // frames the backstop cut an arena out of, since the reset
    s32 lastRepairMask; // 1 << GraphArena for each arena cut out of the last repaired frame
    u32 overrunFrames;  // frames any arena overran, failsafe on or off, since the reset
    GraphArenaStats arena[GRAPH_ARENA_COUNT];
} GraphStats;

void Graph_GetStats(GraphStats* out);
void Graph_ResetStats(void);

// For a planted control, this session only: shrink an arena to `words` (clamped to 64..its size), or
// pass 0 for its size. With the fail-safe off the arena sits flush against the next list, with no guard
// band - OoT's own layout - so `cap xlu 4096` + `failsafe off` is the pre-#247 game.
s32 Graph_SetArenaCap(GraphArena arena, s32 words);
void Graph_SetFailsafe(s32 on);

// Actor_DrawAll's guard. Begin returns 0 when the actor must not be drawn this frame; End records what
// a drawn actor wrote. The pair costs two pointer reads and compares a draw.
s32 Graph_ActorDrawBegin(GraphicsContext* gfxCtx, Actor* actor);
void Graph_ActorDrawEnd(GraphicsContext* gfxCtx, Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // SOH_GRAPH_BUDGET_H
