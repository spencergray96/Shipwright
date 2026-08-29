#include "../CustomSceneData.h"

// Before #41 these went through a SURFACETYPE1 that shifted the material nibble to bits
// 27:24, where SurfaceType_GetSfx never looks and bit 27 is SurfaceType_IsWallDamage - so the
// two 0x08 (grass) types below played no footstep sound and carried a spurious wall-damage
// flag. The conveyor fields were unaffected and still decode to speed 2, direction 48.
SurfaceType test_level_scene_polygonTypes[3] = {
    { SURFACETYPE0(0, 0, 0x00, 0, 0x00, 0x00, 0, 0), SURFACETYPE1(0x08, 0x00, 0, 0, 0, 0, 0, 0) },
    { SURFACETYPE0(0, 0, 0x00, 0, 0x00, 0x00, 0, 0), SURFACETYPE1(0x00, 0x00, 0, 0, 0, 0, 0, 0) },
    // Floor conveyor (issue #25 rig): speed 2 = sFloorConveyorSpeeds[1] = 1.0 unit/frame, direction 48 = -X.
    { SURFACETYPE0(0, 0, 0x00, 0, 0x00, 0x00, 0, 0), SURFACETYPE1(0x08, 0, 0, 0, 0, 2, 48, 0) },
};

Vec3s test_level_scene_vertices[24] = {
    // Box corners: floor (0-3) and wall tops (4-7)
    {  -2000,      0,   2000 },
    {   2000,      0,   2000 },
    {   2000,      0,  -2000 },
    {  -2000,      0,  -2000 },
    {   2000,    500,  -2000 },
    {   2000,    500,   2000 },
    {  -2000,    500,   2000 },
    {  -2000,    500,  -2000 },
    // Floor split line at z=1200 and the NE conveyor pad corner (issue #25 rig)
    {  -2000,      0,   1200 },
    {   2000,      0,   1200 },
    {   1200,      0,   1200 },
    {   1200,      0,   2000 },
    // Control panel (no flags), x -1550..-1250 on the z=500 plane
    {  -1550,      0,    500 },
    {  -1250,      0,    500 },
    {  -1250,    200,    500 },
    {  -1550,    200,    500 },
    // IGNORE_ENTITY panel, x -1050..-750
    {  -1050,      0,    500 },
    {   -750,      0,    500 },
    {   -750,    200,    500 },
    {  -1050,    200,    500 },
    // IGNORE_PROJECTILES panel, x -550..-250
    {   -550,      0,    500 },
    {   -250,      0,    500 },
    {   -250,    200,    500 },
    {   -550,    200,    500 },
};

CollisionPoly test_level_scene_polygons[26] = {
    // Floor, minus the NE conveyor pad: an L of two rects (normals point up — correct as-is)
    // Rect A: x -2000..2000, z -2000..1200
    { 0, COLPOLY_VTX(8, COLPOLY_IGNORE_NONE), COLPOLY_VTX(9, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(2), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(7.549790126404332e-08) }, 0 },
    { 0, COLPOLY_VTX(8, COLPOLY_IGNORE_NONE), COLPOLY_VTX(2, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(3), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(7.549790126404332e-08) }, 0 },
    // Rect B: x -2000..1200, z 1200..2000
    { 0, COLPOLY_VTX(0, COLPOLY_IGNORE_NONE), COLPOLY_VTX(11, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(10), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(7.549790126404332e-08) }, 0 },
    { 0, COLPOLY_VTX(0, COLPOLY_IGNORE_NONE), COLPOLY_VTX(10, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(8), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(7.549790126404332e-08) }, 0 },
    // Conveyor pad: x 1200..2000, z 1200..2000, type 2, conveyor flag on vIB (issue #25 rig)
    { 2, COLPOLY_VTX(11, COLPOLY_IGNORE_NONE), COLPOLY_VTX(1, COLPOLY_VIB_CONVEYOR), COLPOLY_VTX_INDEX(9), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(7.549790126404332e-08) }, 0 },
    { 2, COLPOLY_VTX(11, COLPOLY_IGNORE_NONE), COLPOLY_VTX(9, COLPOLY_VIB_CONVEYOR), COLPOLY_VTX_INDEX(10), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(7.549790126404332e-08) }, 0 },
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
    // Issue #25 rig panels: free-standing walls on the z=500 plane, double-sided (front faces spawn at -z).
    // The ignore flags live on the first vtx word (vIA) — that is the word COLPOLY_VIA_FLAG_TEST reads.
    // Control panel: no flags — Link bumps into it, arrows stick
    { 1, COLPOLY_VTX(12, COLPOLY_IGNORE_NONE), COLPOLY_VTX(13, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(14), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(-1.0) }, 500 },
    { 1, COLPOLY_VTX(12, COLPOLY_IGNORE_NONE), COLPOLY_VTX(14, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(15), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(-1.0) }, 500 },
    { 1, COLPOLY_VTX(13, COLPOLY_IGNORE_NONE), COLPOLY_VTX(12, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(15), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0) }, -500 },
    { 1, COLPOLY_VTX(13, COLPOLY_IGNORE_NONE), COLPOLY_VTX(15, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(14), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0) }, -500 },
    // IGNORE_ENTITY panel: Link walks through, arrows stick
    { 1, COLPOLY_VTX(16, COLPOLY_IGNORE_ENTITY), COLPOLY_VTX(17, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(18), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(-1.0) }, 500 },
    { 1, COLPOLY_VTX(16, COLPOLY_IGNORE_ENTITY), COLPOLY_VTX(18, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(19), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(-1.0) }, 500 },
    { 1, COLPOLY_VTX(17, COLPOLY_IGNORE_ENTITY), COLPOLY_VTX(16, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(19), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0) }, -500 },
    { 1, COLPOLY_VTX(17, COLPOLY_IGNORE_ENTITY), COLPOLY_VTX(19, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(18), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0) }, -500 },
    // IGNORE_PROJECTILES panel: Link bumps into it, arrows pass through
    { 1, COLPOLY_VTX(20, COLPOLY_IGNORE_PROJECTILES), COLPOLY_VTX(21, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(22), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(-1.0) }, 500 },
    { 1, COLPOLY_VTX(20, COLPOLY_IGNORE_PROJECTILES), COLPOLY_VTX(22, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(23), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(-1.0) }, 500 },
    { 1, COLPOLY_VTX(21, COLPOLY_IGNORE_PROJECTILES), COLPOLY_VTX(20, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(23), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0) }, -500 },
    { 1, COLPOLY_VTX(21, COLPOLY_IGNORE_PROJECTILES), COLPOLY_VTX(23, COLPOLY_IGNORE_NONE), COLPOLY_VTX_INDEX(22), { COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(0.0), COLPOLY_SNORMAL(1.0) }, -500 },
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
