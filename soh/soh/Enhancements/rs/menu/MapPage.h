#ifndef SOH_RS_MENU_MAP_PAGE_H
#define SOH_RS_MENU_MAP_PAGE_H
// The scroll's World Map page (sturdy-bassoon#303): the whole world map image (#298, maps/WorldMap.h) with the
// fog of war (MapReveal.h) over it and Link's arrow on it, and a focused view that pans and zooms. MapPage.cpp
// carries the why.
#include <stdint.h>
#include <string>
#include <vector>

#define CVAR_RS_MENU_MAP_FOG CVAR_ENHANCEMENT("RsMenuMapFog")     // 0 hide (default), 1 dim
#define CVAR_RS_MENU_MAP_ICONS CVAR_ENHANCEMENT("RsMenuMapIcons") // 0 off (default), anything else on (#307)
#define CVAR_RS_MENU_MAP_ARROW CVAR_ENHANCEMENT("RsMenuMapArrow") // 0 vanilla compass (default), 1 custom
#define CVAR_RS_MENU_MAP_WALLS CVAR_ENHANCEMENT("RsMenuMapWalls") // RsMenuMapWalls (#309)
// The wall lines' width in hundredths of an N64 screen pixel (#309): 4.5 output px each at 1080p, 9 at 4K.
#define CVAR_RS_MENU_MAP_WALL_WIDTH CVAR_ENHANCEMENT("RsMenuMapWallWidth")

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

// How the walls draw (#309): the picture's own pixels, as #303 shipped (IMAGE); thin quads at the output resolution
// over an edgeless picture at every zoom (LINES); or the picture's pixels at the whole-world view and the lines from
// RS_MENU_MAP_WALLS_ZOOMED_FROM up, where the staircase shows (ZOOMED).
enum RsMenuMapWalls {
    RS_MENU_MAP_WALLS_IMAGE = 0,
    RS_MENU_MAP_WALLS_LINES = 1,
    RS_MENU_MAP_WALLS_ZOOMED = 2,
};
#define RS_MENU_MAP_WALLS_DEFAULT RS_MENU_MAP_WALLS_LINES
#define RS_MENU_MAP_WALLS_ZOOMED_FROM 2.0f
#define RS_MENU_MAP_WALL_WIDTH_DEFAULT 40 // hundredths of an N64 px
#define RS_MENU_MAP_WALL_WIDTH_MIN 10
#define RS_MENU_MAP_WALL_WIDTH_MAX 400

void RsMenuMapPage_Register();

RsMenuMapFog RsMenuMapPage_Fog();
void RsMenuMapPage_SetFog(RsMenuMapFog fog);
const char* RsMenuMapPage_FogName(RsMenuMapFog fog);
// The map icons (#307, RsMapIcons.h) on this page: on at every zoom or off, OFF by default (the owner, 2026-11-07).
// In-page controls for this and the other map options come later; the CVar and `menu map icons` are the knob now.
bool RsMenuMapPage_Icons();
void RsMenuMapPage_SetIcons(bool on);
RsMenuMapArrow RsMenuMapPage_Arrow();
void RsMenuMapPage_SetArrow(RsMenuMapArrow arrow);
const char* RsMenuMapPage_ArrowName(RsMenuMapArrow arrow);
RsMenuMapWalls RsMenuMapPage_Walls();
void RsMenuMapPage_SetWalls(RsMenuMapWalls walls);
const char* RsMenuMapPage_WallsName(RsMenuMapWalls walls);
// The wall width in N64 screen pixels (the CVar / 100, clamped). Set takes hundredths; false if out of range.
float RsMenuMapPage_WallWidth();
bool RsMenuMapPage_SetWallWidth(int32_t hundredths);

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
    // #307: every table icon on the map's scene, and what the last frame did with it. state: in | edge | out (the
    // view), fog (its chunk is hidden), storey (its plane is not on the layer drawn), off (the toggle is off),
    // no_sprite. alpha: the prim alpha it drew with (255).
    // size: its width in game units on the page (constant at every zoom step).
    int32_t iconsShown; // icons drawn last frame with any part showing
    float iconUnits;    // an icon's width in game units
    struct Icon {
        const char* name;
        const char* token;
        const char* state;
        float px, py;
        uint8_t alpha;
    };
    std::vector<Icon> icons;
    // #309: the walls as geometry, the last drawn frame. `wallsDrawn`: the lines were on this frame (LINES, or
    // ZOOMED at the zoom threshold) and the map carries segments; `edgeless`: the blocks drew through the edgeless
    // TLUT. `edges`: segments drawn (after the clip); `edgeWords`: the Gfx words they took. `wallHalfPx`: the half
    // width in image pixels the screen width came to at this zoom. `wallScreenPx`: the full width in output pixels.
    const char* walls;
    bool wallsDrawn;
    bool edgeless;
    int32_t edges;
    int32_t edgeWords;
    float wallWidth;
    float wallHalfPx;
    float wallScreenPx;
    std::string revealedList; // "sx:sy,..." of the map scene's revealed chunks
};
RsMenuMapPageStatus RsMenuMapPage_Status();

#endif // SOH_RS_MENU_MAP_PAGE_H
