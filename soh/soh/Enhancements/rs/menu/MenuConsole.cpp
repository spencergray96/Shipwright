#include "MenuConsole.h"

#include <cstdio>
#include <cstdlib>

#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <ship/Context.h>
#include <ship/debug/Console.h>

#include "NamePanel.h"
#include "PauseLink.h"
#include "QuestPage.h"
#include "RsMenu.h"
#include "VanillaPages.h"
#include "soh/Enhancements/rs/quest/Quest.h"
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
    return "open=" + std::to_string(status.open ? 1 : 0) + " phase=" + RsMenu_PhaseName(status.phase) +
           " page=" + std::to_string(status.page + 1) + " level=" + std::to_string(RsMenu_Level()) +
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

// `close` mirrors the button and starts the SLIDE; `close now` skips it. Both are here because the
// two answer different questions: the slide is the path a player takes and the one a screenshot
// should be taken of, and the instant close is the one a run reaches for when the next command must
// not race a half-second animation. Neither is a refusal, so both are rc=0.
int32_t Close(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const bool instant = args.size() >= 2 && args[1] == "now";
    if (args.size() >= 2 && !instant) {
        Addf(lines, "op=close result=error error=arg %s", Describe().c_str());
        return 1;
    }
    const bool wasOpen = instant ? RsMenu_Close() : RsMenu_BeginClose();
    Addf(lines, "op=close result=ok was_open=%d instant=%d %s", wasOpen ? 1 : 0, instant ? 1 : 0,
         Describe().c_str());
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
// mid-sweep screenshot self-describing: it says which of the seventeen poses the animation itself
// could have drawn this frame belongs to, so a measured position that is not one of them came from
// the renderer rather than from the game tick. `width` is how much parchment is still showing -
// 1 wide open, 0 shut - which is the channel a pixel scan measures.
std::string DescribeSweep(const RsMenuSweepState& sweep) {
    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "sweep=%d loop=%d hold=%d tick=%d of=%d dir=%d from=%d to=%d hand=%s env=%.3f dx=%.2f "
                  "width=%.3f sweeps=%d stick_rolls=%d dpad_rolls=%d",
                  sweep.active ? 1 : 0, sweep.loop ? 1 : 0, sweep.hold ? 1 : 0, sweep.tick, sweep.ticks, sweep.dir,
                  sweep.fromPage + 1,
                  sweep.toPage + 1, sweep.movingHand == 0 ? "left" : "right", sweep.env, sweep.dx, sweep.width,
                  sweep.sweeps, sweep.stickRolls, sweep.dpadRolls);
    return buf;
}

// The HUD line's fields (#125), shared by `hud` and `dump`'s `section=hud`, with the flying equip icon
// last: `flight=` is active, state, item, target, x, y, alpha, size, move timer (RsMenuEquipFlightState).
// #130 adds `margin_t=` and `raised=on/total` (the HUD raise), `magic=` level, capacity, current, and
// `b_shift=` the move the interface would give B this frame.
std::string DescribeHud() {
    const RsMenuHudState h = RsMenu_HudState();
    const RsMenuEquipFlightState f = RsVanilla_EquipFlightState();
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "valid=%d mode=%d prev=%d status=%d,%d,%d,%d,%d,%d,%d,%d,%d "
                  "alpha=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d b_label=%d b_label_shown=%d margin_t=%d raised=%d/%d "
                  "magic=%d,%d,%d b_shift=%d,%d "
                  "flight=%d,%d,%d,%d,%d,%d,%d,%d,%d flight_ticks=%d flight_hold=%d flights=%d landings=%d "
                  "flight_quad=%.3f,%.3f,%.3f flight_loop=%d flight_probe=%d",
                  h.valid ? 1 : 0, h.mode, h.prevMode, h.status[0], h.status[1], h.status[2], h.status[3],
                  h.status[4], h.status[5], h.status[6], h.status[7], h.status[8], h.alpha[0], h.alpha[1],
                  h.alpha[2], h.alpha[3], h.alpha[4], h.alpha[5], h.alpha[6], h.alpha[7], h.alpha[8], h.alpha[9],
                  h.alpha[10], h.alpha[11], h.alpha[12], h.bLabel, h.bLabelShown, h.marginTop, h.raisedOn,
                  h.raiseElements, h.magicLevel, h.magicCapacity, h.magic, h.bShiftX, h.bShiftY, f.active ? 1 : 0,
                  f.state, f.item, f.target, f.x, f.y, f.alpha, f.size, f.moveTimer, f.ticks, f.holdAt, f.flights,
                  f.landings, f.qLeft, f.qTop, f.qSide, f.loop ? 1 : 0, f.probe ? 1 : 0);
    return buf;
}

// Why a roll was refused, named once for every path that can refuse one. `level` when the scroll is
// in a detail view or moving between levels: the ring belongs to the top level, and `no_ring` there
// would send a run looking for a page problem. `fallback` is the path's own kind (a bad tick is
// `range`, a one-page ring is `no_ring`).
const char* SweepRefusal(const char* fallback) {
    const RsMenuLevelState level = RsMenu_LevelState();
    if (level.level != 0 || level.active) {
        return "level";
    }
    return RsMenu_SweepState().active ? "busy" : fallback;
}

