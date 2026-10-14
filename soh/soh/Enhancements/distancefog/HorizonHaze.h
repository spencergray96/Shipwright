#ifndef SOH_HORIZON_HAZE_H
#define SOH_HORIZON_HAZE_H

#include <stdint.h>

#include "WorldFog.h"

// The horizon haze band (sturdy-bassoon#168): a ring of the live fog colour over the bottom of the sky and under
// the world, so that shapes world-unit fog (sturdy-bassoon#167) has fully fogged stand against sky of their own
// colour instead of a painted sky of another, and the clip removing them as the camera turns shows nothing.
//
// The ring is its own mesh, centred on the eye the way the skybox is (so frame interpolation treats it the same)
// but fixed in the world's directions - the skybox's mesh rotates - and min(4000, zFar / 2) units out (why that,
// and not inside the skybox's cube: HorizonHaze.cpp). Opaque from the nadir up to `height` degrees above the eye's
// horizontal plane, fading to clear over a further `fade` degrees. Drawn into POLY_OPA right after
// the skybox (z_play.c, Play_Draw): before the sun and moon, so a low sun stays visible over it, and before the
// lightning flash, so it flashes with the sky. No Z test and no Z write, as the sky; everything the world draws
// paints over it. Its colour is lightCtx.fogColor every tick, so it follows the time of day and the storm.
//
// Under the sky's colour (sturdy-bassoon#229; WorldFog.h WorldFog_SetColour) the band takes that colour instead: it
// samples gSkyColourFrameBuffer's one texel as TEXEL0 where prim gave the fog colour, so it and depth fog draw with the
// same value. And it moves to a second site, right after the sky copy (HorizonHaze_DrawAfterSkyCopy), so it is never in
// the copy the colour is averaged from, nor in the sky the edge layer fades the beyond ring into - no loop, and the
// ring meets the true sky. The cost: a low sun or moon is behind it again. `fog haze site before` keeps it before the
// sun and moon with the previous frame's colour instead (the console then samples the colour above the band).
//
// It draws only while world-unit fog is live this frame (WorldFog_Live), so vanilla scenes are untouched unless
// `fog world` is typed there. The scene's height and fade come from its InitScene (WorldFog_SetHaze); the `fog haze`
// console override (DistanceFogConsole.cpp) resolves them each frame at OnPlayDrawBegin with HorizonHaze_Use.

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

// What the band draws with from this frame on, called every frame at OnPlayDrawBegin: whether it may draw at all
// (`fog haze off` says no), and its height and fade, already in 0..WORLD_FOG_HAZE_MAX (WorldFog_SetHaze holds a
// scene's there, and the console refuses anything else). It also clears the frame's drawn flag, so a frame that
// never reaches the band (the pause menu's) reads as not drawn.
void HorizonHaze_Use(uint8_t enabled, WorldFogHaze haze);
// Which colour and site it draws with from this frame on, also every frame at OnPlayDrawBegin: skyColour 1 samples
// the sky's colour (and must only be set while WorldFog_ColourLive), afterCopy 1 draws at the second site.
void HorizonHaze_UseColour(uint8_t skyColour, uint8_t afterCopy);

// Play_Draw's calls: between the skybox and the sun and moon (the fog colour, or the sky's at site before), and right
// after the sky copy (the sky's colour at site after). One of them draws.
void HorizonHaze_Draw(struct PlayState* play);
void HorizonHaze_DrawAfterSkyCopy(struct PlayState* play);

// The last frame's band, for `fog haze status`: whether it drew, how many ticks have drawn it since boot, and the
// colour it last drew with.
typedef struct HorizonHazeFrame {
    uint8_t drawn;
    uint32_t draws;
    uint8_t color[3];
    uint8_t skyColour; // sturdy-bassoon#229: it drew with the sky's colour (color is then the fog colour it replaced)
    uint8_t afterCopy; // and at the site after the sky copy
} HorizonHazeFrame;

const HorizonHazeFrame* HorizonHaze_LastFrame(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_HORIZON_HAZE_H
