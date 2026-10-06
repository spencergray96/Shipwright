#pragma once

#include <ship/window/gui/GuiWindow.h>

typedef enum { COLVIEW_DISABLED, COLVIEW_SOLID, COLVIEW_TRANSPARENT } ColViewerRenderSetting;

// What the scene and bg actor polys are coloured by (sturdy-bassoon#196). CLASS is upstream's: one
// colour per surface class (hookshot, interactable, void, ...). CLIMB colours walls by what Link's
// ledge climb does at them - vanilla (wall type 0), no-climb (1) or hands-climb (13) - which is how a
// grid-tool prop's climb setting reaches the game, and draws floors and ceilings in a neutral grey.
typedef enum { COLVIEW_COLOR_CLASS, COLVIEW_COLOR_CLIMB } ColViewerColorMode;

#ifdef __cplusplus
#include <stdint.h>

// What the last drawn frame put on screen in CLIMB mode, by wall class. All zero when the viewer is
// off, the scene and bg actor layers are both disabled, or the mode is CLASS: these are counts of
// polys DRAWN, so they are the console's only evidence that the colours reached the frame.
struct ColViewerClimbStats {
    uint32_t vanilla;   // walls of wall type 0
    uint32_t noClimb;   // wall type 1
    uint32_t hands;     // wall type 13, WALL_TYPE_HANDS_CLIMB
    uint32_t otherWall; // every other wall type: ladders, vines, crawlspaces
    uint32_t nonWall;   // floors and ceilings
    uint32_t frames;    // frames drawn in CLIMB mode since boot
};
ColViewerClimbStats ColViewer_GetClimbStats();

class ColViewerWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override{};
};

#endif
