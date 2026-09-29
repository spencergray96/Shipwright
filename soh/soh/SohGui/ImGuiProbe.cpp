#include "ImGuiProbe.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <deque>
#include <mutex>

#include "soh/Enhancements/agenttest/AgentTest.h"

namespace ImGuiProbe {
namespace {

enum class StepType { MouseMove, MouseDown, MouseUp, MousePark, KeyDown, KeyUp };

struct Step {
    StepType type;
    float x = 0.0f;
    float y = 0.0f;
    uint32_t viewport = 0;
    ImGuiKey key = ImGuiKey_None;
    // Written after this step's events are queued, when non-empty.
    std::string doneMarker;
};

// Everything is touched from the game thread today (widget drawing and console commands both run
// there), so the lock is uncontended. It is held anyway because the console can be driven from the
// ImGui console window and from the agent loop's file, and nothing promises those stay one thread.
std::mutex sMutex;
std::atomic<bool> sArmed{ false };
std::vector<Recorded> sCurrent;
std::vector<Recorded> sLast;
int32_t sLastFrame = -1;
std::deque<Step> sSteps;
// Whether the last recording call recorded anything, so Relabel never renames the item before it.
bool sLastPushed = false;
// A button's `edited` line, held until the next recording call. A slider's -/+ buttons go through the
// Button helper, which cannot know it is part of a slider; holding the line lets Relabel rename it to
// `dec`/`inc` under the slider's label before it is written.
std::string sPendingKind;
std::string sPendingLabel;
std::string sPendingValue;

std::string Label(const char* label) {
    return NormaliseLabel(label != nullptr ? label : "");
}

std::string EditedLine(const std::string& kind, const std::string& label, const std::string& value) {
    return "imgui op=edited kind=" + kind + " value=" + value + " label=\"" + label + "\"";
}

// Takes the held button line, if any. Called under sMutex; the caller writes it after unlocking.
std::string TakePendingLocked() {
    if (sPendingKind.empty()) {
        return "";
    }
    const std::string line = EditedLine(sPendingKind, sPendingLabel, sPendingValue);
    sPendingKind.clear();
    return line;
}

void WriteIfAny(const std::string& line) {
    if (!line.empty()) {
        AgentTest_WriteMarker(line.c_str());
    }
}

std::string FormatValue(double value) {
    if (std::floor(value) == value && std::fabs(value) < 1e9) {
        return std::to_string(static_cast<long long>(value));
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", value);
    return buf;
}

void Push(const char* kind, const char* label, ImVec2 min, ImVec2 max, double value, bool disabled) {
    ImGuiWindow* window = ImGui::GetCurrentWindowRead();
    std::string pending;
    if (window == nullptr) {
        {
            std::lock_guard<std::mutex> lock(sMutex);
            sLastPushed = false;
            pending = TakePendingLocked();
        }
        WriteIfAny(pending);
        return;
    }
    Recorded item;
    item.kind = kind;
    item.label = Label(label);
    item.value = FormatValue(value);
    item.min = min;
    item.max = max;
    item.disabled = disabled;
    item.visible = window->ClipRect.Contains((min + max) * 0.5f);
    item.viewport = window->Viewport != nullptr ? window->Viewport->ID : 0;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        pending = TakePendingLocked();
        sCurrent.push_back(std::move(item));
        sLastPushed = true;
    }
    WriteIfAny(pending);
}

void Apply(const Step& step) {
    ImGuiIO& io = ImGui::GetIO();
    switch (step.type) {
        case StepType::MouseMove:
        case StepType::MouseDown:
        case StepType::MouseUp: {
            // The position goes in on every step, not only the first: a backend that did queue the
            // OS cursor this frame (the window has focus, or the real cursor crossed it) would
            // otherwise move the pointer off the item between press and release.
            const ImGuiID viewport = step.viewport != 0 ? step.viewport : ImGui::GetMainViewport()->ID;
            io.AddMouseViewportEvent(viewport);
            io.AddMousePosEvent(step.x, step.y);
            if (step.type == StepType::MouseDown) {
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            } else if (step.type == StepType::MouseUp) {
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            }
        } break;
        case StepType::MousePark:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;
        case StepType::KeyDown:
        case StepType::KeyUp:
            io.AddKeyEvent(step.key, step.type == StepType::KeyDown);
            break;
    }
}

// ImGuiContextHookType_NewFramePre: after the platform backend's NewFrame, before ImGui reads the
// input queue. g.FrameCount has not been incremented yet, so it still names the frame whose items
// sCurrent holds.
void OnNewFramePre(ImGuiContext* ctx, ImGuiContextHook*) {
    std::string done;
    std::string pending;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        pending = TakePendingLocked();
        sLastPushed = false;
        sLast.swap(sCurrent);
        sCurrent.clear();
        sLastFrame = ctx->FrameCount;
        if (!sSteps.empty()) {
            const Step step = sSteps.front();
            sSteps.pop_front();
            Apply(step);
            done = step.doneMarker;
        }
    }
    WriteIfAny(pending);
    WriteIfAny(done);
}

} // namespace

