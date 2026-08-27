#include "terrain_f2p_rooms_2x2_scene_data.h"

Gfx mat_terrain_f2p_rooms_2x2_room_0_dl_grass_layerOpaque[] = {
	gsSPLoadGeometryMode(G_CULL_BACK | G_FOG | G_LIGHTING | G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER),
	gsDPPipeSync(),
	gsDPSetCombineLERP(0, 0, 0, SHADE, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
	gsSPSetOtherMode(G_SETOTHERMODE_H, 4, 20, G_AD_NOISE | G_CD_MAGICSQ | G_CK_NONE | G_CYC_2CYCLE | G_PM_1PRIMITIVE | G_TC_FILT | G_TD_CLAMP | G_TF_BILERP | G_TL_TILE | G_TP_PERSP | G_TT_NONE),
	gsSPSetOtherMode(G_SETOTHERMODE_L, 0, 32, G_AC_NONE | G_RM_AA_ZB_OPA_SURF2 | G_RM_FOG_SHADE_A | G_ZS_PIXEL),
	gsSPTexture(65535, 65535, 0, 0, 1),
	gsDPSetPrimColor(0, 0, 86, 130, 62, 255),
	gsSPEndDisplayList(),
};

Gfx mat_terrain_f2p_rooms_2x2_room_0_dl_path_layerOpaque[] = {
	gsSPLoadGeometryMode(G_CULL_BACK | G_FOG | G_LIGHTING | G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER),
	gsDPPipeSync(),
	gsDPSetCombineLERP(0, 0, 0, SHADE, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
	gsSPSetOtherMode(G_SETOTHERMODE_H, 4, 20, G_AD_NOISE | G_CD_MAGICSQ | G_CK_NONE | G_CYC_2CYCLE | G_PM_1PRIMITIVE | G_TC_FILT | G_TD_CLAMP | G_TF_BILERP | G_TL_TILE | G_TP_PERSP | G_TT_NONE),
	gsSPSetOtherMode(G_SETOTHERMODE_L, 0, 32, G_AC_NONE | G_RM_AA_ZB_OPA_SURF2 | G_RM_FOG_SHADE_A | G_ZS_PIXEL),
	gsSPTexture(65535, 65535, 0, 0, 1),
	gsDPSetPrimColor(0, 0, 150, 128, 92, 255),
	gsSPEndDisplayList(),
};

Gfx mat_terrain_f2p_rooms_2x2_room_0_dl_snow_layerOpaque[] = {
	gsSPLoadGeometryMode(G_CULL_BACK | G_FOG | G_LIGHTING | G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER),
	gsDPPipeSync(),
	gsDPSetCombineLERP(0, 0, 0, SHADE, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
	gsSPSetOtherMode(G_SETOTHERMODE_H, 4, 20, G_AD_NOISE | G_CD_MAGICSQ | G_CK_NONE | G_CYC_2CYCLE | G_PM_1PRIMITIVE | G_TC_FILT | G_TD_CLAMP | G_TF_BILERP | G_TL_TILE | G_TP_PERSP | G_TT_NONE),
	gsSPSetOtherMode(G_SETOTHERMODE_L, 0, 32, G_AC_NONE | G_RM_AA_ZB_OPA_SURF2 | G_RM_FOG_SHADE_A | G_ZS_PIXEL),
	gsSPTexture(65535, 65535, 0, 0, 1),
	gsDPSetPrimColor(0, 0, 232, 238, 245, 255),
	gsSPEndDisplayList(),
};

Gfx mat_terrain_f2p_rooms_2x2_room_0_dl_edge_layerOpaque[] = {
	gsSPLoadGeometryMode(G_CULL_BACK | G_FOG | G_LIGHTING | G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER),
	gsDPPipeSync(),
	gsDPSetCombineLERP(0, 0, 0, SHADE, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED),
	gsSPSetOtherMode(G_SETOTHERMODE_H, 4, 20, G_AD_NOISE | G_CD_MAGICSQ | G_CK_NONE | G_CYC_2CYCLE | G_PM_1PRIMITIVE | G_TC_FILT | G_TD_CLAMP | G_TF_BILERP | G_TL_TILE | G_TP_PERSP | G_TT_NONE),
	gsSPSetOtherMode(G_SETOTHERMODE_L, 0, 32, G_AC_NONE | G_RM_AA_ZB_OPA_SURF2 | G_RM_FOG_SHADE_A | G_ZS_PIXEL),
	gsSPTexture(65535, 65535, 0, 0, 1),
	gsDPSetPrimColor(0, 0, 92, 90, 100, 255),
	gsSPEndDisplayList(),
};
