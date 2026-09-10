#include "RegionConsole.h"

#include <memory>
#include <ship/Context.h>
#include <ship/debug/Console.h>

#include "FloorText.h"
#include "RegionOverlay.h"
#include "RsPrefs.h"
#include "soh/ShipInit.hpp"

namespace {

// One line describing the live setting. Every mutating subcommand ends with it, the way the quest
// console ends every write with its Describe line.
std::string Describe() {
    const int32_t convention = RsPrefs_GetFloorConvention();
    return "convention=" + std::string(RsPrefs_FloorConventionName(convention)) +
           " value=" + std::to_string(convention) +
           " source=" + (RsPrefs_FloorConventionIsLoaded() != 0 ? "file" : "default");
}

// The console-layer pre-validation, the P0 rule applied to a new gate: a bad argument is refused
// HERE, so RsPrefs_SetFloorConvention's own log-and-assert stays unreachable from a console. An
// assert on a path the agent loop walks hangs it with no window to read.
bool ParseConvention(const std::string& word, int32_t* convention) {
    for (int32_t i = 0; i < RS_FLOOR_CONVENTION_COUNT; i++) {
        if (word == RsPrefs_FloorConventionName(i)) {
            *convention = i;
            return true;
        }
    }
    return false;
}

// `region expand <text...>`: the grammar probe. The console tokenizer is a naive split on " ", so
// the remaining arguments are re-joined with single spaces - the same treatment `quest parse` gives
// its input, and for the same reason.
//
// It NEVER echoes its input on the error path. On the success path it does print the EXPANSIONS,
// and that is exactly why the '"' check below exists.
int32_t Expand(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() < 2) {
        lines.push_back("error=expand needs text");
        return 1;
    }
    std::string text;
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) {
            text += " ";
        }
        text += args[i];
    }
    // A '"' is refused, and this is the ONE handler in the tree that needs the check. Every other
    // rs/ console line is built from DEFINITION strings, which the registration gates already
    // refuse a quote in - so the `key="value"` contract every acceptance grep depends on holds for
    // free. This one takes ARBITRARY TYPED TEXT and prints a transformation of it, so a quote would
    // reach the sink and emit an unbalanced `rs_region expand[uk]="…` marker: a line that parses
    // wrong rather than one that fails. Refused rather than escaped, so the probe's rulebook is the
    // same one the prose gates apply. ('%' is handled the other way, by doubling at both sinks,
    // because a '%' is a vsnprintf hazard rather than a field-delimiter one.)
    const size_t quote = text.find('"');
    if (quote != std::string::npos) {
        lines.push_back("op=expand result=error error=quote pos=" + std::to_string(quote));
        return 1;
    }
    // Validate ONCE, before expanding under anything: a malformed token is malformed for every
    // convention, and reporting it twice would suggest the two disagreed.
    const RsFloorTokenResult result = RsFloorText_Validate(text.c_str());
    if (result.error != RS_FLOOR_TOKEN_OK) {
        lines.push_back("op=expand result=error error=" + std::string(RsFloorText_ErrorName(result.error)) +
                        " pos=" + std::to_string(result.pos));
        return 1;
    }
    // EVERY convention, on its own line. This is the command's whole point: one invocation shows
    // both readings of the same sentence, which is what makes "it reads wrong to half the players"
    // a thing a run can assert rather than a thing a human has to notice.
    lines.push_back("op=expand result=ok conventions=" + std::to_string(RS_FLOOR_CONVENTION_COUNT));
    for (int32_t convention = 0; convention < RS_FLOOR_CONVENTION_COUNT; convention++) {
        std::string expanded;
        RsFloorText_Expand(text.c_str(), convention, &expanded);
        lines.push_back("expand[" + std::string(RsPrefs_FloorConventionName(convention)) + "]=\"" + expanded + "\"");
    }
    return 0;
}

