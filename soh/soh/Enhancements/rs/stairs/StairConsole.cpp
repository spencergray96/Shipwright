#include "StairConsole.h"

#include <cstdio>
#include <string>
#include <vector>

#include <ship/debug/Console.h>

#include "StairTable.h"
#include "Stairs.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/rs/actors/RsActorParams.h"
#include "soh/Enhancements/rs/actors/RsStairs.h"
#include "soh/Enhancements/rs/prefs/FloorText.h"
#include "soh/Enhancements/rs/prefs/RsPrefs.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

bool ParseInt(const std::string& word, int32_t* out) {
    try {
        size_t used = 0;
        const int value = std::stoi(word, &used, 0);
        if (used != word.size()) {
            return false;
        }
        *out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool InPlay() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr;
}

// The id argument, pre-validated here so every lookup below it is quiet by construction.
bool ParseStair(const std::vector<std::string>& args, size_t at, int32_t* stairId, std::vector<std::string>& lines) {
    if (args.size() <= at || !ParseInt(args[at], stairId)) {
        Addf(lines, "op=%s result=error error=missing_id", args[0].c_str());
        return false;
    }
    if (!RsStair_IsRegistered(*stairId)) {
        Addf(lines, "op=%s result=error error=unregistered stair=%d", args[0].c_str(), *stairId);
        return false;
    }
    return true;
}

bool ParseRow(const std::vector<std::string>& args, size_t at, int32_t stairId, int32_t* row,
              std::vector<std::string>& lines) {
    if (args.size() <= at || !ParseInt(args[at], row)) {
        Addf(lines, "op=%s result=error error=missing_row stair=%d", args[0].c_str(), stairId);
        return false;
    }
    if (RsStair_GetLanding(stairId, *row) == nullptr) {
        Addf(lines, "op=%s result=error error=bad_row stair=%d row=%d", args[0].c_str(), stairId, *row);
        return false;
    }
    return true;
}

std::string StoreyList(const RsStairDef& def) {
    std::string out;
    for (int32_t row = 0; row < def.landingCount; row++) {
        if (row > 0) {
            out += ",";
        }
        out += std::to_string(def.landings[row].storey);
    }
    return out;
}

// One row's menu, as the textbox composes it under the live convention and in the order the settings
// say now - the order the next box opened would use. The body and labels come from RsStair_Compose*,
// which read the same screen the renderer is handed.
void MenuLines(int32_t stairId, int32_t row, std::vector<std::string>& lines) {
    const int32_t order = RsStair_MenuOrder();
    const RsDialogueRule* screen = RsStair_Screen(stairId, row, order);
    Addf(lines, "menu[%d] options=%d body=\"%s\"", row, screen->optionCount,
         RsStair_ComposeBody(stairId, row, order).c_str());
    for (int32_t i = 0; i < screen->optionCount; i++) {
        Addf(lines, "menu[%d.%d] to_row=%d label=\"%s\"", row, i, RsStair_MenuDestination(stairId, row, order, i),
             RsStair_ComposeLabel(stairId, row, order, i).c_str());
    }
}

// The order fields every menu line set is read under: `list=` and `lead=`.
std::string OrderFields() {
    return std::string("list=") + RsStair_MenuListName(RsStair_GetMenuList()) +
           " lead=" + RsStair_MenuLeadName(RsStair_GetMenuLead());
}

// Where a staircase is: `scene=0x<id>` for a hand one (decision 18), `scene=by_map maps=<n,...>` for a
// generated one, whose rows name their maps.
std::string WhereFields(const RsStairDef& def) {
    char buf[32];
    if (def.sceneId != RS_STAIR_SCENE_BY_MAP) {
        std::snprintf(buf, sizeof(buf), "scene=0x%X", def.sceneId);
        return buf;
    }
    std::string maps;
    for (int32_t row = 0; row < def.landingCount; row++) {
        const std::string m = std::to_string(def.landings[row].map);
        if (("," + maps + ",").find("," + m + ",") == std::string::npos) {
            maps += (maps.empty() ? "" : ",") + m;
        }
    }
    return "scene=by_map maps=" + maps;
}

int32_t List(std::vector<std::string>& lines) {
    std::vector<int32_t> ids(RS_STAIR_MAX); // 32 KB since #214: the heap, not the console's stack
    const int32_t count = RsStair_ListIds(ids.data(), RS_STAIR_MAX);
    Addf(lines, "op=list stairs=%d", count);
    for (int32_t i = 0; i < count; i++) {
        const RsStairDef* def = RsStair_GetDef(ids[i]);
        Addf(lines, "stair[%d] name=%s %s rows=%d storeys=%s tier=%s", def->id, def->name, WhereFields(*def).c_str(),
             def->landingCount, StoreyList(*def).c_str(), RS_STAIR_ID_IS_DEBUG(def->id) ? "debug" : "prod");
    }
    return 0;
}

int32_t Dump(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t stairId = 0;
    if (!ParseStair(args, 1, &stairId, lines)) {
        return 1;
    }
    const RsStairDef* def = RsStair_GetDef(stairId);
    const int32_t convention = RsPrefs_GetFloorConvention();
    Addf(lines, "op=dump stair=%d name=%s %s here=%d rows=%d land_forward=%d convention=%s %s", def->id,
         def->name, WhereFields(*def).c_str(), InPlay() && RsStair_InScene(def->id, gPlayState->sceneNum) ? 1 : 0,
         def->landingCount,
         def->landForward, RsPrefs_FloorConventionName(convention), OrderFields().c_str());
    // Each storey's landing is computed from its placement, so it exists only while that placement
    // is loaded: `placed=0` is a storey in another room, or one nobody placed.
    for (int32_t row = 0; row < def->landingCount; row++) {
        const RsStairLanding& l = def->landings[row];
        // A generated row's map, and its destination name (decision 19) - nothing for a hand row, so its
        // lines are as they were.
        std::string gen;
        if (def->sceneId == RS_STAIR_SCENE_BY_MAP) {
            gen = " map=" + std::to_string(l.map) + " overrides=" + std::to_string(l.overrideCount);
            if (l.destName != nullptr) {
                gen += " dest_name=\"" + RsFloorText_Compose(l.destName) + "\"";
            }
        }
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        int16_t yaw = 0;
        const int32_t placed = InPlay() ? RsStair_PlacedLanding(stairId, row, &x, &y, &z, &yaw) : 0;
        if (placed) {
            Addf(lines, "row[%d] storey=%d room=%d placed=1 landing=%.1f,%.1f,%.1f yaw=%d label=\"%s\"%s", row,
                 l.storey, l.room, x, y, z, yaw, RsFloorText_Label(convention, l.storey, false).c_str(), gen.c_str());
        } else {
            Addf(lines, "row[%d] storey=%d room=%d placed=0 label=\"%s\"%s", row, l.storey, l.room,
                 RsFloorText_Label(convention, l.storey, false).c_str(), gen.c_str());
        }
    }
    for (int32_t row = 0; row < def->landingCount; row++) {
        MenuLines(stairId, row, lines);
    }
    // Words too long for their row, which the menu does not use (decision 23). Text last and quoted.
    const std::vector<RsStairWordsFallback> fallbacks = RsStair_Fallbacks(stairId);
    Addf(lines, "fallbacks=%d", static_cast<int>(fallbacks.size()));
    for (size_t i = 0; i < fallbacks.size(); i++) {
        const RsStairWordsFallback& f = fallbacks[i];
        Addf(lines, "fallback[%d] row=%d to_row=%d kind=%s field=%s text=\"%s\"", static_cast<int>(i), f.row, f.toRow,
             f.kind, f.field, f.text.c_str());
    }
    return 0;
}

int32_t Menu(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t stairId = 0;
    int32_t row = 0;
    if (!ParseStair(args, 1, &stairId, lines) || !ParseRow(args, 2, stairId, &row, lines)) {
        return 1;
    }
    Addf(lines, "op=menu stair=%d row=%d convention=%s %s", stairId, row,
         RsPrefs_FloorConventionName(RsPrefs_GetFloorConvention()), OrderFields().c_str());
    MenuLines(stairId, row, lines);
    return 0;
}

int32_t Where(std::vector<std::string>& lines) {
    if (!InPlay()) {
        // Nothing to report is an answer, not a refusal - `actors` says the same.
        lines.push_back("op=where scene=none stairs_here=0");
        return 0;
    }
    Player* player = GET_PLAYER(gPlayState);
    std::vector<int32_t> ids(RS_STAIR_MAX); // 32 KB since #214: the heap, not the console's stack
    const int32_t count = RsStair_ListIds(ids.data(), RS_STAIR_MAX);
    int32_t here = 0;
    for (int32_t i = 0; i < count; i++) {
        here += RsStair_InScene(ids[i], gPlayState->sceneNum);
    }
    Addf(lines, "op=where scene=0x%X room=%d pos=%.1f,%.1f,%.1f yaw=%d floor_y=%.1f ground=%d stairs_here=%d",
         gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num, player->actor.world.pos.x, player->actor.world.pos.y,
         player->actor.world.pos.z, player->actor.shape.rot.y, player->actor.floorHeight,
         (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 1 : 0, here);
    for (int32_t i = 0; i < count; i++) {
        const RsStairDef* def = RsStair_GetDef(ids[i]);
        if (!RsStair_InScene(def->id, gPlayState->sceneNum)) {
            continue;
        }
        const int32_t row = RsStair_RowNearestPlayer(def->id);
        Addf(lines, "near[%d] name=%s row=%d storey=%d", def->id, def->name, row,
             row >= 0 ? def->landings[row].storey : -1);
    }
    return 0;
}

int32_t Go(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t stairId = 0;
    int32_t row = 0;
    if (!ParseStair(args, 1, &stairId, lines) || !ParseRow(args, 2, stairId, &row, lines)) {
        return 1;
    }
    // "From" is only reported, so the nearest row is good enough - and -1 is an honest answer from
    // somewhere that is not a landing at all.
    const int32_t from = RsStair_RowNearestPlayer(stairId);
    // `result=ok|error` with the reason in `error=`, the wire format every console shares - so a run
    // greps one pattern for any refusal. The move's own `refused` marker carries the same name.
    const int32_t result = RsStair_BeginMove(stairId, from, row, "console");
    if (result != RS_STAIR_OK) {
        Addf(lines, "op=go result=error error=%s stair=%d from_row=%d to_row=%d", RsStair_ResultName(result), stairId,
             from, row);
        return 1;
    }
    Addf(lines, "op=go result=ok stair=%d from_row=%d to_row=%d fade=%d", stairId, from, row, RsStair_GetFadeTicks());
    return 0;
}

int32_t Status(std::vector<std::string>& lines) {
    const RsStairStatus status = RsStair_GetStatus();
    Addf(lines, "op=status phase=%s moving=%d stair=%d from_row=%d to_row=%d ticks=%d move_fade=%d fade=%d source=%s",
         status.phase, status.moving ? 1 : 0, status.stairId, status.fromRow, status.toRow, status.ticks,
         status.fadeTicks, RsStair_GetFadeTicks(), status.source[0] != '\0' ? status.source : "none");
    Addf(lines, "last=\"%s\"", status.last.c_str());
    return 0;
}

int32_t Fade(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        int32_t ticks = 0;
        if (args[1] == "default") {
            RsStair_ClearFadeTicks();
        } else if (!ParseInt(args[1], &ticks) || ticks < 0 || ticks > RS_STAIR_MAX_FADE_TICKS) {
            Addf(lines, "op=fade result=error error=bad_ticks range=0..%d", RS_STAIR_MAX_FADE_TICKS);
            return 1;
        } else {
            RsStair_SetFadeTicks(ticks);
        }
    }
    // `source=` says whether a run left an override behind in the owner's config - the thing to
    // check before a session ends.
    Addf(lines, "op=fade ticks=%d cut=%d source=%s", RsStair_GetFadeTicks(), RsStair_GetFadeTicks() == 0 ? 1 : 0,
         RsStair_FadeTicksOverridden() ? "cvar" : "default");
    return 0;
}

// `stairs bump`: the walk-into setting and its hold, each an override over a build default, the
// fade's shape. Both persist in the owner's config, so a run that sets them clears them again.
int32_t Bump(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        const std::string& what = args[1];
        if (what == "on" || what == "off") {
            RsStair_SetBumpEnabled(what == "on" ? 1 : 0);
        } else if (what == "default") {
            RsStair_ClearBumpEnabled();
        } else if (what == "hold") {
            int32_t ticks = 0;
            if (args.size() < 3) {
                Addf(lines, "op=bump result=error error=missing_ticks range=1..%d", RS_STAIR_MAX_BUMP_HOLD);
                return 1;
            }
            if (args[2] == "default") {
                RsStair_ClearBumpHold();
            } else if (!ParseInt(args[2], &ticks) || ticks < 1 || ticks > RS_STAIR_MAX_BUMP_HOLD) {
                Addf(lines, "op=bump result=error error=bad_ticks range=1..%d", RS_STAIR_MAX_BUMP_HOLD);
                return 1;
            } else {
                RsStair_SetBumpHold(ticks);
            }
        } else {
            lines.push_back(
                "op=bump result=error error=usage usage=\"stairs bump [on|off|default|hold <ticks|default>]\"");
            return 1;
        }
    }
    Addf(lines, "op=bump on=%d source=%s hold=%d hold_source=%s", RsStair_BumpEnabled(),
         RsStair_BumpEnabledOverridden() ? "cvar" : "default", RsStair_GetBumpHold(),
         RsStair_BumpHoldOverridden() ? "cvar" : "default");
    return 0;
}