int32_t Sweep(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        int32_t delta = 0;
        if (args[1] == "loop") {
            if (!RsMenu_StartSweepLoop()) {
                Addf(lines, "op=sweep result=error error=%s %s %s", SweepRefusal("no_ring"),
                     DescribeSweep(RsMenu_SweepState()).c_str(), Describe().c_str());
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
                Addf(lines, "op=sweep result=error error=%s asked=%d %s %s", SweepRefusal("range"), tick,
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
            Addf(lines, "op=sweep result=error error=%s %s %s", SweepRefusal("no_ring"),
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

// The level gesture's fields, formatted once because four lines report them. `pose=` is where the
// scroll is along the DOWNWARD path (0 flat, `of` vertical) whichever way it is travelling, which
// is what a held screenshot is checked against; `phase=` names which of close / turn / open that
// pose is in; `sep=` is roll centre to roll centre in the scroll's own frame and `angle=` its turn in
// degrees counter-clockwise - the two numbers Spencer tunes, printed so a capture carries them.
// #130's fields follow the gesture's: `drop=` the scroll's screen offset, `hand_l=`/`hand_r=` each hand's
// last drawn extent (x0,y0,x1,y1, screen, y down), `hud_shown=`/`b_shown=`/`b_moved=` what level 1 does to
// START and A, and to B, and `detail_rect=` the journal's text band.
std::string DescribeLevel(const RsMenuLevelState& level) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "level=%d moving=%d loop=%d hold=%d tick=%d of=%d dir=%d pose=%d phase=%s sep=%.1f angle=%.1f "
                  "swaps=%d drop=%.1f hand_l=%.1f,%.1f,%.1f,%.1f hand_r=%.1f,%.1f,%.1f,%.1f hud_shown=%.3f "
                  "b_shown=%.3f b_moved=%d detail_rect=%d,%d,%d,%d",
                  level.level, level.active ? 1 : 0, level.loop ? 1 : 0, level.hold ? 1 : 0, level.tick, level.ticks,
                  level.dir, level.pose, level.phase, level.separation, level.angle, level.swaps, level.drop,
                  level.hands[0][0], level.hands[0][1], level.hands[0][2], level.hands[0][3], level.hands[1][0],
                  level.hands[1][1], level.hands[1][2], level.hands[1][3], level.hudShown, level.bShown,
                  level.bMoved ? 1 : 0, level.detail.x0, level.detail.y0, level.detail.x1, level.detail.y1);
    return buf;
}

bool ParseLevelDir(const std::string& word, int32_t* dir) {
    if (word == "down") {
        *dir = 1;
        return true;
    }
    if (word == "up") {
        *dir = -1;
        return true;
    }
    return false;
}

int32_t Level(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        const std::string& word = args[1];
        bool ok = true;
        const char* error = "refused";
        if (word == "down") {
            ok = RsMenu_Descend();
        } else if (word == "up") {
            ok = RsMenu_Ascend();
        } else if (word == "loop") {
            ok = RsMenu_StartLevelLoop();
        } else if (word == "stop") {
            RsMenu_StopLevelLoop();
        } else if (word == "hold") {
            int32_t dir = 0;
            int32_t tick = 0;
            if (args.size() < 4 || !ParseLevelDir(args[2], &dir) || !ParseIndex(args[3], &tick)) {
                Addf(lines, "op=level result=error error=arg %s", Describe().c_str());
                return 1;
            }
            // `range` is a bad tick and nothing else; a hold refused for any other reason (a roll in
            // flight, a page with no detail view) is `refused`, like every other level refusal.
            if (tick > RsMenu_LevelState().ticks) {
                Addf(lines, "op=level result=error error=range asked=%d %s %s", tick,
                     DescribeLevel(RsMenu_LevelState()).c_str(), Describe().c_str());
                return 1;
            }
            ok = RsMenu_HoldLevel(dir, tick);
        } else {
            Addf(lines, "op=level result=error error=arg %s", Describe().c_str());
            return 1;
        }
        if (!ok) {
            // One kind for "cannot from here" - not settled, already moving, wrong level, the cursor
            // on a hand, or a page with no detail view. The state on the same line says which.
            Addf(lines, "op=level result=error error=%s %s %s", error, DescribeLevel(RsMenu_LevelState()).c_str(),
                 Describe().c_str());
            return 1;
        }
    }
    Addf(lines, "op=level result=ok %s %s", DescribeLevel(RsMenu_LevelState()).c_str(), Describe().c_str());
    return 0;
}

int32_t Filler(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        int32_t count = 0;
        if (!ParseIndex(args[1], &count)) {
            Addf(lines, "op=filler result=error error=arg %s", Describe().c_str());
            return 1;
        }
        if (!RsMenuQuestPage_SetFiller(count)) {
            Addf(lines, "op=filler result=error error=range asked=%d max=%d %s", count, RsMenuQuestPage_MaxFiller(),
                 Describe().c_str());
            return 1;
        }
    }
    const RsMenuQuestPageStatus quests = RsMenuQuestPage_Status();
    Addf(lines, "op=filler result=ok filler=%d rows=%d real=%d visible=%d %s", quests.fillerRows, quests.rows,
         quests.realRows, quests.visible, Describe().c_str());
    return 0;
}

// Stage 7's stress mode, formatted once because `stress` and `dump` both print it. `glyphs=-1` is
// off; 0 is on with an empty page. `capacity=` is how many glyphs the page holds in the mode shown,
// so a refusal and a success both say how far a run could have gone.
//
// `memo` is Fast3D's memo of texture-path resolution (Interpreter::SetResolvedResourceCacheEnabled,
// libultraship #1175). OTRGlobals turns it on at startup. Without it every glyph's G_SETTIMG goes
// through ResourceManager::LoadResourceProcess - a std::string built from the path, hashed, looked up
// under a mutex (twice when alt assets are on: the `alt/` path is tried first). Flipping it isolates
// how much of a glyph is that lookup. It lives HERE, not in RsMenu.cpp, because fast/interpreter.h's
// gbi.h collides with z64.h's (C4005 GIMMCMD), and this file includes no z64.h. A lever, not a
// setting: not a CVar, startup's value comes back on restart, and `stress off` puts back the value
// the first `memo` flip found. `memo=` on the line is the interpreter's own state, -1 with none.
// `memo_repaths=` counts memo hits whose address had been rewritten with another path since it was
// memoized (the message font's glyph slots do this every message) - the proof the memo's path check
// is being exercised rather than merely present.
std::shared_ptr<Fast::Interpreter> GetInterpreter() {
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    return window != nullptr ? window->GetInterpreterWeak().lock() : nullptr;
}

// -1 until a `memo` flip, then the state that flip replaced.
int32_t sMemoBeforeFlip = -1;

bool SetResolveMemo(bool on) {
    std::shared_ptr<Fast::Interpreter> interpreter = GetInterpreter();
    if (interpreter == nullptr) {
        return false;
    }
    if (sMemoBeforeFlip < 0) {
        sMemoBeforeFlip = interpreter->IsResolvedResourceCacheEnabled() ? 1 : 0;
    }
    interpreter->SetResolvedResourceCacheEnabled(on);
    return true;
}

std::string DescribeStress(const RsMenuStressState& stress) {
    std::shared_ptr<Fast::Interpreter> interpreter = GetInterpreter();
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "stress=%d glyphs=%d same=%d capacity=%d scale=%.2f pitch=%d memo=%d memo_repaths=%llu",
                  stress.on ? 1 : 0, stress.glyphs, stress.same ? 1 : 0, stress.capacity, stress.scale, stress.pitch,
                  interpreter == nullptr ? -1 : (interpreter->IsResolvedResourceCacheEnabled() ? 1 : 0),
                  interpreter == nullptr ? 0ULL : (unsigned long long)interpreter->GetResolvedResourceCacheRepaths());
    return buf;
}

