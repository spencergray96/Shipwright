#include "RsBoatFitConsole.h"

#include <cmath>
#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>

#include "RsBoatFit.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/cvar_prefixes.h"

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
        line += std::string(" ") + RsBoatFit_LookName(look) + "=" +
                RsBoatFit_LookOptionName(look, RsBoatFit_LookOf(kind, look));
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
    std::string options;
    for (int32_t o = 0; o < RsBoatFit_LookOptionCount(look); o++) {
        options += std::string(o > 0 ? "," : "") + RsBoatFit_LookOptionName(look, o);
    }
    Addf(lines, "op=look result=ok kind=%s %s=%s options=%s", args[1].c_str(), RsBoatFit_LookName(look),
         RsBoatFit_LookOptionName(look, RsBoatFit_LookOf(kind, look)), options.c_str());
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
    if (sub == "own") {
        return Own(args, lines);
    }
    if (sub == "fit") {
        return Fit(args, lines);
    }
    return -1;
}
