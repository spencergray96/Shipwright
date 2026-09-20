#include "MenuConsole.h"

#include <cstdio>
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

// Stage 5's two surfaces, and neither writes a CVar - see RsMenu.h - so a session that wedges
// cannot leave the owner's config on a debug setting the way `primary` can.
//
// The sweep's fields, formatted once because three lines report them. `tick=`/`of=` is what makes a
// mid-sweep screenshot self-describing: it says which of the eleven positions the animation itself
// could have drawn this frame belongs to, so a measured position that is not one of them came from
// the renderer rather than from the game tick. Angles are radians, printed to four places because
// the whole excursion is only about 0.055 of one.
std::string DescribeSweep(const RsMenuSweepState& sweep) {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "sweep=%d loop=%d hold=%d tick=%d of=%d dir=%d from=%d to=%d hand=%s env=%.3f dx=%.2f "
                  "angle=%.4f sweeps=%d",
                  sweep.active ? 1 : 0, sweep.loop ? 1 : 0, sweep.hold ? 1 : 0, sweep.tick, sweep.ticks, sweep.dir,
                  sweep.fromPage + 1,
                  sweep.toPage + 1, sweep.movingHand == 0 ? "left" : "right", sweep.env, sweep.dx, sweep.angle,
                  sweep.sweeps);
    return buf;
}

int32_t Sweep(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        int32_t delta = 0;
        if (args[1] == "loop") {
            if (!RsMenu_StartSweepLoop()) {
                Addf(lines, "op=sweep result=error error=no_ring %s %s", DescribeSweep(RsMenu_SweepState()).c_str(),
                     Describe().c_str());
                return 1;
            }
            Addf(lines, "op=sweep result=ok %s %s", DescribeSweep(RsMenu_SweepState()).c_str(), Describe().c_str());
            return 0;
        }
        if (args[1] == "hold") {
            int32_t tick = 0;
            int32_t held = 0;
            if (args.size() < 4 || !ParseIndex(args[3], &tick) ||
                !(args[2] == "l" || args[2] == "r" || args[2] == "left" || args[2] == "right")) {
                Addf(lines, "op=sweep result=error error=arg %s", Describe().c_str());
                return 1;
            }
            held = (args[2] == "l" || args[2] == "left") ? -1 : 1;
            if (!RsMenu_HoldSweep(held, tick)) {
                Addf(lines, "op=sweep result=error error=range asked=%d %s %s", tick,
                     DescribeSweep(RsMenu_SweepState()).c_str(), Describe().c_str());
                return 1;
            }
            Addf(lines, "op=sweep result=ok %s %s", DescribeSweep(RsMenu_SweepState()).c_str(), Describe().c_str());
            return 0;
        }
        if (args[1] == "stop") {
            RsMenu_StopSweepLoop();
            Addf(lines, "op=sweep result=ok %s %s", DescribeSweep(RsMenu_SweepState()).c_str(), Describe().c_str());
            return 0;
        }
        if (args[1] == "l" || args[1] == "left") {
            delta = -1;
        } else if (args[1] == "r" || args[1] == "right") {
            delta = 1;
        } else {
            Addf(lines, "op=sweep result=error error=arg %s", Describe().c_str());
            return 1;
        }
        if (!RsMenu_StartSweep(delta)) {
            // Dropped, not queued. Named rather than silent, because "the sweep never started" and
            // "the sweep started and finished before you looked" leave identical state behind.
            const RsMenuSweepState live = RsMenu_SweepState();
            Addf(lines, "op=sweep result=error error=%s %s %s", live.active ? "busy" : "no_ring",
                 DescribeSweep(live).c_str(), Describe().c_str());
            return 1;
        }
    }
    const RsMenuSweepState sweep = RsMenu_SweepState();
    Addf(lines, "op=sweep result=ok %s %s", DescribeSweep(sweep).c_str(), Describe().c_str());
    return 0;
}