int32_t Stress(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        const std::string& word = args[1];
        // Every form has a fixed arity, and a stray word is refused rather than ignored.
        if (word == "off") {
            if (args.size() != 2) {
                Addf(lines, "op=stress result=error error=arg %s", Describe().c_str());
                return 1;
            }
            // `off` puts back EVERYTHING stress changed, the memo included - a renderer-wide switch
            // left flipped would quietly skew every later perf line in the session.
            RsMenu_StopStress();
            if (sMemoBeforeFlip >= 0) {
                if (!SetResolveMemo(sMemoBeforeFlip == 1)) {
                    Addf(lines, "op=stress result=error error=no_interpreter %s %s",
                         DescribeStress(RsMenu_StressState()).c_str(), Describe().c_str());
                    return 1;
                }
                sMemoBeforeFlip = -1;
            }
        } else if (word == "memo") {
            if (args.size() != 3 || (args[2] != "on" && args[2] != "off")) {
                Addf(lines, "op=stress result=error error=arg %s", Describe().c_str());
                return 1;
            }
            if (!SetResolveMemo(args[2] == "on")) {
                Addf(lines, "op=stress result=error error=no_interpreter %s %s",
                     DescribeStress(RsMenu_StressState()).c_str(), Describe().c_str());
                return 1;
            }
        } else {
            int32_t glyphs = 0;
            const bool same = args.size() >= 3 && args[2] == "same";
            if (!ParseIndex(word, &glyphs) || args.size() > 3 || (args.size() == 3 && !same)) {
                Addf(lines, "op=stress result=error error=arg %s", Describe().c_str());
                return 1;
            }
            if (!RsMenu_SetStress(glyphs, same)) {
                Addf(lines, "op=stress result=error error=range asked=%d max=%d %s %s", glyphs,
                     RsMenu_StressCapacity(same), DescribeStress(RsMenu_StressState()).c_str(), Describe().c_str());
                return 1;
            }
        }
    }
    Addf(lines, "op=stress result=ok %s %s", DescribeStress(RsMenu_StressState()).c_str(), Describe().c_str());
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
    // The arrival, which is the half of the menu's presentation a `dump` could not otherwise show:
    // `phase` says where it is, `progress` how far (0 below the screen, 1 settled) and `dim` how
    // dark the world is behind it this frame.
    Addf(lines, "op=dump section=entry phase=%s tick=%d of=%d progress=%.3f dim=%d",
         RsMenu_PhaseName(status.phase), status.entryTick, status.entryTicks, status.entryProgress,
         status.dimAlpha);
    Addf(lines,
         "op=dump section=freeze halt=%d halt_prev=%d hud_held=%d hud_prev=%d hud_now=%d hud_reasserts=%d "
         "kaleido=%d viewpoint=%d viewpoint_vetoes=%d free_look_vetoes=%d manual_cam=%d cam_xy=%.1f,%.1f "
         "minimap_off=%d cs_mode=%d cs_index=0x%04X cs_next=0x%04X",
         status.halt ? 1 : 0, status.haltPrev ? 1 : 0, status.hudHeld ? 1 : 0, status.hudPrev, status.hudNow,
         status.hudReasserts,
         status.kaleido, status.viewpoint, status.viewpointVetoes, status.freeLookVetoes,
         status.manualCamera ? 1 : 0, status.camX, status.camY, status.minimapOff,
         status.csMode, status.csIndex, status.csNext);
    // The START filter's witness. `filter_armed` counts the frames it was entitled to swallow on,
    // `start_swallowed` the edges it actually took, and `kaleido=` above says whether vanilla pause
    // got in anyway - which is the difference between "the filter worked" and "no START arrived".
    Addf(lines,
         "op=dump section=start filter_armed=%d start_swallowed=%d start_consumed=%d press_seen=0x%04X "
         "close_dropped=%d start_level_dropped=%d open_refused=%d",
         status.filterArmedFrames, status.startSwallowed, status.startConsumed, status.filterPressSeen,
         status.closeDropped, status.startLevelDropped, status.openRefused);
    // `bindings=-1` means the control deck could not be read at all, which is a different thing
    // from a trigger nobody has bound - and on a GameCube pad `bound=0` is the DEFAULT, not a fault.
    Addf(lines, "op=dump section=trigger button=START mask=0x%04X bindings=%d bound=%d", trigger.mask,
         trigger.bindings, trigger.bound ? 1 : 0);
    // The live roll. `tick=`/`of=` says where in the excursion this line was read, which is what a
    // mid-sweep screenshot is asserted against; `hand=` is the half of "the moving hand follows the
    // shoulder pressed" that a screenshot alone cannot prove it MEANT to do.
    Addf(lines, "op=dump section=sweep %s", DescribeSweep(RsMenu_SweepState()).c_str());
    Addf(lines, "op=dump section=level %s", DescribeLevel(RsMenu_LevelState()).c_str());
    // The probe's lattice, plus `epoch` - the one number that says whether something has quietly
    // switched interpolation off for the rest of the frame - and `pause_mode`, the register that
    // would have exempted a View bracket from bumping it and which this menu must never set.
    Addf(lines, "op=dump section=interp %s pause_mode=%d", DescribeProbe(status).c_str(), status.pauseMenuMode);
    // What the LAST DRAWN FRAME cost, in the units OVERLAY_DISP's 2048-word budget is denominated
    // in. `dl_words` is the heap display list's length: the menu submits one gSPDisplayList, so it
    // no longer spends that budget, but the number is what stage 6's journal has to fit inside and
    // is free to carry here. Zero while the menu is closed - nothing was drawn. `cursor_drawn=` is not a
    // cost: whether that frame drew the cursor box (#124), 0 through a roll or a level change.
    Addf(lines, "op=dump section=draw glyphs=%d quads=%d icons=%d dl_words=%d cursor_drawn=%d", status.drawGlyphs,
         status.drawQuads, status.drawIcons, status.dlWords, status.cursorDrawn ? 1 : 0);
    Addf(lines, "op=dump section=hud %s", DescribeHud().c_str());
    // #128: the page stepper. Every field is `stepper_`-prefixed: `at=`, `alpha=` and `row=` would each
    // collide with another section's field on a grep of the whole dump. `stepper_at=` is 1-based, as `page=`.
    {
        const RsMenuStepperState s = RsMenu_StepperState();
        auto join = [](const std::vector<int32_t>& v) {
            std::string out;
            for (size_t i = 0; i < v.size(); i++) {
                out += (i > 0 ? "," : "") + std::to_string(v[i]);
            }
            return out;
        };
        // Worst case about 250 bytes (seven-character coordinates, two ramps of eight "255"), inside Addf's 512.
        Addf(lines,
             "op=dump section=stepper stepper_shown=%d stepper_alpha=%d stepper_at=%d stepper_stones=%d "
             "stepper_side=%.1f stepper_row=%.1f,%.1f,%.1f,%.1f stepper_ramp_open=%s "
             "stepper_ramp_close=%s",
             s.shown ? 1 : 0, s.alpha, s.at + 1, s.stones, s.stone, s.x0, s.y0, s.x1, s.y1,
             join(s.rampOpen).c_str(), join(s.rampClose).c_str());
    }
    // #132: the name panel, as the last drawn frame drew it. Every field is `name_panel_`-prefixed: `name=` is
    // already the state marker's scene name and `quest`'s, and `item=`, `timer=` and `at=` are other sections'.
    // `name_panel_text=` is what is on the stone (name, prompt, to - a hand's label - or none) and
    // `name_panel_tex=` its texture's resource path, `-` for a prompt or nothing; it goes LAST because a custom
    // name's path is whatever the hook wrote. Worst case about 420 bytes with an 80-character path, inside Addf's
    // 512. The L/R icons are a line of their own for the same budget.
    {
        static const char* const kPrompt[] = { "none", "c_equip", "a_equip", "a_play_melody" };
        static_assert(sizeof(kPrompt) / sizeof(kPrompt[0]) == RS_MENU_PROMPT_COUNT, "one name per RsMenuNamePrompt");
        const RsNamePanelState n = RsNamePanel_State();
        const int32_t prompt = n.prompt >= 0 && n.prompt < RS_MENU_PROMPT_COUNT ? n.prompt : 0;
        Addf(lines,
             "op=dump section=name_panel name_panel_shown=%d name_panel_stone=%d name_panel_text=%s "
             "name_panel_item=%s name_panel_grey=%d name_panel_timer=%d name_panel_alternates=%d name_panel_sub=%d "
             "name_panel_prompt=%s name_panel_custom=%d name_panel_lookups=%d name_panel_customs=%d "
             "name_panel_at=%.1f,%.1f,%.1f,%.1f name_panel_tex=%s",
             n.shown ? 1 : 0, n.stone ? 1 : 0, n.text, n.itemName.c_str(), n.grey ? 1 : 0, n.timer,
             n.alternates ? 1 : 0, n.subState, kPrompt[prompt], n.custom ? 1 : 0, n.lookups, n.customs,
             n.stoneRect[0], n.stoneRect[1], n.stoneRect[2], n.stoneRect[3], n.tex.empty() ? "-" : n.tex.c_str());
        static const char* const kBig[] = { "none", "left", "right" };
        Addf(lines,
             "op=dump section=name_panel_lr name_panel_lr_shown=%d name_panel_lr_big=%s "
             "name_panel_l=%.1f,%.1f,%.1f,%.1f name_panel_r=%.1f,%.1f,%.1f,%.1f",
             n.lr ? 1 : 0, kBig[n.lrBig >= 0 && n.lrBig < 3 ? n.lrBig : 0], n.lRect[0], n.lRect[1], n.lRect[2],
             n.lRect[3], n.rRect[0], n.rRect[1], n.rRect[2], n.rRect[3]);
    }
    // #131: THE HARNESS CANNOT HEAR, so every sound the menu plays is counted and a run asserts the
    // counts. One field per event, named by RsMenu_SfxEventName so adding an event adds a field and
    // nothing here changes. A count proves the call was made, NOT that anything came out of the
    // speaker - Spencer's listening pass is the other half, and neither is sufficient alone.
    {
        // No state suffix: not one other `section=` line carries one, and adding it here put a second
        // `open=` on the line - which is what the first #131 run read as a sound count.
        std::string sfx = "op=dump section=sfx";
        for (int32_t i = 0; i < RS_MENU_SFX_COUNT; i++) {
            sfx += " " + std::string(RsMenu_SfxEventName(i)) + "=" + std::to_string(RsMenu_SfxCount(i));
        }
        lines.push_back(sfx);
    }
    // Stage 7: the same last frame, counting only the VIEW - the page body, detail body or stress
    // body inside the content node - so chrome and content separate. `drawn_rows` is distinct text
    // lines, `drawn_glyphs` glyphs, `drawn_words` the Gfx words the view appended. `view=none` is a
    // frame that drew no content (a roll or a level change in flight). `frame=` equals `draw_frames=`
    // on the counters line when the numbers are this frame's.
    {
        const RsMenuViewStats view = RsMenu_ViewStats();
        Addf(lines,
             "op=dump section=view view=%s frame=%d drawn_rows=%d drawn_glyphs=%d drawn_icons=%d drawn_words=%d",
             view.view, view.frame, view.rows, view.glyphs, view.icons, view.words);
        Addf(lines, "op=dump section=stress %s", DescribeStress(RsMenu_StressState()).c_str());
    }
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
    // The quest page (stage 6): what the list is, and - one `section=row` line each - exactly which
    // rows the LAST DRAWN FRAME put on screen, top to bottom, with the colour each was drawn in.
    // That is what a screenshot of the list is asserted against. `cursor_row=-1` means the cursor
    // is on a hand (or the ring is on another page). `section=journal` is the detail view's last
    // frame: `lines` wrapped lines in all, `top` the first on screen, `drawn` how many fitted, and
    // `max_top` how far it can scroll - 0 means the whole journal fits.
    {
        const RsMenuQuestPageStatus quests = RsMenuQuestPage_Status();
        Addf(lines,
             "op=dump section=quests rows=%d real=%d filler=%d visible=%d top=%d cursor_row=%d drawn=%d "
             "debug_tier=%d",
             quests.rows, quests.realRows, quests.fillerRows, quests.visible, quests.top, quests.cursorRow,
             (int32_t)quests.drawn.size(), quests.showsDebugTier ? 1 : 0);
        for (size_t i = 0; i < quests.drawn.size(); i++) {
            const RsMenuQuestRowInfo& row = quests.drawn[i];
            Addf(lines, "op=dump section=row pos=%d row=%d quest=%d token=%s status=%s colour=%s cursor=%d",
                 (int32_t)i + 1, row.row, row.questId, row.token.c_str(), Quest_StatusName(row.status),
                 RsMenuQuestPage_StatusColourName(row.status), row.row == quests.cursorRow ? 1 : 0);
        }
        Addf(lines,
             "op=dump section=journal selected_row=%d quest=%d token=%s lines=%d top=%d drawn=%d max_top=%d "
             "width=%d",
             quests.selectedRow, quests.selectedQuestId,
             quests.selectedToken.empty() ? "-" : quests.selectedToken.c_str(), quests.journalLines,
             quests.journalTop, quests.journalDrawn, quests.journalMaxTop, quests.journalWidth);
    }
    // Stage 8: the three ported vanilla pages. One `section=port` line per page - its ring position,
    // the one scale and offset its kaleido table was mapped through, and the drawn extent that mapping
    // gives - then one `section=slot` line per slot: vanilla's cursor point, the mapped box, the item
    // (its ITEM_ enum token, so `item=ITEM_HOOKSHOT cursor=1` is "the cursor is on the Hookshot"), and
    // whether it is owned, a cursor node right now, drawn greyed, and under the cursor. Every page is
    // listed, visible or not - a slot list is a function of the save, not of what is on screen.
    // `section=link` is Link's portrait: renders, loads, the framebuffer, and the segment restore.
    {
        const RsMenuCursorNode* cursorNode = RsMenu_CursorAt(RsMenu_CursorIndex());
        const std::string cursorId = cursorNode != nullptr ? cursorNode->id : "";
        for (const RsMenuPortInfo& port : RsMenuVanillaPages_Describe()) {
            int32_t nodes = 0;
            for (const RsMenuSlotInfo& slot : port.slots) {
                nodes += slot.isNode ? 1 : 0;
            }
            Addf(lines,
                 "op=dump section=port page=%d id=%s current=%d scale=%.2f offset=%d,%d extent=%d,%d,%d,%d slots=%d "
                 "nodes=%d",
                 port.pageIndex + 1, port.pageId.c_str(), port.pageIndex == status.page ? 1 : 0, port.scale,
                 port.offsetX, port.offsetY, port.x0, port.y0, port.x1, port.y1, (int32_t)port.slots.size(), nodes);
            const bool visible = port.pageIndex == status.page;
            for (const RsMenuSlotInfo& slot : port.slots) {
                Addf(lines,
                     "op=dump section=slot page=%d id=%s slot=%d box=%d,%d,%d,%d item=%s owned=%d node=%d grey=%d "
                     "cursor=%d",
                     port.pageIndex + 1, slot.node.c_str(), slot.slot, slot.x, slot.y, slot.w, slot.h,
                     slot.itemName.c_str(), slot.owned ? 1 : 0, slot.isNode ? 1 : 0, slot.grey ? 1 : 0,
                     visible && slot.node == cursorId ? 1 : 0);
            }
        }
        const RsPauseLinkStatus link = RsPauseLink_Status();
        Addf(lines,
             "op=dump section=link renders=%d loads=%d last_frame=%d fb=%d age=%d load_size=0x%X seg4=0x%llX "
             "seg4_during=0x%llX seg4_now=0x%llX seg6=0x%llX seg6_during=0x%llX seg6_now=0x%llX",
             link.renders, link.loads, link.lastRenderFrame, link.frameBuffer, link.age, link.loadSize,
             (unsigned long long)link.seg4, (unsigned long long)link.seg4Clobbered,
             (unsigned long long)link.seg4Now, (unsigned long long)link.seg6,
             (unsigned long long)link.seg6Clobbered, (unsigned long long)link.seg6Now);
    }
    Addf(lines, "op=dump section=cursor %s", DescribeCursor().c_str());
    for (int32_t i = 0; i < RsMenu_CursorCount(); i++) {
        const RsMenuCursorNode* node = RsMenu_CursorAt(i);
        if (node == nullptr) {
            continue;
        }
        const RsMenuCursorNode* left = RsMenu_CursorAt(node->left);
        const RsMenuCursorNode* right = RsMenu_CursorAt(node->right);
        const RsMenuCursorNode* up = RsMenu_CursorAt(node->up);
        const RsMenuCursorNode* down = RsMenu_CursorAt(node->down);
        Addf(lines,
             "op=dump section=node index=%d current=%d id=%s hand=%d box=%d,%d,%d,%d left=%s right=%s up=%s down=%s",
             i, i == RsMenu_CursorIndex() ? 1 : 0, node->id.c_str(), node->hand, node->x, node->y, node->w, node->h,
             left != nullptr ? left->id.c_str() : "-", right != nullptr ? right->id.c_str() : "-",
             up != nullptr ? up->id.c_str() : "-", down != nullptr ? down->id.c_str() : "-");
    }
    return 0;
}

