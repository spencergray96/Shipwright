#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx sounds_276_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry sounds_276_room_0_shapeDListsEntry[1] = {
    { sounds_276_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal sounds_276_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(sounds_276_room_0_shapeDListsEntry),
    sounds_276_room_0_shapeDListsEntry,
    sounds_276_room_0_shapeDListsEntry + ARRAY_COUNT(sounds_276_room_0_shapeDListsEntry)
};
