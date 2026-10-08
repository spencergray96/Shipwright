#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx scene_203_still_torch_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry scene_203_still_torch_room_0_shapeDListsEntry[1] = {
    { scene_203_still_torch_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal scene_203_still_torch_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(scene_203_still_torch_room_0_shapeDListsEntry),
    scene_203_still_torch_room_0_shapeDListsEntry,
    scene_203_still_torch_room_0_shapeDListsEntry + ARRAY_COUNT(scene_203_still_torch_room_0_shapeDListsEntry)
};