// Stage 8's two read-only probes for the differential tests. Neither writes anything.
//
// `kaleido` reads VANILLA pause's live cursor - the other half of every "same inputs, same slot"
// comparison. `special=` is none, left or right (the page arrows, which the scroll's hands stand in
// for); `point=` is the cursor point the scroll's node ids carry (`items_09` <-> page=0 point=9).
int32_t Kaleido(std::vector<std::string>& lines) {
    const RsMenuKaleidoCursor k = RsMenu_KaleidoCursor();
    if (!k.valid) {
        Addf(lines, "op=kaleido result=error error=no_play %s", Describe().c_str());
        return 1;
    }
    static const char* const kSpecial[] = { "none", "left", "right" };
    // #132's three after `sub=`, so earlier runs' patterns (which end there) still match.
    Addf(lines,
         "op=kaleido result=ok state=%d debug=%d page=%d point=%d x=%d y=%d special=%s item=%d slot=%d sub=%d "
         "named_item=%s name_timer=%d name_grey=%d",
         k.state, k.debugState, k.page, k.point, k.x, k.y, kSpecial[k.special], k.item, k.slot, k.sub,
         k.namedName.c_str(), k.nameTimer, k.nameGrey);
    return 0;
}

// `hud` (#125): the gameplay HUD as the interface sees it, under either menu (VanillaPages.h,
// RsMenuHudState) - the other half of every "the scroll shows what vanilla pause shows" comparison.
// `status=` is buttonStatus B, C-left, C-down, C-right, A, D-up, D-down, D-left, D-right; `alpha=` is
// B, A, C-left, C-down, C-right, D-up, D-down, D-left, D-right, hearts, magic, minimap, START.
int32_t Hud(std::vector<std::string>& lines) {
    const RsMenuHudState hud = RsMenu_HudState();
    if (!hud.valid) {
        Addf(lines, "op=hud result=error error=no_play %s", Describe().c_str());
        return 1;
    }
    Addf(lines, "op=hud result=ok %s", DescribeHud().c_str());
    return 0;
}

