#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx lumbridge_settlement_x3_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry lumbridge_settlement_x3_room_0_shapeDListsEntry[1] = {
    { lumbridge_settlement_x3_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal lumbridge_settlement_x3_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(lumbridge_settlement_x3_room_0_shapeDListsEntry),
    lumbridge_settlement_x3_room_0_shapeDListsEntry,
    lumbridge_settlement_x3_room_0_shapeDListsEntry + ARRAY_COUNT(lumbridge_settlement_x3_room_0_shapeDListsEntry)
};
