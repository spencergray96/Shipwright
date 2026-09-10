#include "RegionOverlay.h"

#include <memory>
#include <string>

#include <imgui.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "FloorText.h"
#include "RsPrefs.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

// Process-lifetime state, deliberately not a CVar - see RegionOverlay.h.
bool sEnabled = false;
int32_t sConvention = RS_FLOOR_CONVENTION_DEFAULT;
int32_t sDrawn = 0;

// The Gui registry key. It never reaches ImGui - Draw() is overridden below and opens its own
// window - so the "##" has no ImGui meaning here; it follows the "Console##SoH" convention.
const char* const kWindowKey = "Region Overlay##RS";

const ImVec4 kLabel = ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
const ImVec4 kValue = ImVec4(1.0f, 0.82f, 0.25f, 1.0f);
const ImVec4 kSample = ImVec4(0.55f, 0.85f, 1.0f, 1.0f);

// Overrides Draw() outright, the QuestOverlay / TimeDisplay pattern: the base Draw would gate on
// the GuiWindow's own visibility flag and open a titled, saved-settings window under the registry
// key, and this wants neither. Gui::DrawMenu calls Draw() on every registered window every frame
// regardless of visibility, so the gate has to be here.
class RegionOverlayWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }

    void Draw() override {
        if (!sEnabled || gPlayState == nullptr) {
            sDrawn = 0;
            return;
        }

        // Read the setting HERE, on the frame, rather than caching it anywhere: the whole claim
        // this overlay exists to make visible is that a `region set` is live immediately, with
        // nothing telling the renderer to refresh.
        const int32_t convention = RsPrefs_GetFloorConvention();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(vp->ID);
        // RIGHT edge, at the same third-of-the-way-down the quest overlay uses on the left, with the
        // pivot on the window's own right corner so an AlwaysAutoResize window still lands flush.
        //
        // It sat at 0.18 on the LEFT for one run, and the screenshot is why it does not any more:
        // that band is the MAGIC METER, which drew straight through "Floor convention: US". The
        // left column is spoken for top to bottom - hearts, magic bar, then the quest overlay, then
        // the rupee counter - so the two debug overlays take a column each and can never collide,
        // whichever one is switched on.
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 24.0f, vp->Pos.y + vp->Size.y * 0.30f),
                                ImGuiCond_Always, ImVec2(1.0f, 0.0f));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::Begin("RsRegionOverlay", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(1.25f);

        int32_t drawn = 0;
        ImGui::TextColored(kLabel, "Floor convention:");
        ImGui::SameLine(0.0f, 6.0f);
        // Upper case for the human: "UK" reads as a region, "uk" reads as a console token. The
        // console prints the token; this prints the word. "%s", never the string as the format.
        std::string name = RsPrefs_FloorConventionName(convention);
        for (char& c : name) {
            if (c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }
        ImGui::TextColored(kValue, "%s", name.c_str());
        drawn++;

        // The two rows that make the setting MEAN something on screen. A screenshot of "UK" alone
        // proves the store; a screenshot of "0 -> ground floor" proves the substitution, which is
        // the thing that is actually hard to be sure of.
        for (int32_t storey = 0; storey <= 1; storey++) {
            ImGui::TextColored(kLabel, "  {floor:%d} ->", storey);
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextColored(kSample, "%s", RsFloorText_Label(convention, storey, false).c_str());
            drawn++;
        }

        ImGui::End();
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(2);

        sConvention = convention;
        sDrawn = drawn;
    }
};

// ShipInit "*" functions re-run on preset apply and config load, and Gui::AddGuiWindow logs an
// error on a duplicate name, so look before adding. The empty CVar name is deliberate: GuiWindow
// skips the visibility CVar entirely when it is empty, which is what keeps this out of
// shipofharkinian.json.
void RegisterRegionOverlay() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui->GetGuiWindow(kWindowKey) != nullptr) {
        return;
    }
    gui->AddGuiWindow(std::make_shared<RegionOverlayWindow>("", true, std::string(kWindowKey)));
}

RegisterShipInitFunc regionOverlayInitFunc(RegisterRegionOverlay);

} // namespace

void RsRegionOverlay_SetEnabled(bool enabled) {
    sEnabled = enabled;
}

RsRegionOverlayState RsRegionOverlay_Get() {
    return { sEnabled, sConvention, sDrawn };
}
