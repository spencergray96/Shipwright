#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx kind_bench_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry kind_bench_room_0_shapeDListsEntry[1] = {
    { kind_bench_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal kind_bench_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(kind_bench_room_0_shapeDListsEntry),
    kind_bench_room_0_shapeDListsEntry,
    kind_bench_room_0_shapeDListsEntry + ARRAY_COUNT(kind_bench_room_0_shapeDListsEntry)
};