// The cursor's own line. `node=` is an id rather than an index on purpose - the graph is rebuilt
// every tick and an index means a different thing on a different page - and the neighbours are
// printed as ids too, so a run can assert the ADJACENCY rather than only the position. `box=` is
// where the highlight draws and is the only geometric field here; nothing in the graph derives a
// neighbour from it.
std::string DescribeCursor() {
    const int32_t index = RsMenu_CursorIndex();
    const RsMenuCursorNode* node = RsMenu_CursorAt(index);
    const int32_t count = RsMenu_CursorCount();
    if (node == nullptr) {
        char empty[64];
        std::snprintf(empty, sizeof(empty), "node=none index=%d nodes=%d", index, count);
        return empty;
    }
    const RsMenuCursorNode* left = RsMenu_CursorAt(node->left);
    const RsMenuCursorNode* right = RsMenu_CursorAt(node->right);
    const RsMenuCursorNode* up = RsMenu_CursorAt(node->up);
    const RsMenuCursorNode* down = RsMenu_CursorAt(node->down);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "node=%s index=%d nodes=%d hand=%d box=%d,%d,%d,%d left=%s right=%s up=%s down=%s",
                  node->id.c_str(), index, count, node->hand, node->x, node->y, node->w, node->h,
                  left != nullptr ? left->id.c_str() : "-", right != nullptr ? right->id.c_str() : "-",
                  up != nullptr ? up->id.c_str() : "-", down != nullptr ? down->id.c_str() : "-");
    return buf;
}

int32_t Cursor(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        const std::string& word = args[1];
        if (word == "select") {
            const RsMenuSelectResult result = RsMenu_SelectCursor();
            const bool refused = result == RS_MENU_SELECT_NONE || result == RS_MENU_SELECT_BUSY;
            Addf(lines, "op=cursor result=%s select=%s %s %s", refused ? "error" : "ok",
                 RsMenu_SelectResultName(result), DescribeCursor().c_str(), Describe().c_str());
            return refused ? 1 : 0;
        }
        bool moved = false;
        bool isDirection = true;
        if (word == "left") {
            moved = RsMenu_MoveCursor(-1, 0);
        } else if (word == "right") {
            moved = RsMenu_MoveCursor(1, 0);
        } else if (word == "up") {
            moved = RsMenu_MoveCursor(0, -1);
        } else if (word == "down") {
            moved = RsMenu_MoveCursor(0, 1);
        } else {
            isDirection = false;
        }
        if (isDirection) {
            if (!moved) {
                // Refused rather than clamped. A clamp would let a run report reaching the node at
                // the end of a row when the graph in fact had no edge that way.
                Addf(lines, "op=cursor result=error error=no_neighbour %s %s", DescribeCursor().c_str(),
                     Describe().c_str());
                return 1;
            }
        } else if (!RsMenu_SetCursorById(word.c_str())) {
            // The error kind alone, never the typed word - an error path that echoes input is how a
            // console command ends up putting something with a space in it into a key=value line.
            Addf(lines, "op=cursor result=error error=no_such_node %s %s", DescribeCursor().c_str(),
                 Describe().c_str());
            return 1;
        }
    }
    Addf(lines, "op=cursor result=ok %s %s", DescribeCursor().c_str(), Describe().c_str());
    return 0;
}

// The probe's fields, formatted once, because two lines report them and a drift between the two
// would silently break whichever grep a run happened to use. `step` and `phase` are what a
// screenshot is measured against: every position the 20 Hz animation can draw is a whole multiple of
// `step` from the park position, so anything between two of them came from the renderer. `epoch` is
// frame interpolation's camera epoch - the number that says whether something has quietly switched
// interpolation off for the rest of the frame, and it must stand still while the menu is open or
// the sweep cannot be smooth either.
std::string DescribeProbe(const RsMenuStatus& status) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "probe=%d step=%.1f phase=%d dy=%.1f epoch=%d", status.probe ? 1 : 0,
                  status.probeStep, status.probePhase, status.probeDy, status.cameraEpoch);
    return buf;
}

