#include "../GridToolSceneData.h"

// Forward declarations for cross-file and same-file references
extern Gfx rimmington_church_265_agent_room_0_shapeHeader_entry_0_opaque[];

RoomShapeDListsEntry rimmington_church_265_agent_room_0_shapeDListsEntry[1] = {
    { rimmington_church_265_agent_room_0_shapeHeader_entry_0_opaque, NULL }
};

RoomShapeNormal rimmington_church_265_agent_room_0_shapeHeader = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(rimmington_church_265_agent_room_0_shapeDListsEntry),
    rimmington_church_265_agent_room_0_shapeDListsEntry,
    rimmington_church_265_agent_room_0_shapeDListsEntry + ARRAY_COUNT(rimmington_church_265_agent_room_0_shapeDListsEntry)
};
