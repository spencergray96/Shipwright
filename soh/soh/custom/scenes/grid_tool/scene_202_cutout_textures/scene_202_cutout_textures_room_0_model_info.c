#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx scene_202_cutout_textures_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry scene_202_cutout_textures_room_0_shapeDListsEntry[1] = {
    { scene_202_cutout_textures_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal scene_202_cutout_textures_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(scene_202_cutout_textures_room_0_shapeDListsEntry),
    scene_202_cutout_textures_room_0_shapeDListsEntry,
    scene_202_cutout_textures_room_0_shapeDListsEntry + ARRAY_COUNT(scene_202_cutout_textures_room_0_shapeDListsEntry)
};
