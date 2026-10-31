#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx tempor_tantrum_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry tempor_tantrum_room_0_shapeDListsEntry[1] = {
    { tempor_tantrum_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal tempor_tantrum_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(tempor_tantrum_room_0_shapeDListsEntry),
    tempor_tantrum_room_0_shapeDListsEntry,
    tempor_tantrum_room_0_shapeDListsEntry + ARRAY_COUNT(tempor_tantrum_room_0_shapeDListsEntry)
};
