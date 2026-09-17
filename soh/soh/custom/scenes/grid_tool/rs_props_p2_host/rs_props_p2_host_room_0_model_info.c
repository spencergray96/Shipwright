// HAND-MERGED COPY for sturdy-bassoon#78 (RS export P2b). Copied from the grid-tool scene
// grid_test_map_10 and extended by docs/test-runs/2026-09-17-rs-export-p2-props/merge_props.py
// with RS props from fast64-export/rs_props_p2. Not owned by any grid-tool project: no
// re-export touches it. Re-run the script rather than editing by hand.
#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx rs_props_p2_host_room_0_shapeHeader_entry_0_opaque[];
extern Gfx rs_church_pew_5788_opaque_dl[];
extern Gfx rs_stacked_barrels_4641_vcolonly_opaque_dl[];
extern Gfx rs_stacked_barrels_4641_opaque_dl[];

RoomShapeDListsEntry rs_props_p2_host_room_0_shapeDListsEntry[4] = {
    { rs_props_p2_host_room_0_shapeHeader_entry_0_opaque, NULL },
    { rs_church_pew_5788_opaque_dl, NULL },
    { rs_stacked_barrels_4641_vcolonly_opaque_dl, NULL },
    { rs_stacked_barrels_4641_opaque_dl, NULL },
};

RoomShapeNormal rs_props_p2_host_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(rs_props_p2_host_room_0_shapeDListsEntry),
    rs_props_p2_host_room_0_shapeDListsEntry,
    rs_props_p2_host_room_0_shapeDListsEntry + ARRAY_COUNT(rs_props_p2_host_room_0_shapeDListsEntry)
};
