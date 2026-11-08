#ifndef SOH_RS_VEHICLE_MINIMAP_H
#define SOH_RS_VEHICLE_MINIMAP_H

#include <stdint.h>

// The vehicles' minimap arrows (sturdy-bassoon#270), RsVehicleMinimap.cpp: a blue arrow for every boat and an orange
// one for every balloon in the scene, the shape and size of Link's yellow arrow, where the vehicle is and turned the
// way it faces. Drawn under Link's arrow (OnMinimapDrawCompassIconsUnderPlayer), so on a deck or in a basket his is
// on top. While Link holds a vehicle's controls that vehicle's arrow is not drawn: his stands for both. Shown and
// hidden with the minimap, as the vanilla arrows are; scenes with no minimap draw none. On the RS minimap (#306)
// the arrow is printed on the map and clipped to its circle: one wholly past the edge is out_of_view.

struct Actor;

enum {
    RS_VEHICLE_ARROW_NO_MINIMAP, // the last minimap draw did not consider it: no minimap now, or not a vehicle
    RS_VEHICLE_ARROW_DRAWN,
    RS_VEHICLE_ARROW_CONTROLS, // not drawn: Link holds this vehicle's controls
    RS_VEHICLE_ARROW_OUT_OF_VIEW, // drawn, but wholly outside the RS minimap's circle (#306), so none of it shows
};

// What the last minimap draw did with this vehicle's arrow (RS_VEHICLE_ARROW_*), for `boat list` and `balloon
// list`'s `map_arrow=`: the arrow is a few pixels of HUD no marker sees, so a run asserts the rules through this.
int32_t RsVehicleMinimap_Arrow(const Actor* vehicle);
const char* RsVehicleMinimap_ArrowName(int32_t arrow);

#ifdef __cplusplus
#include <vector>
// The same arrows as data, for another map to draw (the pause scroll's World Map, #303): every boat and balloon
// the minimap would show, the same filter and colours (cosmetic overrides applied). `controls` is the minimap's
// rule - Link holds that vehicle's controls, so his arrow stands for both and this one is not drawn.
struct RsVehicleMapArrow {
    const Actor* vehicle;
    bool balloon; // else a boat
    float x, z;   // world position
    int16_t yaw;  // the hull's or basket's heading
    uint8_t rgb[3];
    bool controls;
};
void RsVehicleMinimap_MapArrows(std::vector<RsVehicleMapArrow>& out);
#endif

#endif // SOH_RS_VEHICLE_MINIMAP_H
