#pragma once
/*
 * MAP ICONS (sturdy-bassoon#307): RS map icons placed by hand, as a table keyed by RS tile. Reference: sturdy-bassoon
 * docs/reference/WORLD_MAP.md, "Map icons", which also has the recipe for adding one.
 *
 * THE TABLE is kMapIcons in RsMapIcons.cpp: a name, the sprite token (RsSprites.h; the row must be approved in
 * tools/sprites/rs-sprites.json and packed into mods/rs-sprites.o2r), the RS tile the icon is centred on, and its
 * plane. A tile, not a scene: any scene whose world map covers the tile shows the icon, at the tile's centre.
 *
 * WHO DRAWS THEM. The HUD minimap (RsMinimap.h): this file hooks OnMinimapDrawCompassIconsUnderPlayer and prints each
 * icon on the map with RsMinimap_DrawIcon (under Link's arrow, clipped to the circle). The pause map page (#303,
 * rs/menu/MapPage.cpp) reads the same table through RsMapIcons_ForScene, behind RsMenuMapIcons (off | zoomed | on,
 * default on), and hides an icon whose chunk is still fogged.
 *
 * STOREYS. An icon shows when its plane is in the layer the storey CVar draws: alone k shows plane k, a stack 0..k
 * shows planes 0..k. So with the default (`below`) a ground-floor icon is always there.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <z64.h>

typedef struct {
    const char* name;  // snake_case, unique: lumbridge_general_store
    const char* token; // the sprite: map_general_store
    int32_t rsX, rsY;  // the RS tile the icon is centred on
    uint8_t plane;     // its RS plane (storey)
} RsMapIconDef;

int32_t RsMapIcons_Count(void);
const RsMapIconDef* RsMapIcons_Def(int32_t index); // NULL out of range

typedef struct {
    int32_t index;            // into the table
    const RsMapIconDef* def;
    float x, z;               // world position in this scene: the tile's centre
    bool onLayer;             // its plane is in the storey layer drawn now
} RsMapIcon;

// The table's icons whose tile is on this scene's world map, with their world position, up to `max`. Returns how
// many it wrote (0 with no map).
int32_t RsMapIcons_ForScene(int16_t sceneId, RsMapIcon* out, int32_t max);

// What the minimap did with each icon last frame, for `minimap icons`.
typedef struct {
    int32_t index;
    bool onLayer;
    int32_t state;   // RsMinimapIconState; RS_MINIMAP_ICON_NOT_DRAWING when not on the layer or no minimap
    float sx, sy;    // centre-origin y-up N64 px on the minimap
    float tiles;     // distance from Link, in RS tiles
} RsMapIconFrame;
// Fills up to `max` rows for last frame's minimap and returns the count; 0 when the minimap did not draw icons last
// frame (no minimap, toggled off, faded, paused).
int32_t RsMapIcons_LastFrame(RsMapIconFrame* out, int32_t max);

#ifdef __cplusplus
}
#endif
