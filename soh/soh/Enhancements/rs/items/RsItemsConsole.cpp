// The inventory's console command and its on-screen readout (sturdy-bassoon#286). The surface is RsItemsConsole.h's.

#include "RsItemsConsole.h"

#include <cstdlib>
#include <memory>
#include <string>

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "RsItems.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/rs/actors/RsItemPickup.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

// How far in front of Link `items spawn` puts a pickup: past his collection reach (55), so it waits for him.
constexpr float kSpawnAhead = 80.0f;

bool ParseInt(const std::string& s, long* out) {
    char* end = nullptr;
    *out = strtol(s.c_str(), &end, 0);
    return end != s.c_str() && *end == '\0';
}

int32_t Error(std::vector<std::string>& lines, const std::string& op, const char* error) {
    Addf(lines, "op=%s result=error error=%s", op.c_str(), error);
    return 1;
}

// The token, as a counted item (key=false) or a key item (key=true); false for neither.
bool Resolve(const std::string& token, int32_t* index, bool* key) {
    *index = RsItems_ByToken(token.c_str());
    *key = false;
    if (*index >= 0) {
        return true;
    }
    *index = RsKeyItems_ByToken(token.c_str());
    *key = true;
    return *index >= 0;
}

// The optional count at args[at]: 1 when absent; false for anything but a whole number.
bool CountArg(const std::vector<std::string>& args, size_t at, long* count) {
    *count = 1;
    return args.size() <= at || ParseInt(args[at], count);
}

const char* ErrorOf(int32_t result) {
    return RsItems_ResultName(result);
}

int32_t List(std::vector<std::string>& lines) {
    for (int32_t i = 0; i < RS_ITEM_COUNT; i++) {
        const RsItemDef* d = RsItems_Def(i);
        Addf(lines, "item token=%s count=%d cap=%d icon=%d ground_icon=%d", d->token, (int)RsItems_Count(i),
             (int)d->cap, (int)RsItems_IconFound(i, 0), (int)RsItems_IconFound(i, 1));
    }
    for (int32_t i = 0; i < RS_KEY_ITEM_COUNT; i++) {
        const RsKeyItemDef* d = RsKeyItems_Def(i);
        Addf(lines, "key token=%s held=%d category=%s flag=%d icon=%d ground_icon=%d", d->token,
             (int)RsKeyItems_Held(i), RsKeyItems_CategoryName(d->category), (int)d->worldFlag,
             (int)RsKeyItems_IconFound(i, 0), (int)RsKeyItems_IconFound(i, 1));
    }
    Addf(lines, "op=list result=ok items=%d keys=%d ceremonies=%d", (int)RS_ITEM_COUNT, (int)RS_KEY_ITEM_COUNT,
         (int)RsKeyItems_CeremoniesQueued());
    return 0;
}

int32_t Give(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t index;
    bool key;
    long count;
    if (args.size() < 2 || args.size() > 3) {
        return Error(lines, "give", "usage");
    }
    if (!Resolve(args[1], &index, &key)) {
        return Error(lines, "give", "unknown_item");
    }
    if (key) {
        if (args.size() == 3) {
            return Error(lines, "give", "bad_value"); // a key item has no count
        }
        const int32_t result = RsKeyItems_Grant(index, 1, RS_ITEMS_SRC_CONSOLE);
        if (result != RS_ITEMS_OK) {
            return Error(lines, "give", ErrorOf(result));
        }
        Addf(lines, "op=give result=ok item=%s held=1 ceremonies=%d", args[1].c_str(),
             (int)RsKeyItems_CeremoniesQueued());
        return 0;
    }
    if (!CountArg(args, 2, &count) || count < 1 || count > RS_ITEM_COUNT_MAX) {
        return Error(lines, "give", "bad_value");
    }
    const int32_t result = RsItems_Gain(index, (int32_t)count, 1, RS_ITEMS_SRC_CONSOLE);
    if (result != RS_ITEMS_OK) {
        return Error(lines, "give", ErrorOf(result));
    }
    Addf(lines, "op=give result=ok item=%s count=%d total=%d", args[1].c_str(), (int)count,
         (int)RsItems_Count(index));
    return 0;
}