// `region overlay [on|off]`: the switch for the on-screen overlay (RegionOverlay.h). Living on this
// command rather than its own is what puts it on both sinks at once. With no argument it only
// reports. `drawn=` is what the LAST ImGui frame rendered - the only console-readable evidence that
// the overlay drew anything, since a marker cannot see a pixel.
int32_t Overlay(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2) {
        if (args[1] == "on") {
            RsRegionOverlay_SetEnabled(true);
        } else if (args[1] == "off") {
            RsRegionOverlay_SetEnabled(false);
        } else {
            lines.push_back("error=overlay takes on|off");
            return 1;
        }
    }
    const RsRegionOverlayState state = RsRegionOverlay_Get();
    lines.push_back("op=overlay enabled=" + std::to_string(state.enabled ? 1 : 0) + " drawn=" +
                    std::to_string(state.drawn) + " drawn_convention=" +
                    RsPrefs_FloorConventionName(state.convention));
    return 0;
}

const char* kUsage = "usage: region get | set <uk|us> | toggle | expand <text...> | overlay [on|off]";

} // namespace

int32_t RsRegionConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string& sub = args[0];

    if (sub == "get") {
        lines.push_back("op=get " + Describe());
        return 0;
    }
    if (sub == "set") {
        int32_t convention = 0;
        if (args.size() < 2 || !ParseConvention(args[1], &convention)) {
            lines.push_back("error=set takes uk or us");
            return 1;
        }
        RsPrefs_SetFloorConvention(convention);
        lines.push_back("op=set " + Describe());
        return 0;
    }
    if (sub == "toggle") {
        // Two conventions today, so "the other one" is arithmetic. It stays correct at three: this
        // is a debug convenience, and cycling is the only sensible generalisation of a toggle.
        const int32_t next = (RsPrefs_GetFloorConvention() + 1) % RS_FLOOR_CONVENTION_COUNT;
        RsPrefs_SetFloorConvention(next);
        lines.push_back("op=toggle " + Describe());
        return 0;
    }
    if (sub == "expand") {
        return Expand(args, lines);
    }
    if (sub == "overlay") {
        return Overlay(args, lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `region` console command -----------------------------------------------

namespace {

int32_t RegionCommandHandler(std::shared_ptr<Ship::Console> console, const std::vector<std::string>& args,
                             std::string* output) {
    std::vector<std::string> sub(args.begin() + 1, args.end());
    std::vector<std::string> lines;
    const int32_t rc = RsRegionConsole_Run(sub, lines);
    if (output) {
        for (size_t i = 0; i < lines.size(); i++) {
            if (i > 0) {
                *output += "\n";
            }
            // ConsoleWindow hands the output to vsnprintf as the FORMAT string. Unlike the other
            // rs/ commands, `expand` takes ARBITRARY TYPED TEXT and prints a transformation of it,
            // so a '%' really can reach here - this doubling is load-bearing, not belt and braces.
            for (char c : lines[i]) {
                *output += c;
                if (c == '%') {
                    *output += '%';
                }
            }
        }
    }
    return rc;
}

// ShipInit "*" functions re-run on preset apply and config load; AddCommand only warns on a
// duplicate, but the guard keeps the log clean. `region` collides with nothing in
// debugger/debugconsole.cpp's CMD_REGISTER list.
void RegisterRegionConsole() {
    auto console = Ship::Context::GetRawInstance()->GetConsole();
    if (console->HasCommand("region")) {
        return;
    }
    console->AddCommand("region",
                        { RegionCommandHandler,
                          "The save file's floor convention (sturdy-bassoon#94): get | set <uk|us> | toggle | "
                          "expand <text...> | overlay [on|off]. UK calls the storey at ground level the ground "
                          "floor, US calls it the first floor; every piece of rs/ prose writes {floor:N} and is "
                          "expanded at read time. expand prints a sentence under BOTH conventions, which is the "
                          "fastest way to see what the other half of your players will read.",
                          { { "get|set|toggle|expand|overlay", Ship::ArgumentType::TEXT },
                            { "argument", Ship::ArgumentType::TEXT, true } } });
}

RegisterShipInitFunc regionConsoleInitFunc(RegisterRegionConsole);

} // namespace
