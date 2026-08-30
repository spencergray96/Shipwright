/*
 * Game-side half of the static-geometry bake (sturdy-bassoon#40 Stage 1).
 *
 * libultraship's StaticMeshCache will only consider a display list the host has explicitly handed
 * it. This file is the only thing that ever hands it one, and it is only reached from the
 * compiled-in room-load branch - the path a scene defined in this fork's C takes, and the one an
 * OTR-loaded vanilla scene never does. So "vanilla scenes never engage the bake" is a property of
 * where this call sits, not of a check inside it.
 *
 * Gated by SOH_STATIC_BAKE=1 in the process environment, read once, the same shape as
 * SOH_AGENT_TEST in Enhancements/agenttest/AgentTest.cpp. With it unset nothing is registered, so
 * every interpreter hook downstream early-outs on an empty registry and the renderer behaves
 * exactly as it did before.
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

// One getenv for the life of the process. A function-local static is initialised exactly once and
// is the cheapest correct way to say that here; the branch that reads it is not on any hot path
// anyway (once per room load).
bool BakeEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv(BAKE_ENV);
        const bool on = value != nullptr && std::string(value) == "1";
        if (on) {
            Fast::StaticBakeSetEnabled(true);
            SPDLOG_INFO("[staticbake] {}=1: compiled-in room geometry will be baked", BAKE_ENV);
        }
        return on;
    }();
    return enabled;
}

// Registrations are keyed by display-list pointer and a scene's display lists are its own
// symbols, so entries from a scene that is no longer loaded are dead weight holding GPU buffers
// open. Nothing else would ever free them.
s32 sLastScene = -1;

} // namespace

extern "C" void StaticBake_RegisterRoom(PlayState* play, RoomContext* roomCtx) {
    if (!BakeEnabled() || play == nullptr || roomCtx == nullptr) {
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
                "{} entries ({} baked, {} rejected)",
                play->sceneNum, roomCtx->curRoom.num, registered, total, baked, rejected);
}
