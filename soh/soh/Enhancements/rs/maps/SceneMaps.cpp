#include "SceneMaps.h"

#include <cassert>
#include <cstdio>
#include <vector>

#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/ShipInit.hpp"
#include <spdlog/spdlog.h>

extern "C" {
#include <z64.h> // the SCENE_* and ENTR_* enumerators the generated rows name
#include "variables.h"
}

// See SceneMaps.h for what this table is and why the game needs it.

namespace {

struct SceneEntry {
    int32_t scene;
    int32_t entrance;
    int32_t world;
    std::vector<RsSceneMap> maps;
};

std::vector<SceneEntry> sScenes;

const SceneEntry* Find(int32_t sceneId) {
    for (const SceneEntry& entry : sScenes) {
        if (entry.scene == sceneId) {
            return &entry;
        }
    }
    return nullptr;
}

bool Contains(const RsSceneMap& m, float x, float z) {
    return x >= static_cast<float>(m.minX) && x < static_cast<float>(m.maxX) && z >= static_cast<float>(m.minZ) &&
           z < static_cast<float>(m.maxZ);
}

bool Overlap(const RsSceneMap& a, const RsSceneMap& b) {
    return a.minX < b.maxX && b.minX < a.maxX && a.minZ < b.maxZ && b.minZ < a.maxZ;
}

// Every scene's rows, from the one aggregate the export keeps (the grid tool README, "The generated
// tables"). A function rather than an array initialiser, so an aggregate with no rows yet compiles.
void Collect(std::vector<RsGenSceneRow>& scenes, std::vector<RsGenSceneMapRow>& maps) {
#define RS_GEN_SCENE(scene, entrance, world) scenes.push_back({ (scene), (entrance), (world) });
#define RS_GEN_SCENE_MAP(scene, map, minX, minZ, maxX, maxZ) \
    maps.push_back({ (scene), { (map), (minX), (minZ), (maxX), (maxZ) } });
#include "soh/custom/scenes/grid_tool/generated/GridToolMaps.inc"
#undef RS_GEN_SCENE
#undef RS_GEN_SCENE_MAP
}

void RegisterSceneMaps() {
    if (!sScenes.empty()) {
        return; // a ShipInit re-run: the table is compiled in and cannot have changed
    }
    std::vector<RsGenSceneRow> scenes;
    std::vector<RsGenSceneMapRow> maps;
    Collect(scenes, maps);
    int32_t where = -1;
    const int32_t problem = RsMaps_TableProblem(scenes.data(), static_cast<int32_t>(scenes.size()), maps.data(),
                                                static_cast<int32_t>(maps.size()), &where);
    if (problem != RS_MAPS_PROBLEM_NONE) {
        // BUG CLASS, as a staircase or warp table's refusal is: the exporter wrote something the game
        // cannot read. Refused whole - a scene with half its maps would mis-name positions.
        char line[160];
        std::snprintf(line, sizeof(line), "rs_maps event=refused problem=%s row=%d", RsMaps_ProblemName(problem),
                      where);
        AgentTest_WriteMarker(line);
        SPDLOG_ERROR("RsMaps: the generated map->scene table: {} at row {}", RsMaps_ProblemName(problem), where);
        assert(false && "generated map->scene table failed validation");
        return;
    }
    for (const RsGenSceneRow& s : scenes) {
        SceneEntry entry;
        entry.scene = s.scene;
        entry.entrance = s.entrance;
        entry.world = s.world;
        for (const RsGenSceneMapRow& m : maps) {
            if (m.scene == s.scene) {
                entry.maps.push_back(m.map);
            }
        }
        sScenes.push_back(entry);
    }
}

RegisterShipInitFunc sceneMapsInitFunc(RegisterSceneMaps);

} // namespace

extern "C" const char* RsMaps_ProblemName(int32_t problem) {
    switch (problem) {
        case RS_MAPS_PROBLEM_NONE:
            return "none";
        case RS_MAPS_PROBLEM_SCENE_TWICE:
            return "scene_twice";
        case RS_MAPS_PROBLEM_BAD_ENTRANCE:
            return "bad_entrance";
        case RS_MAPS_PROBLEM_BAD_WORLD:
            return "bad_world";
        case RS_MAPS_PROBLEM_NO_SCENE:
            return "no_scene";
        case RS_MAPS_PROBLEM_NO_MAPS:
            return "no_maps";
        case RS_MAPS_PROBLEM_BAD_MAP:
            return "bad_map";
        case RS_MAPS_PROBLEM_MAP_TWICE:
            return "map_twice";
        case RS_MAPS_PROBLEM_BAD_RECT:
            return "bad_rect";
        case RS_MAPS_PROBLEM_OVERLAP:
            return "overlap";
        default:
            return "unknown";
    }
}

