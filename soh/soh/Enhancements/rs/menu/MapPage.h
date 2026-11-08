#ifndef SOH_RS_MENU_MAP_PAGE_H
#define SOH_RS_MENU_MAP_PAGE_H
// The scroll's World Map page (sturdy-bassoon#303): the whole world map image (#298, maps/WorldMap.h) with the
// fog of war (MapReveal.h) over it and Link's arrow on it, and a focused view that pans and zooms. MapPage.cpp
// carries the why.
#include <stdint.h>
#include <string>
#include <vector>

#define CVAR_RS_MENU_MAP_FOG CVAR_ENHANCEMENT("RsMenuMapFog")     // 0 hide (default), 1 dim
#define CVAR_RS_MENU_MAP_ICONS CVAR_ENHANCEMENT("RsMenuMapIcons") // 0 off (default), 1 on; #307 draws them
#define CVAR_RS_MENU_MAP_ARROW CVAR_ENHANCEMENT("RsMenuMapArrow") // 0 vanilla compass (default), 1 custom

enum RsMenuMapFog {
    RS_MENU_MAP_FOG_HIDE = 0,
    RS_MENU_MAP_FOG_DIM = 1,
};

// Link's arrow: vanilla's gCompassArrowDL as #306's minimap draws it (the owner, 2026-11-07), or the page's own
// two-triangle arrow from 296d1d210, kept for a later look.
enum RsMenuMapArrow {
    RS_MENU_MAP_ARROW_VANILLA = 0,
    RS_MENU_MAP_ARROW_CUSTOM = 1,
};

void RsMenuMapPage_Register();

RsMenuMapFog RsMenuMapPage_Fog();
void RsMenuMapPage_SetFog(RsMenuMapFog fog);
const char* RsMenuMapPage_FogName(RsMenuMapFog fog);
bool RsMenuMapPage_Icons();
void RsMenuMapPage_SetIcons(bool on);
RsMenuMapArrow RsMenuMapPage_Arrow();
void RsMenuMapPage_SetArrow(RsMenuMapArrow arrow);
const char* RsMenuMapPage_ArrowName(RsMenuMapArrow arrow);

// The focused view's controls, for the console: what D-pad up (+1) / down (-1) and the C buttons do. Both
// refuse (false) unless the scroll is focused; a zoom refuses past either end too. `dx`/`dy` are visible
// widths/heights.
bool RsMenuMapPage_StepZoom(int32_t dir);
bool RsMenuMapPage_Pan(float dx, float dy);

// What `menu dump section=map` reports. `source`/`scene`/`reason` and the view are worked out live; `frame`,
// `dst*`, `drawnView`, `blocks`, `fogQuads`, `words` and `arrowDrawn` are the LAST DRAWN frame's.
struct RsMenuMapPageStatus {
    int32_t pageIndex;    // 0-based ring position, -1 unregistered
    bool current;         // the ring is on this page
    bool map;             // a map to draw
    int16_t scene;        // the scene whose map it is (-1 none)
    const char* source;   // scene | context | none
    const char* reason;   // RsWorldMap_Problem of the current scene, "" when it has a map
    int32_t layer;        // the layer the storey CVar picks
    const char* storeyMode;
    int32_t chunks;       // the map scene's chunk rows
    int32_t revealed;     // of those, revealed
    bool link;            // Link is in the map's own scene
    float linkPx, linkPy; // his image pixel
    int32_t linkSx, linkSy, linkFlag;
    bool linkRevealed;
    // the focused view, this tick (image pixels; zoom 1 is the whole world)
    float viewCx, viewCy, viewZoom;
    int32_t zoomStep;  // index into the zoom steps
    int32_t zoomSteps; // zoom steps taken this session
    int32_t pans;      // ticks a pan moved the view
    const char* arrow; // vanilla | custom
    // the last drawn frame
    int32_t frame;
    float dstX, dstY, dstW, dstH; // the map's box, game space, y down, before the scroll's own matrix
    float drawnView[4];           // the image rect it showed: x0, y0, w, h
    int32_t blocks;
    int32_t fogQuads;
    int32_t words;
    int32_t arrowDrawn;
    // #303 round 3: every arrow the last frame considered - Link's, the red entrance's, each boat's and balloon's -
    // and whether it was in the view, cut by its edge, or out. `controls`: a vehicle Link holds the controls of (not
    // drawn; his arrow stands for both). `scissored`: the frame clipped to the box with a scissor (the scroll still).
    const char* linkArrow;
    bool scissored;
    struct Arrow {
        const char* kind;
        const char* state;
        float px, py;
        bool controls;
    };
    std::vector<Arrow> arrows;
    std::string revealedList; // "sx:sy,..." of the map scene's revealed chunks
};
RsMenuMapPageStatus RsMenuMapPage_Status();

#endif // SOH_RS_MENU_MAP_PAGE_H
