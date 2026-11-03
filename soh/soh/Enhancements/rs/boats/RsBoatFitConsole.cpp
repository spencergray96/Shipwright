#include "RsBoatFitConsole.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>

#include "RsBoatFit.h"
#include "RsBoatSail.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/cvar_prefixes.h"

#include <ship/Context.h>
#include <ship/window/FileDropMgr.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

namespace {

using ConsoleSink::Addf;

bool ParseInt(const std::string& s, long* out) {
    char* end = nullptr;
    *out = strtol(s.c_str(), &end, 0);
    return end != s.c_str() && *end == '\0';
}

int32_t BadValue(std::vector<std::string>& lines, const std::string& op) {
    Addf(lines, "op=%s result=error error=bad_value", op.c_str());
    return 1;
}

int32_t Usage(std::vector<std::string>& lines) {
    lines.push_back("op=usage result=error usage=\"boat " RS_BOAT_FIT_GRAMMAR "\"");
    return 1;
}

void FitoutLine(int32_t kind, std::vector<std::string>& lines) {
    std::string line = std::string("fitout kind=") + RsBoat_KindName(kind) +
                       " owned=" + std::to_string(RsBoatFit_Owned(kind));
    for (int32_t slot = 0; slot < RS_BOAT_SLOT_COUNT; slot++) {
        if (!RsBoatFit_KindHasSlot(kind, slot)) {
            continue;
        }
        const int32_t tier = RsBoatFit_TierOf(kind, slot);
        const RsBoatTier* row = RsBoatFit_Tier(kind, slot, tier);
        line += std::string(" ") + RsBoatFit_SlotName(slot) + "=" + std::to_string(tier) + "/" +
                (row != nullptr ? row->name : "-");
    }
    for (int32_t look = 0; look < RS_BOAT_LOOK_COUNT; look++) {
        if (!RsBoatFit_KindHasLook(kind, look)) {
            continue;
        }
        const int32_t option = RsBoatFit_LookOf(kind, look);
        line += std::string(" ") + RsBoatFit_LookName(look) + "=" + RsBoatFit_LookOptionName(look, option);
        if (RsBoatFit_LookOptionKind(look, option) == RS_BOAT_LOOK_OPTION_OWN_COLOUR) {
            uint8_t rgb[3];
            char hex[8];
            RsBoatFit_LookRgb(kind, look, rgb);
            std::snprintf(hex, sizeof(hex), "%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
            line += std::string(" ") + RsBoatFit_LookName(look) + "_rgb=" + hex;
        }
    }
    RsBoatFitStats stats;
    RsBoatFit_Stats(kind, &stats);
    line += " top=" + std::to_string((int)lroundf(stats.topSpeed * 100.0f)) +
            " accel=" + std::to_string((int)lroundf(stats.accel * 100.0f));
    lines.push_back(line);
}

int32_t Fitout(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() >= 2 && args[1] == "hud") {
        if (args.size() == 3 && (args[2] == "on" || args[2] == "off")) {
            CVarSetInteger(CVAR_RS_BOAT_FIT_HUD, args[2] == "on" ? 1 : 0);
            CVarSave();
        } else if (args.size() != 2) {
            return Usage(lines);
        }
        Addf(lines, "op=fitout result=ok hud=%d", CVarGetInteger(CVAR_RS_BOAT_FIT_HUD, 1) ? 1 : 0);
        return 0;
    }
    if (args.size() == 2 && args[1] == "reset") {
        RsBoatFit_Reset();
        Addf(lines, "op=fitout result=ok reset=1");
        return 0;
    }
    if (args.size() > 2) {
        return Usage(lines);
    }
    if (args.size() == 2) {
        const int32_t kind = RsBoat_KindByName(args[1].c_str());
        if (kind < 0) {
            return BadValue(lines, "fitout");
        }
        FitoutLine(kind, lines);
    } else {
        for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
            FitoutLine(kind, lines);
        }
    }
    Addf(lines, "op=fitout result=ok");
    return 0;
}

int32_t Upgrades(std::vector<std::string>& lines) {
    for (int32_t id = 0; id < RS_BOAT_UPGRADE_COUNT; id++) {
        const RsBoatUpgrade* u = RsBoatFit_Upgrade(id);
        int32_t failIndex = -1;
        const int32_t check = RsBoatFit_Check(id, &failIndex);
        std::string fits;
        for (int32_t f = 0; f < u->fitCount; f++) {
            const RsBoatUpgradeFit& fit = u->fits[f];
            fits += std::string(f > 0 ? "," : "") + RsBoat_KindName(fit.kind) + "." +
                    (fit.slot == RS_BOAT_FIT_OWN ? "own" : RsBoatFit_SlotName(fit.slot)) + ":" +
                    std::to_string(fit.slot == RS_BOAT_FIT_OWN ? 1 : fit.tier);
        }
        std::string costs;
        for (int32_t c = 0; c < u->costCount; c++) {
            costs += std::string(c > 0 ? "," : "") + "rupees:" + std::to_string(u->costs[c].amount);
        }
        Addf(lines, "upgrade name=%s unlock_flag=%d unlocked=%d check=%s fail_index=%d fits=%s costs=%s placeholder=%d",
             u->name, (int)u->unlockFlag, Flags_GetWorldFlag(u->unlockFlag) ? 1 : 0, RsBoatFit_ResultName(check),
             (int)failIndex, fits.c_str(), costs.empty() ? "none" : costs.c_str(), (int)u->placeholder);
    }
    Addf(lines, "op=upgrades result=ok count=%d", (int)RS_BOAT_UPGRADE_COUNT);
    return 0;
}

