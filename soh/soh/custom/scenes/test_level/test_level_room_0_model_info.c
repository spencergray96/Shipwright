#include "test_level_scene_data.h"

// Forward declarations for cross-file and same-file references
extern Gfx test_level_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry test_level_room_0_shapeDListsEntry[1] = {
    { test_level_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal test_level_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(test_level_room_0_shapeDListsEntry),
    test_level_room_0_shapeDListsEntry,
    test_level_room_0_shapeDListsEntry + ARRAY_COUNT(test_level_room_0_shapeDListsEntry)
};

