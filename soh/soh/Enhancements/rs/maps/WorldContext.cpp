#include "WorldContext.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <spdlog/spdlog.h>

#include "SceneMaps.h"
#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

// See WorldContext.h for what the world context is and why.

namespace {

// The store. A project-owned global, not a gSaveContext member, for the reasons RsPrefs.cpp records -
// and the same accepted costs: savestates do not capture it, and the threaded save reads it live.
int32_t sContext = RS_WORLD_CONTEXT_UNSET;

// Not serialized: where the live value came from, a fact about this session. RsWorld_Source.
const char* sSource = "default";

void Marker(const char* fmt, ...) {
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    AgentTest_WriteMarker(line);
}

// --- SaveManager section ---------------------------------------------------------------------
//
// JSON shape, version 1:
//   "rsWorld": { "context": -3 }
//
// RsPrefs.cpp's version rule: a new key joins at version 1, a changed meaning or type needs version 2.
// A save written before this landed has no section, and reads UNSET - which is what it was.
//
// The value is a SCENE ID for a stitched world. Scene ids are positions in scene_table.h, and an
// export into another tree can renumber one before its branch merges (the agent memory "new scene ids
// collide across trees"), so the loader checks it names a stitched scene THIS build has, and repairs
// anything else to UNSET - logged, never asserted, as RsPrefs repairs a bad convention: the value came
// from a file. UNSET picks a stitched scene first, which is the right guess.

// Every new game, and the top of every LoadFile before the JSON is parsed: a slot switch cannot leak
// the previous file's world.
void WorldInitFile(bool isDebug) {
    sContext = RS_WORLD_CONTEXT_UNSET;
    sSource = "default";
}

void SaveWorld(SaveContext* saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveData("context", sContext);
}

void LoadWorldV1() {
    int32_t context = RS_WORLD_CONTEXT_UNSET;
    SaveManager::Instance->LoadData("context", context, static_cast<int32_t>(RS_WORLD_CONTEXT_UNSET));
    if (!RsWorld_IsValid(context)) {
        SPDLOG_ERROR("RsWorld: the save's world context {} names no stitched scene in this build; using unset",
                     context);
        Marker("rs_worldctx event=repaired value=%d to=unset", context);
        context = RS_WORLD_CONTEXT_UNSET;
    }
    sContext = context;
    sSource = "file";
}

// ENTERING a scene (decision 12): one holding a chunk map sets the context to its world; a neutral one
// leaves it. Every scene says what it did, so a run can assert "the basement left it alone" as well as
// "the stitched scene set it".
void OnSceneInitWorld(int16_t sceneNum) {
    const int32_t world = RsMaps_MapCount(sceneNum) > 0 ? RsMaps_SceneWorld(sceneNum) : RS_GEN_WORLD_NEUTRAL;
    const bool chunk = world == RS_GEN_WORLD_SOLO || world >= 0;
    const int32_t before = sContext;
    if (chunk && world != sContext) {
        RsWorld_Set(world, "scene");
    }
    char was[16];
    std::snprintf(was, sizeof(was), "%s", RsWorld_Name(before));
    Marker("rs_worldctx event=scene scene=0x%X scene_world=%s ctx=%s was=%s changed=%d", sceneNum,
           RsMaps_MapCount(sceneNum) > 0 ? RsMaps_WorldName(world) : "none", RsWorld_Name(sContext), was,
           sContext != before ? 1 : 0);
}

void RegisterWorldContext() {
    // As RsPrefs.cpp: ShipInit re-runs, and AddSaveFunction asserts on a duplicate name.
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    SaveManager::Instance->AddInitFunction(WorldInitFile);
    SaveManager::Instance->AddSaveFunction("rsWorld", 1, SaveWorld, true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction("rsWorld", 1, LoadWorldV1);
    COND_HOOK(OnSceneInit, true, OnSceneInitWorld);
}

RegisterShipInitFunc worldContextInitFunc(RegisterWorldContext);

} // namespace

extern "C" int32_t RsWorld_Get(void) {
    return sContext;
}

extern "C" int32_t RsWorld_IsValid(int32_t context) {
    return context == RS_WORLD_CONTEXT_UNSET || context == RS_GEN_WORLD_SOLO || RsMaps_IsStitched(context) ? 1 : 0;
}

extern "C" int32_t RsWorld_Set(int32_t context, const char* cause) {
    if (!RsWorld_IsValid(context)) {
        return 1;
    }
    char from[16];
    std::snprintf(from, sizeof(from), "%s", RsWorld_Name(sContext));
    sContext = context;
    sSource = cause != nullptr ? cause : "scene"; // a string literal from every caller: `scene`, `console`
    Marker("rs_worldctx event=set from=%s to=%s cause=%s", from, RsWorld_Name(context), cause != nullptr ? cause : "");
    return 0;
}

extern "C" const char* RsWorld_Name(int32_t context) {
    static char buf[16];
    if (context == RS_WORLD_CONTEXT_UNSET) {
        return "unset";
    }
    if (context == RS_GEN_WORLD_SOLO) {
        return "solo";
    }
    std::snprintf(buf, sizeof(buf), "0x%X", static_cast<unsigned>(context));
    return buf;
}

extern "C" const char* RsWorld_Source(void) {
    return sSource;
}
