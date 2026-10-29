#ifndef SOH_RS_BOAT_HUD_H
#define SOH_RS_BOAT_HUD_H

#include <stdint.h>

// The boat prototype's steering label (sturdy-bassoon#256), RsBoatHud.cpp: "Not steering" (white), "Steering
// boat" or, at a balloon's controls (#274), "Steering balloon" (green) under the C buttons during gameplay, while
// CVAR_RS_BOAT_HUD is on (the default).

enum {
    RS_BOAT_HUD_HIDDEN,       // switched off, no play state, or the pause menu is up
    RS_BOAT_HUD_NOT_STEERING,
    RS_BOAT_HUD_STEERING,
    RS_BOAT_HUD_STEERING_BALLOON,
};

// RS_BOAT_HUD_STEERING while Link holds any boat's helm in the loaded scene, RS_BOAT_HUD_STEERING_BALLOON while he
// holds a balloon's controls, else RS_BOAT_HUD_NOT_STEERING.
int32_t RsBoatHud_LinkSteering();

// What the last drawn frame showed (RS_BOAT_HUD_*), for `boat list`'s `hud=`: the label is ImGui, which no
// marker sees, so this is how a run asserts it without reading pixels.
int32_t RsBoatHud_Shown();
const char* RsBoatHud_ShownName(int32_t shown);

#endif // SOH_RS_BOAT_HUD_H
