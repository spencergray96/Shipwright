// The boat prototype's steering label (sturdy-bassoon#256): "Not steering" in white or "Steering boat" in green
// ("Steering balloon" at a balloon's controls, #274),
// just under the C buttons, while there are no steering animations or poses to say it. On at all times in a
// build of the prototype, scene with a boat or not; `boat hud off` (CVAR_RS_BOAT_HUD) hides it.
//
// RegionOverlay.cpp's pattern: an ImGui window drawn over the game, with Draw() overridden so no titled,
// saved-settings window opens and the gate is here rather than on the GuiWindow's visibility flag.

#include "RsBoatHud.h"

#include <memory>
#include <string>

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "RsBoat.h"
#include "RsBalloon.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

const char* const kWindowKey = "Boat Steering Label##RS";

const ImVec4 kNotSteering = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
const ImVec4 kSteering = ImVec4(0.35f, 1.0f, 0.35f, 1.0f);

int32_t sShown = RS_BOAT_HUD_HIDDEN; // what the last frame drew, for `boat list`

class RsBoatHudWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }

    void Draw() override {
        // Gameplay only: no play state on the title screen and file select, and the pause menu has its own
        // screen the label would sit on top of.
        if (!CVarGetInteger(CVAR_RS_BOAT_HUD, 1) || gPlayState == nullptr || gPlayState->pauseCtx.state != 0) {
            sShown = RS_BOAT_HUD_HIDDEN;
            return;
        }
        const int32_t what = RsBoatHud_LinkSteering();
        const bool steering = what != RS_BOAT_HUD_NOT_STEERING;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(vp->ID);
        // Under the C buttons, right-aligned to them: they take the top fifth of the screen at the right edge,
        // and the region overlay starts at 0.30, so 0.22 sits between the two.
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.955f, vp->Pos.y + vp->Size.y * 0.22f),
                                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::Begin("RsBoatSteeringLabel", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextColored(steering ? kSteering : kNotSteering, "%s",
                           what == RS_BOAT_HUD_STEERING_BALLOON ? "Steering balloon"
                           : steering                           ? "Steering boat"
                                                                : "Not steering");
        ImGui::End();
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(2);
        sShown = what;
    }
};

// ShipInit "*" functions re-run on preset apply and config load, and Gui::AddGuiWindow logs an error on a
// duplicate name, so look before adding. The empty CVar name keeps it out of shipofharkinian.json's window list.
void RegisterRsBoatHud() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui->GetGuiWindow(kWindowKey) != nullptr) {
        return;
    }
    gui->AddGuiWindow(std::make_shared<RsBoatHudWindow>("", true, std::string(kWindowKey)));
}

RegisterShipInitFunc rsBoatHudInitFunc(RegisterRsBoatHud);

} // namespace

int32_t RsBoatHud_LinkSteering() {
    if (gPlayState == nullptr) {
        return RS_BOAT_HUD_NOT_STEERING;
    }
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_RS_BOAT && a->update != nullptr && reinterpret_cast<const RsBoat*>(a)->helm) {
            return RS_BOAT_HUD_STEERING;
        }
    }
    return RsBalloon_LinkAtControls(gPlayState) ? RS_BOAT_HUD_STEERING_BALLOON : RS_BOAT_HUD_NOT_STEERING;
}

int32_t RsBoatHud_Shown() {
    return sShown;
}

const char* RsBoatHud_ShownName(int32_t shown) {
    switch (shown) {
        case RS_BOAT_HUD_STEERING:
            return "steering";
        case RS_BOAT_HUD_STEERING_BALLOON:
            return "steering_balloon";
        case RS_BOAT_HUD_NOT_STEERING:
            return "not_steering";
        default:
            return "hidden";
    }
}
