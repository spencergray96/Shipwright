#ifndef SOH_RS_SCENE_MAPS_H
#define SOH_RS_SCENE_MAPS_H

#include <stdint.h>

// ============================================================================================
//  WHICH MAPS A SCENE HOLDS, AND WHERE  (sturdy-bassoon#173 slice F3)
// ============================================================================================
//
// The grid tool authors staircases and warp tiles on a MAP, and names a map by its MAP NUMBER
// (CONTEXT.md). A scene holds one map (a map's solo scene) or several (a stitched scene, one per
// chunk). So the game needs to know, for any scene, which maps are in it and where - and that is the
// map->scene table every export writes beside the scene's C (`<slug>_maps.inc`, the slice F ADR's
// decision 15; the macros are the grid tool README's "The generated tables").
//
// The lookup goes position -> map -> (map, local id) (decision 17): the warp scan asks which map's
// rectangle a warp polygon is in, and a generated staircase row's `wrong_scene` refusal becomes "is
// this row's map in this scene". A scene with no rows here is not MAP-KEYED: its staircases and warp
// tiles are the hand-written, scene-keyed fixtures (decision 18), and nothing here changes them.
//
// The table is read once, at boot, from every scene's file through one aggregate
// (custom/scenes/grid_tool/generated/GridToolMaps.inc). A malformed one is refused whole - BUG class,
// as a staircase or warp table is: an error, a debug assert and a `refused` marker. `warps badcheck`
// asks the silent validator about planted ones.

#ifdef __cplusplus
extern "C" {
#endif

// A scene's WORLD (decision 15), as `RS_GEN_SCENE` carries it: a stitched scene's own scene id, or one
// of these two, which are negative so they are never a scene id. F5's world context reads it.
#define RS_GEN_WORLD_NEUTRAL (-1) // a map in no world matrix
#define RS_GEN_WORLD_SOLO (-2)    // the solo scene of a map that sits in a matrix

// One map in one scene: its rectangle in scene coordinates, whole units, min <= p < max on each axis.
typedef struct RsSceneMap {
    int32_t map; // the map number, from 1
    int32_t minX;
    int32_t minZ;
    int32_t maxX;
    int32_t maxZ;
} RsSceneMap;

// The rows as the generated files carry them, for the validator.
typedef struct RsGenSceneRow {
    int32_t scene;
    int32_t entrance;
    int32_t world;
} RsGenSceneRow;

typedef struct RsGenSceneMapRow {
    int32_t scene;
    RsSceneMap map;
} RsGenSceneMapRow;

typedef enum RsMapsProblem {
    RS_MAPS_PROBLEM_NONE = 0,
    RS_MAPS_PROBLEM_SCENE_TWICE,  // two RS_GEN_SCENE rows for one scene
    RS_MAPS_PROBLEM_BAD_ENTRANCE, // not an entrance id, or one that loads another scene
    RS_MAPS_PROBLEM_BAD_WORLD,    // neither of the two names above nor a scene id
    RS_MAPS_PROBLEM_NO_SCENE,     // a map row for a scene with no RS_GEN_SCENE
    RS_MAPS_PROBLEM_NO_MAPS,      // a scene with no map rows
    RS_MAPS_PROBLEM_BAD_MAP,      // a map number under 1
    RS_MAPS_PROBLEM_MAP_TWICE,    // one map twice in one scene
    RS_MAPS_PROBLEM_BAD_RECT,     // a rectangle with min >= max on an axis
    RS_MAPS_PROBLEM_OVERLAP,      // two of one scene's rectangles share a point: a position would name two maps
    RS_MAPS_PROBLEM_COUNT,
} RsMapsProblem;

// "Would these rows register?" - no write, no log, no assert. `where` is the offending row: an index
// into `maps` for a map row's problem, into `scenes` for a scene row's; -1 for none.
int32_t RsMaps_TableProblem(const RsGenSceneRow* scenes, int32_t sceneCount, const RsGenSceneMapRow* maps,
                            int32_t mapCount, int32_t* where);
const char* RsMaps_ProblemName(int32_t problem);

// Lookups. Quiet: 0 / -1 / NULL for a scene that is not map-keyed.
int32_t RsMaps_MapCount(int32_t sceneId);                    // the maps the scene holds; 0 = not map-keyed
const RsSceneMap* RsMaps_Map(int32_t sceneId, int32_t slot); // slot 0..count-1, in table order
int32_t RsMaps_SlotAt(int32_t sceneId, float x, float z);    // the slot whose rectangle holds (x, z), or -1
int32_t RsMaps_SlotOf(int32_t sceneId, int32_t map);         // the slot holding that map number, or -1
int32_t RsMaps_SceneEntrance(int32_t sceneId);               // its ENTR_*_0, or -1
int32_t RsMaps_SceneWorld(int32_t sceneId);                  // RS_GEN_WORLD_*, a scene id, or 0 if not map-keyed
const char* RsMaps_WorldName(int32_t world);                 // "neutral", "solo", or "0x<scene>"

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_SCENE_MAPS_H