extern "C" int32_t RsMaps_TableProblem(const RsGenSceneRow* scenes, int32_t sceneCount, const RsGenSceneMapRow* maps,
                                       int32_t mapCount, int32_t* where) {
    int32_t unused = -1;
    int32_t* at = where != nullptr ? where : &unused;
    *at = -1;
    for (int32_t i = 0; i < sceneCount; i++) {
        const RsGenSceneRow& s = scenes[i];
        *at = i;
        for (int32_t j = 0; j < i; j++) {
            if (scenes[j].scene == s.scene) {
                return RS_MAPS_PROBLEM_SCENE_TWICE;
            }
        }
        if (s.entrance < 0 || s.entrance >= ENTR_MAX || gEntranceTable[s.entrance].scene != s.scene) {
            return RS_MAPS_PROBLEM_BAD_ENTRANCE;
        }
        if (s.world != RS_GEN_WORLD_NEUTRAL && s.world != RS_GEN_WORLD_SOLO && (s.world < 0 || s.world >= SCENE_ID_MAX)) {
            return RS_MAPS_PROBLEM_BAD_WORLD;
        }
        bool any = false;
        for (int32_t k = 0; k < mapCount; k++) {
            any = any || maps[k].scene == s.scene;
        }
        if (!any) {
            return RS_MAPS_PROBLEM_NO_MAPS;
        }
    }
    for (int32_t i = 0; i < mapCount; i++) {
        const RsGenSceneMapRow& m = maps[i];
        *at = i;
        bool scene = false;
        for (int32_t j = 0; j < sceneCount; j++) {
            scene = scene || scenes[j].scene == m.scene;
        }
        if (!scene) {
            return RS_MAPS_PROBLEM_NO_SCENE;
        }
        if (m.map.map < 1) {
            return RS_MAPS_PROBLEM_BAD_MAP;
        }
        if (m.map.minX >= m.map.maxX || m.map.minZ >= m.map.maxZ) {
            return RS_MAPS_PROBLEM_BAD_RECT;
        }
        for (int32_t j = 0; j < i; j++) {
            if (maps[j].scene != m.scene) {
                continue;
            }
            if (maps[j].map.map == m.map.map) {
                return RS_MAPS_PROBLEM_MAP_TWICE;
            }
            if (Overlap(maps[j].map, m.map)) {
                return RS_MAPS_PROBLEM_OVERLAP;
            }
        }
    }
    *at = -1;
    return RS_MAPS_PROBLEM_NONE;
}

extern "C" int32_t RsMaps_MapCount(int32_t sceneId) {
    const SceneEntry* entry = Find(sceneId);
    return entry != nullptr ? static_cast<int32_t>(entry->maps.size()) : 0;
}

extern "C" const RsSceneMap* RsMaps_Map(int32_t sceneId, int32_t slot) {
    const SceneEntry* entry = Find(sceneId);
    if (entry == nullptr || slot < 0 || slot >= static_cast<int32_t>(entry->maps.size())) {
        return nullptr;
    }
    return &entry->maps[slot];
}

extern "C" int32_t RsMaps_SlotAt(int32_t sceneId, float x, float z) {
    const SceneEntry* entry = Find(sceneId);
    if (entry == nullptr) {
        return -1;
    }
    for (size_t slot = 0; slot < entry->maps.size(); slot++) {
        if (Contains(entry->maps[slot], x, z)) {
            return static_cast<int32_t>(slot);
        }
    }
    return -1;
}

extern "C" int32_t RsMaps_SlotOf(int32_t sceneId, int32_t map) {
    const SceneEntry* entry = Find(sceneId);
    if (entry == nullptr) {
        return -1;
    }
    for (size_t slot = 0; slot < entry->maps.size(); slot++) {
        if (entry->maps[slot].map == map) {
            return static_cast<int32_t>(slot);
        }
    }
    return -1;
}

extern "C" int32_t RsMaps_SceneEntrance(int32_t sceneId) {
    const SceneEntry* entry = Find(sceneId);
    return entry != nullptr ? entry->entrance : -1;
}

extern "C" int32_t RsMaps_SceneWorld(int32_t sceneId) {
    const SceneEntry* entry = Find(sceneId);
    return entry != nullptr ? entry->world : 0;
}

extern "C" const char* RsMaps_WorldName(int32_t world) {
    static char buf[16];
    if (world == RS_GEN_WORLD_NEUTRAL) {
        return "neutral";
    }
    if (world == RS_GEN_WORLD_SOLO) {
        return "solo";
    }
    std::snprintf(buf, sizeof(buf), "0x%X", static_cast<unsigned>(world));
    return buf;
}