void Item(const char* kind, const char* label, double value, bool disabled) {
    if (!sArmed.load(std::memory_order_relaxed)) {
        return;
    }
    Push(kind, label, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), value, disabled);
}

void ItemAt(const char* kind, const char* label, ImVec2 min, ImVec2 max, double value, bool disabled) {
    if (!sArmed.load(std::memory_order_relaxed)) {
        return;
    }
    Push(kind, label, min, max, value, disabled);
}

void ComboFrame(const char* label, float width, double value, bool disabled) {
    if (!sArmed.load(std::memory_order_relaxed)) {
        return;
    }
    const ImVec2 min = ImGui::GetCursorScreenPos();
    Push("combo", label, min, min + ImVec2(width, ImGui::GetFrameHeight()), value, disabled);
}

void Relabel(const char* kind, const char* label) {
    if (!sArmed.load(std::memory_order_relaxed)) {
        return;
    }
    std::string pending;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        if (sLastPushed && !sCurrent.empty()) {
            sCurrent.back().kind = kind;
            sCurrent.back().label = Label(label);
        }
        if (!sPendingKind.empty()) {
            sPendingKind = kind;
            sPendingLabel = Label(label);
        }
        pending = TakePendingLocked();
    }
    WriteIfAny(pending);
}

void Edited(const char* kind, const char* label, double value) {
    if (!sArmed.load(std::memory_order_relaxed)) {
        return;
    }
    std::string pending;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        pending = TakePendingLocked();
        if (std::string(kind) == "button") {
            sPendingKind = kind;
            sPendingLabel = Label(label);
            sPendingValue = FormatValue(value);
        }
    }
    WriteIfAny(pending);
    if (std::string(kind) != "button") {
        WriteIfAny(EditedLine(kind, Label(label), FormatValue(value)));
    }
}

void Arm() {
    if (sArmed.load()) {
        return;
    }
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx == nullptr) {
        return;
    }
    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_NewFramePre;
    hook.Callback = OnNewFramePre;
    ImGui::AddContextHook(ctx, &hook);
    sArmed.store(true);
}

bool IsArmed() {
    return sArmed.load();
}

std::vector<Recorded> LastFrame(int32_t* frame) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (frame != nullptr) {
        *frame = sLastFrame;
    }
    return sLast;
}

std::string NormaliseLabel(const std::string& label) {
    std::string text = label;
    const size_t hash = text.find("##");
    if (hash == 0) {
        text.erase(0, 2); // "##id" alone: the id is all there is to address it by
    } else if (hash != std::string::npos) {
        text.resize(hash);
    }
    std::string out;
    bool pendingSpace = false;
    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        // A '"' would unbalance the label="..." field every marker ends with.
        out += c == '"' ? '\'' : c;
    }
    return out;
}

void QueueClick(float x, float y, uint32_t viewport) {
    char done[96];
    std::snprintf(done, sizeof(done), "imgui op=delivered what=click x=%.0f y=%.0f", x, y);
    std::lock_guard<std::mutex> lock(sMutex);
    sSteps.push_back({ StepType::MouseMove, x, y, viewport });
    sSteps.push_back({ StepType::MouseDown, x, y, viewport });
    sSteps.push_back({ StepType::MouseUp, x, y, viewport, ImGuiKey_None, done });
    sSteps.push_back({ StepType::MousePark });
}

bool QueueKey(const std::string& name) {
    ImGuiKey key = ImGuiKey_None;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        if (name == ImGui::GetKeyName(static_cast<ImGuiKey>(k))) {
            key = static_cast<ImGuiKey>(k);
            break;
        }
    }
    if (key == ImGuiKey_None) {
        return false;
    }
    const std::string done = "imgui op=delivered what=key key=" + name;
    std::lock_guard<std::mutex> lock(sMutex);
    sSteps.push_back({ StepType::KeyDown, 0.0f, 0.0f, 0, key });
    sSteps.push_back({ StepType::KeyUp, 0.0f, 0.0f, 0, key, done });
    return true;
}

size_t Pending() {
    std::lock_guard<std::mutex> lock(sMutex);
    return sSteps.size();
}

} // namespace ImGuiProbe
