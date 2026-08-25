#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx ledge_hop_retest_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry ledge_hop_retest_room_0_shapeDListsEntry[1] = {
    { ledge_hop_retest_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal ledge_hop_retest_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(ledge_hop_retest_room_0_shapeDListsEntry),
    ledge_hop_retest_room_0_shapeDListsEntry,
    ledge_hop_retest_room_0_shapeDListsEntry + ARRAY_COUNT(ledge_hop_retest_room_0_shapeDListsEntry)
};
