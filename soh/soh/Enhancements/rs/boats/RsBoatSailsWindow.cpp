// The Boat Sails window (sturdy-bassoon#287): each sail's colour and picture, a preview of the picture inside the
// sail's outline, and the two ways a picture gets into sails/ - the system's file dialog, or a file dropped on the game
// window once a sail is armed for it. Enhancements > Boat Sails opens it; `boat sails window on` too. Everything it
// sets is the save's looks (RsBoatFit_SetLook) and RsBoatSail.h's files, so the console and the sailmaker see the same
// state.

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>
#include <SDL2/SDL.h>
#include <stb_image.h>
#include <fast/Fast3dGui.h>
#include <fast/resource/type/Texture.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/resource/File.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "RsBoatFit.h"
#include "RsBoatSail.h"
#include "RsBoatSailDialog.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

namespace {

const char* const kWindowName = "Boat Sails";
const char* const kTitles[RS_BOAT_SAIL_COUNT] = { "Raft sail", "Skiff sail", "Sloop mainsail", "Sloop jib" };
// The sail's own cloth colour, under a plain pattern: the archive's 226, 226, 222.
const uint8_t kPlain[3] = { 226, 226, 222 };
constexpr int kPreview = 128;

// The system's file browser on the sails folder. The folder's string may be UTF-8 or the code page (RsBoatSail.cpp's
// PathOf), and SDL wants a UTF-8 URL.
void OpenFolder(const std::string& folder) {
    std::filesystem::path path;
    try {
        path = std::filesystem::path(std::u8string(folder.begin(), folder.end()));
    } catch (const std::exception&) {
        path = std::filesystem::path(folder);
    }
    std::error_code ec;
    const std::u8string url = u8"file:///" + std::filesystem::absolute(path, ec).u8string();
    SDL_OpenURL(std::string(url.begin(), url.end()).c_str());
}

// A sail's outline on the picture's square, from its archive's template (generate_props' template_png): 0 outside
// the cloth, 1 inside, 2 on its edge. Empty when the archive has none.
std::vector<uint8_t> LoadOutline(int32_t sail) {
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    char path[256];
    if (!RsBoatSail_TemplateInArchive(sail, path, sizeof(path)) || !archives->HasFile(path)) {
        return {};
    }
    auto file = archives->LoadFile(path);
    if (file == nullptr || file->Buffer == nullptr) {
        return {};
    }
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(file->Buffer->data() + file->BufferOffset),
                                        (int)(file->Buffer->size() - file->BufferOffset), &w, &h, &n, 3);
    if (px == nullptr || w != h || w < RS_BOAT_SAIL_SIZE) {
        stbi_image_free(px);
        return {};
    }
    std::vector<uint8_t> out((size_t)RS_BOAT_SAIL_SIZE * RS_BOAT_SAIL_SIZE);
    const int step = w / RS_BOAT_SAIL_SIZE;
    for (int y = 0; y < RS_BOAT_SAIL_SIZE; y++) {
        for (int x = 0; x < RS_BOAT_SAIL_SIZE; x++) {
            const uint8_t r = px[((size_t)(y * step) * w + x * step) * 3];
            out[(size_t)y * RS_BOAT_SAIL_SIZE + x] = r < 64 ? 2 : (r > 200 ? 1 : 0);
        }
    }
    stbi_image_free(px);
    return out;
}

class RsBoatSailsWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void UpdateElement() override {
    }

    void DrawElement() override {
        ImGui::TextWrapped("Put a picture on a sail: choose an image, or press \"Drop an image now\" and drop one on "
                           "the game window. Pictures live in the sails folder, with a template of each sail's shape.");
        if (ImGui::Button("Open sails folder")) {
            RsBoatSail_Check();
            OpenFolder(RsBoatSail_Folder());
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload pictures")) {
            RsBoatSail_Reload();
        }
        for (int32_t sail = 0; sail < RS_BOAT_SAIL_COUNT; sail++) {
            ImGui::Separator();
            DrawSail(sail);
        }
    }

  private:
    std::array<std::vector<uint8_t>, RS_BOAT_SAIL_COUNT> mOutlines;
    std::array<bool, RS_BOAT_SAIL_COUNT> mOutlineRead = {};
    std::array<std::string, RS_BOAT_SAIL_COUNT> mPreviewKey;

    // The colour a sail draws under its picture now: its pattern (a match followed), else the cloth's own.
    void SailColour(int32_t sail, uint8_t rgb[3]) {
        const int32_t pattern = RsBoatSail_Pattern(sail);
        int32_t drawnAs = pattern;
        const int32_t option = RsBoatFit_LookDrawn(RsBoatSail_Kind(sail), pattern, &drawnAs);
        if (!RsBoatFit_LookColour(drawnAs, option, rgb)) {
            rgb[0] = kPlain[0], rgb[1] = kPlain[1], rgb[2] = kPlain[2];
        }
    }

    // The preview: what the sail shows, its colour under the picture, the square outside the cloth dimmed and the
    // cloth's edge drawn. Uploaded again only when something in it changes.
    std::string Preview(int32_t sail) {
        const std::string name = std::string("RsBoatSailPreview-") + RsBoatSail_Name(sail);
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        if (gui == nullptr) {
            return "";
        }
        if (!mOutlineRead[sail]) {
            mOutlines[sail] = LoadOutline(sail);
            mOutlineRead[sail] = true;
        }
        uint8_t rgb[3];
        SailColour(sail, rgb);
        const uint8_t* picture = RsBoatSail_DrawnPixels(sail);
        const std::string key = std::to_string(RsBoatSail_Generation()) + "/" + std::to_string((uintptr_t)picture) +
                                "/" + std::to_string(rgb[0]) + "," + std::to_string(rgb[1]) + "," +
                                std::to_string(rgb[2]);
        if (key == mPreviewKey[sail] && gui->HasTextureByName(name)) {
            return name;
        }
        mPreviewKey[sail] = key;
        const int size = RS_BOAT_SAIL_SIZE;
        auto buffer = std::make_shared<std::vector<char>>((size_t)size * size * 4);
        const std::vector<uint8_t>& outline = mOutlines[sail];
        for (int i = 0; i < size * size; i++) {
            float c[3] = { (float)rgb[0], (float)rgb[1], (float)rgb[2] };
            if (picture != nullptr) {
                const float a = picture[i * 4 + 3] / 255.0f;
                for (int k = 0; k < 3; k++) {
                    c[k] = picture[i * 4 + k] * a + c[k] * (1.0f - a);
                }
            }
            const uint8_t where = outline.empty() ? 1 : outline[i];
            for (int k = 0; k < 3; k++) {
                const float v = where == 2 ? 20.0f : (where == 0 ? c[k] * 0.35f + 40.0f : c[k]);
                (*buffer)[(size_t)i * 4 + k] = (char)(uint8_t)v;
            }
            (*buffer)[(size_t)i * 4 + 3] = (char)255;
        }
        // A resource's destructor logs its init data's path, so a texture built here needs one (a default-built
        // Fast::Texture has none, and crashed the window on its first frame).
        auto init = std::make_shared<Ship::ResourceInitData>();
        init->Path = name;
        Fast::Texture tex(init);
        tex.Type = Fast::TextureType::RGBA32bpp;
        tex.Width = size;
        tex.Height = size;
        tex.ImageDataSize = (uint32_t)buffer->size();
        tex.mImageBuffer = buffer;
        tex.ImageData = reinterpret_cast<uint8_t*>(buffer->data());
        if (gui->HasTextureByName(name)) {
            gui->UnloadTexture(name);
        }
        gui->LoadGuiTexture(name, tex);
        return name;
    }

    void LookCombo(int32_t sail, int32_t look, const char* label) {
        const int32_t kind = RsBoatSail_Kind(sail);
        const int32_t current = RsBoatFit_LookOf(kind, look);
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo(label, RsBoatFit_LookOptionName(look, current))) {
            for (int32_t o = 0; o < RsBoatFit_LookOptionCount(look); o++) {
                const bool custom = RsBoatFit_LookOptionKind(look, o) == RS_BOAT_LOOK_OPTION_CUSTOM;
                const bool usable = !custom || RsBoatSail_Accepted(sail);
                if (ImGui::Selectable(RsBoatFit_LookOptionName(look, o), o == current,
                                      usable ? 0 : ImGuiSelectableFlags_Disabled)) {
                    RsBoatFit_SetLook(kind, look, o);
                }
                if (custom && !usable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Choose or drop a picture first");
                }
            }
            ImGui::EndCombo();
        }
    }

    void DrawSail(int32_t sail) {
        const int32_t kind = RsBoatSail_Kind(sail);
        const bool owned = RsBoatFit_Owned(kind) != 0;
        ImGui::PushID(sail);
        const std::string preview = Preview(sail);
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        if (!preview.empty() && gui->HasTextureByName(preview)) {
            ImGui::Image(gui->GetTextureByName(preview), ImVec2(kPreview, kPreview));
        } else {
            ImGui::Dummy(ImVec2(kPreview, kPreview));
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", kTitles[sail]);
        if (!owned) {
            ImGui::SameLine();
            ImGui::TextDisabled("(not owned)");
        }
        ImGui::BeginDisabled(!owned);
        LookCombo(sail, RsBoatSail_Pattern(sail), "Colour");
        LookCombo(sail, RsBoatSail_Look(sail), "Picture");
        const bool waiting = RsBoatSailDialog_Waiting() >= 0;
        ImGui::BeginDisabled(waiting);
        if (ImGui::Button("Choose image...")) {
            RsBoatSail_Arm(-1);
            RsBoatSailDialog_Open(sail, "");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (RsBoatSail_Armed() == sail) {
            if (ImGui::Button("Cancel drop")) {
                RsBoatSail_Arm(-1);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Drop an image on the game window");
        } else if (ImGui::Button("Drop an image now")) {
            RsBoatSail_Arm(sail);
        }
        ImGui::EndDisabled();
        const std::string file = std::string(RsBoatSail_Name(sail)) + ".png";
        if (RsBoatSailDialog_Waiting() == sail) {
            ImGui::TextDisabled("Waiting for the file dialog...");
        } else if (*RsBoatSail_Refusal(sail) != '\0') {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s refused: %s", file.c_str(),
                               RsBoatSail_Refusal(sail));
            ImGui::PopTextWrapPos();
        } else if (RsBoatSail_Accepted(sail)) {
            ImGui::TextDisabled("%s: ready", file.c_str());
        } else {
            ImGui::TextDisabled("No picture (%s)", file.c_str());
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
};

// ShipInit "*" functions re-run on preset apply and config drop; Gui::AddGuiWindow logs a duplicate name.
void RegisterRsBoatSailsWindow() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui->GetGuiWindow(kWindowName) != nullptr) {
        return;
    }
    gui->AddGuiWindow(
        std::make_shared<RsBoatSailsWindow>(CVAR_WINDOW("RsBoatSails"), kWindowName, ImVec2(520.0f, 720.0f)));
}

RegisterShipInitFunc rsBoatSailsWindowInitFunc(RegisterRsBoatSailsWindow);

} // namespace
