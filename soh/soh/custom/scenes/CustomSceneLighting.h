#pragma once
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Gives a just-spawned custom scene the light settings the weather asks for (sturdy-bassoon#164). A
// scene exported with only its own four (config 0: dawn, day, dusk, night) gets a 12-entry table:
// its four, the same four again as config 1 (only an underwater light index asks for it), and a
// default storm as config 2, which the Song of Storms' gloomy sky blends to. A scene carrying any
// other count is left alone - twelve or more means it brought its own storm.
void CustomSceneLighting_AddDefaultStorm(PlayState* play);

#ifdef __cplusplus
}
#endif
