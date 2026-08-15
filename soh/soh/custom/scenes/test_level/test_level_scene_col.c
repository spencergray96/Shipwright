#include "test_level_scene_data.h"

SurfaceType test_level_scene_polygonTypes[2] = {
    { SURFACETYPE0(0, 0, 0x00, 0, 0x00, 0x00, 0, 0), SURFACETYPE1(0x08, 0x00, 0, 0, 0, 0, 0, 0) },
    { SURFACETYPE0(0, 0, 0x00, 0, 0x00, 0x00, 0, 0), SURFACETYPE1(0x00, 0x00, 0, 0, 0, 0, 0, 0) },
};

Vec3s test_level_scene_vertices[8] = {
    {  -2000,      0,   2000 },
    {   2000,      0,   2000 },
    {   2000,      0,  -2000 },
    {  -2000,      0,  -2000 },
    {   2000,    500,  -2000 },
    {   2000,    500,   2000 },
    {  -2000,    500,   2000 },
    {  -2000,    500,  -2000 },
};

CollisionPoly test_level_scene_polygons[10] = {
    // Floor (normals point up — correct as-is)
    { 0, COLPOLY_VTX(0, COLPOLY_IGNORE_NONE), COLPOLY_VTX(1, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(2), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(7.549790126404332e-08) }, 0 },
    { 0, COLPOLY_VTX(0, COLPOLY_IGNORE_NONE), COLPOLY_VTX(2, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(3), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(7.549790126404332e-08) }, 0 },
    // East wall (x=+2000): normal flipped to point inward (-x), dist = +2000
    { 1, COLPOLY_VTX(1, COLPOLY_IGNORE_NONE), COLPOLY_VTX(2, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(4), { COLPOLY_SNORMAL(-1.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0) }, 2000 },
    { 1, COLPOLY_VTX(1, COLPOLY_IGNORE_NONE), COLPOLY_VTX(4, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(5), { COLPOLY_SNORMAL(-1.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0) }, 2000 },
    // West wall (x=-2000): normal flipped to point inward (+x), dist = +2000
    { 1, COLPOLY_VTX(3, COLPOLY_IGNORE_NONE), COLPOLY_VTX(0, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(6), { COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0) }, 2000 },
    { 1, COLPOLY_VTX(3, COLPOLY_IGNORE_NONE), COLPOLY_VTX(6, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(7), { COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0) }, 2000 },
    // South wall (z=-2000): normal flipped to point inward (+z), dist = +2000
    { 1, COLPOLY_VTX(2, COLPOLY_IGNORE_NONE), COLPOLY_VTX(3, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(7), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(7.549790126404332e-08), COLPOLY_SNORMAL(1.0) }, 2000 },
    { 1, COLPOLY_VTX(2, COLPOLY_IGNORE_NONE), COLPOLY_VTX(7, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(4), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(7.549790126404332e-08), COLPOLY_SNORMAL(1.0) }, 2000 },
    // North wall (z=+2000): normal flipped to point inward (-z), dist = +2000
    { 1, COLPOLY_VTX(0, COLPOLY_IGNORE_NONE), COLPOLY_VTX(1, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(5), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(-7.549790126404332e-08), COLPOLY_SNORMAL(-1.0) }, 2000 },
    { 1, COLPOLY_VTX(0, COLPOLY_IGNORE_NONE), COLPOLY_VTX(5, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(6), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(-7.549790126404332e-08), COLPOLY_SNORMAL(-1.0) }, 2000 },
};

CamData test_level_camData[] = {
    { CAM_SET_NORMAL0, 0, NULL },
};

CollisionHeader test_level_scene_collisionHeader = {
    { -2000, 0, -2000 },
    { 2000, 500, 2000 },
    ARRAY_COUNT(test_level_scene_vertices), test_level_scene_vertices,
    ARRAY_COUNT(test_level_scene_polygons), test_level_scene_polygons,
    test_level_scene_polygonTypes,
    test_level_camData,
    0, NULL,
    ARRAY_COUNT(test_level_camData)
};

