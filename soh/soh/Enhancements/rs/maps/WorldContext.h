#ifndef SOH_RS_WORLD_CONTEXT_H
#define SOH_RS_WORLD_CONTEXT_H

#include <stdint.h>

// ============================================================================================
//  WHICH WORLD A SAVE IS PLAYING IN  (sturdy-bassoon#173 slice F5, the slice F ADR's decision 12)
// ============================================================================================
//
// A chunk map is in two scenes: its own solo scene, for testing, and the stitched overworld (F2P).
// Coming up a basement ladder into that chunk, the game must load one of them - and the basement
// cannot say which, because a basement is in no world. The answer is the WORLD CONTEXT (CONTEXT.md):
// one value per save, the world the player was last in.
//
//   - UNSET: a fresh save, until it enters a scene holding a chunk map. The scene picker then takes a
//     stitched scene first (SceneMaps.h, rule 3).
//   - RS_GEN_WORLD_SOLO: playing solo chunk scenes - testing, or the console.
//   - a stitched scene's id: playing that world (F2P).
//
// ENTERING a scene that holds a chunk map sets it - a stitched scene to its own id, a matrix map's
// solo scene to SOLO. A NEUTRAL scene (a basement, a dungeon, any map in no matrix, any vanilla scene)
// leaves it alone, so it survives a detour of any depth. It is SAVED with the save file, so a reload in
// a basement goes up into the world it came down from.
//
// Its own SaveManager section, `rsWorld`, beside `rsPrefs` - game state of the save, not a preference
// of the install: `"rsWorld": { "context": -3 }`.
//
// `worldctx` in the console reads and sets it (WorldConsole.h), with a `rs_worldctx` marker beside it:
// the manual switch for human and agent testing.

#ifdef __cplusplus
extern "C" {
#endif

// Not a scene id, and neither of SceneMaps.h's two world names.
#define RS_WORLD_CONTEXT_UNSET (-3)

int32_t RsWorld_Get(void);
// Whether `context` is a value the context may hold: UNSET, SOLO, or a stitched scene's id
// (RsMaps_IsStitched). Anything else - a solo or neutral scene's id, a scene that is not exported -
// is refused by Set and repaired on load.
int32_t RsWorld_IsValid(int32_t context);
// Sets it and writes `rs_worldctx event=set from= to= cause=`. 0, or 1 (and nothing changed) for a
// value RsWorld_IsValid refuses. `cause` is a word: scene, console, ...
int32_t RsWorld_Set(int32_t context, const char* cause);
// "unset", "solo", or "0x<scene>". A static buffer: use it before the next call.
const char* RsWorld_Name(int32_t context);
// Where the live value came from: "default" (a fresh save, or a save without the section), "file"
// (the save's `rsWorld` section), "scene" (a scene entered since), or "console".
const char* RsWorld_Source(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_WORLD_CONTEXT_H
