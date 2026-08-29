#include "../CustomSceneData.h"

// SceneCmd header array is unused (room is initialised directly in C++).
// Only the object list and actor list are needed.
// ACTOR_PLAYER is intentionally excluded — Link spawns via the linkActorEntry/spawn mechanism, not the actor list.

s16 test_level_room_0_header00_objectList[2] = {
    OBJECT_KANBAN,
    OBJECT_WARP1,
};

ActorEntry test_level_room_0_header00_actorList[2] = {
    // Signpost — scene-agnostic, always renders, no crash-prone update logic
    { ACTOR_EN_KANBAN,  { 300, 0, 0 },  { 0, 0, 0 }, 0x0 },
    // Blue warp
    { ACTOR_DOOR_WARP1, { -300, 0, 0 }, { 0, 0, 0 }, 0x0 },
};