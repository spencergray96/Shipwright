/*
 * Game-side half of the static-geometry bake (sturdy-bassoon#40 Stage 1, #142 Stage 2).
 *
 * libultraship's StaticMeshCache will only consider a display list the host has explicitly handed
 * it. This file is the only thing that ever hands it one, and it is only reached from the
 * compiled-in room-load branch - the path a scene defined in this fork's C takes, and the one an
 * OTR-loaded vanilla scene never does. So "vanilla scenes never engage the bake" is a property of
 * where this call sits, not of a check inside it.
 *
 * Two separate things, on purpose:
 *   - REGISTRATION always happens for a compiled-in room. It is a map insert per opaque display
 *     list, and with the bake off the interpreter's hooks early-out on the switch before looking at
 *     the map, so the renderer behaves exactly as if nothing were registered.
 *   - The SWITCH (StaticBake_SetActive) decides whether registered lists are baked and replayed. It
 *     starts on when SOH_STATIC_BAKE=1 is in the process environment - read once, the same shape as
 *     SOH_AGENT_TEST in Enhancements/agenttest/AgentTest.cpp - and off otherwise. After that it is
 *     whatever StaticBake_SetActive last set: the `staticbake` console command today, a setting
 *     later (sturdy-bassoon#153).
 * Keeping them apart is what lets the switch move at runtime: a room loaded while the bake is off is
 * already registered when it comes on, and turning it off keeps every bake, so turning it back on
 * replays them without re-recording. That is the same-session baked/interpreted A/B.
 */

#include "StaticBakeRegistry.h"

#include <cstdlib>
#include <string>

#include <fast/StaticMeshCache.h>
#include <spdlog/spdlog.h>

#include "global.h"
#include "soh/custom/scenes/CustomSceneData.h"

namespace {

constexpr const char* BAKE_ENV = "SOH_STATIC_BAKE";

// Apply the starting state exactly once, before anything reads or writes the switch: a
// function-local static is initialised once and is the cheapest correct way to say that. Every
// entry point calls it, so the environment can never override a later StaticBake_SetActive.
void ApplyStartupState() {
    static const bool applied = [] {
        const char* value = std::getenv(BAKE_ENV);
        const bool on = value != nullptr && std::string(value) == "1";
        Fast::StaticBakeSetEnabled(on);
        if (on) {
            SPDLOG_INFO("[staticbake] {}=1: compiled-in room geometry will be baked", BAKE_ENV);
        }
        return true;
    }();
    (void)applied;
}

// Registrations are keyed by display-list pointer and a scene's display lists are its own
// symbols, so entries from a scene that is no longer loaded are dead weight holding GPU buffers
// open. Nothing else would ever free them.
s32 sLastScene = -1;

} // namespace

extern "C" void StaticBake_SetActive(int active) {
    ApplyStartupState();
    const bool on = active != 0;
    if (on != Fast::StaticBakeIsEnabled()) {
        SPDLOG_INFO("[staticbake] bake switched {}", on ? "on" : "off");
    }
    Fast::StaticBakeSetEnabled(on);
}

extern "C" int StaticBake_IsActive(void) {
    ApplyStartupState();
    return Fast::StaticBakeIsEnabled() ? 1 : 0;
}

extern "C" void StaticBake_RegisterRoom(PlayState* play, RoomContext* roomCtx) {
    ApplyStartupState();
    if (play == nullptr || roomCtx == nullptr) {
        return;
    }

    if (play->sceneNum != sLastScene) {
        Fast::StaticBakeReset();
        sLastScene = play->sceneNum;
    }

    MeshHeader* header = (MeshHeader*)roomCtx->curRoom.meshHeader;
    if (header == nullptr || header->base.type != ROOM_SHAPE_TYPE_NORMAL) {
        return;
    }

    RoomShapeNormal* shape = &header->polygon0;
    RoomShapeDListsEntry* entries = (RoomShapeDListsEntry*)shape->start;
    if (entries == nullptr || shape->num == 0) {
        return;
    }

    u32 registered = 0;
    for (u32 i = 0; i < shape->num; i++) {
        // Opaque only. Translucent geometry is a Stage 2 problem: it needs draw order preserved
        // against the actors it is sorted among, which a single replayed buffer cannot express.
        if (entries[i].opa != nullptr) {
            Fast::StaticBakeRegister(entries[i].opa);
            registered++;
        }
    }

    uint32_t total = 0;
    uint32_t baked = 0;
    uint32_t rejected = 0;
    Fast::StaticBakeGetStats(&total, &baked, &rejected);
    SPDLOG_INFO("[staticbake] scene {:#x} room {}: offered {} opaque display list(s); registry now "
                "{} entries ({} baked, {} rejected); bake {}",
                play->sceneNum, roomCtx->curRoom.num, registered, total, baked, rejected,
                Fast::StaticBakeIsEnabled() ? "on" : "off");
}
