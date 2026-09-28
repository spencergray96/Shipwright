#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx step_warp_fixture_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry step_warp_fixture_room_0_shapeDListsEntry[1] = {
    { step_warp_fixture_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal step_warp_fixture_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(step_warp_fixture_room_0_shapeDListsEntry),
    step_warp_fixture_room_0_shapeDListsEntry,
    step_warp_fixture_room_0_shapeDListsEntry + ARRAY_COUNT(step_warp_fixture_room_0_shapeDListsEntry)
};
