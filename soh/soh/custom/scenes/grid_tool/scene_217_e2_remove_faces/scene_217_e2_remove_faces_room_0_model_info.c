#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx scene_217_e2_remove_faces_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry scene_217_e2_remove_faces_room_0_shapeDListsEntry[1] = {
    { scene_217_e2_remove_faces_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal scene_217_e2_remove_faces_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(scene_217_e2_remove_faces_room_0_shapeDListsEntry),
    scene_217_e2_remove_faces_room_0_shapeDListsEntry,
    scene_217_e2_remove_faces_room_0_shapeDListsEntry + ARRAY_COUNT(scene_217_e2_remove_faces_room_0_shapeDListsEntry)
};
