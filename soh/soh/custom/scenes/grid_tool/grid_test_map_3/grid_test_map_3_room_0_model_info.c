#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx grid_test_map_3_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry grid_test_map_3_room_0_shapeDListsEntry[1] = {
    { grid_test_map_3_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal grid_test_map_3_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(grid_test_map_3_room_0_shapeDListsEntry),
    grid_test_map_3_room_0_shapeDListsEntry,
    grid_test_map_3_room_0_shapeDListsEntry + ARRAY_COUNT(grid_test_map_3_room_0_shapeDListsEntry)
};
