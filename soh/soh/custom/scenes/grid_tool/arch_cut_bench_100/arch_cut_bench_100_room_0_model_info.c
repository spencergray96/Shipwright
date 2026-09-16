#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx arch_cut_bench_100_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry arch_cut_bench_100_room_0_shapeDListsEntry[1] = {
    { arch_cut_bench_100_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal arch_cut_bench_100_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(arch_cut_bench_100_room_0_shapeDListsEntry),
    arch_cut_bench_100_room_0_shapeDListsEntry,
    arch_cut_bench_100_room_0_shapeDListsEntry + ARRAY_COUNT(arch_cut_bench_100_room_0_shapeDListsEntry)
};
