#ifndef FRAMEBUFFER_EFFECTS_H
#define FRAMEBUFFER_EFFECTS_H

#include <libultraship/libultra.h>

extern s32 gPauseFrameBuffer;
extern s32 gBlurFrameBuffer;
extern s32 gReusableFrameBuffer;
extern s32 gN64ResFrameBuffer;
// The sky as it stands just before the rooms draw, for sky-colour fog (sturdy-bassoon#226; WorldFog.h)
extern s32 gSkyFogFrameBuffer;
// One texel: the sky's colour just above the horizon, averaged from that copy on the GPU (sturdy-bassoon#229)
extern s32 gSkyColourFrameBuffer;

void FB_CreateFramebuffers(void);
void FB_CopyToFramebuffer(Gfx** gfxp, s32 fb_src, s32 fb_dest, u8 oncePerFrame, u8* hasCopied);
void FB_WriteFramebufferSliceToCPU(Gfx** gfxp, void* buffer, u8 byteSwap);
void FB_DrawFromFramebuffer(Gfx** gfxp, s32 fb, u8 alpha);
void FB_DrawFromFramebufferScaled(Gfx** gfxp, s32 fb, u8 alpha, float scaleX, float scaleY);

#endif // FRAMEBUFFER_EFFECTS_H