// `stairs order`: the menu's order (decision 22, #173 F3) - two settings, each an override over a build
// default, the fade's shape. Read when a menu opens, so a change shows on the next box with no reload.
// Both persist in the owner's config, so a run that sets them clears them again (`order default`).
int32_t Order(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        const std::string& what = args[1];
        const std::string value = args.size() >= 3 ? args[2] : "";
        if (what == "default") {
            RsStair_ClearMenuOrder();
        } else if (what == "list" && (value == "nearest" || value == "highest" || value == "lowest")) {
            RsStair_SetMenuList(value == "lowest"    ? RS_STAIR_LIST_LOWEST_FIRST
                                : value == "highest" ? RS_STAIR_LIST_HIGHEST_FIRST
                                                     : RS_STAIR_LIST_NEAREST_FIRST);
        } else if (what == "lead" && (value == "up" || value == "down")) {
            RsStair_SetMenuLead(value == "down" ? RS_STAIR_LEAD_DOWN : RS_STAIR_LEAD_UP);
        } else {
            lines.push_back(
                "op=order result=error error=usage usage=\"stairs order [list nearest|highest|lowest | lead up|down | default]\"");
            return 1;
        }
    }
    // `open_*` is the order the last box was laid out in, which `event=text` named as it opened.
    const int32_t open = RsStair_LatchedMenuOrder();
    Addf(lines, "op=order %s list_source=%s lead_source=%s open_list=%s open_lead=%s", OrderFields().c_str(),
         RsStair_MenuListOverridden() ? "cvar" : "default", RsStair_MenuLeadOverridden() ? "cvar" : "default",
         RsStair_MenuListName(RS_STAIR_MENU_ORDER_LIST(open)), RsStair_MenuLeadName(RS_STAIR_MENU_ORDER_LEAD(open)));
    return 0;
}

