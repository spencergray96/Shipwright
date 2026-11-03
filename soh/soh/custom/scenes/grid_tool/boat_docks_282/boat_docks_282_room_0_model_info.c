#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx boat_docks_282_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry boat_docks_282_room_0_shapeDListsEntry[1] = {
    { boat_docks_282_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal boat_docks_282_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(boat_docks_282_room_0_shapeDListsEntry),
    boat_docks_282_room_0_shapeDListsEntry,
    boat_docks_282_room_0_shapeDListsEntry + ARRAY_COUNT(boat_docks_282_room_0_shapeDListsEntry)
};
