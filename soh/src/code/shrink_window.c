#include "global.h"

s32 D_8012CED0 = 0;

s32 sShrinkWindowVal = 0;
s32 sShrinkWindowCurrentVal = 0;
// The letterbox size one game tick ago. The visible bars are drawn as interpolated geometry
// sweeping prev -> current across the tick (Gfx_SetupFrame), so every consumer that carves
// the frame around the bars (z-buffer clear, base fill, the 3D scissor) must use the SMALLER
// of the two - otherwise the sweep exposes rows nothing has drawn (sturdy-bassoon#42).
s32 sShrinkWindowPrevVal = 0;

void Letterbox_SetSizeTarget(s32 value) {
    if (CVarGetInteger(CVAR_ENHANCEMENT("DisableBlackBars"), 0)) {
        sShrinkWindowVal = 0;
        return;
    }
    if (HREG(80) == 0x13 && HREG(81) == 1) {
        osSyncPrintf("shrink_window_setval(%d)\n", value);
    }
    sShrinkWindowVal = value;
}

u32 ShrinkWindow_GetVal(void) {
    return sShrinkWindowVal;
}

void ShrinkWindow_SetCurrentVal(s32 currentVal) {
    if (CVarGetInteger(CVAR_ENHANCEMENT("DisableBlackBars"), 0)) {
        sShrinkWindowCurrentVal = 0;
        sShrinkWindowPrevVal = 0;
        return;
    }
    if (HREG(80) == 0x13 && HREG(81) == 1) {
        osSyncPrintf("shrink_window_setnowval(%d)\n", currentVal);
    }
    sShrinkWindowCurrentVal = currentVal;
    // A direct set is a hard cut, not an animation step - there is no prev->current sweep to
    // protect, so the safe value snaps with it.
    sShrinkWindowPrevVal = currentVal;
}

u32 ShrinkWindow_GetCurrentVal(void) {
    return sShrinkWindowCurrentVal;
}

/** The bound safe to carve the frame with while the drawn bars sweep between last tick's size
 *  and this tick's: the smaller of the two, so the swept region is always fully drawn
 *  underneath the bars. Equal to the current value whenever the size is not animating. */
u32 ShrinkWindow_GetSafeVal(void) {
    return sShrinkWindowPrevVal < sShrinkWindowCurrentVal ? sShrinkWindowPrevVal : sShrinkWindowCurrentVal;
}

void ShrinkWindow_Init(void) {
    if (HREG(80) == 0x13 && HREG(81) == 1) {
        osSyncPrintf("shrink_window_init()\n");
    }
    D_8012CED0 = 0;
    sShrinkWindowVal = 0;
    sShrinkWindowCurrentVal = 0;
    sShrinkWindowPrevVal = 0;
}

void ShrinkWindow_Destroy(void) {
    if (HREG(80) == 0x13 && HREG(81) == 1) {
        osSyncPrintf("shrink_window_cleanup()\n");
    }
    sShrinkWindowCurrentVal = 0;
    sShrinkWindowPrevVal = 0;
}

void ShrinkWindow_Update(s32 updateRate) {
    s32 off;

    sShrinkWindowPrevVal = sShrinkWindowCurrentVal;

    if (updateRate == 3) {
        off = 10;
    } else {
        off = 30 / updateRate;
    }

    if (sShrinkWindowCurrentVal < sShrinkWindowVal) {
        if (D_8012CED0 != 1) {
            D_8012CED0 = 1;
        }

        if (sShrinkWindowCurrentVal + off < sShrinkWindowVal) {
            sShrinkWindowCurrentVal += off;
        } else {
            sShrinkWindowCurrentVal = sShrinkWindowVal;
        }
    } else if (sShrinkWindowVal < sShrinkWindowCurrentVal) {
        if (D_8012CED0 != 2) {
            D_8012CED0 = 2;
        }

        if (sShrinkWindowVal < sShrinkWindowCurrentVal - off) {
            sShrinkWindowCurrentVal -= off;
        } else {
            sShrinkWindowCurrentVal = sShrinkWindowVal;
        }
    } else {
        D_8012CED0 = 0;
    }

    if (HREG(80) == 0x13) {
        if (HREG(94) != 0x13) {
            HREG(94) = 0x13;
            HREG(81) = 0;
            HREG(82) = 0;
            HREG(83) = 0;
            HREG(84) = 0;
            HREG(85) = 0;
            HREG(86) = 0;
            HREG(87) = 0;
            HREG(88) = 0;
            HREG(89) = 0;
        }
        HREG(83) = D_8012CED0;
        HREG(84) = sShrinkWindowCurrentVal;
        HREG(85) = sShrinkWindowVal;
        HREG(86) = off;
    }
}
