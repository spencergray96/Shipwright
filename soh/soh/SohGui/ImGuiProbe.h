#ifndef SOH_IMGUI_PROBE_H
#define SOH_IMGUI_PROBE_H

#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

// The agent test loop's way into the ImGui menu (sturdy-bassoon#163). Two halves:
//
//   - RECORDING. The menu's widget helpers (UIWidgets.cpp/.hpp, and the header and sidebar entries
//     in Menu.cpp) report each widget they draw: its kind, its visible label, its screen rect and
//     its value. The probe keeps the last complete frame, so the loop can find a widget by label
//     instead of by coordinates, which move with window size, DPI and font. The helpers also report
//     when a widget was OPERATED (`Edited`), which becomes an `imgui op=edited` marker: that marker
//     comes from the widget's own code path, so it is the proof that a setting changed through its
//     widget rather than through `set`.
//   - INJECTION. Mouse and key events queued into ImGui's IO from an ImGuiContextHook of type
//     NewFramePre. That hook runs inside ImGui::NewFrame, AFTER the platform backend's NewFrame
//     (ImGui_ImplWin32_NewFrame, which may queue the OS cursor position) and BEFORE ImGui processes
//     the input queue, so an injected event is the last word for that frame. One step per frame,
//     because ImGui needs to see the cursor over an item before it sees the button go down.
//
// Everything is dormant until Arm(): the recording calls return at once and no hook is installed,
// so a player who never types `imgui` pays one flag test per widget. Arm() is called by the first
// `imgui`/`agenttest imgui` command.
//
// Coordinates are ImGui's. With multi-viewports on (SoH's default) that is the OS desktop, the same
// space the backend feeds io.MousePos from, which is why a recorded rect can be clicked without any
// conversion. Each item also records the viewport it was drawn in, and a click names that viewport
// explicitly, so the real cursor hovering some other ImGui window cannot redirect it.
namespace ImGuiProbe {

// ---- recording: called by the widget helpers. Each returns immediately unless armed. ----

// Records the item ImGui drew last (its LastItemData rect). `label` may carry an ImGui "##id"
// suffix or wrap newlines; both are normalised away. `value` is the widget's value as the helper
// had it at that point: 0/1 for a checkbox or a selected header, the key for a combo or an option.
// A combo is recorded before its popup can change it, so its new value shows in the NEXT frame's
// record - and at once in the `edited` marker.
void Item(const char* kind, const char* label, double value, bool disabled);
// Records an item whose rect the caller knows.
void ItemAt(const char* kind, const char* label, ImVec2 min, ImVec2 max, double value, bool disabled);
// Records a combo's frame. Called just before ImGui::BeginCombo, from the cursor: once the popup
// has begun, LastItemData belongs to the popup window rather than to the combo.
void ComboFrame(const char* label, float width, double value, bool disabled);
// Renames the item recorded last - how a slider's "-" and "+" buttons, which the Button helper
// records as `button "-"`, become `dec`/`inc` under the slider's label.
void Relabel(const char* kind, const char* label);
// The widget was operated this frame and now holds `value`. Writes `imgui op=edited ...`. A button's
// line is held until the next recording call (or the frame's end), so a Relabel straight after it
// renames the line too: a slider step reads `kind=dec label="<slider>"`, never `kind=button label="-"`.
void Edited(const char* kind, const char* label, double value);

// ---- the agent-loop side ----

struct Recorded {
    std::string kind;
    std::string label;
    std::string value;
    ImVec2 min;
    ImVec2 max;
    bool disabled;
    bool visible; // the centre lies inside the window's clip rect, so a click there reaches it
    uint32_t viewport;
};

// Installs the NewFramePre hook and starts recording. Idempotent.
void Arm();
bool IsArmed();
// The last complete frame's items, and the ImGui frame number they were drawn in (-1 before the
// first frame after Arm()).
std::vector<Recorded> LastFrame(int32_t* frame);
// Collapses whitespace and cuts an ImGui "##id" suffix, so a typed label and a recorded one compare.
std::string NormaliseLabel(const std::string& label);

// Queue a left click at (x, y) in `viewport` (0 = the main viewport). Four frames: move, press,
// release, then park the cursor off-screen so no hover tooltip lingers into a capture. Writes
// `imgui op=delivered what=click` when the release has gone in.
void QueueClick(float x, float y, uint32_t viewport);
// Queue a press and release of one ImGui key, by ImGui's own name for it ("Escape", "Enter", "Tab",
// "F1"). Returns false for a name ImGui does not know. Writes `imgui op=delivered what=key` after.
bool QueueKey(const std::string& name);
// Injection steps still waiting for a frame.
size_t Pending();

} // namespace ImGuiProbe

#endif // SOH_IMGUI_PROBE_H