// `stairs ordercheck`: the menu's order for staircases of two, three and four storeys, against the
// owner's spec (2026-10-09) written out row by row - not worked out by the rule under test. The default
// must be his; `list highest` with `lead up` must be the menu every staircase had before #173 F3.
// One line per (order, storeys, row), then `op=ordercheck ... result=ok|error`; rc 1 on any mismatch.
struct OrderCase {
    int32_t list;
    int32_t lead;
    int32_t storeys;
    int32_t row;
    const char* want; // the destination rows, top to bottom
};
const OrderCase kOrderCases[] = {
    // The default (nearest first, up first): up ascending, then down descending.
    { RS_STAIR_LIST_NEAREST_FIRST, RS_STAIR_LEAD_UP, 2, 0, "1" },
    { RS_STAIR_LIST_NEAREST_FIRST, RS_STAIR_LEAD_UP, 2, 1, "0" },
    { RS_STAIR_LIST_NEAREST_FIRST, RS_STAIR_LEAD_UP, 3, 0, "1,2" },
    { RS_STAIR_LIST_NEAREST_FIRST, RS_STAIR_LEAD_UP, 3, 1, "2,0" },
    { RS_STAIR_LIST_NEAREST_FIRST, RS_STAIR_LEAD_UP, 3, 2, "1,0" },
    { RS_STAIR_LIST_NEAREST_FIRST, RS_STAIR_LEAD_UP, 4, 0, "1,2,3" },
    { RS_STAIR_LIST_NEAREST_FIRST, RS_STAIR_LEAD_UP, 4, 1, "2,3,0" },
    { RS_STAIR_LIST_NEAREST_FIRST, RS_STAIR_LEAD_UP, 4, 2, "3,1,0" },
    { RS_STAIR_LIST_NEAREST_FIRST, RS_STAIR_LEAD_UP, 4, 3, "2,1,0" },
    // The old lift panel (highest first, up first), as BuildMenus listed it before the settings.
    { RS_STAIR_LIST_HIGHEST_FIRST, RS_STAIR_LEAD_UP, 3, 0, "2,1" },
    { RS_STAIR_LIST_HIGHEST_FIRST, RS_STAIR_LEAD_UP, 3, 1, "2,0" },
    { RS_STAIR_LIST_HIGHEST_FIRST, RS_STAIR_LEAD_UP, 3, 2, "1,0" },
    { RS_STAIR_LIST_HIGHEST_FIRST, RS_STAIR_LEAD_UP, 4, 1, "3,2,0" },
    { RS_STAIR_LIST_HIGHEST_FIRST, RS_STAIR_LEAD_UP, 4, 2, "3,1,0" },
    // The other two settings, one case each that only they would produce.
    { RS_STAIR_LIST_LOWEST_FIRST, RS_STAIR_LEAD_UP, 4, 3, "0,1,2" },
    { RS_STAIR_LIST_NEAREST_FIRST, RS_STAIR_LEAD_DOWN, 4, 2, "1,0,3" },
};

