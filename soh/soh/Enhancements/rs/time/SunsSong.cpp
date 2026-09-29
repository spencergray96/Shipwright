#include "SunsSong.h"

#include <cstdarg>
#include <cstdio>

#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/custom/scenes/grid_tool/GridToolSceneRegistry.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

// See SunsSong.h for what this replaces and what it deliberately leaves out.

namespace {

// --- tuning -------------------------------------------------------------------------------------

// Vanilla's reload fades out FAST and back in at the normal speed: transFadeDuration 20 and 60
// (Play_Update, z_play.c), counted in VIs at an update rate of 3 - so about 7 and 20 game ticks. The
// hold stands in for the load itself, which a same-scene reload spends at full black.
constexpr int32_t kFadeOutTicks = 7;
constexpr int32_t kHoldTicks = 4;
constexpr int32_t kFadeInTicks = 20;

// Vanilla's two targets (z_parameter.c): midnight when played by day, just past noon by night.
constexpr u16 kMidnight = 0x0000;
constexpr u16 kNoon = 0x8001;

// nextDayTime values Play_Init leaves after a reload, which Environment_Update counts down by 0x10 a
// tick to the rooster (0xFF0E) or the dog (0xFF0D).
constexpr u16 kQueueRooster = 0xFFFE;
constexpr u16 kQueueDog = 0xFFFD;

// The engine's night: Environment_Update's nightFlag threshold. The song's own day window,
// 0x4555 <= dayTime < 0xC001, is exactly its complement, so one test serves both.
bool IsNight(u16 time) {
    return time > 0xC000 || time < 0x4555;
}

enum class Phase { Idle, FadeOut, Hold, FadeIn };

struct Run {
    Phase phase = Phase::Idle;
    int32_t phaseTick = 0;
    int32_t ticks = 0;
    int16_t scene = -1;
    u16 to = 0;
    bool white = false;
};

Run sRun;

void Marker(const char* fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    AgentTest_WriteMarker(line);
}

// The staircase controller's fill (Stairs.cpp SetFill), in the song's colour.
void SetFill(PlayState* play, int32_t alpha) {
    const u8 shade = sRun.white ? 255 : 0;
    play->envCtx.fillScreen = true;
    play->envCtx.screenFillColor[0] = shade;
    play->envCtx.screenFillColor[1] = shade;
    play->envCtx.screenFillColor[2] = shade;
    play->envCtx.screenFillColor[3] = static_cast<u8>(alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha));
}

void ClearFill(PlayState* play) {
    play->envCtx.fillScreen = false;
    play->envCtx.screenFillColor[3] = 0;
}

int32_t FadeAlpha(int32_t t, int32_t n) {
    return n <= 0 ? 255 : (255 * t) / n;
}

// A scene change is the one thing that ends a fade from outside - a death, a warp, a console
// `entrance`. Play_Init resets the screen fill itself, and a new scene has a Link of its own.
void Abort(const char* reason) {
    if (sRun.phase == Phase::Idle) {
        return;
    }
    Marker("rs_suns event=abort reason=%s ticks=%d", reason, sRun.ticks);
    sRun = Run();
}

// What the reload did to the clock. Behind a full fill, so nothing is seen to jump.
void Swap() {
    gSaveContext.skyboxTime = gSaveContext.dayTime = sRun.to;
    // Environment_Update re-derives this every frame; set here too so anything reading IS_NIGHT this
    // tick agrees with the new clock (agenttest time does the same).
    gSaveContext.nightFlag = IsNight(gSaveContext.dayTime) ? 1 : 0;
    if (sRun.to == kNoon) {
        // Play_Init's accounting for a new day, which the reload reached through nextDayTime == 0x8001.
        gSaveContext.totalDays++;
        gSaveContext.bgsDayCount++;
        gSaveContext.dogIsLost = true;
        gSaveContext.nextDayTime = kQueueRooster;
    } else {
        gSaveContext.nextDayTime = kQueueDog;
    }
    Marker("rs_suns event=swap time=0x%04X night=%d total_days=%d", gSaveContext.dayTime, gSaveContext.nightFlag,
           gSaveContext.totalDays);
}

