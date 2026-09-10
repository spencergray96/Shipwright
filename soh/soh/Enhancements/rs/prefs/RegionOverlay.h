#ifndef SOH_RS_REGION_OVERLAY_H
#define SOH_RS_REGION_OVERLAY_H

#include <stdint.h>

// The on-screen floor-convention overlay (sturdy-bassoon#94), built to QuestOverlay.h's pattern
// because that file already settled every question this one raises:
//
//   - pinned to the MAIN VIEWPORT, so the agent loop's PrintWindow capture includes it (an ImGui
//     window that drifted into its own OS window would be invisible to a screenshot, and this
//     exists to be screenshotted),
//   - state is PROCESS-LIFETIME only - no CVar, nothing in shipofharkinian.json, every launch
//     starts off - because a debug overlay that came back by itself would surprise a run that
//     never asked for it,
//   - switched from the CONSOLE, so the switch exists on both sinks at once, and
//   - a `drawn` counter, so a console leg can prove a frame rendered before a screenshot proves
//     what it rendered.
//
// A SEPARATE WINDOW from the quest overlay, deliberately. Folding one line into that one is
// tempting: the quest overlay tracks QUESTS, and this is a global setting that colours every piece
// of prose in the mod. One window per concern keeps them switchable independently, which is what a
// run wants when it is asserting that the convention changed and nothing else did.

struct RsRegionOverlayState {
    bool enabled;
    int32_t convention; // what the LAST ImGui frame read - not necessarily what is live right now
    int32_t drawn;      // lines the last frame rendered; 0 when it did not draw
};

void RsRegionOverlay_SetEnabled(bool enabled);
RsRegionOverlayState RsRegionOverlay_Get();

#endif // SOH_RS_REGION_OVERLAY_H
