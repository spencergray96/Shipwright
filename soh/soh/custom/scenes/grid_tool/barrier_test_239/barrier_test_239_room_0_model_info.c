#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx barrier_test_239_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry barrier_test_239_room_0_shapeDListsEntry[1] = {
    { barrier_test_239_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal barrier_test_239_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(barrier_test_239_room_0_shapeDListsEntry),
    barrier_test_239_room_0_shapeDListsEntry,
    barrier_test_239_room_0_shapeDListsEntry + ARRAY_COUNT(barrier_test_239_room_0_shapeDListsEntry)
};