int32_t OrderCheck(std::vector<std::string>& lines) {
    int32_t wrong = 0;
    const int32_t count = static_cast<int32_t>(sizeof(kOrderCases) / sizeof(kOrderCases[0]));
    for (int32_t i = 0; i < count; i++) {
        const OrderCase& c = kOrderCases[i];
        int32_t rows[RS_STAIR_MAX_ROWS];
        const int32_t n = RsStair_DestinationRows(c.storeys, c.row, RS_STAIR_MENU_ORDER(c.list, c.lead), rows);
        std::string got;
        for (int32_t k = 0; k < n; k++) {
            got += (k > 0 ? "," : "") + std::to_string(rows[k]);
        }
        const bool ok = got == c.want;
        wrong += ok ? 0 : 1;
        Addf(lines, "order[%d] list=%s lead=%s storeys=%d row=%d got=%s want=%s %s", i, RsStair_MenuListName(c.list),
             RsStair_MenuLeadName(c.lead), c.storeys, c.row, got.c_str(), c.want, ok ? "ok" : "wrong");
    }
    Addf(lines, "op=ordercheck cases=%d wrong=%d default=%s,%s result=%s", count, wrong,
         RsStair_MenuListName(RS_STAIR_MENU_ORDER_LIST(0)), RsStair_MenuLeadName(RS_STAIR_MENU_ORDER_LEAD(0)),
         wrong == 0 ? "ok" : "error");
    return wrong == 0 ? 0 : 1;
}