int32_t Take(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t index;
    bool key;
    long count;
    if (args.size() < 2 || args.size() > 3) {
        return Error(lines, "take", "usage");
    }
    if (!Resolve(args[1], &index, &key)) {
        return Error(lines, "take", "unknown_item");
    }
    if (key) {
        if (args.size() == 3) {
            return Error(lines, "take", "bad_value");
        }
        RsKeyItems_Clear(index);
        Addf(lines, "op=take result=ok item=%s held=0", args[1].c_str());
        return 0;
    }
    if (!CountArg(args, 2, &count) || count < 1 || count > RS_ITEM_COUNT_MAX) {
        return Error(lines, "take", "bad_value");
    }
    const int32_t result = RsItems_Take(index, (int32_t)count, RS_ITEMS_SRC_CONSOLE);
    if (result != RS_ITEMS_OK) {
        return Error(lines, "take", ErrorOf(result));
    }
    Addf(lines, "op=take result=ok item=%s count=%d total=%d", args[1].c_str(), (int)count,
         (int)RsItems_Count(index));
    return 0;
}

int32_t Set(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t index;
    bool key;
    long count;
    if (args.size() != 3) {
        return Error(lines, "set", "usage");
    }
    if (!Resolve(args[1], &index, &key) || key) {
        return Error(lines, "set", "unknown_item");
    }
    if (!ParseInt(args[2], &count) || RsItems_Set(index, (int32_t)count) != RS_ITEMS_OK) {
        return Error(lines, "set", "bad_value");
    }
    Addf(lines, "op=set result=ok item=%s total=%d", args[1].c_str(), (int)RsItems_Count(index));
    return 0;
}

int32_t Float(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t index;
    bool key;
    long count;
    if (args.size() < 2 || args.size() > 3) {
        return Error(lines, "float", "usage");
    }
    if (!Resolve(args[1], &index, &key) || key) {
        return Error(lines, "float", "unknown_item");
    }
    if (!CountArg(args, 2, &count) || count < 1 || count > RS_ITEM_COUNT_MAX) {
        return Error(lines, "float", "bad_value");
    }
    if (gPlayState == nullptr) {
        return Error(lines, "float", "no_play");
    }
    RsItems_ShowGain(index, (int32_t)count);
    Addf(lines, "op=float result=ok item=%s count=%d", args[1].c_str(), (int)count);
    return 0;
}

// spawn <item> [n] [once <flag>]
int32_t Spawn(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t index;
    bool key;
    long count = 1;
    long flag = -1;
    if (args.size() < 2 || args.size() > 5) {
        return Error(lines, "spawn", "usage");
    }
    if (!Resolve(args[1], &index, &key)) {
        return Error(lines, "spawn", "unknown_item");
    }
    size_t at = 2;
    if (args.size() > at && args[at] != "once") {
        if (key || !ParseInt(args[at], &count) || count < 1 || count > RsItems_Def(index)->cap) {
            return Error(lines, "spawn", "bad_value");
        }
        at++;
    }
    if (args.size() > at) {
        if (key || args[at] != "once" || args.size() != at + 2 || !ParseInt(args[at + 1], &flag) || flag < 0 ||
            flag >= WORLD_FLAG_MAX || !WORLD_FLAG_IS_PLACEMENT(flag)) {
            return Error(lines, "spawn", "bad_value");
        }
    }
    if (gPlayState == nullptr) {
        return Error(lines, "spawn", "no_play");
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return Error(lines, "spawn", "no_play");
    }
    const s16 yaw = player->actor.shape.rot.y;
    const float x = player->actor.world.pos.x + Math_SinS(yaw) * kSpawnAhead;
    const float z = player->actor.world.pos.z + Math_CosS(yaw) * kSpawnAhead;
    const s16 params = key ? RS_PICKUP_PARAMS_KEY_ITEM(index) : RS_PICKUP_PARAMS(index);
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_RS_ITEM_PICKUP, x,
                           player->actor.world.pos.y + 20.0f, z, key ? 0 : (s16)count, 0,
                           flag >= 0 ? RS_PICKUP_ONCE(flag) : 0, params);
    if (a == nullptr) {
        return Error(lines, "spawn", "spawn_failed");
    }
    Addf(lines, "op=spawn result=ok item=%s count=%d once_flag=%d", args[1].c_str(), key ? 1 : (int)count,
         (int)flag);
    return 0;
}

int32_t Hud(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2 && (args[1] == "on" || args[1] == "off")) {
        CVarSetInteger(CVAR_RS_ITEMS_HUD, args[1] == "on" ? 1 : 0);
        CVarSave();
    } else if (args.size() != 1) {
        return Error(lines, "hud", "usage");
    }
    Addf(lines, "op=hud result=ok hud=%d", CVarGetInteger(CVAR_RS_ITEMS_HUD, 0) ? 1 : 0);
    return 0;
}

// --- the readout ----------------------------------------------------------------------------------------------
//
// RsBoatHud.cpp's pattern: an ImGui window over the game with Draw() overridden, so no titled, saved-settings window
// opens and the gate is here. At the right, under the region overlay's corner, clear of the boat readout at the
// left: the counted items, then the key items grouped by category, held ones bright and the rest dim.