// TEST-ONLY `flight hold <n>|release` (#125): parks the flying equip icon once it has taken n ticks,
// so a run can read one pose and screenshot it (the flight is ten ticks, half a second - shorter than a
// console round trip). The reply is the `hud` line.
int32_t Flight(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t tick = 0;
    if (args.size() == 3 && args[1] == "hold" && ParseIndex(args[2], &tick)) {
        RsVanilla_SetEquipFlightHold(tick);
    } else if (args.size() == 2 && args[1] == "release") {
        RsVanilla_SetEquipFlightHold(-1);
    } else if (args.size() == 2 && args[1] == "loop") {
        // #133. Refused rather than silently doing nothing when no flight has been started yet: the
        // loop replays one, and "it is looping but nothing moved" is the hardest kind of run to read.
        if (!RsVanilla_StartEquipFlightLoop()) {
            Addf(lines, "op=flight result=error error=no_flight %s", DescribeHud().c_str());
            return 1;
        }
    } else if (args.size() == 2 && args[1] == "stop") {
        RsVanilla_StopEquipFlightLoop();
    } else if (args.size() == 3 && args[1] == "probe" && (args[2] == "on" || args[2] == "off")) {
        RsVanilla_SetEquipFlightProbe(args[2] == "on");
    } else {
        Addf(lines, "op=flight result=error error=arg %s", Describe().c_str());
        return 1;
    }
    Addf(lines, "op=flight result=ok %s", DescribeHud().c_str());
    return 0;
}

