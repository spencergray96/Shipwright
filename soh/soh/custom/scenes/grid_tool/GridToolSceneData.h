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

// Local fixtures (sturdy-bassoon tools/fixtures) test LOCAL_FIXTURE_SLOT_COUNT from here, after including
// this file, rather than testing for LocalFixtureSlots.h with __has_include: MSBuild tracks this include, so
// a branch switch that adds or drops the slots rewrites this file and recompiles every fixture source. A
// __has_include of a missing file leaves no dependency behind, and the fixture then kept stale objects.
#include "LocalFixtureSlots.h"
