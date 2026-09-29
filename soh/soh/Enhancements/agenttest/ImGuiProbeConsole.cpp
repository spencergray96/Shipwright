#include "ImGuiProbeConsole.h"

#include <cstdlib>

#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/SohGui/ImGuiProbe.h"

namespace {

using ConsoleSink::Addf;

int MenuVisible() {
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    if (window == nullptr || window->GetGui() == nullptr || window->GetGui()->GetMenu() == nullptr) {
        return 0;
    }
    return window->GetGui()->GetMenu()->IsVisible() ? 1 : 0;
}

void AddItem(std::vector<std::string>& lines, const ImGuiProbe::Recorded& item) {
    const ImVec2 centre = { (item.min.x + item.max.x) * 0.5f, (item.min.y + item.max.y) * 0.5f };
    Addf(lines, "item kind=%s x=%.0f y=%.0f w=%.0f h=%.0f value=%s disabled=%d visible=%d label=\"%s\"",
         item.kind.c_str(), centre.x, centre.y, item.max.x - item.min.x, item.max.y - item.min.y, item.value.c_str(),
         item.disabled ? 1 : 0, item.visible ? 1 : 0, item.label.c_str());
}

bool IsClickable(const std::string& kind) {
    // A slider is recorded for its value only: a click on its bar sets whatever value sits under the
    // cursor. Its dec/inc buttons are the way to move it.
    return kind != "slider";
}

bool IsKind(const std::string& word) {
    static const char* kKinds[] = {
        "header", "sidebar", "checkbox", "combo", "option", "slider", "dec", "inc", "button"
    };
    for (const char* kind : kKinds) {
        if (word == kind) {
            return true;
        }
    }
    return false;
}

int32_t Status(std::vector<std::string>& lines) {
    int32_t frame = -1;
    const size_t items = ImGuiProbe::LastFrame(&frame).size();
    Addf(lines, "op=status result=ok armed=%d frame=%d items=%zu menu=%d pending=%zu", ImGuiProbe::IsArmed() ? 1 : 0,
         frame, items, MenuVisible(), ImGuiProbe::Pending());
    return 0;
}

int32_t Dump(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string kind = args.size() >= 2 ? args[1] : "";
    if (!kind.empty() && !IsKind(kind)) {
        lines.push_back("op=dump result=error error=unknown_kind");
        return 1;
    }
    int32_t frame = -1;
    const std::vector<ImGuiProbe::Recorded> items = ImGuiProbe::LastFrame(&frame);
    size_t shown = 0;
    for (const auto& item : items) {
        if (kind.empty() || item.kind == kind) {
            shown++;
        }
    }
    Addf(lines, "op=dump result=ok frame=%d items=%zu of=%zu menu=%d", frame, shown, items.size(), MenuVisible());
    for (const auto& item : items) {
        if (kind.empty() || item.kind == kind) {
            AddItem(lines, item);
        }
    }
    return 0;
}

int32_t Click(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    // The console tokenizer splits on " ", so the label arrives as several words.
    std::string typed;
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) {
            typed += " ";
        }
        typed += args[i];
    }
    std::string kind;
    const size_t colon = typed.find(':');
    if (colon != std::string::npos && IsKind(typed.substr(0, colon))) {
        kind = typed.substr(0, colon);
        typed = typed.substr(colon + 1);
    }
    const std::string label = ImGuiProbe::NormaliseLabel(typed);
    if (label.empty()) {
        lines.push_back("op=click result=error error=no_label");
        return 1;
    }

    int32_t frame = -1;
    const std::vector<ImGuiProbe::Recorded> items = ImGuiProbe::LastFrame(&frame);
    const ImGuiProbe::Recorded* match = nullptr;
    size_t matches = 0;
    for (const auto& item : items) {
        if (item.label == label && (kind.empty() || item.kind == kind)) {
            match = &item;
            matches++;
        }
    }
    if (matches == 0) {
        lines.push_back("op=click result=error error=not_found");
        return 1;
    }
    if (matches > 1) {
        Addf(lines, "op=click result=error error=ambiguous matches=%zu", matches);
        return 1;
    }
    if (!IsClickable(match->kind)) {
        Addf(lines, "op=click result=error error=not_clickable kind=%s", match->kind.c_str());
        return 1;
    }
    if (match->disabled) {
        Addf(lines, "op=click result=error error=disabled kind=%s", match->kind.c_str());
        return 1;
    }
    if (!match->visible) {
        Addf(lines, "op=click result=error error=clipped kind=%s", match->kind.c_str());
        return 1;
    }
    const float x = (match->min.x + match->max.x) * 0.5f;
    const float y = (match->min.y + match->max.y) * 0.5f;
    ImGuiProbe::QueueClick(x, y, match->viewport);
    Addf(lines, "op=click result=ok kind=%s x=%.0f y=%.0f value=%s queued=%zu label=\"%s\"", match->kind.c_str(), x, y,
         match->value.c_str(), ImGuiProbe::Pending(), match->label.c_str());
    return 0;
}

bool ParseFloat(const std::string& word, float* out) {
    char* end = nullptr;
    const float value = std::strtof(word.c_str(), &end);
    if (word.empty() || end == nullptr || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

int32_t ClickAt(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    float x = 0.0f;
    float y = 0.0f;
    if (args.size() != 3 || !ParseFloat(args[1], &x) || !ParseFloat(args[2], &y)) {
        lines.push_back("op=clickat result=error error=bad_argument");
        return 1;
    }
    ImGuiProbe::QueueClick(x, y, 0);
    Addf(lines, "op=clickat result=ok x=%.0f y=%.0f queued=%zu", x, y, ImGuiProbe::Pending());
    return 0;
}

int32_t Key(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() != 2) {
        lines.push_back("op=key result=error error=bad_argument");
        return 1;
    }
    if (!ImGuiProbe::QueueKey(args[1])) {
        lines.push_back("op=key result=error error=unknown_key");
        return 1;
    }
    Addf(lines, "op=key result=ok key=%s queued=%zu", args[1].c_str(), ImGuiProbe::Pending());
    return 0;
}

} // namespace

int32_t ImGuiProbeConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    ImGuiProbe::Arm();
    const std::string sub = args.empty() ? "status" : args[0];
    if (sub == "status") {
        return Status(lines);
    }
    if (sub == "dump") {
        return Dump(args, lines);
    }
    // Without the hook nothing drains the queue, so an injection would report ok and never arrive.
    if ((sub == "click" || sub == "clickat" || sub == "key") && !ImGuiProbe::IsArmed()) {
        Addf(lines, "op=%s result=error error=not_armed", sub.c_str());
        return 1;
    }
    if (sub == "click") {
        return Click(args, lines);
    }
    if (sub == "clickat") {
        return ClickAt(args, lines);
    }
    if (sub == "key") {
        return Key(args, lines);
    }
    lines.push_back("op=unknown result=error error=unknown_subcommand usage=status|dump|click|clickat|key");
    return 1;
}

namespace {

const ConsoleSink::Command imguiCommand(
    "imgui", ImGuiProbeConsole_Run,
    "Drive the ImGui menu without a mouse (sturdy-bassoon#163): status | dump [kind] | click [kind:]<label> | "
    "clickat <x> <y> | key <name>. dump lists what the menu drew last frame, by label, with each widget's value; "
    "click operates one by label, so a run never writes coordinates. `key Escape` opens the menu. The first call "
    "arms the probe, which records nothing before it.",
    { { "status|dump|click|clickat|key", Ship::ArgumentType::TEXT }, { "argument", Ship::ArgumentType::TEXT, true } });

} // namespace