// `song` (#127, read-only): the Quest Status page's song playback and the ocarina's two staves
// (VanillaPages.h, RsMenuSongState). The staves are global, so under vanilla pause this line reads the
// same demo and attempt kaleido is running - the vanilla half of the comparison.
int32_t Song(std::vector<std::string>& lines) {
    const RsMenuSongState s = RsMenu_SongState();
    Addf(lines,
         "op=song result=ok state=%d point=%d song=%d count=%d notes=%d,%d,%d,%d,%d,%d,%d,%d muted=%d "
         "playback=%d,%d,%d playing=%d,%d,%d bgm_muted=%d previews=%d demos=%d hits=%d misses=%d %s",
         s.state, s.point, s.songIdx, s.count, s.notes[0], s.notes[1], s.notes[2], s.notes[3], s.notes[4],
         s.notes[5], s.notes[6], s.notes[7], s.muted ? 1 : 0, s.playbackPos, s.playbackState, s.playbackButton,
         s.playingPos, s.playingState, s.playingButton, s.bgmMutedByAudio, s.previews, s.demos, s.hits, s.misses,
         Describe().c_str());
    return 0;
}

// `equips` reads the save fields an equip writes, so a run can compare the scroll's result with
// vanilla's field for field: the eight button items (B, C-left, C-down, C-right, D-up, D-down, D-left,
// D-right), the seven C/D slots, the equipment word, the swordless flag and infTable[29], the sword's
// health and the BGS flag. `dpad=` is SoH's DpadEquips, `done=` how many equips the ported pages have
// performed this session.
//
// A second line, `op=equips section=player`, is Link in the world (#123): the live Player's sword item,
// shield, tunic and boots, the model group and hand/sheath types Player_SetModelGroup derived from them,
// the save's age, and `syncs=` - how many times the scroll's close has handed the save to the Player.
// The save line changing while this one does not is exactly the #123 bug.
int32_t Equips(std::vector<std::string>& lines) {
    const RsMenuEquipState e = RsMenu_EquipState();
    Addf(lines,
         "op=equips result=ok buttons=%d,%d,%d,%d,%d,%d,%d,%d slots=%d,%d,%d,%d,%d,%d,%d equipment=0x%04X "
         "swordless=%d inf29=0x%04X sword_health=%d bgs=%d dpad=%d done=%d",
         e.buttons[0], e.buttons[1], e.buttons[2], e.buttons[3], e.buttons[4], e.buttons[5], e.buttons[6], e.buttons[7],
         e.slots[0], e.slots[1], e.slots[2], e.slots[3], e.slots[4], e.slots[5], e.slots[6], e.equipment,
         e.swordless ? 1 : 0, e.inf29, e.swordHealth, e.bgsFlag, e.dpadEquips ? 1 : 0, e.equipsDone);
    Addf(lines,
         "op=equips section=player valid=%d sword=%d shield=%d tunic=%d boots=%d model_group=%d anim_type=%d "
         "left_hand=%d right_hand=%d sheath=%d age=%d syncs=%d",
         e.player ? 1 : 0, e.playerSword, e.playerShield, e.playerTunic, e.playerBoots, e.modelGroup,
         e.modelAnimType, e.leftHandType, e.rightHandType, e.sheathType, e.linkAge, e.playerSyncs);
    return 0;
}

