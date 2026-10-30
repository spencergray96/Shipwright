#ifndef SOH_RS_VEHICLE_MINIMAP_H
#define SOH_RS_VEHICLE_MINIMAP_H

#include <stdint.h>

// The vehicles' minimap arrows (sturdy-bassoon#270), RsVehicleMinimap.cpp: a blue arrow for every boat and an orange
// one for every balloon in the scene, the shape and size of Link's yellow arrow, where the vehicle is and turned the
// way it faces. Drawn under Link's arrow (OnMinimapDrawCompassIconsUnderPlayer), so on a deck or in a basket his is
// on top. While Link holds a vehicle's controls that vehicle's arrow is not drawn: his stands for both. Shown and
// hidden with the minimap, as the vanilla arrows are; scenes with no minimap draw none.

struct Actor;

enum {
    RS_VEHICLE_ARROW_NO_MINIMAP, // the last minimap draw did not consider it: no minimap now, or not a vehicle
    RS_VEHICLE_ARROW_DRAWN,
    RS_VEHICLE_ARROW_CONTROLS, // not drawn: Link holds this vehicle's controls
};

// What the last minimap draw did with this vehicle's arrow (RS_VEHICLE_ARROW_*), for `boat list` and `balloon
// list`'s `map_arrow=`: the arrow is a few pixels of HUD no marker sees, so a run asserts the rules through this.
int32_t RsVehicleMinimap_Arrow(const Actor* vehicle);
const char* RsVehicleMinimap_ArrowName(int32_t arrow);

#endif // SOH_RS_VEHICLE_MINIMAP_H
