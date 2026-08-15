#include "test_level_scene_data.h"

// Forward declarations for display lists referenced before their definitions
extern Gfx test_level_room_0_dl_Floor_mesh_layer_Opaque[];
extern Gfx test_level_room_0_dl_Walls_mesh_layer_Opaque[];

Gfx test_level_room_0_shapeHeader_entry_0_opaque[] = {
	gsSPDisplayList(test_level_room_0_dl_Floor_mesh_layer_Opaque),
	gsSPDisplayList(test_level_room_0_dl_Walls_mesh_layer_Opaque),
	gsSPEndDisplayList(),
};

Vtx test_level_room_0_dl_Floor_mesh_layer_Opaque_vtx_cull[8] = {
	{{ {-2000, 0, 2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {-2000, 0, 2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {-2000, 0, -2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {-2000, 0, -2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {2000, 0, 2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {2000, 0, 2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {2000, 0, -2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {2000, 0, -2000}, 0, {0, 0}, {0, 0, 0, 0} }},
};

Vtx test_level_room_0_dl_Floor_mesh_layer_Opaque_vtx_0[4] = {
	{{ {-2000, 0, 2000}, 0, {-16, 1008}, {0, 127, 0, 255} }},
	{{ {2000, 0, 2000}, 0, {1008, 1008}, {0, 127, 0, 255} }},
	{{ {2000, 0, -2000}, 0, {1008, -16}, {0, 127, 0, 255} }},
	{{ {-2000, 0, -2000}, 0, {-16, -16}, {0, 127, 0, 255} }},
};

Gfx test_level_room_0_dl_Floor_mesh_layer_Opaque_tri_0[] = {
	gsSPVertex(test_level_room_0_dl_Floor_mesh_layer_Opaque_vtx_0 + 0, 4, 0),
	gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
	gsSPEndDisplayList(),
};

Vtx test_level_room_0_dl_Walls_mesh_layer_Opaque_vtx_cull[8] = {
	{{ {-2000, 0, 2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {-2000, 500, 2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {-2000, 500, -2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {-2000, 0, -2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {2000, 0, 2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {2000, 500, 2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {2000, 500, -2000}, 0, {0, 0}, {0, 0, 0, 0} }},
	{{ {2000, 0, -2000}, 0, {0, 0}, {0, 0, 0, 0} }},
};

Vtx test_level_room_0_dl_Walls_mesh_layer_Opaque_vtx_0[16] = {
	{{ {2000, 0, 2000}, 0, {1008, 1008}, {127, 0, 0, 255} }},
	{{ {2000, 0, -2000}, 0, {1008, -16}, {127, 0, 0, 255} }},
	{{ {2000, 500, -2000}, 0, {1008, -16}, {127, 0, 0, 255} }},
	{{ {2000, 500, 2000}, 0, {1008, 1008}, {127, 0, 0, 255} }},
	{{ {-2000, 0, -2000}, 0, {-16, -16}, {129, 0, 0, 255} }},
	{{ {-2000, 0, 2000}, 0, {-16, 1008}, {129, 0, 0, 255} }},
	{{ {-2000, 500, 2000}, 0, {-16, 1008}, {129, 0, 0, 255} }},
	{{ {-2000, 500, -2000}, 0, {-16, -16}, {129, 0, 0, 255} }},
	{{ {2000, 0, -2000}, 0, {1008, -16}, {0, 0, 129, 255} }},
	{{ {-2000, 0, -2000}, 0, {-16, -16}, {0, 0, 129, 255} }},
	{{ {-2000, 500, -2000}, 0, {-16, -16}, {0, 0, 129, 255} }},
	{{ {2000, 500, -2000}, 0, {1008, -16}, {0, 0, 129, 255} }},
	{{ {-2000, 0, 2000}, 0, {-16, 1008}, {0, 0, 127, 255} }},
	{{ {2000, 0, 2000}, 0, {1008, 1008}, {0, 0, 127, 255} }},
	{{ {2000, 500, 2000}, 0, {1008, 1008}, {0, 0, 127, 255} }},
	{{ {-2000, 500, 2000}, 0, {-16, 1008}, {0, 0, 127, 255} }},
};

Gfx test_level_room_0_dl_Walls_mesh_layer_Opaque_tri_0[] = {
	gsSPVertex(test_level_room_0_dl_Walls_mesh_layer_Opaque_vtx_0 + 0, 16, 0),
	gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
	gsSP2Triangles(4, 5, 6, 0, 4, 6, 7, 0),
	gsSP2Triangles(8, 9, 10, 0, 8, 10, 11, 0),
	gsSP2Triangles(12, 13, 14, 0, 12, 14, 15, 0),
	gsSPEndDisplayList(),
};

Gfx mat_test_level_room_0_dl_Material_001_f3d_layerOpaque[] = {
	gsSPLoadGeometryMode(G_SHADE | G_SHADING_SMOOTH | G_LIGHTING | G_FOG | G_CULL_BACK | G_ZBUFFER),
	gsDPPipeSync(),
	gsDPSetCombineLERP(0, 0, 0, SHADE, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
	gsSPSetOtherMode(G_SETOTHERMODE_H, 4, 20, G_CYC_2CYCLE | G_TF_BILERP | G_TP_PERSP | G_TC_FILT | G_TD_CLAMP | G_CD_MAGICSQ | G_TL_TILE | G_PM_NPRIMITIVE | G_TT_NONE | G_AD_NOISE | G_CK_NONE),
	gsSPSetOtherMode(G_SETOTHERMODE_L, 0, 32, G_RM_FOG_SHADE_A | G_RM_AA_ZB_OPA_SURF2 | G_AC_NONE | G_ZS_PIXEL),
	gsSPTexture(65535, 65535, 0, 0, 1),
	gsDPSetPrimColor(0, 0, 57, 154, 38, 255),
	gsSPEndDisplayList(),
};

Gfx mat_test_level_room_0_dl_Material_002_f3d_layerOpaque[] = {
	gsSPLoadGeometryMode(G_SHADE | G_SHADING_SMOOTH | G_LIGHTING | G_FOG | G_CULL_BACK | G_ZBUFFER),
	gsDPPipeSync(),
	gsDPSetCombineLERP(0, 0, 0, SHADE, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
	gsSPSetOtherMode(G_SETOTHERMODE_H, 4, 20, G_CYC_2CYCLE | G_TF_BILERP | G_TP_PERSP | G_TC_FILT | G_TD_CLAMP | G_CD_MAGICSQ | G_TL_TILE | G_PM_NPRIMITIVE | G_TT_NONE | G_AD_NOISE | G_CK_NONE),
	gsSPSetOtherMode(G_SETOTHERMODE_L, 0, 32, G_RM_FOG_SHADE_A | G_RM_AA_ZB_OPA_SURF2 | G_AC_NONE | G_ZS_PIXEL),
	gsSPTexture(65535, 65535, 0, 0, 1),
	gsDPSetPrimColor(0, 0, 255, 255, 255, 255),
	gsSPEndDisplayList(),
};

Gfx test_level_room_0_dl_Floor_mesh_layer_Opaque[] = {
	gsSPClearGeometryMode(G_LIGHTING),
	gsSPVertex(test_level_room_0_dl_Floor_mesh_layer_Opaque_vtx_cull + 0, 8, 0),
	gsSPSetGeometryMode(G_LIGHTING),
	gsSPCullDisplayList(0, 7),
	gsSPDisplayList(mat_test_level_room_0_dl_Material_001_f3d_layerOpaque),
	gsSPDisplayList(test_level_room_0_dl_Floor_mesh_layer_Opaque_tri_0),
	gsSPEndDisplayList(),
};

Gfx test_level_room_0_dl_Walls_mesh_layer_Opaque[] = {
	gsSPClearGeometryMode(G_LIGHTING),
	gsSPVertex(test_level_room_0_dl_Walls_mesh_layer_Opaque_vtx_cull + 0, 8, 0),
	gsSPSetGeometryMode(G_LIGHTING),
	gsSPCullDisplayList(0, 7),
	gsSPDisplayList(mat_test_level_room_0_dl_Material_002_f3d_layerOpaque),
	gsSPDisplayList(test_level_room_0_dl_Walls_mesh_layer_Opaque_tri_0),
	gsSPEndDisplayList(),
};

