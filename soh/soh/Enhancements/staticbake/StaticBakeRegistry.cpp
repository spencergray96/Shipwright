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
 *     is ON by default (sturdy-bassoon#153), and it starts from one of two places, decided once at
 *     boot:
 *       - SOH_STATIC_BAKE=0 or =1 in the process environment - read once, the same shape as
 *         SOH_AGENT_TEST in Enhancements/agenttest/AgentTest.cpp. It holds for the whole session:
 *         the menu checkbox is greyed out, and a preset or config load does not move the switch;
 *       - otherwise the saved setting CVAR_STATIC_BAKE (default 1), which the Settings > Graphics
 *         checkbox and the human `staticbake on|off` command write (StaticBake_SetSetting).
 *     `agenttest staticbake on|off` moves the switch for the session only, so an agent's A/B never
 *     leaves the owner's saved setting off.
 * Keeping them apart is what lets the switch move at runtime: a room loaded while the bake is off is
 * already registered when it comes on, and turning it off keeps every bake, so turning it back on
 * replays them without re-recording. That is the same-session baked/interpreted A/B.
 */

#include "StaticBakeRegistry.h"

#include <cstdlib>
#include <string>

#include <fast/Fast3dWindow.h>
#include <fast/StaticMeshCache.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <spdlog/spdlog.h>

#include "global.h"
#include "soh/ShipInit.hpp"
#include "soh/SohGui/MenuTypes.h"
#include "soh/SohGui/SohMenu.h"
#include "soh/custom/scenes/CustomSceneData.h"

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

namespace {

constexpr const char* BAKE_ENV = "SOH_STATIC_BAKE";

// Set once, by ApplyStartupState: the environment variable was 0 or 1, so it holds for the session.
bool sEnvOverride = false;
std::string sEnvValue;

// Apply the starting state exactly once, before anything reads or writes the switch. Every entry
// point calls it, so the startup state can never override a later switch. The ShipInit hook below
// calls it at boot, so the state line is logged whether or not a compiled-in scene is ever loaded -
// a run can always tell which it had. Returns true on the one call that applied it.
bool ApplyStartupState() {
    static bool applied = false;
    if (applied) {
        return false;
    }
    applied = true;

    const char* value = std::getenv(BAKE_ENV);
    const std::string env = value != nullptr ? value : "";
    bool on;
    std::string source;
    if (env == "0" || env == "1") {
        sEnvOverride = true;
        sEnvValue = env;
        on = env == "1";
        source = std::string(BAKE_ENV) + "=" + env;
    } else {
        if (value != nullptr) {
            SPDLOG_WARN("[staticbake] {} is set but is neither 0 nor 1; ignoring it", BAKE_ENV);
        }
        // A sentinel default tells a saved setting from none (CVarExists is declared but never
        // defined), so the log line can say which decided.
        const int32_t saved = CVarGetInteger(CVAR_STATIC_BAKE, -1);
        on = (saved == -1 ? STATIC_BAKE_DEFAULT : saved) != 0;
        source = saved == -1 ? std::string("default; ") + CVAR_STATIC_BAKE + " not saved"
                             : std::string("setting ") + CVAR_STATIC_BAKE + "=" + std::to_string(saved);
    }
    Fast::StaticBakeSetEnabled(on);
    SPDLOG_INFO("[staticbake] bake starts {} ({}): compiled-in room geometry {} baked", on ? "on" : "off", source,
                on ? "will be" : "will not be");
    return true;
}

// Moves the switch, and logs a change so a run log shows every flip. Saves nothing.
void ApplySwitch(bool on) {
    if (on != Fast::StaticBakeIsEnabled()) {
        SPDLOG_INFO("[staticbake] bake switched {}", on ? "on" : "off");
    }
    Fast::StaticBakeSetEnabled(on);
}

// ShipInit runs this once at boot and again whenever CVAR_STATIC_BAKE may have changed - the menu
// checkbox, or a preset or config load (ShipInit::Init("*")). The boot call applies the starting
// state; a later one means the setting moved, and the switch follows it - unless the environment
// variable decided this session, which then holds.
void OnStaticBakeSetting() {
    if (ApplyStartupState() || sEnvOverride) {
        return;
    }
    ApplySwitch(StaticBake_Setting() != 0);
}

void RegisterStaticBakeWidgets() {
    WidgetPath path = { "Settings", "Graphics", SECTION_COLUMN_2 };
    SohGui::mSohMenu->AddWidget(path, "Static Geometry Bake", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_STATIC_BAKE)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            // Built here rather than through SohMenu's disabledMap, which is not reachable from
            // outside the menu class. The tooltip strings are literals, so they outlive the frame.
            if (Ship::Context::GetRawInstance()->GetWindow()->GetWindowBackend() !=
                Fast::WindowBackend::FAST3D_DXGI_DX11) {
                // Only the DX11 backend replays a bake (StaticBakeIntercept's SupportsStaticBake).
                info.options->disabled = true;
                info.options->disabledTooltip = "Available only on DirectX 11";
            } else if (sEnvOverride) {
                info.options->disabled = true;
                info.options->disabledTooltip =
                    sEnvValue == "1" ? "SOH_STATIC_BAKE=1 was set at launch: the bake is on this session"
                                     : "SOH_STATIC_BAKE=0 was set at launch: the bake is off this session";
            }
        })
        .Options(UIWidgets::CheckboxOptions()
                     .Tooltip("Records the room geometry of this mod's own scenes once and replays it from the GPU, "
                              "instead of rebuilding it every frame. Much faster on large maps. Takes effect at "
                              "once; the first frame of a room pays for the recording. Vanilla scenes are never "
                              "baked. The SOH_STATIC_BAKE environment variable (0 or 1) overrides this for a "
                              "session.")
                     .DefaultValue(STATIC_BAKE_DEFAULT != 0));
}

static RegisterShipInitFunc sStaticBakeInit(OnStaticBakeSetting, { CVAR_STATIC_BAKE });
static RegisterMenuInitFunc sStaticBakeMenuInit(RegisterStaticBakeWidgets);

// Registrations are keyed by display-list pointer and a scene's display lists are its own
// symbols, so entries from a scene that is no longer loaded are dead weight holding GPU buffers
// open. Nothing else would ever free them.
s32 sLastScene = -1;

} // namespace

extern "C" void StaticBake_SetActive(int active) {
    ApplyStartupState();
    ApplySwitch(active != 0);
}

extern "C" void StaticBake_SetSetting(int active) {
    ApplyStartupState();
    // Saved, so the choice survives a restart; the environment variable still decides the next boot
    // if it is set then.
    CVarSetInteger(CVAR_STATIC_BAKE, active != 0 ? 1 : 0);
    CVarSave();
    ApplySwitch(active != 0);
}

extern "C" int StaticBake_Setting(void) {
    return CVarGetInteger(CVAR_STATIC_BAKE, STATIC_BAKE_DEFAULT) != 0 ? 1 : 0;
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
