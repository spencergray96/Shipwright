/**
 * RedirectKokiriForest.cpp
 *
 * Proof-of-concept for the "custom map" long-term goal: redirects any entrance
 * into Kokiri Forest to one of the game's built-in debug/test scenes instead.
 * These test scenes are leftover developer rooms already shipped in the OTR
 * assets (simple/blank geometry) - no new scene/asset authoring required yet.
 *
 * This works around a Play_Init quirk: setting the transition on OnSceneInit
 * gets immediately overwritten by Play_Init's own "finish loading" logic
 * (play->transitionTrigger = TRANS_TRIGGER_END right after Play_SpawnScene).
 * So we just arm a flag in OnSceneInit, then fire the actual warp one frame
 * later on OnPlayerUpdate, once Play_Init has already finished touching the
 * transition state - the same timing the built-in debug console commands use.
 *
 * To try a different placeholder scene, change TARGET_ENTRANCE below and
 * rebuild (no CMake reconfigure needed - same file). To preview candidates
 * live without recompiling at all, use the built-in debug console:
 *   entrance 94   -> ENTR_TEST01_0     "Test Map"
 *   entrance b6   -> ENTR_DEPTH_TEST_0 "depth test"
 *   entrance 24   -> ENTR_TESTROOM_0   "Treasure Chest Warp" test room
 *   entrance 18   -> ENTR_SASATEST_0   "SRD Map"
 *   entrance 520  -> ENTR_BESITU_0     "Test Room" (has furniture/props)
 *
 * Author: Spencer
 * Created: 2026-08-12
 */

#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// Swap this to try a different built-in debug/test scene - see candidates above.
#define TARGET_ENTRANCE ENTR_TEST01_0

static bool sPendingRedirect = false;

void RedirectKokiriForest_OnSceneInit(int16_t sceneNum) {
    if (sceneNum == SCENE_KOKIRI_FOREST) {
        sPendingRedirect = true;
    }
}

void RedirectKokiriForest_OnPlayerUpdate() {
    if (sPendingRedirect) {
        sPendingRedirect = false;
        gPlayState->nextEntranceIndex = TARGET_ENTRANCE;
        gPlayState->transitionTrigger = TRANS_TRIGGER_START;
        gPlayState->transitionType = TRANS_TYPE_INSTANT;
        gSaveContext.nextTransitionType = TRANS_TYPE_INSTANT;
    }
}

void RegisterRedirectKokiriForest() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(RedirectKokiriForest_OnSceneInit);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(RedirectKokiriForest_OnPlayerUpdate);
}

static RegisterShipInitFunc initFunc(RegisterRedirectKokiriForest);

