#include "CustomSceneLighting.h"

// Child Hyrule Field's storm, its light settings 8-11 (dawn, day, dusk, night) exactly as vanilla has
// them - read out of the running game with `agenttest env lights` (sturdy-bassoon#164). A first default
// that reads as a storm, to be tuned per scene later: a scene that carries its own twelve settings
// keeps them. fogNear keeps vanilla's blend-rate bits above bit 9, which the outdoor path masks off.
static const EnvLightSettings sDefaultStormLightSettings[4] = {
    { { 92, 92, 92 }, { 73, 73, 73 }, { 160, 134, 118 }, { -73, -73, -73 }, { 30, 10, 10 }, { 30, 10, 10 }, 2010,
      12800 },
    { { 95, 80, 80 }, { 73, 73, 73 }, { 145, 145, 130 }, { -73, -73, -73 }, { 40, 40, 80 }, { 95, 95, 85 }, 2010,
      12800 },
    { { 80, 70, 70 }, { 73, 73, 73 }, { 150, 70, 35 }, { -73, -73, -73 }, { 10, 10, 25 }, { 35, 10, 10 }, 2012,
      12800 },
    { { 70, 70, 90 }, { 73, 73, 73 }, { 0, 0, 15 }, { -73, -73, -73 }, { 30, 30, 80 }, { 0, 0, 10 }, 2012, 12800 },
};

// Only one scene is loaded at a time, so one table serves whichever custom scene is up.
static EnvLightSettings sExtendedLightSettings[12];

extern "C" void CustomSceneLighting_AddDefaultStorm(PlayState* play) {
    EnvironmentContext* env = &play->envCtx;
    if (env->numLightSettings != 4 || env->lightSettingsList == nullptr) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        sExtendedLightSettings[i] = env->lightSettingsList[i];     // config 0: the scene's own
        sExtendedLightSettings[4 + i] = env->lightSettingsList[i]; // config 1: only water asks for it
        sExtendedLightSettings[8 + i] = sDefaultStormLightSettings[i];
    }
    env->lightSettingsList = sExtendedLightSettings;
    env->numLightSettings = 12;
}
