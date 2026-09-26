#include "../CustomSceneData.h"

extern Gfx vert_probe_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry vert_probe_room_0_shapeDListsEntry[1] = {
	{ vert_probe_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal vert_probe_room_0_shapeHeader = {
	ROOM_SHAPE_TYPE_NORMAL,
	ARRAY_COUNT(vert_probe_room_0_shapeDListsEntry),
	vert_probe_room_0_shapeDListsEntry,
	vert_probe_room_0_shapeDListsEntry + ARRAY_COUNT(vert_probe_room_0_shapeDListsEntry)
};
