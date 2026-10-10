#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx f5_cellar_173_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry f5_cellar_173_room_0_shapeDListsEntry[1] = {
    { f5_cellar_173_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal f5_cellar_173_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(f5_cellar_173_room_0_shapeDListsEntry),
    f5_cellar_173_room_0_shapeDListsEntry,
    f5_cellar_173_room_0_shapeDListsEntry + ARRAY_COUNT(f5_cellar_173_room_0_shapeDListsEntry)
};
