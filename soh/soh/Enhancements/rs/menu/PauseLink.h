#ifndef SOH_RS_MENU_PAUSE_LINK_H
#define SOH_RS_MENU_PAUSE_LINK_H

#include <stdint.h>

// Link's 3D portrait for the scroll's Equipment page (sturdy-bassoon#111 stage 8). VANILLA'S CODE, NOT
// ITS MEMORY TRICK (Spencer, 2026-09-21): kaleido carves pause-Link's buffer out of the scene's object
// memory when pause opens (z_kaleido_scope_PAL.c:3884), which is safe only because it has photographed
// the world and stops drawing it, and it reloads every object on close. The scroll draws the live world
// behind it, so this owns a long-lived buffer instead and calls vanilla's loader (func_80091738) and
// Player_DrawPause unchanged into SoH's pause-Link framebuffer, exactly as KaleidoScope_DrawPlayerWork
// does. Both repoint gSegments[4] and [6] - the segments the world's own draw resolves gameplay_keep
// and the current object through - and vanilla never puts them back; this saves and restores both
// around the load and around the draw.
//
// In SoH the loader's DMA is a stub (DmaMgr_SendRequest1 returns 0) and gObjectTable's ROM ranges are
// all zero, so no bytes are copied: Link's model comes from resources by path, and the buffer only
// carries the skeleton's joint table at +0x8800. The buffer is sized for kaleido's layout anyway.
//
// No z64.h here: MenuConsole.cpp includes this for its dump line.

// Drop the loaded skeleton, so the next render loads it again - on every menu open, as kaleido loads
// it on every pause, and so an age change between opens gets the other Link.
void RsPauseLink_Invalidate();

// Render pause-Link into the framebuffer this frame. From RsMenu_OnPlayDrawEnd only, outside every
// menu interpolation node; the draw it emits is WORK_DISP, which runs before any pool the composite is
// in. `play` is a PlayState*.
void RsPauseLink_Render(void* play);

// The CPU buffer RsMenu_DrawPauseLink names as the texture image before swapping in the framebuffer,
// the way KaleidoScope_DrawEquipmentImage names its playerSegment.
const void* RsPauseLink_Buffer();

// The pause-Link framebuffer's id (gPauseLinkFrameBuffer), -1 before the game creates it.
int32_t RsPauseLink_FrameBuffer();

// What `menu dump`'s `section=link` line reports. `seg4`/`seg6` are the world's segment addresses as
// they stood before the last render, `seg4Clobbered`/`seg6Clobbered` what Player_DrawPause had left in
// them before the restore - a render that did NOT move them would make the restore unproven, so both
// are printed - and `seg4Now`/`seg6Now` what they hold when the dump is read, between frames.
struct RsPauseLinkStatus {
    int32_t loads;
    int32_t renders;
    int32_t lastRenderFrame; // RsMenuStatus::drawFrames of the last render, -1 before any
    uint32_t loadSize;       // func_80091738's return value
    int32_t frameBuffer;
    int32_t age;             // the linkAge the skeleton was loaded for, -1 before any load
    uintptr_t seg4, seg6;
    uintptr_t seg4Clobbered, seg6Clobbered;
    uintptr_t seg4Now, seg6Now;
};
RsPauseLinkStatus RsPauseLink_Status();

#endif // SOH_RS_MENU_PAUSE_LINK_H
