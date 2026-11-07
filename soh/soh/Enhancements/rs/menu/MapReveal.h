#ifndef SOH_RS_MENU_MAP_REVEAL_H
#define SOH_RS_MENU_MAP_REVEAL_H
/*
 * THE PAUSE MAP'S FOG OF WAR (sturdy-bassoon#303): which RS map squares the player has seen.
 *
 * STORED PER CHUNK, REVEALED PER NODE. A chunk is one 64 x 64 RS map square of a stitched world (#173);
 * its flag lives in the project world-flag store, in the map reveal band (WorldFlagIds.h,
 * WORLD_FLAG_MAP_SQUARE), keyed on the square's ABSOLUTE RS coordinates. A map node is a chunk or a group of
 * chunks with one title (#173 decision 7). Revealing reveals the WHOLE NODE - walking into one of Varrock's
 * four squares shows all four - but the flags are per square, so regrouping chunks in the matrix never
 * invalidates a save.
 *
 * TWO WAYS IN:
 *   - ENTRY. Once per gameplay tick (OnPlayerUpdate, the zone director's hook) in a scene with a chunk table,
 *     Link's square is read in the same RS frame the director uses, from the same world-export row, and if its
 *     flag is clear, its node is revealed. NO DWELL: a reveal is permanent and only adds, so the director's
 *     reason for one (do not switch music because Link wobbled across a border) does not apply here.
 *   - EXPLICIT, for quests and the console: RsMapReveal_RevealChunk / RsMapReveal_RevealNode.
 * Every reveal that changes a flag writes `rs_menu op=map event=reveal square=<sx>,<sy> flag= cause= node="..."`.
 *
 * WHERE THE CHUNKS COME FROM: each opted-in world's `<slug>_world.inc` (RS_WORLD_CHUNK rows), the scenes
 * WorldMap.cpp opts in. A square outside the band's window has no flag: it reads as REVEALED, so a world grown
 * past the window is never wrongly hidden, and a boot line names it.
 *
 * PERSISTENCE: the worldFlags save section, written on every full save. Nothing else is saved.
 */
#include <stdint.h>
#include <vector>

struct RsMapChunk {
    int16_t sceneId;
    int16_t sx, sy;           // the RS map square
    const char* title;        // the chunk's own title
    const char* nodeTitle;    // the node it belongs to (its own title for a node of one)
    int32_t nodeRsX, nodeRsY; // the node's RS tile; with nodeTitle, the node's identity
};

// The chunks of a stitched scene, in the include's order (north row first). Empty for any other scene.
const std::vector<RsMapChunk>& RsMapReveal_Chunks(int16_t sceneId);
// The chunk of square (sx, sy) in `sceneId`, or null.
const RsMapChunk* RsMapReveal_ChunkAt(int16_t sceneId, int32_t sx, int32_t sy);

// The flag of square (sx, sy), or -1 outside the band's window.
int32_t RsMapReveal_Flag(int32_t sx, int32_t sy);
// Whether square (sx, sy) shows on the map. A square with no flag is always revealed.
bool RsMapReveal_IsRevealed(int32_t sx, int32_t sy);

// One square. 1 if its flag was set now, 0 if it already was, -1 if it has no flag. `cause` is a literal:
// entry, quest or console.
int32_t RsMapReveal_RevealChunk(int32_t sx, int32_t sy, const char* cause);
// Every square of the node that square (sx, sy) belongs to, in every world that lists it. Answers how many
// flags were set now, or -1 when no world lists that square (then nothing is set: use RevealChunk).
int32_t RsMapReveal_RevealNode(int32_t sx, int32_t sy, const char* cause);
// Clears one square's flag (test only). 1 cleared, 0 already clear, -1 no flag.
int32_t RsMapReveal_Hide(int32_t sx, int32_t sy);
// Every chunk of a scene revealed, or the whole band cleared (test only). Answers the flags changed.
int32_t RsMapReveal_RevealAll(int16_t sceneId);
int32_t RsMapReveal_HideAll();

// Link's square in the current scene, in its world's RS frame. False outside a scene with a chunk table.
bool RsMapReveal_LinkSquare(int32_t* sx, int32_t* sy);

struct RsMapRevealStats {
    int32_t entryReveals;  // nodes revealed by entry since boot
    int32_t lastSx, lastSy; // the last square any reveal named, -1 before one
    const char* lastCause;  // "" before one
};
RsMapRevealStats RsMapReveal_Stats();

#endif // SOH_RS_MENU_MAP_REVEAL_H
