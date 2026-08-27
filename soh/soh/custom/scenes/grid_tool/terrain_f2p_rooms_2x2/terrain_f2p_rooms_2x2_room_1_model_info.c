#include "terrain_f2p_rooms_2x2_scene_data.h"

RoomShapeNormal terrain_f2p_rooms_2x2_room_1_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(terrain_f2p_rooms_2x2_room_1_shapeDListsEntry),
    terrain_f2p_rooms_2x2_room_1_shapeDListsEntry,
    terrain_f2p_rooms_2x2_room_1_shapeDListsEntry + ARRAY_COUNT(terrain_f2p_rooms_2x2_room_1_shapeDListsEntry)
};

RoomShapeDListsEntry terrain_f2p_rooms_2x2_room_1_shapeDListsEntry[1] = {
    { terrain_f2p_rooms_2x2_room_1_shapeHeader_entry_0_opaque, NULL }
};