void Finish(PlayState* play, Player* player) {
    ClearFill(play);
    // CsAction 7 ends a cutscene action from any state, and clears the halt with it.
    Player_SetCsAction(play, nullptr, 7);
    // Play_Init opens this box on the reload's new day; here it waits until the screen is clear.
    if (sRun.to == kNoon && (Inventory_HatchWeirdEgg(play) || Inventory_HatchPocketCucco(play))) {
        Message_StartTextbox(play, 0x3066, nullptr);
    }
    Marker("rs_suns event=done ticks=%d time=0x%04X pos=%.1f,%.1f,%.1f", sRun.ticks, gSaveContext.dayTime,
           player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z);
    sRun = Run();
}

// Takes the song over in a compiled-in custom scene. False leaves it to vanilla's reload.
bool BeginInPlace(PlayState* play) {
    if (play == nullptr || !GridToolSceneRegistry_IsCustomScene(play->sceneNum) || GET_PLAYER(play) == nullptr) {
        return false;
    }
    if (sRun.phase != Phase::Idle) {
        return true; // already fading: the song cannot be played again mid-fade, but a trigger can arrive
    }
    const u16 from = gSaveContext.dayTime;
    const bool day = !IsNight(from);
    sRun.phase = Phase::FadeOut;
    sRun.scene = play->sceneNum;
    sRun.to = day ? kMidnight : kNoon;
    sRun.white = !day; // the reload's colours: FADE_BLACK_FAST by day, FADE_WHITE_FAST by night

    Player* player = GET_PLAYER(play);
    // Frozen with no cutscene actor, as a staircase move freezes him - CsAction 1 turns Link to face
    // its csActor every frame, and with none he keeps his own facing - and with every other actor halted,
    // as the reload halts them (haltAllActors) for its fade.
    Player_SetCsActionWithHaltedActors(play, nullptr, 1);

    Marker("rs_suns event=begin scene=0x%X from=0x%04X to=0x%04X fill=%s pos=%.1f,%.1f,%.1f", play->sceneNum, from,
           sRun.to, sRun.white ? "white" : "black", player->actor.world.pos.x, player->actor.world.pos.y,
           player->actor.world.pos.z);
    return true;
}

void OnPlayerUpdateSunsSong() {
    if (sRun.phase == Phase::Idle) {
        return;
    }
    PlayState* play = gPlayState;
    if (play == nullptr || play->sceneNum != sRun.scene || GET_PLAYER(play) == nullptr) {
        Abort("scene_changed");
        return;
    }
    Player* player = GET_PLAYER(play);
    sRun.ticks++;

    switch (sRun.phase) {
        case Phase::FadeOut:
            sRun.phaseTick++;
            SetFill(play, FadeAlpha(sRun.phaseTick, kFadeOutTicks));
            if (sRun.phaseTick >= kFadeOutTicks) {
                Swap();
                sRun.phase = Phase::Hold;
                sRun.phaseTick = 0;
            }
            return;
        case Phase::Hold:
            if (++sRun.phaseTick >= kHoldTicks) {
                sRun.phase = Phase::FadeIn;
                sRun.phaseTick = kFadeInTicks;
            }
            return;
        case Phase::FadeIn:
            sRun.phaseTick--;
            if (sRun.phaseTick > 0) {
                SetFill(play, FadeAlpha(sRun.phaseTick, kFadeInTicks));
                return;
            }
            Finish(play, player);
            return;
        case Phase::Idle:
            return;
    }
}

void OnSceneInitSunsSong(int16_t sceneNum) {
    Abort("scene_changed");
}

void RegisterSunsSongHooks() {
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateSunsSong);
    COND_HOOK(OnSceneInit, true, OnSceneInitSunsSong);
    COND_VB_SHOULD(VB_SUNS_SONG_RELOAD_SCENE, true, {
        if (BeginInPlace(gPlayState)) {
            *should = false;
        }
    });
}

RegisterShipInitFunc sunsSongHooksInitFunc(RegisterSunsSongHooks);

} // namespace
