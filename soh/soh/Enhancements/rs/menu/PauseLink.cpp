/*
 * PauseLink.cpp - Link's portrait for the scroll's Equipment page (sturdy-bassoon#111 stage 8).
 * PauseLink.h has the design; this is KaleidoScope_DrawPlayerWork (z_kaleido_equipment.c:129-167) with
 * its own buffer and the segment restore vanilla never needed.
 *
 * OPEN_DISPS is used from a FILE-STATIC function, never from an anonymous namespace: inside one, MSVC
 * gives OPEN_DISPS's block-scope redeclaration of FrameInterpolation_RecordOpenChild C++ linkage and
 * the link fails (RsMenu.cpp's header has the full story).
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-21
 */

#include "PauseLink.h"

#include <cstdint>

#include <libultraship/bridge/consolevariablebridge.h>

#include "RsMenu.h"
#include "soh/cvar_prefixes.h"
#include "soh/frame_interpolation.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern int gPauseLinkFrameBuffer;
}

// Kaleido's layout inside the buffer: gameplay_keep at +0x3800, Link's object at +0x8800 and the joint
// table after it (func_80091738, z_player_lib.c:1996-2018). With SoH's zero-length ROM ranges the
// loader reports 0x8890 bytes; 0x9000 leaves room for the joint table's 16-byte alignment. 64-aligned
// like kaleido's carve (`& ~0x3F`).
alignas(64) static u8 sBuffer[0x9000];
static SkelAnime sSkelAnime;
static bool sLoaded = false;
static int32_t sLoadedAge = -1;
static int32_t sLoads = 0;
static int32_t sRenders = 0;
static int32_t sLastRenderFrame = -1;
static uint32_t sLoadSize = 0;
static uintptr_t sSeg4 = 0;
static uintptr_t sSeg6 = 0;
static uintptr_t sSeg4Clobbered = 0;
static uintptr_t sSeg6Clobbered = 0;

static void LoadPauseLink(PlayState* play) {
    const uintptr_t seg4 = gSegments[4];
    const uintptr_t seg6 = gSegments[6];
    sLoadSize = func_80091738(play, sBuffer, &sSkelAnime);
    gSegments[4] = seg4;
    gSegments[6] = seg6;
    sLoaded = true;
    sLoadedAge = gSaveContext.linkAge;
    sLoads++;
}

// KaleidoScope_DrawPlayerWork, verbatim but for the buffer and the restore: the three poses (child;
// adult without the Master Sword; adult with it), the framebuffer switch on WORK_DISP, and
// Player_DrawPause with the save's current sword, tunic, shield and boots.
static void DrawPauseLinkWork(PlayState* play) {
    Vec3f pos;
    Vec3s rot;
    f32 scale;

    if (LINK_AGE_IN_YEARS == YEARS_CHILD) {
        pos.x = 2.0f;
        pos.y = -130.0f;
        pos.z = -150.0f;
        scale = 0.046f;
    } else if (CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD) != EQUIP_VALUE_SWORD_MASTER &&
               !CVarGetInteger(CVAR_GENERAL("PauseMenuAnimatedLinkTriforce"), 0)) {
        pos.x = 25.0f;
        pos.y = -228.0f;
        pos.z = 60.0f;
        scale = 0.056f;
    } else {
        pos.x = 20.0f;
        pos.y = -180.0f;
        pos.z = -40.0f;
        scale = 0.047f;
    }
    rot.y = 32300;
    rot.x = rot.z = 0;

    const uintptr_t seg4 = gSegments[4];
    const uintptr_t seg6 = gSegments[6];

    OPEN_DISPS(play->state.gfxCtx);
    // Player_DrawPause SETS the matrix stack's top (Matrix_SetTranslateRotateYXZ) rather than pushing,
    // so the caller's matrix is kept out of its way - inside this node, so the push and pop are recorded
    // with everything else this node records, on exactly the frames it draws.
    Matrix_Push();
    gsSPSetFB(WORK_DISP++, gPauseLinkFrameBuffer);
    Player_DrawPause(play, sBuffer, &sSkelAnime, &pos, &rot, scale,
                     SWORD_EQUIP_TO_PLAYER(CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD)),
                     TUNIC_EQUIP_TO_PLAYER(CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC)),
                     SHIELD_EQUIP_TO_PLAYER(CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD)),
                     BOOTS_EQUIP_TO_PLAYER(CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS)));
    gsSPResetFB(WORK_DISP++);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);

    sSeg4Clobbered = gSegments[4];
    sSeg6Clobbered = gSegments[6];
    gSegments[4] = seg4;
    gSegments[6] = seg6;
    sSeg4 = seg4;
    sSeg6 = seg6;
}

void RsPauseLink_Invalidate() {
    sLoaded = false;
}

void RsPauseLink_Render(void* playArg) {
    PlayState* play = (PlayState*)playArg;
    if (play == nullptr || gPauseLinkFrameBuffer < 0) {
        return;
    }
    if (!sLoaded || sLoadedAge != gSaveContext.linkAge) {
        LoadPauseLink(play);
    }
    DrawPauseLinkWork(play);
    sRenders++;
    sLastRenderFrame = RsMenu_Status().drawFrames;
}

const void* RsPauseLink_Buffer() {
    return sBuffer;
}

int32_t RsPauseLink_FrameBuffer() {
    return gPauseLinkFrameBuffer;
}

RsPauseLinkStatus RsPauseLink_Status() {
    RsPauseLinkStatus status;
    status.loads = sLoads;
    status.renders = sRenders;
    status.lastRenderFrame = sLastRenderFrame;
    status.loadSize = sLoadSize;
    status.frameBuffer = gPauseLinkFrameBuffer;
    status.age = sLoaded ? sLoadedAge : -1;
    status.seg4 = sSeg4;
    status.seg6 = sSeg6;
    status.seg4Clobbered = sSeg4Clobbered;
    status.seg6Clobbered = sSeg6Clobbered;
    // Read live, from outside any render: what the world's segments hold between frames, which is what
    // the next frame's update and draw will see.
    status.seg4Now = gSegments[4];
    status.seg6Now = gSegments[6];
    return status;
}
