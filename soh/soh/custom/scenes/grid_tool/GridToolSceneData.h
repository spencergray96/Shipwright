#pragma once
// Shim. Everything that used to live here now lives in ../CustomSceneData.h, shared with
// test_level, because the two copies drifted apart on SURFACETYPE1 and only one of them was right
// (issue #41).
//
// Kept under this name because it is the include line the generators write into every scene file
// they emit: tools/grid-scene-tool/server/cExport.ts (collision, model, model-info) and
// sceneTemplate.ts, plus tools/terrain/fast64-scene-cpp.ts and integrate-fast64.ts. Nothing
// generates this file itself, so a re-export cannot clobber the shim.
#include "../CustomSceneData.h"
