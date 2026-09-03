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
ActorEntry test_level_room_0_header00_actorList[7] = {
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

    // Step 0 of the giver's quest. Its sibling (step 1) is in terrain_f2p_step2, so finishing the
    // quest requires both scenes — which is what makes "advancing in one scene is reflected in the
    // other" a thing the run has to do rather than a thing it can fake.
    { ACTOR_RS_QUEST_ITEM, { 0, 20, 200 }, { 0, 0, 0 }, RS_ITEM_PARAMS(QUEST_DEBUG_GIVER, 0) },
};