// TEST-ONLY: `inv <item|equip|upgrade|quest|sword|age> <a> <b>` - the sparse-inventory fixture (VanillaPages.h,
// RsMenu_TestSetInventory). Writes gSaveContext (and `age` reloads the scene); never run it on a save
// anyone cares about.
int32_t Inv(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t a = 0;
    int32_t b = 0;
    if (args.size() != 4 || !ParseIndex(args[2], &a) || !ParseIndex(args[3], &b) ||
        !RsMenu_TestSetInventory(args[1], a, b)) {
        Addf(lines, "op=inv result=error error=arg %s", Describe().c_str());
        return 1;
    }
    Addf(lines, "op=inv result=ok kind=%s a=%d b=%d %s", args[1].c_str(), a, b, Describe().c_str());
    return 0;
}

// TEST-ONLY `namepanel custom <item>|off` (#132): a VB_DRAW_CUSTOM_ITEM_NAME handler naming ITEM_ id <item> with
// the Ocarina of Time's name, as rando's Roc's Feather names its item (NamePanel.h). Refused while the menu is
// closed, because the handler comes off when the menu does. `custom_item=` is the item now named, -1 for off;
// the panel asks the hook again on its next settled tick, so read `section=name_panel` after that.
int32_t NamePanelCmd(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t item = -1;
    const bool off = args.size() == 2 && args[1] == "off";
    const bool custom = args.size() == 3 && args[1] == "custom" && ParseIndex(args[2], &item) && item <= 0xFF;
    if (!off && !custom) {
        Addf(lines, "op=namepanel result=error error=arg %s", Describe().c_str());
        return 1;
    }
    if (!RsMenu_IsOpen()) {
        Addf(lines, "op=namepanel result=error error=closed %s", Describe().c_str());
        return 1;
    }
    const int32_t now = RsNamePanel_SetTestCustom(off ? -1 : item);
    Addf(lines, "op=namepanel result=ok custom_item=%d %s", now, Describe().c_str());
    return 0;
}

