#ifndef SOH_RS_ITEM_ART_H
#define SOH_RS_ITEM_ART_H

#include <stdint.h>

#include "align_asset_macro.h"

// Per-item artwork for the quest-item actor (sturdy-bassoon#58 P6 / #82, D19).
//
// WHAT THIS IS. One table mapping a (questId, step) pair to a texture, so three items of one quest
// look like three different things without three actors. RsQuestItem_Draw asks this table; a pair
// with no entry gets NULL and keeps drawing the gameplay_keep display list it always did, which is
// why adding art to one quest cannot change how any other quest's items look.
//
// WHY THE SYMBOLS ARE STRINGS. A texture is addressed by its archive path, not by a pointer to
// bytes in the executable. The name below is handed straight to gDPLoadTextureBlock, and the
// renderer recognises the "__OTR__" prefix and resolves it through the resource manager
// (gfx_set_timg_handler_rdp, libultraship/src/fast/interpreter.cpp). This is the same idiom as
// soh/assets/soh_assets.h; it is repeated here rather than added there so that no upstream file is
// edited (D6).
//
// ALIGN_ASSET(2) IS LOAD-BEARING, not decoration. gfx_check_image_signature refuses to dereference
// an ODD address and returns 0 for one, so an unaligned string would be taken for a raw pixel
// pointer and drawn as garbage. Every asset symbol in the tree carries it for this reason.
//
// WHERE THE PNGS LIVE. soh/assets/custom/objects/rs/<name>.rgba32.png, packed into soh.o2r by
// soh-o2r-packer at the GenerateSohOtr target. THE ARCHIVE IS NOT REBUILT BY A NORMAL BUILD AND IS
// NOT COPIED NEXT TO soh.exe BY ANYTHING - see docs/reference/ASSET_PIPELINE.md in sturdy-bassoon
// for the four-step recipe and for what a missed copy looks like on screen (garbage, not the old
// art).

#define dgRsItemEggTex "__OTR__objects/rs/gRsItemEggTex"
static const ALIGN_ASSET(2) char gRsItemEggTex[] = dgRsItemEggTex;

#define dgRsItemMilkTex "__OTR__objects/rs/gRsItemMilkTex"
static const ALIGN_ASSET(2) char gRsItemMilkTex[] = dgRsItemMilkTex;

#define dgRsItemFlourTex "__OTR__objects/rs/gRsItemFlourTex"
static const ALIGN_ASSET(2) char gRsItemFlourTex[] = dgRsItemFlourTex;

// Every sprite is 32x32 RGBA16 and every one of them has to be, because RsQuestItem_Draw hands it
// to VANILLA's gItemDropDL - the display list every recovery heart and deku nut in the game is
// drawn on - and that display list has 32x32 RGBA16 baked into its own tile setup. A texture of
// another size or format tiles, crops or garbles with no error anywhere. The PNGs are therefore
// named `.rgb5a1.png`, which is the packer's token for RGBA16; its 1-bit alpha costs nothing here
// because the sprites are authored with binary alpha anyway (the render mode is a cutout, not a
// blend).
#define RS_ITEM_ART_SIZE 32

// One row of the art table as the console surfaces see it.
typedef struct RsItemArtInfo {
    int32_t questId;
    int32_t step;
    const char* name; // a token for logs and markers, never shown to a player
    const char* tex;
    int32_t resolved; // 1 when the boot check found the texture; meaningless until `checked`
} RsItemArtInfo;

#ifdef __cplusplus
extern "C" {
#endif

// The texture for one quest item, or NULL when that (quest, step) has no art of its own and should
// keep the shared fallback model. Never asserts: an unknown pair is the normal answer, not a bug.
const char* RsItemArt_Texture(int32_t questId, int32_t step);

// The table, for the `quest itemart` console surface. Fills `out` and returns 1, or returns 0 for
// an index outside [0, RsItemArt_Count).
int32_t RsItemArt_Count(void);
int32_t RsItemArt_Get(int32_t index, RsItemArtInfo* out);

// How many textures the boot check could not resolve, and WHETHER IT RAN AT ALL. The second is a
// separate question on purpose: "checked and found everything" and "never checked" both report
// zero missing, and only one of them is evidence.
int32_t RsItemArt_MissingCount(void);
int32_t RsItemArt_Checked(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_ITEM_ART_H
