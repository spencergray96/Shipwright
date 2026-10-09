#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx storey_heights_in_game_178_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry storey_heights_in_game_178_room_0_shapeDListsEntry[1] = {
    { storey_heights_in_game_178_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal storey_heights_in_game_178_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(storey_heights_in_game_178_room_0_shapeDListsEntry),
    storey_heights_in_game_178_room_0_shapeDListsEntry,
    storey_heights_in_game_178_room_0_shapeDListsEntry + ARRAY_COUNT(storey_heights_in_game_178_room_0_shapeDListsEntry)
};
