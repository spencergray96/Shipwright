#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx terrain_f2p_step2_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry terrain_f2p_step2_room_0_shapeDListsEntry[1] = {
    { terrain_f2p_step2_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal terrain_f2p_step2_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(terrain_f2p_step2_room_0_shapeDListsEntry),
    terrain_f2p_step2_room_0_shapeDListsEntry,
    terrain_f2p_step2_room_0_shapeDListsEntry + ARRAY_COUNT(terrain_f2p_step2_room_0_shapeDListsEntry)
};
