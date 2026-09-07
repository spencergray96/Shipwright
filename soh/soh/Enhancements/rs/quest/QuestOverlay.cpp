#include "QuestOverlay.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "Quest.h"
#include "QuestJournal.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

// Process-lifetime state, deliberately not a CVar: a debug overlay that came back on by itself at
// the next launch would surprise an agent run that never asked for it.
bool sEnabled = false;
int32_t sTrack = QUEST_OVERLAY_TRACK_ALL;
int32_t sDrawnEntries = 0;
int32_t sDrawnLines = 0;

// The Gui registry key. It never reaches ImGui - Draw() is overridden below and opens its own
// window - so the "##" has no ImGui meaning here; it just follows the "Console##SoH" convention.
const char* const kWindowKey = "Quest Overlay##RS";

const ImVec4 kPlain = ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
const ImVec4 kKey = ImVec4(1.0f, 0.82f, 0.25f, 1.0f);   // `item`: a thing to get or hold
const ImVec4 kGuide = ImVec4(0.55f, 0.85f, 1.0f, 1.0f); // `hint` (and the npc/place tags that alias it)
const ImVec4 kStruck = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
const ImVec4 kMeta = ImVec4(0.70f, 0.70f, 0.70f, 1.0f); // status, the "nothing yet" line

// The emphasis-to-colour table. This is the whole reason the overlay exists: KEY and GUIDE must
// never share a colour (D23), and until now nothing rendered them at all.
ImVec4 ColourFor(QuestRunEmphasis emphasis, bool struck) {
    if (struck) {
        return kStruck;
    }
    switch (emphasis) {
        case QUEST_EMPHASIS_KEY:
            return kKey;
        case QUEST_EMPHASIS_GUIDE:
            return kGuide;
        default:
            return kPlain;
    }
}

// One rendered line: the runs side by side, each in its own colour. A struck line is drawn grey with
// a rule through every run - ImGui has no strike-through, so it is a line over each item rect.
void DrawRuns(const std::vector<QuestRun>& runs, bool struck) {
    bool first = true;
    for (const QuestRun& run : runs) {
        if (!first) {
            ImGui::SameLine(0.0f, 0.0f);
        }
        first = false;
        // "%s", never the run text as the format: a registered definition cannot carry '%', but
        // this is a renderer and renderers do not get to rely on the validator.
        ImGui::TextColored(ColourFor(QuestJournal_StyleEmphasis(run.style), struck), "%s", run.text.c_str());
        if (struck) {
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            const float y = (a.y + b.y) * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), ImGui::GetColorU32(kStruck), 1.0f);
        }
    }
    if (first) {
        ImGui::TextUnformatted(""); // an empty run list still occupies its line
    }
}

// One resolved entry: a header (title runs + status), then every visible line. Returns the number
// of lines it drew, so the console can report what a frame rendered.
int32_t DrawEntry(const QuestJournalEntry& entry) {
    int32_t lines = 0;
    DrawRuns(entry.title, false);
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextColored(kMeta, "[%s]", Quest_StatusName(entry.status));
    lines++;
    if (entry.lines.empty()) {
        ImGui::TextColored(kMeta, "  (nothing yet)");
        lines++;
        return lines;
    }
    for (const QuestJournalLine& line : entry.lines) {
        const bool struck = line.kind == QUEST_LINE_CHECK_ITEM && line.checked;
        if (line.kind == QUEST_LINE_CHECK_ITEM) {
            ImGui::TextColored(ColourFor(QUEST_EMPHASIS_NONE, struck), "%s", line.checked ? "  [x] " : "  [ ] ");
            ImGui::SameLine(0.0f, 0.0f);
        } else {
            ImGui::TextUnformatted("  ");
            ImGui::SameLine(0.0f, 0.0f);
        }
        DrawRuns(line.runs, struck);
        lines++;
    }
    return lines;
}

// Overrides Draw() outright, the TimeDisplay / Notification pattern: the base Draw would gate on the
// GuiWindow's own visibility flag and open a titled, saved-settings window under the registry key,
// and this wants neither. Gui::DrawMenu calls Draw() on every registered window every frame
// regardless of visibility, so the gate has to be here.
class QuestOverlayWindow final : public Ship::GuiWindow {
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
            sDrawnEntries = 0;
            sDrawnLines = 0;
            return;
        }

        // Resolve first, draw second. Every predicate is evaluated live here, which is the
        // accumulation model working as designed (D14): a block appears or vanishes on the frame
        // the store moves, with nothing telling it to.
        std::vector<QuestJournalEntry> entries;
        if (sTrack >= 0) {
            QuestJournalEntry entry;
            if (QuestJournal_Build(sTrack, &entry)) {
                entries.push_back(std::move(entry));
            }
        } else {
            // D15 says filtering is the caller's display policy. This caller's is "something to show".
            std::vector<QuestJournalEntry> all = QuestJournal_Snapshot();
            for (QuestJournalEntry& entry : all) {
                if (entry.visibleCount > 0) {
                    entries.push_back(std::move(entry));
                }
            }
        }

        // Pinned to the main viewport (multi-viewport is on by default on Windows): an ImGui window
        // that drifted into its own OS window would be invisible to the agent loop's PrintWindow
        // capture, and this overlay exists to be screenshotted.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(vp->ID);
        // Left edge, a third of the way down: clear of the hearts (top-left) and the rupee counter
        // (bottom-left). A debug overlay, so a fixed spot beats a setting.
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 24.0f, vp->Pos.y + vp->Size.y * 0.30f), ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::Begin("RsQuestOverlay", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(1.25f);

        int32_t drawnEntries = 0;
        int32_t drawnLines = 0;
        if (entries.empty()) {
            // A tracked id that is not registered cannot arrive here through the console, which
            // refuses it first; the "all" case with nothing visible is the normal quiet state.
            if (sTrack >= 0) {
                ImGui::TextColored(kMeta, "quest %d: not registered", sTrack);
            } else {
                ImGui::TextColored(kMeta, "quest overlay: nothing visible");
            }
            drawnLines++;
        }
        for (const QuestJournalEntry& entry : entries) {
            if (drawnEntries > 0) {
                ImGui::Spacing();
            }
            drawnLines += DrawEntry(entry);
            drawnEntries++;
        }

        ImGui::End();
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(2);

        sDrawnEntries = drawnEntries;
        sDrawnLines = drawnLines;
    }
};

// ShipInit "*" functions re-run on preset apply and config load, and Gui::AddGuiWindow logs an error
// on a duplicate name, so look before adding - the same guard the console commands use. The empty
// CVar name is deliberate: GuiWindow skips the visibility CVar entirely when it is empty, which is
// what keeps this out of shipofharkinian.json.
void RegisterQuestOverlay() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui->GetGuiWindow(kWindowKey) != nullptr) {
        return;
    }
    gui->AddGuiWindow(std::make_shared<QuestOverlayWindow>("", true, std::string(kWindowKey)));
}

RegisterShipInitFunc questOverlayInitFunc(RegisterQuestOverlay);

} // namespace

void QuestOverlay_SetEnabled(bool enabled) {
    sEnabled = enabled;
}

void QuestOverlay_SetTrack(int32_t questId) {
    sTrack = questId < 0 ? QUEST_OVERLAY_TRACK_ALL : questId;
}

QuestOverlayState QuestOverlay_Get() {
    return { sEnabled, sTrack, sDrawnEntries, sDrawnLines };
}
