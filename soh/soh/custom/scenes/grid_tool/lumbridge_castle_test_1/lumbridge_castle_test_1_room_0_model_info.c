#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx lumbridge_castle_test_1_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry lumbridge_castle_test_1_room_0_shapeDListsEntry[1] = {
    { lumbridge_castle_test_1_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal lumbridge_castle_test_1_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(lumbridge_castle_test_1_room_0_shapeDListsEntry),
    lumbridge_castle_test_1_room_0_shapeDListsEntry,
    lumbridge_castle_test_1_room_0_shapeDListsEntry + ARRAY_COUNT(lumbridge_castle_test_1_room_0_shapeDListsEntry)
};