int32_t Probe(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        if (args[1] == "on") {
            RsMenu_SetProbe(true);
        } else if (args[1] == "off") {
            RsMenu_SetProbe(false);
        } else {
            Addf(lines, "op=probe result=error error=arg %s", Describe().c_str());
            return 1;
        }
    }
    const RsMenuStatus status = RsMenu_Status();
    Addf(lines, "op=probe result=ok %s %s", DescribeProbe(status).c_str(), Describe().c_str());
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
    // The live roll. `tick=`/`of=` says where in the excursion this line was read, which is what a
    // mid-sweep screenshot is asserted against; `hand=` is the half of "the moving hand follows the
    // shoulder pressed" that a screenshot alone cannot prove it MEANT to do.
    Addf(lines, "op=dump section=sweep %s", DescribeSweep(RsMenu_SweepState()).c_str());
    // The probe's lattice, plus `epoch` - the one number that says whether something has quietly
    // switched interpolation off for the rest of the frame - and `pause_mode`, the register that
    // would have exempted a View bracket from bumping it and which this menu must never set.
    Addf(lines, "op=dump section=interp %s pause_mode=%d", DescribeProbe(status).c_str(), status.pauseMenuMode);
    // What the LAST DRAWN FRAME cost, in the units OVERLAY_DISP's 2048-word budget is denominated
    // in. `dl_words` is the heap display list's length: the menu submits one gSPDisplayList, so it
    // no longer spends that budget, but the number is what stage 6's journal has to fit inside and
    // is free to carry here. Zero while the menu is closed - nothing was drawn.
    Addf(lines, "op=dump section=draw glyphs=%d quads=%d dl_words=%d", status.drawGlyphs, status.drawQuads,
         status.dlWords);
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
    // One line per cursor node, so the whole graph is readable from a marker - including its
    // ADJACENCY, which is the thing no screenshot can show and the thing the settled design is
    // actually making a claim about. At stage 5 that is two lines, and the empty middle between
    // them is the point: no page contributes items yet.
    Addf(lines, "op=dump section=cursor %s", DescribeCursor().c_str());
    for (int32_t i = 0; i < RsMenu_CursorCount(); i++) {
        const RsMenuCursorNode* node = RsMenu_CursorAt(i);
        if (node == nullptr) {
            continue;
        }
        const RsMenuCursorNode* left = RsMenu_CursorAt(node->left);
        const RsMenuCursorNode* right = RsMenu_CursorAt(node->right);
        Addf(lines, "op=dump section=node index=%d current=%d id=%s hand=%d box=%d,%d,%d,%d left=%s right=%s", i,
             i == RsMenu_CursorIndex() ? 1 : 0, node->id.c_str(), node->hand, node->x, node->y, node->w, node->h,
             left != nullptr ? left->id.c_str() : "-", right != nullptr ? right->id.c_str() : "-");
    }
    return 0;
}

const char* kUsage = "usage: menu open | close | page <n> | primary [custom|vanilla] | "
                     "sweep [l|r|loop|hold <l|r> <tick>|stop] | "
                     "cursor [left|right|up|down|select|<id>] | probe [on|off] | dump";

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
    if (sub == "sweep") {
        return Sweep(args, lines);
    }
    if (sub == "cursor") {
        return Cursor(args, lines);
    }
    if (sub == "probe") {
        return Probe(args, lines);
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
    "primary [custom|vanilla] | sweep [l|r|loop|hold <l|r> <tick>|stop] | "
    "cursor [left|right|up|down|select|<id>] | "
    "probe [on|off] | dump. The scroll opens on the N64 L bit and hard-freezes the world; primary "
    "decides which menu START opens, and is a subcommand because there is no console `set`. sweep "
    "rolls the scroll one page the way a shoulder press does, and reports the tick it is on so a "
    "mid-sweep screenshot is self-describing; cursor walks the node graph whose end nodes are the "
    "two hands, and is the only way to assert which node the cursor is on, because a screenshot "
    "shows a box and not an adjacency. probe drives a stepped per-tick offset into the geometry and "
    "into a string at once, so one screenshot shows which of the two frame-interpolates. dump "
    "reports open/closed, the page ring, the freeze and HUD state, the live sweep, the cursor "
    "graph, what the last frame cost, whether N64 L has a binding at all, and how many frames of "
    "input arrived while the world was frozen.",
    { { "open|close|page|primary|sweep|cursor|probe|dump", Ship::ArgumentType::TEXT },
      { "argument", Ship::ArgumentType::TEXT, true } });

} // namespace
