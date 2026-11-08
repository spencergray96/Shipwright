// The pause map's fog of war (sturdy-bassoon#303). MapReveal.h is the contract and the why.
#include "MapReveal.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include <spdlog/spdlog.h>

#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

// --- the worlds and their chunks, from the world export's include --------------------------------------------
// The same scenes WorldMap.cpp opts in, read the same way MusicZoneTable.cpp and WorldMap.cpp read them.
struct RevealScene {
    int16_t sceneId;
    int32_t directorRsX; // the RS tile whose NORTH-west corner is OoT (0, 0): the director's anchor
    int32_t directorRsY;
    int32_t unitsPerTile;
};

#define RS_WORLD_SCENE(scene_, originRsX_, originRsY_, directorRsX_, directorRsY_, unitsPerTile_) \
    { scene_, directorRsX_, directorRsY_, unitsPerTile_ },
#define RS_WORLD_CHUNK(scene_, squareX_, squareY_, title_, nodeTitle_, nodeRsX_, nodeRsY_, nodeX_, nodeZ_)
const RevealScene kScenes[] = {
#include "soh/custom/scenes/grid_tool/f2p_overworld/f2p_overworld_world.inc"
#if __has_include("soh/custom/scenes/grid_tool/agent298_storeys/agent298_storeys_world.inc")
#include "soh/custom/scenes/grid_tool/agent298_storeys/agent298_storeys_world.inc"
#endif
};
#undef RS_WORLD_SCENE
#undef RS_WORLD_CHUNK

#define RS_WORLD_SCENE(scene_, originRsX_, originRsY_, directorRsX_, directorRsY_, unitsPerTile_)
#define RS_WORLD_CHUNK(scene_, squareX_, squareY_, title_, nodeTitle_, nodeRsX_, nodeRsY_, nodeX_, nodeZ_) \
    { scene_, squareX_, squareY_, title_, nodeTitle_, nodeRsX_, nodeRsY_ },
const RsMapChunk kChunks[] = {
#include "soh/custom/scenes/grid_tool/f2p_overworld/f2p_overworld_world.inc"
#if __has_include("soh/custom/scenes/grid_tool/agent298_storeys/agent298_storeys_world.inc")
#include "soh/custom/scenes/grid_tool/agent298_storeys/agent298_storeys_world.inc"
#endif
};
#undef RS_WORLD_SCENE
#undef RS_WORLD_CHUNK

const RevealScene* SceneOf(int16_t sceneId) {
    for (const RevealScene& s : kScenes) {
        if (s.sceneId == sceneId) {
            return &s;
        }
    }
    return nullptr;
}

std::map<int16_t, std::vector<RsMapChunk>>& ChunksByScene() {
    static std::map<int16_t, std::vector<RsMapChunk>> byScene;
    static bool built = false;
    if (!built) {
        built = true;
        for (const RsMapChunk& c : kChunks) {
            byScene[c.sceneId].push_back(c);
            if (WORLD_FLAG_MAP_SQUARE(c.sx, c.sy) < 0) {
                // Drawn revealed rather than hidden; widen the window (WorldFlagIds.h) when this appears.
                SPDLOG_WARN("[mapreveal] scene 0x{:X} square {},{} ({}) is outside the map reveal window: no fog there",
                            c.sceneId, c.sx, c.sy, c.title);
            }
        }
    }
    return byScene;
}

bool SameNode(const RsMapChunk& a, const RsMapChunk& b) {
    return a.sceneId == b.sceneId && a.nodeRsX == b.nodeRsX && a.nodeRsY == b.nodeRsY &&
           std::strcmp(a.nodeTitle, b.nodeTitle) == 0;
}

RsMapRevealStats sStats = { 0, -1, -1, "" };

int32_t FloorDiv(int32_t a, int32_t b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

void MarkerReveal(int32_t sx, int32_t sy, int32_t flag, const char* cause, const char* node) {
    char line[200];
    std::snprintf(line, sizeof(line), "rs_menu op=map event=reveal square=%d,%d flag=%d cause=%s node=\"%s\"", sx, sy,
                  flag, cause, node != nullptr ? node : "");
    AgentTest_WriteMarker(line);
}

// Sets one flag, with its marker. 1 set now, 0 already set, -1 no flag.
int32_t SetSquare(int32_t sx, int32_t sy, const char* cause, const char* node) {
    const int32_t flag = WORLD_FLAG_MAP_SQUARE(sx, sy);
    if (flag < 0) {
        return -1;
    }
    sStats.lastSx = sx;
    sStats.lastSy = sy;
    sStats.lastCause = cause;
    if (Flags_GetWorldFlag(flag)) {
        return 0;
    }
    Flags_SetWorldFlag(flag);
    MarkerReveal(sx, sy, flag, cause, node);
    return 1;
}

// --- entry -----------------------------------------------------------------------------------------------------
void OnPlayerUpdateMapReveal() {
    int32_t sx = 0;
    int32_t sy = 0;
    if (!RsMapReveal_LinkSquare(&sx, &sy) || RsMapReveal_IsRevealed(sx, sy)) {
        return;
    }
    // A square no chunk row names (off the matrix) has nothing to show; only a listed one is revealed.
    if (RsMapReveal_ChunkAt(gPlayState->sceneNum, sx, sy) == nullptr) {
        return;
    }
    if (RsMapReveal_RevealNode(sx, sy, "entry") > 0) {
        sStats.entryReveals++;
    }
}

void RegisterMapRevealHooks() {
    ChunksByScene(); // the boot line for squares outside the window
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateMapReveal);
}

