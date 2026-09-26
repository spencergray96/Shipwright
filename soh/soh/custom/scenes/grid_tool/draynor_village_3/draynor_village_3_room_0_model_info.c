#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx draynor_village_3_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry draynor_village_3_room_0_shapeDListsEntry[1] = {
    { draynor_village_3_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal draynor_village_3_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(draynor_village_3_room_0_shapeDListsEntry),
    draynor_village_3_room_0_shapeDListsEntry,
    draynor_village_3_room_0_shapeDListsEntry + ARRAY_COUNT(draynor_village_3_room_0_shapeDListsEntry)
};