// --- `stairs limits` and `stairs measure`: how long a destination name or override may be ----------
//
// The menu's budgets are PIXELS in the game's variable-width font, read from the renderer: an option
// row is 216 - 32 = 184 (the choice indent), a body line 216 (RsActors.cpp, RsText_LabelWouldOverflow
// and RsText_BodyWouldWrap). A registered menu must fit under BOTH floor conventions. These print how
// much room a name has once the menu's own words are counted, so the grid tool's cap can be set from
// a measurement rather than a guess (#173 F3).
constexpr int32_t kLabelPx = 216 - 32;
constexpr int32_t kBodyPx = 216;

// The renderer's width of one line: the least budget LineFitsInPixels accepts it in. -1 past 4096.
int32_t PixelWidth(const std::string& text) {
    int32_t lo = 0;
    int32_t hi = 4096;
    if (!CustomMessage::LineFitsInPixels(text, hi)) {
        return -1;
    }
    while (lo < hi) {
        const int32_t mid = (lo + hi) / 2;
        if (CustomMessage::LineFitsInPixels(text, mid)) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

// How many characters of `sample`, from its start, fit in `px` - a prefix ending in a space does not
// count, since a name is stored trimmed (and the renderer does not charge a line its trailing space).
int32_t CharsThatFit(const std::string& sample, int32_t px) {
    int32_t best = 0;
    for (int32_t n = 1; n <= static_cast<int32_t>(sample.size()); n++) {
        if (!CustomMessage::LineFitsInPixels(sample.substr(0, n), px)) {
            break;
        }
        if (sample[n - 1] != ' ') {
            best = n;
        }
    }
    return best;
}

// The width a menu's own words take in front of a name. Measured with a glyph after them: the
// renderer does not charge a line for a trailing space, so "Down to " alone reads 6 px short.
int32_t PrefixWidth(const std::string& prefix) {
    return PixelWidth(prefix + "x") - PixelWidth("x");
}

// The widest question a two-storey staircase's box takes, from RsStair_QuestionFits - the check the
// fallback itself makes. Grows `lead` + prose + `tail` a character at a time; returns the last width
// that fitted, and its characters of prose in `chars`.
int32_t QuestionLimit(const std::string& lead, const std::string& prose, const std::string& tail, int32_t* chars) {
    int32_t best = 0;
    *chars = 0;
    for (int32_t n = 1; n <= static_cast<int32_t>(prose.size()); n++) {
        if (prose[n - 1] == ' ') {
            continue;
        }
        const std::string q = lead + prose.substr(0, n) + tail;
        if (!RsStair_QuestionFits(q, 0)) {
            break;
        }
        best = PixelWidth(q);
        *chars = n;
    }
    return best;
}

int32_t Limits(std::vector<std::string>& lines) {
    // Typical prose (lower case, spaces) and the widest glyph the font has in common use.
    const std::string prose =
        "the roof terrace of the old watchtower above the great hall where the bells hang in the dark";
    const std::string wide(96, 'W');
    Addf(lines, "op=limits label_px=%d body_px=%d", kLabelPx, kBodyPx);
    for (int32_t convention = 0; convention < RS_FLOOR_CONVENTION_COUNT; convention++) {
        int32_t widest = 0;
        int32_t widestStorey = 0;
        for (int32_t storey = -1; storey <= 9; storey++) {
            const int32_t w =
                PixelWidth(RsFloorText_ExpandUnder("the {floor:" + std::to_string(storey) + "}", convention));
            if (w > widest) {
                widest = w;
                widestStorey = storey;
            }
        }
        const std::string label = RsFloorText_ExpandUnder("the {floor:" + std::to_string(widestStorey) + "}", convention);
        Addf(lines, "convention=%s widest_floor_name_px=%d storey=%d down_label_px=%d question_px=%d name=\"%s\"",
             RsPrefs_FloorConventionName(convention), widest, widestStorey, PixelWidth("Down to " + label),
             PixelWidth("Go down to " + label + "?"), label.c_str());
    }
    // The QUESTION's own limit, asked of the fallback's check (two options, the body above the choice):
    // the widest question that fits, as a whole override and with "Go down to ... ?" around a name.
    int32_t overrideChars = 0;
    int32_t namedChars = 0;
    const int32_t questionPx = QuestionLimit("", prose, "", &overrideChars);
    const int32_t namedPx = QuestionLimit("Go down to ", prose, "?", &namedChars);
    Addf(lines, "question_limit px=%d override_chars=%d named_px=%d name_chars=%d", questionPx, overrideChars,
         namedPx, namedChars);
    // ...and to the pixel: a line of every width from 195 to 225, built from a prose prefix that ends in
    // a letter plus up to three narrow letters (l 3, ` 4, f 5, t 6, e 7), each asked of the same check.
    // `exact_px` is the widest that fits; `monotonic=1` says nothing wider fitted after a narrower one
    // failed, so the edge is one number, not a band.
    {
        const char glyphs[] = { 'l', '`', 'f', 't', 'e' };
        const int32_t glyphPx[] = { 3, 4, 5, 6, 7 };
        int32_t exact = -1;
        int32_t firstFail = -1;
        bool monotonic = true;
        int32_t tried = 0;
        for (int32_t want = 195; want <= 225; want++) {
            std::string line;
            for (int32_t n = static_cast<int32_t>(prose.size()); n >= 1 && line.empty(); n--) {
                if (prose[n - 1] == ' ') {
                    continue;
                }
                const std::string base = prose.substr(0, n);
                const int32_t rest = want - PixelWidth(base);
                if (rest < 0 || rest > 21) {
                    continue;
                }
                // Up to three glyphs summing to `rest` (0 is the prefix alone).
                for (int32_t a = -1; a < 5 && line.empty(); a++) {
                    for (int32_t b = -1; b < 5 && line.empty(); b++) {
                        for (int32_t c = -1; c < 5 && line.empty(); c++) {
                            const int32_t sum = (a >= 0 ? glyphPx[a] : 0) + (b >= 0 ? glyphPx[b] : 0) +
                                                (c >= 0 ? glyphPx[c] : 0);
                            if (sum != rest) {
                                continue;
                            }
                            std::string tail;
                            if (a >= 0) {
                                tail += glyphs[a];
                            }
                            if (b >= 0) {
                                tail += glyphs[b];
                            }
                            if (c >= 0) {
                                tail += glyphs[c];
                            }
                            if (PixelWidth(base + tail) == want) {
                                line = base + tail;
                            }
                        }
                    }
                }
            }
            if (line.empty()) {
                continue;
            }
            tried++;
            if (RsStair_QuestionFits(line, 0)) {
                if (firstFail >= 0) {
                    monotonic = false;
                }
                exact = want;
            } else if (firstFail < 0) {
                firstFail = want;
            }
        }
        Addf(lines, "question_exact exact_px=%d first_fail_px=%d widths_tried=%d monotonic=%d", exact, firstFail, tried,
             monotonic ? 1 : 0);
    }
    // A destination name stands where "the {floor:N}" does, so its room is the budget less the menu's
    // own words around it - the same under both conventions. The widest of each pair counts: "Down to "
    // over "Up to ", "Go down to " and "?" over "Go up to ".
    const int32_t inLabel = kLabelPx - PrefixWidth("Down to ");
    const int32_t inQuestion = questionPx - PrefixWidth("Go down to ") - PixelWidth("?");
    Addf(lines, "name_room label_px=%d question_px=%d prose_chars=%d,%d wide_chars=%d,%d", inLabel, inQuestion,
         CharsThatFit(prose, inLabel), CharsThatFit(prose, inQuestion), CharsThatFit(wide, inLabel),
         CharsThatFit(wide, inQuestion));
    // An override is the whole option, or the whole question on two storeys.
    Addf(lines, "override_room label_px=%d question_px=%d prose_chars=%d,%d wide_chars=%d,%d", kLabelPx, questionPx,
         CharsThatFit(prose, kLabelPx), CharsThatFit(prose, questionPx), CharsThatFit(wide, kLabelPx),
         CharsThatFit(wide, questionPx));
    // The grid tool's cap is 64 characters: what 64 of each sample measure.
    Addf(lines, "cap64 prose_px=%d wide_px=%d", PixelWidth(prose.substr(0, 64)), PixelWidth(wide.substr(0, 64)));
    return 0;
}

int32_t Measure(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    std::string text;
    for (size_t i = 1; i < args.size(); i++) {
        text += (i > 1 ? " " : "") + args[i];
    }
    if (text.empty()) {
        lines.push_back("op=measure result=error error=usage usage=\"stairs measure <text>\"");
        return 1;
    }
    for (int32_t convention = 0; convention < RS_FLOOR_CONVENTION_COUNT; convention++) {
        const int32_t px = PixelWidth(RsFloorText_ExpandUnder(text, convention));
        Addf(lines, "op=measure convention=%s px=%d chars=%d fits_label=%d fits_body=%d",
             RsPrefs_FloorConventionName(convention), px, static_cast<int>(text.size()),
             px >= 0 && px <= kLabelPx ? 1 : 0, px >= 0 && px <= kBodyPx ? 1 : 0);
    }
    return 0;
}

int32_t Actors(std::vector<std::string>& lines) {
    if (gPlayState == nullptr) {
        lines.push_back("op=actors scene=none actors=0");
        return 0;
    }
    const Player* player = GET_PLAYER(gPlayState);
    const Actor* focusActor = player != nullptr ? player->focusActor : nullptr;
    const TargetContext& targetCtx = gPlayState->actorCtx.targetCtx;
    int32_t found = 0;
    for (int32_t cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* actor = gPlayState->actorCtx.actorLists[cat].head; actor != nullptr; actor = actor->next) {
            if (actor->id != ACTOR_RS_STAIRS) {
                continue;
            }
            const int32_t stairId = RS_STAIR_PARAMS_GET_ID(actor->params);
            const int32_t row = RS_STAIR_PARAMS_GET_ROW(actor->params);
            // The walk-into state: `bump=` the count against the hold (`offered=1` once it is reached),
            // `latched=1` from a conversation's end until the push that was running is let go.
            const RsStairs* stairs = reinterpret_cast<const RsStairs*>(actor);
            const RsStairsBump& bump = stairs->bump;
            // #178: the floor under its landing, as Init measured it (RsStairs_MeasureLanding).
            // `floor=1 floor_y= floor_dy=` (the placement's height less that floor's; 0 is exact),
            // `floor=0` nothing under the landing, `floor=-1` not measured (no such staircase).
            char floorText[64];
            if (stairs->landingFloor == RS_STAIRS_FLOOR_FOUND) {
                std::snprintf(floorText, sizeof(floorText), "floor=1 floor_y=%.1f floor_dy=%.1f",
                              stairs->landingFloorY, actor->home.pos.y - stairs->landingFloorY);
            } else {
                std::snprintf(floorText, sizeof(floorText), "floor=%d", stairs->landingFloor);
            }
            // Targeting (#192): `attention=1` while it may be targeted at all (Link on its storey),
            // `focus=1` while Link is locked on to it, `arrow=1` while the attention arrow is over it -
            // what the next Z press locks on to when nothing is locked on yet - and `next=1` while it
            // is the candidate a Z press switches to from the current lock-on. `ydist` is
            // yDistToPlayer, the number the storey gate tests.
            Addf(lines,
                 "actor[%d]=rs_stairs stair=%d row=%d params=0x%04X text=0x%04X registered=%d landing=%d room=%d yaw=%d "
                 "pos=%d,%d,%d bump=%d offered=%d latched=%d attention=%d focus=%d arrow=%d next=%d ydist=%.1f %s",
                 found, stairId, row, static_cast<unsigned>(actor->params) & 0xFFFF,
                 static_cast<unsigned>(actor->textId), RsStair_IsRegistered(stairId),
                 RsStair_GetLanding(stairId, row) != nullptr ? 1 : 0, actor->room, actor->home.rot.y,
                 static_cast<int>(actor->world.pos.x), static_cast<int>(actor->world.pos.y),
                 static_cast<int>(actor->world.pos.z), bump.count, bump.offered, bump.latched,
                 (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) ? 1 : 0, focusActor == actor ? 1 : 0,
                 targetCtx.arrowPointedActor == actor ? 1 : 0, targetCtx.unk_94 == actor ? 1 : 0,
                 actor->yDistToPlayer, floorText);
            found++;
        }
    }
    // `focus=` the actor id Link is locked on to, whatever it is, so a lock on something other than a
    // staircase is visible too.
    char focus[16] = "none";
    if (focusActor != nullptr) {
        std::snprintf(focus, sizeof(focus), "0x%X", static_cast<unsigned>(focusActor->id) & 0xFFFF);
    }
    Addf(lines, "op=actors scene=0x%X actors=%d focus=%s", gPlayState->sceneNum, found, focus);
    return 0;
}

int32_t BadCheck(std::vector<std::string>& lines) {
    const int32_t count = RsStairTable_BadCount();
    int32_t accepted = 0;
    for (int32_t i = 0; i < count; i++) {
        int32_t where = -1;
        const int32_t problem = RsStair_DefProblem(RsStairTable_Bad(i), &where);
        accepted += problem == RS_STAIR_PROBLEM_NONE ? 1 : 0;
        // The kind and the row, never the definition's name - one of these rows exists to have an
        // unhygienic name, and the validator's contract is that it never echoes prose.
        Addf(lines, "bad[%d] problem=%s row=%d", i, RsStair_ProblemName(problem), where);
    }
    Addf(lines, "op=badcheck rows=%d refused=%d accepted=%d result=%s", count, count - accepted, accepted,
         accepted == 0 ? "ok" : "error");
    // The GENERATED rows' merge (#173 F3): each planted table is rows as an export would write them, one
    // mistake each, through the same merge RsStair_RegisterGenerated runs at boot. Last of them is a
    // good table carrying a row twice, as a solo and a stitched scene both would: it must be accepted,
    // so the merge is shown to tell a repeat from a disagreement.
    const int32_t genCount = RsStairTable_BadGenCount();
    int32_t genWrong = 0;
    for (int32_t i = 0; i < genCount; i++) {
        const RsStairBadGen* bad = RsStairTable_BadGen(i);
        int32_t stairId = -1;
        int32_t where = -1;
        const int32_t problem =
            RsStair_GeneratedProblem(bad->rows, bad->rowCount, bad->options, bad->optionCount, &stairId, &where);
        const bool ok = problem == bad->expect;
        genWrong += ok ? 0 : 1;
        Addf(lines, "badgen[%d] problem=%s stair=%d row=%d want=%s %s", i, RsStair_ProblemName(problem), stairId, where,
             RsStair_ProblemName(bad->expect), ok ? "ok" : "wrong");
    }
    Addf(lines, "op=badcheck_gen tables=%d wrong=%d result=%s", genCount, genWrong, genWrong == 0 ? "ok" : "error");
    // Words too long for their row fall back, one option at a time (decision 23): each planted
    // staircase must still be accepted, and drop exactly the words it is built to.
    const int32_t wordsCount = RsStairTable_WordsCount();
    int32_t wordsWrong = 0;
    for (int32_t i = 0; i < wordsCount; i++) {
        const RsStairWordsCase* c = RsStairTable_Words(i);
        int32_t where = -1;
        const int32_t problem = RsStair_DefProblem(c->def, &where);
        const int32_t got = static_cast<int32_t>(RsStair_FallbacksOf(c->def).size());
        const bool ok = problem == RS_STAIR_PROBLEM_NONE && got == c->fallbacks;
        wordsWrong += ok ? 0 : 1;
        Addf(lines, "words[%d] problem=%s fallbacks=%d want=%d %s", i, RsStair_ProblemName(problem), got, c->fallbacks,
             ok ? "ok" : "wrong");
    }
    Addf(lines, "op=badcheck_words cases=%d wrong=%d result=%s", wordsCount, wordsWrong,
         wordsWrong == 0 ? "ok" : "error");
    return (accepted == 0 && genWrong == 0 && wordsWrong == 0) ? 0 : 1;
}

const char* kUsage =
    "usage: stairs list | dump <id> | menu <id> <row> | where | go <id> <row> | status | fade [ticks|default] | "
    "bump [on|off|default|hold <ticks|default>] | order [list nearest|highest|lowest | lead up|down | default] | ordercheck | "
    "limits | measure <text> | actors | badcheck";

} // namespace

