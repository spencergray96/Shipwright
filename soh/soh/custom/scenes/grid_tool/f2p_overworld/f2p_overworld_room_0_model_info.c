#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx f2p_overworld_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry f2p_overworld_room_0_shapeDListsEntry[1] = {
    { f2p_overworld_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal f2p_overworld_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(f2p_overworld_room_0_shapeDListsEntry),
    f2p_overworld_room_0_shapeDListsEntry,
    f2p_overworld_room_0_shapeDListsEntry + ARRAY_COUNT(f2p_overworld_room_0_shapeDListsEntry)
};
