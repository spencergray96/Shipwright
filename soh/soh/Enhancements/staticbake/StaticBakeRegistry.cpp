/*
 * Game-side half of the static-geometry bake (sturdy-bassoon#40 Stage 1, #142 Stage 2).
 *
 * libultraship's StaticMeshCache will only consider a display list the host has explicitly handed
 * it. This file is the only thing that ever hands it one, with ArchivePropLists.cpp - which only
 * this file calls, for a room's archive prop lists (sturdy-bassoon#171) - and both are only reached
 * from the compiled-in room-load branch - the path a scene defined in this fork's C takes, and the
 * one an OTR-loaded vanilla scene never does. So "vanilla scenes never engage the bake" is a property of
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
#include "ArchivePropLists.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <vector>

#include <fast/Fast3dWindow.h>
#include <fast/StaticMeshCache.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <spdlog/spdlog.h>

#include "global.h"
#include "soh/ShipInit.hpp"
#include "soh/SohGui/MenuTypes.h"
#include "soh/SohGui/SohMenu.h"
#include "soh/Enhancements/rs/warps/Warps.h"
#include "soh/custom/scenes/CustomSceneData.h"

extern "C" PlayState* gPlayState;

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
            if (!StaticBake_BackendSupported()) {
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

// WHAT STAYS RESIDENT (sturdy-bassoon#157): everything recorded in the current BAKE GROUP, until
// Link enters a compiled-in scene of another group. The group is the step-warp group
// (RsWarp_SceneGroup): an overworld and the underground areas its trapdoors lead to (#148), so
// going back up replays the overworld instead of recording it again - about 1.2 s at the 4.1M F2P
// fixture's scale, for ~650 MB of GPU memory held through the visit (the #157 run). The owner chose
// this policy over freeing each underground scene as Link leaves it, which would need a per-scene
// release in libultraship; an underground scene is small beside its overworld. Nothing is freed
// within a group - no budget, no eviction - so a group's scenes load on top of everything it holds.
//
// Why a kept entry is sound: registrations are keyed by display-list pointer, a compiled-in scene's
// display lists are static symbols at the same address on every load, an archive prop list's
// resource is held by ArchivePropLists.cpp until ResetRegistry (#171), and re-registering a key is a
// no-op - so a returning scene's rooms find their bakes already there. Lights and fog are replay
// uniforms (#142 slice A), so nothing a bake holds goes stale while it waits.
//
// `staticbake link` can join two groups for a session (BakeGroup below), for measuring this on
// content no step warp reaches yet.
//
// Leaving the group frees everything (Fast::StaticBakeReset): the entries from a group Link has left
// are dead weight holding GPU buffers and textures open, and nothing else would ever free them.
// Vanilla scenes never reach this file, so a trip through one keeps whatever the group holds.
s32 sLastGroup = -1;
// The scenes whose rooms registered since the last reset, for the log line and `staticbake status`.
std::vector<s32> sHeldScenes;
// Session-only extra joins between two scenes' groups (`staticbake link`), for measuring a kept return
// on content no step warp reaches yet - the at-scale F2P fixture has no warp tiles.
std::vector<std::array<s32, 2>> sLinks;

// The scene's bake group: its step-warp group, joined through any `staticbake link`s to others, named
// by the smallest id in the result - which with no links is exactly RsWarp_SceneGroup.
s32 BakeGroup(s32 sceneNum) {
    std::vector<s32> groups = { RsWarp_SceneGroup(sceneNum) };
    for (size_t i = 0; i < groups.size(); i++) {
        for (const auto& link : sLinks) {
            const s32 a = RsWarp_SceneGroup(link[0]);
            const s32 b = RsWarp_SceneGroup(link[1]);
            const s32 other = a == groups[i] ? b : b == groups[i] ? a : -1;
            if (other != -1 && std::find(groups.begin(), groups.end(), other) == groups.end()) {
                groups.push_back(other);
            }
        }
    }
    return *std::min_element(groups.begin(), groups.end());
}

void ResetRegistry() {
    Fast::StaticBakeReset();
    // The archive prop lists' resources are held exactly as long as their entries (#171).
    ArchiveProps::ReleaseHeld();
    sHeldScenes.clear();
    sLastGroup = -1;
}

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

extern "C" int StaticBake_BackendSupported(void) {
    // Only the DX11 backend replays a bake (StaticBakeIntercept's SupportsStaticBake); on any other
    // the switch can be on and nothing ever bakes.
    auto context = Ship::Context::GetRawInstance();
    auto window = context != nullptr ? context->GetWindow() : nullptr;
    return window != nullptr && window->GetWindowBackend() == Fast::WindowBackend::FAST3D_DXGI_DX11 ? 1 : 0;
}

extern "C" int StaticBake_IsActive(void) {
    ApplyStartupState();
    return Fast::StaticBakeIsEnabled() && StaticBake_BackendSupported() ? 1 : 0;
}

extern "C" void StaticBake_RegisterRoom(PlayState* play, RoomContext* roomCtx) {
    ApplyStartupState();
    if (play == nullptr || roomCtx == nullptr) {
        return;
    }

    // Before the mesh checks below, on purpose: a compiled-in room with nothing to bake is still in its
    // scene's group, and entering it from another group still frees the old one.
    const s32 group = BakeGroup(play->sceneNum);
    if (group != sLastGroup) {
        if (sLastGroup != -1) {
            uint32_t freed = 0;
            Fast::StaticBakeGetStats(&freed, nullptr, nullptr);
            SPDLOG_INFO("[staticbake] scene {:#x} is in group {:#x}, not {:#x}: reset, freeing {} entries from {} "
                        "scene(s)",
                        play->sceneNum, group, sLastGroup, freed, sHeldScenes.size());
        }
        ResetRegistry();
        sLastGroup = group;
    }
    if (std::find(sHeldScenes.begin(), sHeldScenes.end(), play->sceneNum) == sHeldScenes.end()) {
        sHeldScenes.push_back(play->sceneNum);
    }

    uint32_t before = 0;
    Fast::StaticBakeGetStats(&before, nullptr, nullptr);

    // The room's archive prop lists (#171), offered inside the before/after count so `already held`
    // counts them too. (The #160 proof offered its list outside `registered`, which underflowed it.)
    // Only from a ROOM_SHAPE_TYPE_NORMAL room: func_80095AB4, the one room draw that submits them.
    MeshHeader* header = (MeshHeader*)roomCtx->curRoom.meshHeader;
    const bool normal = header != nullptr && header->base.type == ROOM_SHAPE_TYPE_NORMAL;
    const u32 archived = normal ? ArchiveProps::OfferRoom(play->sceneNum, roomCtx->curRoom.num) : 0;

    RoomShapeDListsEntry* entries = nullptr;
    u32 count = 0;
    if (normal) {
        entries = (RoomShapeDListsEntry*)header->polygon0.start;
        count = header->polygon0.num;
    }
    if ((entries == nullptr || count == 0) && archived == 0) {
        return; // nothing offered, nothing logged - as before #171
    }

    u32 registered = 0;
    for (u32 i = 0; entries != nullptr && i < count; i++) {
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
    // `already held`: offered lists the registry kept from an earlier visit in this group (#157),
    // which replay on their first draw instead of recording - the archive prop lists included.
    // `archive prop list(s)` (#171) goes after the fields older run scripts parse.
    SPDLOG_INFO("[staticbake] scene {:#x} room {}: offered {} opaque display list(s); registry now "
                "{} entries ({} baked, {} rejected); bake {}; {} already held; group {:#x}, {} scene(s) held; "
                "{} archive prop list(s) offered",
                play->sceneNum, roomCtx->curRoom.num, registered, total, baked, rejected,
                Fast::StaticBakeIsEnabled() ? "on" : "off", registered + archived - (total - before), group,
                sHeldScenes.size(), archived);
}

extern "C" void StaticBake_Reset(void) {
    ApplyStartupState();
    uint32_t freed = 0;
    Fast::StaticBakeGetStats(&freed, nullptr, nullptr);
    // Put the current room back if it is one of ours, so it records on its next draw rather than
    // being interpreted until the next room load. Anything else registered is gone.
    const bool current = gPlayState != nullptr && std::find(sHeldScenes.begin(), sHeldScenes.end(),
                                                            gPlayState->sceneNum) != sHeldScenes.end();
    SPDLOG_INFO("[staticbake] reset by command: freeing {} entries from {} scene(s)", freed, sHeldScenes.size());
    ResetRegistry();
    if (current) {
        StaticBake_RegisterRoom(gPlayState, &gPlayState->roomCtx);
    }
}

extern "C" void StaticBake_Link(int sceneA, int sceneB) {
    sLinks.push_back({ sceneA, sceneB });
    // A link only ever merges groups, so what is held stays one group - under a new name, possibly,
    // which has to be followed here or the next registration would read the rename as leaving it.
    if (!sHeldScenes.empty()) {
        sLastGroup = BakeGroup(sHeldScenes.front());
    }
    SPDLOG_INFO("[staticbake] link {:#x} and {:#x} for this session: scene {:#x} is now in group {:#x}", sceneA,
                sceneB, sceneA, BakeGroup(sceneA));
}

extern "C" int StaticBake_Links(void) {
    return (int)sLinks.size();
}

extern "C" int StaticBake_Group(void) {
    return sLastGroup;
}

extern "C" int StaticBake_HeldScenes(void) {
    return (int)sHeldScenes.size();
}