int32_t RsStairConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string& sub = args[0];
    if (sub == "list") {
        return List(lines);
    }
    if (sub == "dump") {
        return Dump(args, lines);
    }
    if (sub == "menu") {
        return Menu(args, lines);
    }
    if (sub == "where") {
        return Where(lines);
    }
    if (sub == "go") {
        return Go(args, lines);
    }
    if (sub == "status") {
        return Status(lines);
    }
    if (sub == "fade") {
        return Fade(args, lines);
    }
    if (sub == "bump") {
        return Bump(args, lines);
    }
    if (sub == "order") {
        return Order(args, lines);
    }
    if (sub == "ordercheck") {
        return OrderCheck(lines);
    }
    if (sub == "limits") {
        return Limits(lines);
    }
    if (sub == "measure") {
        return Measure(args, lines);
    }
    if (sub == "actors") {
        return Actors(lines);
    }
    if (sub == "badcheck") {
        return BadCheck(lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink -----------------------------------------------------------------------------
//
// `stairs` collides with nothing in debugconsole.cpp's command list.

namespace {

const ConsoleSink::Command stairsCommand(
    "stairs", RsStairConsole_Run,
    "Staircases - menu-driven storey moves (sturdy-bassoon#147): list | dump <id> | menu <id> <row> | where | "
    "go <id> <row> | status | fade [ticks|default] | bump [on|off|default|hold <ticks|default>] | "
    "order [list nearest|highest|lowest | lead up|down | default] | ordercheck | limits | measure <text> | actors | badcheck. `go` runs the same move a staircase's menu does, without the conversation. The move fades by "
    "default; `fade 0` makes it a hard cut and `fade default` goes back. `bump` is walk-into (#151): pushing into "
    "a staircase opens its menu after the hold. It is off by default; `bump on` switches it on. `order` is the "
    "menu's order (#173): destinations nearest (the default), highest or lowest first, and from a middle storey up or down first.",
    { { "list|dump|menu|where|go|status|fade|bump|order|ordercheck|limits|measure|actors|badcheck", Ship::ArgumentType::TEXT },
      { "staircase id, ticks, or on|off|default|hold", Ship::ArgumentType::TEXT, true },
      { "row or ticks", Ship::ArgumentType::TEXT, true } });

} // namespace
