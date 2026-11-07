#ifndef SOH_RS_MENU_MAP_PAGE_H
#define SOH_RS_MENU_MAP_PAGE_H
// The scroll's World Map page (sturdy-bassoon#303): the whole world map image (#298, maps/WorldMap.h) with the
// fog of war (MapReveal.h) over it and Link's arrow on it. MapPage.cpp carries the why.
#include <stdint.h>
#include <string>

#define CVAR_RS_MENU_MAP_FOG CVAR_ENHANCEMENT("RsMenuMapFog")     // 0 hide (default), 1 dim
#define CVAR_RS_MENU_MAP_ICONS CVAR_ENHANCEMENT("RsMenuMapIcons") // 0 off (default), 1 on; #307 draws them

enum RsMenuMapFog {
    RS_MENU_MAP_FOG_HIDE = 0,
    RS_MENU_MAP_FOG_DIM = 1,
};

void RsMenuMapPage_Register();

RsMenuMapFog RsMenuMapPage_Fog();
void RsMenuMapPage_SetFog(RsMenuMapFog fog);
const char* RsMenuMapPage_FogName(RsMenuMapFog fog);
bool RsMenuMapPage_Icons();
void RsMenuMapPage_SetIcons(bool on);

// What `menu dump section=map` reports. `source`/`scene`/`reason` are worked out live (they do not need the
// page on screen); `drawn`, `frame`, `dst*`, `blocks`, `fogQuads` and `words` are the LAST DRAWN frame's.
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
    bool link;            // Link is in the map's own scene, so the arrow draws
    float linkPx, linkPy; // his image pixel
    int32_t linkSx, linkSy, linkFlag;
    bool linkRevealed;
    // the last drawn frame
    int32_t frame;
    float dstX, dstY, dstW, dstH; // game space, y down
    int32_t blocks;
    int32_t fogQuads;
    int32_t words;
    int32_t arrowDrawn;
    std::string revealedList; // "sx:sy,..." of the map scene's revealed chunks
};
RsMenuMapPageStatus RsMenuMapPage_Status();

#endif // SOH_RS_MENU_MAP_PAGE_H
