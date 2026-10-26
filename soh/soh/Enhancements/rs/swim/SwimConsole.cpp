#include "SwimConsole.h"

#include <cstdlib>

#include <ship/debug/Console.h>

#include "SwimStamina.h"
#include "soh/Enhancements/console/ConsoleSink.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

std::string Describe() {
    const RsSwimStatus s = RsSwim_GetStatus();
    char buf[400];
    std::snprintf(buf, sizeof(buf),
                  "enabled=%d phase=%s budget=%d max=%d seconds=%d length=%d length_source=%s swimming=%d "
                  "grounded=%d hold=%s clock=%d refuge=%s drowns=%d last=%s",
                  s.enabled ? 1 : 0, s.phase, s.budget, s.max, RsSwim_ClockSeconds(), RsSwim_GetLengthSeconds(),
                  RsSwim_LengthOverridden() ? "cvar" : "default", s.swimming ? 1 : 0, s.grounded ? 1 : 0,
                  s.holdReason, s.clockOwned ? 1 : 0, !s.refuge ? "none" : (s.refugeOnActor ? "actor" : "world"),
                  s.drowns, s.lastRespawn);
    return buf;
}

// A whole number and nothing else after it.
bool ParseInt(const std::string& word, int32_t* out) {
    if (word.empty()) {
        return false;
    }
    char* end = nullptr;
    const long v = std::strtol(word.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

const char* kUsage =
    "usage: swim status | on | off | length <s>|default | budget <ticks> | drown | refuge here|clear | points";

} // namespace

int32_t RsSwimConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    const std::string& sub = args[0];

    if (sub == "status") {
        lines.push_back("op=status " + Describe());
        return 0;
    }
    if (sub == "on" || sub == "off") {
        RsSwim_SetEnabled(sub == "on" ? 1 : 0);
        lines.push_back("op=" + sub + " " + Describe());
        return 0;
    }
    if (sub == "length") {
        int32_t seconds = 0;
        if (args.size() >= 2 && args[1] == "default") {
            RsSwim_ClearLengthSeconds();
        } else if (args.size() >= 2 && ParseInt(args[1], &seconds) && seconds >= 1 &&
                   seconds <= RS_SWIM_STAMINA_MAX_SECONDS) {
            RsSwim_SetLengthSeconds(seconds);
        } else {
            Addf(lines, "op=length result=error error=bad_length max=%d", RS_SWIM_STAMINA_MAX_SECONDS);
            return 1;
        }
        lines.push_back("op=length result=ok " + Describe());
        return 0;
    }
    if (sub == "budget") {
        int32_t ticks = 0;
        if (args.size() < 2 || !ParseInt(args[1], &ticks) || ticks < 1) {
            lines.push_back("op=budget result=error error=bad_budget");
            return 1;
        }
        RsSwim_SetBudgetTicks(ticks);
        lines.push_back("op=budget result=ok " + Describe());
        return 0;
    }
    if (sub == "drown") {
        if (RsSwim_DrownNow() == 0) {
            lines.push_back("op=drown result=error error=" + std::string(RsSwim_Enabled() ? "busy" : "off") + " " +
                            Describe());
            return 1;
        }
        lines.push_back("op=drown result=ok " + Describe());
        return 0;
    }
    if (sub == "refuge") {
        if (args.size() >= 2 && args[1] == "clear") {
            RsSwim_ClearRefuge();
        } else if (args.size() >= 2 && args[1] == "here") {
            if (gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr) {
                lines.push_back("op=refuge result=error error=no_play");
                return 1;
            }
            const Player* player = GET_PLAYER(gPlayState);
            RsSwim_SetRefuge(nullptr, player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                             player->actor.shape.rot.y);
        } else {
            lines.push_back("op=refuge result=error error=bad_refuge");
            return 1;
        }
        lines.push_back("op=refuge result=ok " + Describe());
        return 0;
    }
    if (sub == "points") {
        for (int32_t i = 0; i < RsSwim_PointCount(); i++) {
            const RsSwimPointStatus p = RsSwim_GetPoint(i);
            Addf(lines, "op=points name=%s scene=0x%X rs=%d,%d unlocked=%d in_scene=%d placed=%d x=%.0f y=%.0f z=%.0f wet=%d",
                 p.name, p.sceneId, p.rsX, p.rsY, p.unlocked ? 1 : 0, p.inScene ? 1 : 0, p.placed ? 1 : 0, p.x, p.y,
                 p.z, p.wet ? 1 : 0);
        }
        return 0;
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `swim` console command --------------------------------------------------
//
// `swim` collides with nothing in debugger/debugconsole.cpp's CMD_REGISTER list.

namespace {

const ConsoleSink::Command swimCommand(
    "swim", RsSwimConsole_Run,
    "Swim stamina (sturdy-bassoon#257): status | on | off | length <s>|default | budget <ticks> | drown | "
    "refuge here|clear | points. With it on, Link swims for a budget shown on the hazard clock, refilled the "
    "moment he stands on anything, and drowns when it runs out: back to his boat, the nearest drown point "
    "(Port Sarim), or a void-out.",
    { { "status|on|off|length|budget|drown|refuge|points", Ship::ArgumentType::TEXT },
      { "argument", Ship::ArgumentType::TEXT, true } });

} // namespace
