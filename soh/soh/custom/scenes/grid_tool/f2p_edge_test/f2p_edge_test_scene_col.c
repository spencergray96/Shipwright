#include "../GridToolSceneData.h"

SurfaceType f2p_edge_test_scene_polygonTypes[3] = {
    // 0: floor/roof/ground - light footstep sound
    { SURFACETYPE0(0, 0, 0x00, 0, 0x00, 0, 0, 0), SURFACETYPE1(0x08, 0, 0, 0, 0, 0, 0, 0) },
    // 1: walls
    { SURFACETYPE0(0, 0, 0x00, 0, 0x00, 0, 0, 0), SURFACETYPE1(0x00, 0, 0, 0, 0, 0, 0, 0) },
    // 2: no-climb prop walls (#109) - wall type 1 (WALL_FLAG_0): Link's ledge climb refuses it
    { SURFACETYPE0(0, 0, 0x00, 0, 0x01, 0, 0, 0), SURFACETYPE1(0x00, 0, 0, 0, 0, 0, 0, 0) },
};

Vec3s f2p_edge_test_scene_vertices[422134] = {
#include "f2p_edge_test_scene_col.001.inc"
};

CollisionPoly f2p_edge_test_scene_polygons[835649] = {
#include "f2p_edge_test_scene_col.002.inc"
#include "f2p_edge_test_scene_col.003.inc"
#include "f2p_edge_test_scene_col.004.inc"
#include "f2p_edge_test_scene_col.005.inc"
#include "f2p_edge_test_scene_col.006.inc"
#include "f2p_edge_test_scene_col.007.inc"
#include "f2p_edge_test_scene_col.008.inc"
#include "f2p_edge_test_scene_col.009.inc"
#include "f2p_edge_test_scene_col.010.inc"
#include "f2p_edge_test_scene_col.011.inc"
#include "f2p_edge_test_scene_col.012.inc"
};

CamData f2p_edge_test_camData[] = {
    { CAM_SET_NORMAL0, 0, NULL },
};

CollisionHeader f2p_edge_test_scene_collisionHeader = {
    { -12800, -100, -12800 },
    { 12800, 1270, 12800 },
    ARRAY_COUNT(f2p_edge_test_scene_vertices), f2p_edge_test_scene_vertices,
    ARRAY_COUNT(f2p_edge_test_scene_polygons), f2p_edge_test_scene_polygons,
    f2p_edge_test_scene_polygonTypes,
    f2p_edge_test_camData,
    0, NULL,
    ARRAY_COUNT(f2p_edge_test_camData)
};
