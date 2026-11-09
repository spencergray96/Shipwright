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
// The wall lines' width (#309, after the first look): hundredths of an RS TILE, so a wall thickens with the map as it
// zooms; a FLOOR in hundredths of a render pixel under which it never thins; and SNAP (0/1), whole pixels on the grid.
#define CVAR_RS_MENU_MAP_WALL_TILES CVAR_ENHANCEMENT("RsMenuMapWallTiles")
#define CVAR_RS_MENU_MAP_WALL_FLOOR CVAR_ENHANCEMENT("RsMenuMapWallFloor")
#define CVAR_RS_MENU_MAP_WALL_SNAP CVAR_ENHANCEMENT("RsMenuMapWallSnap")
// The focused view's C-button pan (#313): a velocity that ramps in while a C button is held and coasts out after it
// is let go. Its top speed in hundredths of a VIEW a second (a view is the visible width or height), and the ramp in
// and the coast out in milliseconds. 100 / 0 / 0 is #303's pan: full speed at once, a dead stop on release.
#define CVAR_RS_MENU_MAP_PAN_SPEED CVAR_ENHANCEMENT("RsMenuMapPanSpeed")
#define CVAR_RS_MENU_MAP_PAN_RAMP CVAR_ENHANCEMENT("RsMenuMapPanRamp")
#define CVAR_RS_MENU_MAP_PAN_COAST CVAR_ENHANCEMENT("RsMenuMapPanCoast")
#define RS_MENU_MAP_PAN_SPEED_DEFAULT 80
#define RS_MENU_MAP_PAN_SPEED_MIN 10
#define RS_MENU_MAP_PAN_SPEED_MAX 300
#define RS_MENU_MAP_PAN_RAMP_DEFAULT 250
#define RS_MENU_MAP_PAN_COAST_DEFAULT 150
#define RS_MENU_MAP_PAN_SPEED_OFF 100 // `paneasing off`: #303's pan
#define RS_MENU_MAP_PAN_RAMP_OFF 0
#define RS_MENU_MAP_PAN_COAST_OFF 0
#define RS_MENU_MAP_PAN_EASE_MAX 2000 // ms, either

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
// 0.20 of a tile is 1.8 px at 6x and 1080p, the width the owner approved at the first look (0.40 N64 px there); the
// floor of 1 render px is where it stops thinning at the whole-world view.
#define RS_MENU_MAP_WALL_TILES_DEFAULT 20 // hundredths of an RS tile
#define RS_MENU_MAP_WALL_TILES_MIN 2
#define RS_MENU_MAP_WALL_TILES_MAX 100
#define RS_MENU_MAP_WALL_FLOOR_DEFAULT 100 // hundredths of a render pixel
#define RS_MENU_MAP_WALL_FLOOR_MIN 25
#define RS_MENU_MAP_WALL_FLOOR_MAX 600

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
// The wall width in RS tiles and its floor in render pixels (each CVar / 100, clamped), and whether lines snap to
// whole pixels. The Sets take hundredths; false if out of range.
float RsMenuMapPage_WallTiles();
bool RsMenuMapPage_SetWallTiles(int32_t hundredths);
float RsMenuMapPage_WallFloor();
bool RsMenuMapPage_SetWallFloor(int32_t hundredths);
bool RsMenuMapPage_WallSnap();
void RsMenuMapPage_SetWallSnap(bool on);

// The focused view's controls, for the console: what D-pad up (+1) / down (-1) and the C buttons do. Both
// refuse (false) unless the scroll is focused; a zoom refuses past either end too. `dx`/`dy` are visible
// widths/heights.
bool RsMenuMapPage_StepZoom(int32_t dir);
bool RsMenuMapPage_Pan(float dx, float dy);

// The pan's velocity settings (#313), read every tick; the Set saves them. False if any is out of range.
struct RsMenuMapPanEasing {
    int32_t speed;   // hundredths of a view a second
    int32_t rampMs;  // 0 to full speed
    int32_t coastMs; // full speed to 0, once released
};
RsMenuMapPanEasing RsMenuMapPage_PanEasing();
bool RsMenuMapPage_SetPanEasing(RsMenuMapPanEasing e);

// The drawn view, FRAME by frame (#313). The page draws once a tick (20 Hz) and the frames between two ticks show
// the two ticks' matrices lerped (frame_interpolation.cpp), so a capture cannot resolve one frame. With the trace on,
// RunCommands calls NoteFrame for every frame it presents with that frame's step t in (0, 1]; the page records the
// view rect lerped between its last two ticks by t - exact for a pan, since the placement matrix is linear in the
// view's corner at a fixed zoom. Rows are kept in a ring of RS_MENU_MAP_TRACE_ROWS.
#define RS_MENU_MAP_TRACE_ROWS 8192
struct RsMenuMapTraceRow {
    int32_t row;        // frames recorded since `on`
    int32_t draw;       // the page's draw count (sFrame) whose display list this frame showed
    float t;            // the frame's interpolation step, 1 on a tick's own frame
    double ms;          // wall clock since `on`
    float x0, y0, w, h; // the view rect shown, image pixels
    float renderPerPx;  // render pixels an image pixel at this view
    float vx, vy;       // the pan velocity that tick, views a second
};
void RsMenuMapPage_SetTrace(bool on);
bool RsMenuMapPage_Trace();
std::vector<RsMenuMapTraceRow> RsMenuMapPage_TraceRows(); // the ring, oldest first
void RsMenuMapPage_NoteFrame(float t);

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
    float panVx, panVy; // the pan velocity, views a second (#313)
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
    // TLUT. `edges`: segments drawn (after the clip); `joins`: corners filled; `edgeWords`: the Gfx words they took.
    // `wallNominalPx`: the tile width in render pixels at this zoom; `wallScreenPx`: what was drawn after the floor
    // and the snap's rounding; `wallSnapped`: the endpoints went onto the pixel grid (snap on, the scroll still);
    // `wallHalfPx`: the half width in image pixels.
    const char* walls;
    bool wallsDrawn;
    bool edgeless;
    int32_t edges;
    int32_t joins;
    int32_t edgeWords;
    float wallTiles;
    float wallFloor;
    bool wallSnap;
    bool wallSnapped;
    float wallNominalPx;
    float wallHalfPx;
    float wallScreenPx;
    std::string revealedList; // "sx:sy,..." of the map scene's revealed chunks
};
RsMenuMapPageStatus RsMenuMapPage_Status();

#endif // SOH_RS_MENU_MAP_PAGE_H
