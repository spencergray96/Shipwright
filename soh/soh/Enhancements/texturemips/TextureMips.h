#ifndef SOH_TEXTURE_MIPS_H
#define SOH_TEXTURE_MIPS_H

#include "z64.h"
#include "soh/cvar_prefixes.h"

// Mipmaps for this mod's own scene textures (sturdy-bassoon#146): the grid tool's textures and the
// imported RS props. Without them a distant texture crawls as the camera moves, because each pixel
// picks one texel out of many. libultraship builds the chain at upload and the three-point shader
// picks and blends two levels by distance (fast/TextureMips.h).
//
// Which textures are "ours" is decided by where they are drawn from: every raw texture a compiled-in
// room's display lists load. Vanilla scenes and texture packs are untouched - their rooms never
// reach TextureMips_RegisterRoom, and an archive texture never joins the set anyway.
//
// The saved setting behind the switch: 1 mipmaps, 0 uploads one level as before. On by default.
#define CVAR_TEXTURE_MIPS CVAR_SETTING("CustomSceneMipmaps")
#define TEXTURE_MIPS_DEFAULT 1
// How the shader picks a level, and a bias added to it (fast/TextureMips.h TextureMipsLodMode):
// 0 max (the first choice, softest on grazing walls), 1 mean, 2 aniso. Saved by the human
// `mipmaps lod` and `mipmaps bias`. Aniso by default: the owner chose it at the furnace-wall view,
// and on the 4.1M F2P map it costs nothing measurable while crawling least of the three (the #146
// run's "Follow-up: the level choice"). Mean crawls on the castle walls.
#define CVAR_TEXTURE_MIPS_LOD CVAR_SETTING("CustomSceneMipmapsLod")
#define CVAR_TEXTURE_MIPS_BIAS CVAR_SETTING("CustomSceneMipmapsBias")
#define TEXTURE_MIPS_LOD_DEFAULT 2 // Fast::TEXTURE_MIPS_LOD_ANISO; this header is C, the enum C++

#ifdef __cplusplus
extern "C" {
#endif

// Name a freshly initialised compiled-in room's display lists, opaque and translucent, as the
// mod's own. Called from the same compiled-in branch of the room load as StaticBake_RegisterRoom,
// which is the whole of the vanilla-safety argument.
void TextureMips_RegisterRoom(PlayState* play, RoomContext* roomCtx);

//   TextureMips_SetActive   moves the switch for this session only (`agenttest mipmaps on|off`)
//   TextureMips_SetSetting  saves CVAR_TEXTURE_MIPS and moves the switch (the human `mipmaps on|off`)
//   TextureMips_IsActive    whether mips are built: the switch is on AND the backend builds them
//   TextureMips_Setting     the saved setting
//   TextureMips_BackendSupported  whether this rendering backend builds them (DX11 only)
// A change clears the texture cache and re-records every bake, so both paths redraw at once.
void TextureMips_SetActive(int active);
void TextureMips_SetSetting(int active);
// The level choice. save=1 writes the CVars (the human command), 0 is for this session only.
void TextureMips_SetLod(int mode, float bias, int save);
int TextureMips_IsActive(void);
int TextureMips_Setting(void);
int TextureMips_BackendSupported(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_TEXTURE_MIPS_H
