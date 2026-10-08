#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx world_export_173_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry world_export_173_room_0_shapeDListsEntry[1] = {
    { world_export_173_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal world_export_173_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(world_export_173_room_0_shapeDListsEntry),
    world_export_173_room_0_shapeDListsEntry,
    world_export_173_room_0_shapeDListsEntry + ARRAY_COUNT(world_export_173_room_0_shapeDListsEntry)
};
