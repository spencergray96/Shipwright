#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx hands_climb_179_bench_copy_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry hands_climb_179_bench_copy_room_0_shapeDListsEntry[1] = {
    { hands_climb_179_bench_copy_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal hands_climb_179_bench_copy_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(hands_climb_179_bench_copy_room_0_shapeDListsEntry),
    hands_climb_179_bench_copy_room_0_shapeDListsEntry,
    hands_climb_179_bench_copy_room_0_shapeDListsEntry + ARRAY_COUNT(hands_climb_179_bench_copy_room_0_shapeDListsEntry)
};