const char* kUsage = "usage: menu open | close [now] | page <n> | primary [custom|vanilla] | "
                     "sweep [l|r|loop|hold <l|r> <tick>|stop] | "
                     "level [down|up|loop|hold <down|up> <tick>|stop] | filler [n] | "
                     "stress [<n> [same]|off|memo <on|off>] | "
                     "cursor [left|right|up|down|select|<id>] | probe [on|off] | kaleido | equips | hud | "
                     "flight hold <n>|release | song | "
                     "inv <kind> <a> <b> | namepanel custom <item>|off | dump";

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
        return Close(args, lines);
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
    if (sub == "level") {
        return Level(args, lines);
    }
    if (sub == "filler") {
        return Filler(args, lines);
    }
    if (sub == "stress") {
        return Stress(args, lines);
    }
    if (sub == "probe") {
        return Probe(args, lines);
    }
    if (sub == "kaleido") {
        return Kaleido(lines);
    }
    if (sub == "equips") {
        return Equips(lines);
    }
    if (sub == "hud") {
        return Hud(lines);
    }
    if (sub == "flight") {
        return Flight(args, lines);
    }
    if (sub == "song") {
        return Song(lines);
    }
    if (sub == "inv") {
        return Inv(args, lines);
    }
    if (sub == "namepanel") {
        return NamePanelCmd(args, lines);
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
    "The mod-owned pause interface (sturdy-bassoon#111): open | close [now] | page <n> | "
    "primary [custom|vanilla] | sweep [l|r|loop|hold <l|r> <tick>|stop] | "
    "level [down|up|loop|hold <down|up> <tick>|stop] | filler [n] | "
    "stress [<n> [same]|off|memo <on|off>] | cursor [left|right|up|down|select|<id>] | "
    "probe [on|off] | kaleido | equips | hud | flight hold <n>|release | song | inv <kind> <a> <b> | "
    "namepanel custom <item>|off | dump. "
    "The scroll opens on START when primary is custom (N64 L is the minimap's) "
    "and hard-freezes the world; primary decides which menu START opens, and is a subcommand because "
    "there is no console `set`. sweep rolls the scroll one page the way a shoulder press does, and "
    "reports the tick it is on so a mid-sweep screenshot is self-describing; level goes down into the "
    "quest under the cursor (close, turn, open) or back up, with the same loop and hold instruments; "
    "filler adds n test-only rows to the quest list so it has more rows than fit; stress (test-only) "
    "replaces the page with n glyphs to measure what a dense horizontal page costs, and memo flips "
    "Fast3D's texture-path memo to isolate the per-glyph resource lookup; cursor walks the "
    "node graph whose end nodes are the two hands, and is the only way to assert which node the "
    "cursor is on, because a screenshot shows a box and not an adjacency. probe drives a stepped "
    "per-tick offset into the geometry and "
    "into a string at once, so one screenshot shows which of the two frame-interpolates. dump "
    "reports open/closed, the page ring, the freeze and HUD state, the live sweep, the cursor "
    "graph, what the last frame cost, whether START has a binding at all, and how many frames of "
    "input arrived while the world was frozen, plus every slot of the three ported vanilla pages. kaleido "
    "reads vanilla pause's live cursor and equips the save's equip fields plus what Link in the world "
    "is wearing - the two halves of the stage-8 differential tests; inv (test-only) writes a sparse "
    "inventory for them, and can set the Biggoron flags or switch Link's age. namepanel (test-only) names an item "
    "through the custom-name hook rando uses, so the name panel's hook path can be tested without a seed.",
    { { "open|close|page|primary|sweep|level|filler|stress|cursor|probe|kaleido|equips|hud|flight|song|inv|namepanel|"
        "dump",
        Ship::ArgumentType::TEXT },
      { "argument", Ship::ArgumentType::TEXT, true } });

} // namespace
