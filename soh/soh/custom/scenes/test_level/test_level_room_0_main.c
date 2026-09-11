#include "../CustomSceneData.h"
#include "soh/Enhancements/rs/actors/RsActorParams.h"
#include "soh/Enhancements/rs/dialogue/NpcIds.h"
#include "soh/Enhancements/rs/quest/QuestIds.h"

// SceneCmd header array is unused (room is initialised directly in C++).
// Only the object list and actor list are needed.
// ACTOR_PLAYER is intentionally excluded — Link spawns via the linkActorEntry/spawn mechanism, not the actor list.

s16 test_level_room_0_header00_objectList[2] = {
    OBJECT_KANBAN,
    OBJECT_WARP1,
};

// The RS actors below need NO object-list entry: they draw gameplay_keep display lists, and
// gameplay_keep is spawned at bank index 0 by Object_InitBank before any actor spawns. That matters
// on this path specifically — Actor_SpawnEntry sets gMapLoading, which suppresses Actor_Spawn's
// "fall back to bank 0" branch, so an actor whose object is missing silently fails to spawn.
//
// Placement is hand-inserted here on purpose (sturdy-bassoon#58 D24). It never depended on the grid
// tool and is not waiting for it: authored placement arrives later through standard Blender +
// Fast64, which carries `params` today. `params` is the NpcId (or the quest/step pair) — see
// soh/soh/Enhancements/rs/actors/RsActorParams.h for the packing and why the mask is baked in now.
//
// The y values are deliberately above the floor: both RS actors carry gravity and settle onto
// whatever is beneath them, so a hand-guessed height cannot leave one floating.
ActorEntry test_level_room_0_header00_actorList[] = {
    // Signpost — scene-agnostic, always renders, no crash-prone update logic
    { ACTOR_EN_KANBAN,  { 300, 0, 0 },  { 0, 0, 0 }, 0x0 },
    // Blue warp
    { ACTOR_DOOR_WARP1, { -300, 0, 0 }, { 0, 0, 0 }, 0x0 },

    // NPC_DEBUG_GIVER, placed TWICE. One character, two placements: the same identity, the same
    // rule table and the same state (D21/D22), and the pair is what proves a reward or a one-shot
    // cannot fire twice. They are 800 units apart, well outside the 110-unit talk range, so only
    // one of them is ever being talked to.
    { ACTOR_RS_NPC, { -400, 20, -200 }, { 0, 0x4000, 0 }, RS_NPC_PARAMS(NPC_DEBUG_GIVER) },
    { ACTOR_RS_NPC, { 400, 20, -200 },  { 0, -0x4000, 0 }, RS_NPC_PARAMS(NPC_DEBUG_GIVER) },

    // A three-option rule, and a different character on the same actor type and model as the giver.
    { ACTOR_RS_NPC, { -400, 20, 200 }, { 0, 0x4000, 0 }, RS_NPC_PARAMS(NPC_DEBUG_THREE) },
    { ACTOR_RS_NPC, { 400, 20, 200 },  { 0, -0x4000, 0 }, RS_NPC_PARAMS(NPC_DEBUG_TWIN) },

    // A question plus FOUR options, in the taller box (sturdy-bassoon#59). Placed 400 units from
    // 193 and 800 from the twin: outside the 110-unit talk range of either, so the four-way box and
    // the three-way box can be screenshotted side by side without ever being in range at once.
    { ACTOR_RS_NPC, { -400, 20, 600 }, { 0, 0x4000, 0 }, RS_NPC_PARAMS(NPC_DEBUG_FOUR) },

    // The floor-convention fixture (sturdy-bassoon#94). Placed so a screenshot can show the ONE
    // thing a marker cannot: the textbox itself, with `{floor:N}` expanded in the body AND in the
    // option labels the renderer lays out. 400 units from the four-way and 800 from 193, i.e.
    // outside the 110-unit talk range of either.
    { ACTOR_RS_NPC, { 400, 20, 600 }, { 0, -0x4000, 0 }, RS_NPC_PARAMS(NPC_DEBUG_FLOOR) },

    // The dialogue-TREE fixtures (sturdy-bassoon#96). 197 is the tree proper - navigation,
    // loop-back and the option that only appears once another has been taken; 198 is authored
    // paging, "More..." and "Back". Placed at z=-600, between the givers at z=-200 and the Cook at
    // z=-1000, and 800 units apart from each other: outside the 110-unit talk range of anything, so
    // a run can screenshot one conversation without a second NPC ever offering to talk.
    { ACTOR_RS_NPC, { -400, 20, -600 }, { 0, 0x4000, 0 }, RS_NPC_PARAMS(NPC_DEBUG_TREE) },
    { ACTOR_RS_NPC, { 400, 20, -600 }, { 0, -0x4000, 0 }, RS_NPC_PARAMS(NPC_DEBUG_PAGE) },
    // The tree follow-ups (#96): an entry statement that leads on, a node group, and the
    // missing-steps clause on a node. 400 units from 197 along -X and 800 from Link's spawn; the
    // floor runs to +/-2000 on both axes, so it is well inside it.
    { ACTOR_RS_NPC, { -800, 20, -600 }, { 0, 0x4000, 0 }, RS_NPC_PARAMS(NPC_DEBUG_QUEST_MENU) },

    // Step 0 of the giver's quest. Its sibling (step 1) is in terrain_f2p_step2, so finishing the
    // quest requires both scenes — which is what makes "advancing in one scene is reflected in the
    // other" a thing the run has to do rather than a thing it can fake.
    { ACTOR_RS_QUEST_ITEM, { 0, 20, 200 }, { 0, 0, 0 }, RS_ITEM_PARAMS(QUEST_DEBUG_GIVER, 0) },

    // --- The Cook's Assistant (sturdy-bassoon#58 P4) --------------------------------------------
    //
    // The first PRODUCTION-band character and quest to be placed anywhere. NPC_COOK is 0, so his
    // params word is a literal 0x0000 — the same value an unspecified placement carries. That is
    // legitimate and not a hazard (nothing else emits RS actors), and it is exactly why `npc actors`
    // prints `registered=` and `rsvd=` on every row: an id of 0 that resolves to a real character
    // looks different from one that resolves to nothing.
    //
    // He stands 500 units from Link's spawn at { 0, 0, -500 } — clear of the 110-unit talk range,
    // and not directly under the entry camera.
    { ACTOR_RS_NPC, { 0, 20, -1000 }, { 0, 0, 0 }, RS_NPC_PARAMS(NPC_COOK) },

    // His three ingredients, one per step, spread so that no two are inside the 55-unit collect
    // range at once and every one of them is a separate walk. They do not respawn once collected:
    // a ShouldActorInit hook (rs/actors/RsActors.cpp) refuses to build an item whose step is
    // already set — which is a READ, and so stays on the right side of the "flags are set on
    // collection, never on spawn" pitfall.
    //
    // They use the GET-ITEM pickup style (sturdy-bassoon#99): Link holds each one up with the fanfare,
    // and its params word reads 0x08xx rather than 0x00xx. The debug giver's item above stays TOUCH on
    // purpose - fixtures stay fast to drive from the agent loop.
    { ACTOR_RS_QUEST_ITEM, { -700, 20, 700 }, { 0, 0, 0 }, RS_ITEM_PARAMS_STYLED(QUEST_COOKS_ASSISTANT, 0, RS_ITEM_STYLE_GET_ITEM) },
    { ACTOR_RS_QUEST_ITEM, { 0, 20, 900 },    { 0, 0, 0 }, RS_ITEM_PARAMS_STYLED(QUEST_COOKS_ASSISTANT, 1, RS_ITEM_STYLE_GET_ITEM) },
    { ACTOR_RS_QUEST_ITEM, { 700, 20, 700 },  { 0, 0, 0 }, RS_ITEM_PARAMS_STYLED(QUEST_COOKS_ASSISTANT, 2, RS_ITEM_STYLE_GET_ITEM) },
};

// The count, derived HERE - the only translation unit where the array's size is known. The scene's
// C++ side sees it through an unbounded `extern ActorEntry[]`, so it cannot measure the array
// itself, and a hand-typed count that drifts short does not fail: it silently drops the last
// placements off the end of the list.
const s32 test_level_room_0_header00_actorCount = ARRAY_COUNT(test_level_room_0_header00_actorList);
