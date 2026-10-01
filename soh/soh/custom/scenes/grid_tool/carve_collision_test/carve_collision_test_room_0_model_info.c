#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx carve_collision_test_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry carve_collision_test_room_0_shapeDListsEntry[1] = {
    { carve_collision_test_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal carve_collision_test_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(carve_collision_test_room_0_shapeDListsEntry),
    carve_collision_test_room_0_shapeDListsEntry,
    carve_collision_test_room_0_shapeDListsEntry + ARRAY_COUNT(carve_collision_test_room_0_shapeDListsEntry)
};