const char* const kWindowKey = "Inventory Readout##RS";
const ImVec4 kBright = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
const ImVec4 kDim = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
const ImVec4 kHead = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);

class RsItemsReadout final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }

    void Draw() override {
        if (!CVarGetInteger(CVAR_RS_ITEMS_HUD, 0) || gPlayState == nullptr || gPlayState->pauseCtx.state != 0) {
            return;
        }
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.955f, vp->Pos.y + vp->Size.y * 0.42f),
                                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("RsItemsReadout", nullptr, flags);
        ImGui::SetWindowFontScale(0.9f);
        ImGui::TextColored(kHead, "Items");
        for (int32_t i = 0; i < RS_ITEM_COUNT; i++) {
            const int32_t count = RsItems_Count(i);
            ImGui::TextColored(count > 0 ? kBright : kDim, "  %s  %d / %d", RsItems_Def(i)->name, (int)count,
                               (int)RsItems_Def(i)->cap);
        }
        for (int32_t cat = 0; cat < RS_KEY_CAT_COUNT; cat++) {
            bool header = false;
            for (int32_t k = 0; k < RS_KEY_ITEM_COUNT; k++) {
                const RsKeyItemDef* d = RsKeyItems_Def(k);
                if (d->category != cat) {
                    continue;
                }
                if (!header) {
                    ImGui::TextColored(kHead, "Key items: %s", RsKeyItems_CategoryName(cat));
                    header = true;
                }
                ImGui::TextColored(RsKeyItems_Held(k) ? kBright : kDim, "  %s%s", d->name,
                                   RsKeyItems_Held(k) ? "" : "  (not held)");
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(2);
    }
};

// RsBoatHud.cpp's reason to look before adding: ShipInit "*" functions re-run, and a duplicate name logs an error.
void RegisterRsItemsReadout() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui->GetGuiWindow(kWindowKey) != nullptr) {
        return;
    }
    gui->AddGuiWindow(std::make_shared<RsItemsReadout>("", true, std::string(kWindowKey)));
}

RegisterShipInitFunc rsItemsReadoutInitFunc(RegisterRsItemsReadout);

} // namespace

int32_t RsItemsConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? "list" : args[0];
    if (sub == "list") {
        return List(lines);
    }
    if (sub == "give") {
        return Give(args, lines);
    }
    if (sub == "take") {
        return Take(args, lines);
    }
    if (sub == "set") {
        return Set(args, lines);
    }
    if (sub == "reset" && args.size() == 1) {
        RsItems_Reset();
        Addf(lines, "op=reset result=ok");
        return 0;
    }
    if (sub == "float") {
        return Float(args, lines);
    }
    if (sub == "spawn") {
        return Spawn(args, lines);
    }
    if (sub == "hud") {
        return Hud(args, lines);
    }
    lines.push_back("op=usage result=error usage=\"items list | give <item> [n] | take <item> [n] | set <item> <n> | "
                    "reset | float <item> [n] | spawn <item> [n] [once <flag>] | hud [on|off]\"");
    return 1;
}

// --- the human sink -------------------------------------------------------------------------------------------
//
// `items` collides with nothing in debugger/debugconsole.cpp's CMD_REGISTER list (vanilla's is `give_item`).

namespace {

const ConsoleSink::Command itemsCommand(
    "items", RsItemsConsole_Run,
    "The inventory (sturdy-bassoon#286): list | give <item> [n] | take <item> [n] | set <item> <n> | reset | "
    "float <item> [n] | spawn <item> [n] [once <flag>] | hud [on|off]. Counted items (planks, logs, nails) are a "
    "count per save; key items (hammer, saw, schematic) are world flags with a get-item ceremony. `items list` names "
    "every token.",
    { { "list|give|take|set|reset|float|spawn|hud", Ship::ArgumentType::TEXT },
      { "item", Ship::ArgumentType::TEXT, true },
      { "count", Ship::ArgumentType::TEXT, true } });

// What the dev console lists for the next word (sturdy-bassoon#292).
const ConsoleSink::Hints itemsHints("items", R"(
list                        every count and key item, with tokens
give                        add a counted item, or grant a key item
  <item> [n]                a token from items list; n defaults to 1
take                        remove a counted item, or clear a key item
  <item> [n]                a token from items list; n defaults to 1
set <item> <n>              a count outright, 0 up to its cap
reset                       every count to 0; key items stay
float <item> [n]            show the icon over Link's head, change nothing
spawn                       a pickup in front of Link
  <item> [n]                comes back on every load of the scene
  <item> [n] once <flag>    taken once, remembered by that world flag
hud                         the on-screen readout of every item
  on                        show the readout
  off                       hide the readout
)");

} // namespace
