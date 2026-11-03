#ifndef SOH_RS_SCENE_WATER_H
#define SOH_RS_SCENE_WATER_H

// A scene's own water, from code (sturdy-bassoon#282): the grid tool cannot author a waterbox yet, so a map that wants
// water names its rectangles here, keyed by its SceneID. On the scene's first player update (RsBoatDock.cpp's arrival,
// before any boat is spawned) the collision context is pointed at a COPY of the scene's header whose waterbox list is
// the scene's own plus these, as the boat console's test water does; the scene's header is never written, so nothing
// outlives the visit. Each rectangle is drawn as a translucent sheet a little under its surface (the water rings draw
// at the surface itself, #289), at OnPlayDrawEnd, so the water shows with no boat in the scene.
//
// The owner's call (2026-11-02): code for now; water authored in the grid tool comes when water areas are designed.
// `boat water` still works on top: it copies whichever header is live, this one included.

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

// Floods the loaded scene with its rows, if it has any. Safe to call once per scene load; a second call is a no-op.
void RsSceneWater_Apply(struct PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_SCENE_WATER_H