int32_t BuildOrGrant(const std::vector<std::string>& args, std::vector<std::string>& lines, bool build) {
    const char* op = build ? "build" : "grant";
    if (args.size() != 2) {
        return Usage(lines);
    }
    const int32_t id = RsBoatFit_UpgradeByName(args[1].c_str());
    if (id < 0) {
        return BadValue(lines, op);
    }
    int32_t failIndex = -1;
    const int32_t result = build ? RsBoatFit_Build(id, &failIndex) : RsBoatFit_Grant(id);
    if (result != RS_BOAT_FIT_OK) {
        Addf(lines, "op=%s result=error error=%s upgrade=%s fail_index=%d", op, RsBoatFit_ResultName(result),
             args[1].c_str(), (int)failIndex);
        return 1;
    }
    Addf(lines, "op=%s result=ok upgrade=%s rupees=%d", op, args[1].c_str(), (int)RsBoatFit_RupeesAfterPending());
    return 0;
}

int32_t Look(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() < 2 || args.size() > 4) {
        return Usage(lines);
    }
    const int32_t kind = RsBoat_KindByName(args[1].c_str());
    const int32_t look = args.size() >= 3 ? RsBoatFit_LookByName(args[2].c_str()) : 0;
    if (kind < 0 || look < 0) {
        return BadValue(lines, "look");
    }
    if (args.size() == 4) {
        const int32_t option = RsBoatFit_LookOptionByName(look, args[3].c_str());
        if (option < 0) {
            return BadValue(lines, "look");
        }
        const int32_t result = RsBoatFit_SetLook(kind, look, option);
        if (result != RS_BOAT_FIT_OK) {
            Addf(lines, "op=look result=error error=%s kind=%s", RsBoatFit_ResultName(result), args[1].c_str());
            return 1;
        }
    }
    if (!RsBoatFit_KindHasLook(kind, look)) {
        return BadValue(lines, "look");
    }
    std::string options;
    for (int32_t o = 0; o < RsBoatFit_LookOptionCount(look); o++) {
        options += std::string(o > 0 ? "," : "") + RsBoatFit_LookOptionName(look, o);
    }
    Addf(lines, "op=look result=ok kind=%s %s=%s options=%s", args[1].c_str(), RsBoatFit_LookName(look),
         RsBoatFit_LookOptionName(look, RsBoatFit_LookOf(kind, look)), options.c_str());
    return 0;
}

int32_t Colour(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() < 3 || args.size() > 4) {
        return Usage(lines);
    }
    const int32_t kind = RsBoat_KindByName(args[1].c_str());
    const int32_t look = RsBoatFit_LookByName(args[2].c_str());
    const int32_t own = RsBoatFit_LookOwnColourOption(look);
    if (kind < 0 || !RsBoatFit_KindHasLook(kind, look) || own < 0) {
        return BadValue(lines, "colour");
    }
    uint8_t rgb[3];
    if (args.size() == 4) {
        if (!RsBoatFit_ParseRgb(args[3].c_str(), rgb)) {
            return BadValue(lines, "colour");
        }
        int32_t result = RsBoatFit_SetLookRgb(kind, look, rgb);
        if (result == RS_BOAT_FIT_OK) {
            result = RsBoatFit_SetLook(kind, look, own);
        }
        if (result != RS_BOAT_FIT_OK) {
            Addf(lines, "op=colour result=error error=%s kind=%s", RsBoatFit_ResultName(result), args[1].c_str());
            return 1;
        }
    }
    RsBoatFit_LookRgb(kind, look, rgb);
    Addf(lines, "op=colour result=ok kind=%s %s=%s rgb=%02x%02x%02x", args[1].c_str(), RsBoatFit_LookName(look),
         RsBoatFit_LookOptionName(look, RsBoatFit_LookOf(kind, look)), rgb[0], rgb[1], rgb[2]);
    return 0;
}

int32_t Own(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    long owned = 0;
    if (args.size() != 3) {
        return Usage(lines);
    }
    const int32_t kind = RsBoat_KindByName(args[1].c_str());
    if (kind < 0 || !ParseInt(args[2], &owned) || (owned != 0 && owned != 1)) {
        return BadValue(lines, "own");
    }
    RsBoatFit_SetOwned(kind, (int32_t)owned);
    Addf(lines, "op=own result=ok kind=%s owned=%d", args[1].c_str(), RsBoatFit_Owned(kind));
    return 0;
}

