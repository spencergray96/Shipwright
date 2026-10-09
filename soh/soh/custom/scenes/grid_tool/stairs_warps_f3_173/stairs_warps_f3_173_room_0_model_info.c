#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx stairs_warps_f3_173_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry stairs_warps_f3_173_room_0_shapeDListsEntry[1] = {
    { stairs_warps_f3_173_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal stairs_warps_f3_173_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(stairs_warps_f3_173_room_0_shapeDListsEntry),
    stairs_warps_f3_173_room_0_shapeDListsEntry,
    stairs_warps_f3_173_room_0_shapeDListsEntry + ARRAY_COUNT(stairs_warps_f3_173_room_0_shapeDListsEntry)
};
