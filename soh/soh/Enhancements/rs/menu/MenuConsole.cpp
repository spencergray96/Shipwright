#include "MenuConsole.h"

#include <cstdlib>

#include <ship/debug/Console.h>

#include "RsMenu.h"
#include "soh/Enhancements/console/ConsoleSink.h"

namespace {

using ConsoleSink::Addf;

// The state line EVERY line ends with, success or refusal, the way RegionConsole's Describe() does.
// The refusal paths carry it too on purpose: a run that greps one line should never have to go and
// fetch a second one to find out what state the refusal left behind. `title=` can contain a space,
// so it is quoted AND last - the `key=value` field contract every acceptance grep depends on.
std::string Describe() {
    const RsMenuStatus status = RsMenu_Status();
    const RsMenuPage* page = RsMenu_PageAt(status.page);
    return "open=" + std::to_string(status.open ? 1 : 0) + " page=" + std::to_string(status.page + 1) +
           " pages=" + std::to_string(status.pages) + " primary=" + RsMenu_PrimaryName(status.primary) +
           " enabled=" + std::to_string(status.enabled ? 1 : 0) + " title=\"" + (page != nullptr ? page->title : "") +
           "\"";
}

// Strict: the whole word must be a non-negative decimal. `strtol` alone would accept "3abc" and
// "0x3", and a page a run only half-typed should be refused rather than silently reached.
bool ParseIndex(const std::string& word, int32_t* value) {
    if (word.empty()) {
        return false;
    }
    for (const char c : word) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    *value = (int32_t)std::strtol(word.c_str(), nullptr, 10);
    return true;
}

int32_t Open(std::vector<std::string>& lines) {
    const bool wasOpen = RsMenu_IsOpen();
    const RsMenuOpenResult result = RsMenu_Open();
    if (result != RS_MENU_OPEN_OK && result != RS_MENU_OPEN_ALREADY) {
        Addf(lines, "op=open result=error error=%s %s", RsMenu_OpenResultName(result), Describe().c_str());
        return 1;
    }
    Addf(lines, "op=open result=ok was_open=%d %s", wasOpen ? 1 : 0, Describe().c_str());
    return 0;
}

int32_t Close(std::vector<std::string>& lines) {
    const bool wasOpen = RsMenu_Close();
    Addf(lines, "op=close result=ok was_open=%d %s", wasOpen ? 1 : 0, Describe().c_str());
    return 0;
}

int32_t Page(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t ordinal = 0;
    if (args.size() < 2 || !ParseIndex(args[1], &ordinal)) {
        Addf(lines, "op=page result=error error=arg %s", Describe().c_str());
        return 1;
    }
    // 1-based on the console, 0-based in the ring. Refused rather than clamped: a clamp would let a
    // run report reaching a page it never reached.
    if (!RsMenu_SetPage(ordinal - 1)) {
        // `asked=` rather than `page=`, because `page=` means "the page the ring is on" on every
        // other line and a refusal must not redefine a field mid-format.
        Addf(lines, "op=page result=error error=range asked=%d %s", ordinal, Describe().c_str());
        return 1;
    }
    Addf(lines, "op=page result=ok %s", Describe().c_str());
    return 0;
}

int32_t Primary(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        int32_t primary = 0;
        if (!RsMenu_ParsePrimary(args[1], &primary)) {
            Addf(lines, "op=primary result=error error=arg %s", Describe().c_str());
            return 1;
        }
        RsMenu_SetPrimary(primary);
    }
    const int32_t live = RsMenu_GetPrimary();
    Addf(lines, "op=primary result=ok primary=%s value=%d %s", RsMenu_PrimaryName(live), live, Describe().c_str());
    return 0;
}

int32_t Dump(std::vector<std::string>& lines) {
    const RsMenuStatus status = RsMenu_Status();
    const RsMenuTriggerInfo trigger = RsMenu_TriggerInfo();

    Addf(lines, "op=dump result=ok %s", Describe().c_str());
    Addf(lines, "op=dump section=freeze halt=%d halt_prev=%d hud_hidden=%d hud_prev=%d hud_now=%d kaleido=%d",
         status.halt ? 1 : 0, status.haltPrev ? 1 : 0, status.hudHidden ? 1 : 0, status.hudPrev, status.hudNow,
         status.kaleido);
    // The START filter's witness. `filter_armed` counts the frames it was entitled to swallow on,
    // `start_swallowed` the edges it actually took, and `kaleido=` above says whether vanilla pause
    // got in anyway - which is the difference between "the filter worked" and "no START arrived".
    Addf(lines, "op=dump section=start filter_armed=%d start_swallowed=%d start_consumed=%d press_seen=0x%04X",
         status.filterArmedFrames, status.startSwallowed, status.startConsumed, status.filterPressSeen);
    // `bindings=-1` means the control deck could not be read at all, which is a different thing
    // from a trigger nobody has bound - and on a GameCube pad `bound=0` is the DEFAULT, not a fault.
    Addf(lines, "op=dump section=trigger button=N64_L mask=0x%04X bindings=%d bound=%d", trigger.mask,
         trigger.bindings, trigger.bound ? 1 : 0);
    Addf(lines,
         "op=dump section=counters opens=%d closes=%d page_changes=%d open_frames=%d draw_frames=%d "
         "input_frames=%d stick_frames=%d button_frames=%d last_stick=%d,%d last_buttons=0x%04X",
         status.opens, status.closes, status.pageChanges, status.openFrames, status.drawFrames, status.inputFrames,
         status.stickFrames, status.buttonFrames, status.lastStickX, status.lastStickY, status.lastButtons);
    // One line per registered page, so the ring's contents are readable without a screenshot and a
    // page added at stage 6 shows up here for free.
    for (int32_t i = 0; i < status.pages; i++) {
        const RsMenuPage* page = RsMenu_PageAt(i);
        if (page == nullptr) {
            continue;
        }
        Addf(lines, "op=dump section=page page=%d current=%d id=%s title=\"%s\"", i + 1, i == status.page ? 1 : 0,
             page->id.c_str(), page->title.c_str());
    }
    return 0;
}

const char* kUsage = "usage: menu open | close | page <n> | primary [custom|vanilla] | dump";

} // namespace

int32_t RsMenuConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string& sub = args[0];

    if (sub == "open") {
        return Open(lines);
    }
    if (sub == "close") {
        return Close(lines);
    }
    if (sub == "page") {
        return Page(args, lines);
    }
    if (sub == "primary") {
        return Primary(args, lines);
    }
    if (sub == "dump") {
        return Dump(lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `menu` console command --------------------------------------------------
//
// The mechanical half is ConsoleSink (sturdy-bassoon#112). `menu` collides with nothing in the
// engine's own command list.

namespace {

const ConsoleSink::Command menuCommand(
    "menu", RsMenuConsole_Run,
    "The mod-owned pause interface (sturdy-bassoon#111): open | close | page <n> | "
    "primary [custom|vanilla] | dump. The scroll opens on the N64 L bit and hard-freezes the world; "
    "primary decides which menu START opens, and is a subcommand because there is no console `set`. "
    "dump reports open/closed, the page ring, the freeze and HUD state, whether N64 L has a binding "
    "at all, and how many frames of input arrived while the world was frozen.",
    { { "open|close|page|primary|dump", Ship::ArgumentType::TEXT },
      { "argument", Ship::ArgumentType::TEXT, true } });

} // namespace
