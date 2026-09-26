#include "StairConsole.h"

#include <cstdio>
#include <string>

#include <ship/debug/Console.h>

#include "StairTable.h"
#include "Stairs.h"
#include "soh/Enhancements/console/ConsoleSink.h"
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

// One row's menu, as the textbox composes it under the live convention. The body and labels come
// from RsStair_Compose*, which read the same screen the renderer is handed.
void MenuLines(int32_t stairId, int32_t row, std::vector<std::string>& lines) {
    const RsDialogueRule* screen = RsStair_Screen(stairId, row);
    Addf(lines, "menu[%d] options=%d body=\"%s\"", row, screen->optionCount,
         RsStair_ComposeBody(stairId, row).c_str());
    for (int32_t i = 0; i < screen->optionCount; i++) {
        Addf(lines, "menu[%d.%d] to_row=%d label=\"%s\"", row, i, RsStair_MenuDestination(stairId, row, i),
             RsStair_ComposeLabel(stairId, row, i).c_str());
    }
}

int32_t List(std::vector<std::string>& lines) {
    int32_t ids[RS_STAIR_MAX];
    const int32_t count = RsStair_ListIds(ids, RS_STAIR_MAX);
    Addf(lines, "op=list stairs=%d", count);
    for (int32_t i = 0; i < count; i++) {
        const RsStairDef* def = RsStair_GetDef(ids[i]);
        Addf(lines, "stair[%d] name=%s scene=0x%X rows=%d storeys=%s tier=%s", def->id, def->name, def->sceneId,
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
    Addf(lines, "op=dump stair=%d name=%s scene=0x%X here=%d rows=%d land_forward=%d convention=%s", def->id,
         def->name, def->sceneId, InPlay() && gPlayState->sceneNum == def->sceneId ? 1 : 0, def->landingCount,
         def->landForward, RsPrefs_FloorConventionName(convention));
    // Each storey's landing is computed from its placement, so it exists only while that placement
    // is loaded: `placed=0` is a storey in another room, or one nobody placed.
    for (int32_t row = 0; row < def->landingCount; row++) {
        const RsStairLanding& l = def->landings[row];
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        int16_t yaw = 0;
        const int32_t placed = InPlay() ? RsStair_PlacedLanding(stairId, row, &x, &y, &z, &yaw) : 0;
        if (placed) {
            Addf(lines, "row[%d] storey=%d room=%d placed=1 landing=%.1f,%.1f,%.1f yaw=%d label=\"%s\"", row, l.storey,
                 l.room, x, y, z, yaw, RsFloorText_Label(convention, l.storey, false).c_str());
        } else {
            Addf(lines, "row[%d] storey=%d room=%d placed=0 label=\"%s\"", row, l.storey, l.room,
                 RsFloorText_Label(convention, l.storey, false).c_str());
        }
    }
    for (int32_t row = 0; row < def->landingCount; row++) {
        MenuLines(stairId, row, lines);
    }
    return 0;
}

int32_t Menu(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    int32_t stairId = 0;
    int32_t row = 0;
    if (!ParseStair(args, 1, &stairId, lines) || !ParseRow(args, 2, stairId, &row, lines)) {
        return 1;
    }
    Addf(lines, "op=menu stair=%d row=%d convention=%s", stairId, row,
         RsPrefs_FloorConventionName(RsPrefs_GetFloorConvention()));
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
    int32_t ids[RS_STAIR_MAX];
    const int32_t count = RsStair_ListIds(ids, RS_STAIR_MAX);
    int32_t here = 0;
    for (int32_t i = 0; i < count; i++) {
        here += RsStair_GetDef(ids[i])->sceneId == gPlayState->sceneNum ? 1 : 0;
    }
    Addf(lines, "op=where scene=0x%X room=%d pos=%.1f,%.1f,%.1f yaw=%d floor_y=%.1f ground=%d stairs_here=%d",
         gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num, player->actor.world.pos.x, player->actor.world.pos.y,
         player->actor.world.pos.z, player->actor.shape.rot.y, player->actor.floorHeight,
         (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 1 : 0, here);
    for (int32_t i = 0; i < count; i++) {
        const RsStairDef* def = RsStair_GetDef(ids[i]);
        if (def->sceneId != gPlayState->sceneNum) {
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

int32_t Actors(std::vector<std::string>& lines) {
    if (gPlayState == nullptr) {
        lines.push_back("op=actors scene=none actors=0");
        return 0;
    }
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
            const RsStairsBump& bump = reinterpret_cast<const RsStairs*>(actor)->bump;
            Addf(lines,
                 "actor[%d]=rs_stairs stair=%d row=%d params=0x%04X rsvd=%d registered=%d landing=%d room=%d yaw=%d "
                 "pos=%d,%d,%d bump=%d offered=%d latched=%d",
                 found, stairId, row, static_cast<unsigned>(actor->params) & 0xFFFF,
                 RS_STAIR_PARAMS_GET_RSVD(actor->params), RsStair_IsRegistered(stairId),
                 RsStair_GetLanding(stairId, row) != nullptr ? 1 : 0, actor->room, actor->home.rot.y,
                 static_cast<int>(actor->world.pos.x), static_cast<int>(actor->world.pos.y),
                 static_cast<int>(actor->world.pos.z), bump.count, bump.offered, bump.latched);
            found++;
        }
    }
    Addf(lines, "op=actors scene=0x%X actors=%d", gPlayState->sceneNum, found);
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
    return accepted == 0 ? 0 : 1;
}

const char* kUsage =
    "usage: stairs list | dump <id> | menu <id> <row> | where | go <id> <row> | status | fade [ticks|default] | "
    "bump [on|off|default|hold <ticks|default>] | actors | badcheck";

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
    "go <id> <row> | status | fade [ticks|default] | bump [on|off|default|hold <ticks|default>] | actors | "
    "badcheck. `go` runs the same move a staircase's menu does, without the conversation. The move fades by "
    "default; `fade 0` makes it a hard cut and `fade default` goes back. `bump` is walk-into (#151): pushing into "
    "a staircase opens its menu after the hold; `bump off` leaves target-and-talk as the only way in.",
    { { "list|dump|menu|where|go|status|fade|bump|actors|badcheck", Ship::ArgumentType::TEXT },
      { "staircase id, ticks, or on|off|default|hold", Ship::ArgumentType::TEXT, true },
      { "row or ticks", Ship::ArgumentType::TEXT, true } });

} // namespace
