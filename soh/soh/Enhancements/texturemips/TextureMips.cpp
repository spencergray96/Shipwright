/*
 * Game-side half of the mipmaps for this mod's own scene textures (sturdy-bassoon#146).
 *
 * libultraship mipmaps only textures loaded by display lists the host has named, and two things name
 * any: TextureMips_RegisterRoom here, and ArchiveProps::OfferRoom for a room's archive prop lists
 * (#171), which names and withdraws them with the resources it holds. Both are reached only from the
 * compiled-in branch of the room load, the one a scene defined in this fork's C takes. So "vanilla
 * textures are never mipmapped" is a property of where the calls sit, the same argument
 * StaticBakeRegistry.cpp makes.
 *
 * The switch is the saved setting CVAR_TEXTURE_MIPS (Settings > Graphics, and the human
 * `mipmaps on|off`), on by default. `agenttest mipmaps on|off` moves it for the session only, so an
 * agent's A/B never leaves the owner's setting off.
 */

#include "TextureMips.h"

#include <fast/Fast3dWindow.h>
#include <fast/TextureMips.h>
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

void ApplySwitch(bool on, const char* why) {
    if (on != Fast::TextureMipsIsEnabled()) {
        SPDLOG_INFO("[texturemips] mipmaps for this mod's scene textures switched {} ({})", on ? "on" : "off", why);
    }
    Fast::TextureMipsSetEnabled(on);
}

// ShipInit runs this at boot and again whenever CVAR_TEXTURE_MIPS may have changed (the menu
// checkbox, a preset or config load).
void OnTextureMipsSetting() {
    ApplySwitch(TextureMips_Setting() != 0, "setting");
}

// A session-only choice (`agenttest mipmaps lod|bias`) is in force: the saved setting stays out of the
// way - on a room load and on a preset or config reload alike - until the human command saves one.
bool sLodSessionOverride = false;

void ApplyLodSetting() {
    if (sLodSessionOverride) {
        return;
    }
    Fast::TextureMipsSetLod(CVarGetInteger(CVAR_TEXTURE_MIPS_LOD, TEXTURE_MIPS_LOD_DEFAULT),
                            CVarGetFloat(CVAR_TEXTURE_MIPS_BIAS, 0.0f));
}

void RegisterTextureMipsWidgets() {
    WidgetPath path = { "Settings", "Graphics", SECTION_COLUMN_2 };
    SohGui::mSohMenu->AddWidget(path, "Mipmaps for Mod Scenes", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_TEXTURE_MIPS)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!TextureMips_BackendSupported()) {
                info.options->disabled = true;
                info.options->disabledTooltip = "Available only on DirectX 11";
            }
        })
        .Options(UIWidgets::CheckboxOptions()
                     .Tooltip("Gives the textures of this mod's own scenes smaller copies for the distance, so "
                              "far-off walls, barrels and crates stop shimmering as the camera moves. Vanilla "
                              "scenes and texture packs are not affected. Takes effect at once.")
                     .DefaultValue(TEXTURE_MIPS_DEFAULT != 0));
}

static RegisterShipInitFunc sTextureMipsInit(OnTextureMipsSetting, { CVAR_TEXTURE_MIPS });
static RegisterShipInitFunc sTextureMipsLodInit(ApplyLodSetting, { CVAR_TEXTURE_MIPS_LOD, CVAR_TEXTURE_MIPS_BIAS });
static RegisterMenuInitFunc sTextureMipsMenuInit(RegisterTextureMipsWidgets);

} // namespace

extern "C" void TextureMips_RegisterRoom(PlayState* play, RoomContext* roomCtx) {
    if (play == nullptr || roomCtx == nullptr) {
        return;
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
    // Translucent lists too: unlike the bake, which only takes opaque ones, a mip is as right for a
    // see-through texture as for a solid one.
    for (u32 i = 0; i < shape->num; i++) {
        Fast::TextureMipsRegisterDisplayList(entries[i].opa);
        Fast::TextureMipsRegisterDisplayList(entries[i].xlu);
    }
    // Applied again at every room load, in case ShipInit ran before the renderer existed.
    ApplyLodSetting();
    uint32_t lists = 0;
    uint32_t addrs = 0;
    uint64_t mipped = 0;
    Fast::TextureMipsGetStats(&lists, &addrs, &mipped);
    SPDLOG_INFO("[texturemips] scene {:#x} room {}: {} display list(s) named; {} in scope, {} raw addresses seen, "
                "{} mipped uploads; mipmaps {}",
                play->sceneNum, roomCtx->curRoom.num, shape->num, lists, addrs, mipped,
                Fast::TextureMipsIsEnabled() ? "on" : "off");
}

extern "C" void TextureMips_SetActive(int active) {
    ApplySwitch(active != 0, "session");
}

extern "C" void TextureMips_SetSetting(int active) {
    CVarSetInteger(CVAR_TEXTURE_MIPS, active != 0 ? 1 : 0);
    CVarSave();
    ApplySwitch(active != 0, "setting");
}

extern "C" void TextureMips_SetLod(int mode, float bias, int save) {
    if (save) {
        sLodSessionOverride = false;
        CVarSetInteger(CVAR_TEXTURE_MIPS_LOD, mode);
        CVarSetFloat(CVAR_TEXTURE_MIPS_BIAS, bias);
        CVarSave();
    } else {
        sLodSessionOverride = true;
    }
    Fast::TextureMipsSetLod(mode, bias);
}

extern "C" int TextureMips_Setting(void) {
    return CVarGetInteger(CVAR_TEXTURE_MIPS, TEXTURE_MIPS_DEFAULT) != 0 ? 1 : 0;
}

extern "C" int TextureMips_BackendSupported(void) {
    // Only DX11 builds a chain (GfxRenderingAPI::SetNextUploadMipmaps); on any other backend the
    // switch can be on and every texture still uploads one level.
    auto context = Ship::Context::GetRawInstance();
    auto window = context != nullptr ? context->GetWindow() : nullptr;
    return window != nullptr && window->GetWindowBackend() == Fast::WindowBackend::FAST3D_DXGI_DX11 ? 1 : 0;
}

extern "C" int TextureMips_IsActive(void) {
    return Fast::TextureMipsIsEnabled() && TextureMips_BackendSupported() ? 1 : 0;
}
