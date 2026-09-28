#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx underground_entry_basements_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry underground_entry_basements_room_0_shapeDListsEntry[1] = {
    { underground_entry_basements_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal underground_entry_basements_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(underground_entry_basements_room_0_shapeDListsEntry),
    underground_entry_basements_room_0_shapeDListsEntry,
    underground_entry_basements_room_0_shapeDListsEntry + ARRAY_COUNT(underground_entry_basements_room_0_shapeDListsEntry)
};