int32_t Fit(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    long tier = 0;
    if (args.size() != 4) {
        return Usage(lines);
    }
    const int32_t kind = RsBoat_KindByName(args[1].c_str());
    const int32_t slot = RsBoatFit_SlotByName(args[2].c_str());
    if (kind < 0 || slot < 0 || !ParseInt(args[3], &tier) ||
        RsBoatFit_SetTier(kind, slot, (int32_t)tier) != RS_BOAT_FIT_OK) {
        return BadValue(lines, "fit");
    }
    Addf(lines, "op=fit result=ok kind=%s slot=%s tier=%d", args[1].c_str(), args[2].c_str(),
         RsBoatFit_TierOf(kind, slot));
    return 0;
}

// The rest of the line from word `from`, spaces kept: a path may have them.
std::string Rest(const std::vector<std::string>& args, size_t from) {
    std::string out;
    for (size_t i = from; i < args.size(); i++) {
        out += (i > from ? " " : "") + args[i];
    }
    return out;
}

void SailLine(int32_t sail, std::vector<std::string>& lines) {
    const int32_t kind = RsBoatSail_Kind(sail);
    const int32_t look = RsBoatSail_Look(sail);
    const int32_t drawn = RsBoatSail_DrawnPixels(sail) != nullptr ? 1 : 0;
    Addf(lines, "sail name=%s kind=%s look=%s option=%s drawn=%d file=%d accepted=%d armed=%d refusal=\"%s\"",
         RsBoatSail_Name(sail), RsBoat_KindName(kind), RsBoatFit_LookName(look),
         RsBoatFit_LookOptionName(look, RsBoatFit_LookOf(kind, look)), drawn, RsBoatSail_Present(sail),
         RsBoatSail_Accepted(sail), RsBoatSail_Armed() == sail ? 1 : 0, RsBoatSail_Refusal(sail));
}

int32_t Sails(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string op = args.size() >= 2 ? args[1] : "";
    if (op == "reload" && args.size() == 2) {
        RsBoatSail_Reload();
    } else if (op == "import" && args.size() >= 4) {
        const int32_t sail = RsBoatSail_ByName(args[2].c_str());
        if (sail < 0) {
            return BadValue(lines, "sails");
        }
        char why[256];
        if (!RsBoatSail_Import(sail, Rest(args, 3).c_str(), why, sizeof(why))) {
            Addf(lines, "op=sails result=error error=refused sail=%s why=\"%s\"", args[2].c_str(), why);
            return 1;
        }
    } else if (op == "arm" && args.size() == 3) {
        const int32_t sail = args[2] == "off" ? -1 : RsBoatSail_ByName(args[2].c_str());
        if (sail < 0 && args[2] != "off") {
            return BadValue(lines, "sails");
        }
        RsBoatSail_Arm(sail);
    } else if (op == "drop" && args.size() >= 3) {
        // Dropping the file on the game window, for the loop: through the drop manager, so every handler runs in its
        // real order (the armed sail's first, then SoH's config and spoiler handlers, then the "unsupported" toast).
        const int32_t armedBefore = RsBoatSail_Armed();
        std::string path = Rest(args, 2);
        Ship::Context::GetRawInstance()->GetFileDropMgr()->SetDroppedFile(path.data());
        Addf(lines, "op=sails result=ok drop_handled=%d armed_before=%s", armedBefore >= 0 ? 1 : 0,
             armedBefore >= 0 ? RsBoatSail_Name(armedBefore) : "none");
        return 0;
    } else if (op == "window" && args.size() <= 3) {
        if (args.size() == 3 && (args[2] == "on" || args[2] == "off")) {
            CVarSetInteger(CVAR_WINDOW("RsBoatSails"), args[2] == "on" ? 1 : 0);
            auto window = Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Boat Sails");
            if (window != nullptr) {
                args[2] == "on" ? window->Show() : window->Hide();
            }
        } else if (args.size() != 2) {
            return Usage(lines);
        }
        Addf(lines, "op=sails result=ok window=%d", CVarGetInteger(CVAR_WINDOW("RsBoatSails"), 0) ? 1 : 0);
        return 0;
    } else if (args.size() != 1) {
        return Usage(lines);
    }
    for (int32_t sail = 0; sail < RS_BOAT_SAIL_COUNT; sail++) {
        SailLine(sail, lines);
    }
    Addf(lines, "op=sails result=ok folder=\"%s\" armed=%s", RsBoatSail_Folder(),
         RsBoatSail_Armed() >= 0 ? RsBoatSail_Name(RsBoatSail_Armed()) : "none");
    return 0;
}

} // namespace

int32_t RsBoatFitConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string& sub = args[0];
    if (sub == "fitout") {
        return Fitout(args, lines);
    }
    if (sub == "upgrades") {
        return args.size() == 1 ? Upgrades(lines) : Usage(lines);
    }
    if (sub == "build" || sub == "grant") {
        return BuildOrGrant(args, lines, sub == "build");
    }
    if (sub == "look") {
        return Look(args, lines);
    }
    if (sub == "colour") {
        return Colour(args, lines);
    }
    if (sub == "own") {
        return Own(args, lines);
    }
    if (sub == "fit") {
        return Fit(args, lines);
    }
    if (sub == "sails") {
        return Sails(args, lines);
    }
    return -1;
}