RegisterShipInitFunc mapRevealInitFunc(RegisterMapRevealHooks);

} // namespace

const std::vector<RsMapChunk>& RsMapReveal_Chunks(int16_t sceneId) {
    static const std::vector<RsMapChunk> kNone;
    auto& byScene = ChunksByScene();
    auto it = byScene.find(sceneId);
    return it != byScene.end() ? it->second : kNone;
}

const RsMapChunk* RsMapReveal_ChunkAt(int16_t sceneId, int32_t sx, int32_t sy) {
    for (const RsMapChunk& c : RsMapReveal_Chunks(sceneId)) {
        if (c.sx == sx && c.sy == sy) {
            return &c;
        }
    }
    return nullptr;
}

int32_t RsMapReveal_Flag(int32_t sx, int32_t sy) {
    return WORLD_FLAG_MAP_SQUARE(sx, sy);
}

bool RsMapReveal_IsRevealed(int32_t sx, int32_t sy) {
    const int32_t flag = WORLD_FLAG_MAP_SQUARE(sx, sy);
    return flag < 0 || Flags_GetWorldFlag(flag) != 0;
}

int32_t RsMapReveal_RevealChunk(int32_t sx, int32_t sy, const char* cause) {
    return SetSquare(sx, sy, cause, "");
}

int32_t RsMapReveal_RevealNode(int32_t sx, int32_t sy, const char* cause) {
    bool listed = false;
    int32_t set = 0;
    for (auto& [sceneId, chunks] : ChunksByScene()) {
        const RsMapChunk* at = RsMapReveal_ChunkAt(sceneId, sx, sy);
        if (at == nullptr) {
            continue;
        }
        listed = true;
        for (const RsMapChunk& c : chunks) {
            if (SameNode(c, *at) && SetSquare(c.sx, c.sy, cause, c.nodeTitle) > 0) {
                set++;
            }
        }
    }
    if (!listed) {
        return -1;
    }
    // The square the caller named is the one a run asserts on, whatever order the node's squares went in.
    sStats.lastSx = sx;
    sStats.lastSy = sy;
    sStats.lastCause = cause;
    return set;
}

int32_t RsMapReveal_Hide(int32_t sx, int32_t sy) {
    const int32_t flag = WORLD_FLAG_MAP_SQUARE(sx, sy);
    if (flag < 0) {
        return -1;
    }
    if (!Flags_GetWorldFlag(flag)) {
        return 0;
    }
    Flags_UnsetWorldFlag(flag);
    return 1;
}

int32_t RsMapReveal_RevealAll(int16_t sceneId) {
    int32_t set = 0;
    for (const RsMapChunk& c : RsMapReveal_Chunks(sceneId)) {
        if (SetSquare(c.sx, c.sy, "console", c.nodeTitle) > 0) {
            set++;
        }
    }
    return set;
}

int32_t RsMapReveal_HideAll() {
    int32_t cleared = 0;
    for (int32_t flag = WORLD_FLAG_MAP_FIRST; flag < WORLD_FLAG_MAP_FIRST + WORLD_FLAG_MAP_COUNT; flag++) {
        if (Flags_GetWorldFlag(flag)) {
            Flags_UnsetWorldFlag(flag);
            cleared++;
        }
    }
    return cleared;
}

bool RsMapReveal_LinkSquare(int32_t* sx, int32_t* sy) {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return false;
    }
    const RevealScene* scene = SceneOf(play->sceneNum);
    Player* player = GET_PLAYER(play);
    if (scene == nullptr || scene->unitsPerTile <= 0 || player == nullptr) {
        return false;
    }
    // The zone director's WorldToRs, on the same row: rsX = anchorX + floor(x / 40), rsY = anchorY - floor(z / 40).
    const float perTile = (float)scene->unitsPerTile;
    const int32_t rsX = scene->directorRsX + (int32_t)std::floor(player->actor.world.pos.x / perTile);
    const int32_t rsY = scene->directorRsY - (int32_t)std::floor(player->actor.world.pos.z / perTile);
    *sx = FloorDiv(rsX, 64);
    *sy = FloorDiv(rsY, 64);
    return true;
}

RsMapRevealStats RsMapReveal_Stats() {
    return sStats;
}
