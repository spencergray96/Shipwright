#include "RsFireConsole.h"

#include <cmath>
#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/debug/Console.h>

#include "RsFire.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/lights/LightsBudget.h"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"

extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

const char* kUsage =
    "op=usage result=error usage=\"rsfire list | pool | distance [<n>|default] | glow [on|off|default]\"";

// The live fires, in the prop list's order.
template <typename Fn> void ForEachFire(Fn fn) {
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_RS_FIRE) {
            fn(a, reinterpret_cast<const RsFire*>(a));
        }
    }
}

// Writes the settings into every live fire, culled ones included: a culled fire never updates, so it
// would otherwise keep the old distance (and never wake to read the new one) and its old halo.
void ApplyToAll() {
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_RS_FIRE) {
            RsFire_ApplySettings(reinterpret_cast<RsFire*>(a));
        }
    }
}

// A fire's light is showing its halo: it holds a node, of the glow type.
bool Glows(const RsFire* fire) {
    return fire->lightNode != nullptr && fire->lightInfo.type == LIGHT_POINT_GLOW;
}

int32_t List(std::vector<std::string>& lines) {
    int32_t count = 0;
    int32_t lit = 0;
    int32_t lights = 0;
    int32_t awake = 0;
    int32_t drawn = 0;

    ForEachFire([&](const Actor* a, const RsFire* fire) {
        // awake: inside its cull volume, so it updates and registers its collider. drawn: the last frame
        // drew it (an unlit fire's draw draws nothing, but still counts).
        const int32_t isAwake = (a->flags & ACTOR_FLAG_INSIDE_CULLING_VOLUME) != 0 ? 1 : 0;
        Addf(lines,
             "fire idx=%d x=%d y=%d z=%d size=%d hit=%d lit=%d by=%s flag=%d want_light=%d light=%d params=0x%04X "
             "awake=%d drawn=%d forward=%d glow=%d",
             count, (int)lroundf(a->world.pos.x), (int)lroundf(a->world.pos.y), (int)lroundf(a->world.pos.z),
             (int)lroundf(fire->size * 100.0f), (int)lroundf(fire->hitSize * 100.0f), fire->lit,
             RsFire_LitByName(fire->litBy), (int)fire->flag, fire->lightWanted, fire->lightNode != nullptr ? 1 : 0,
             (unsigned)(u16)a->params, isAwake, a->isDrawn ? 1 : 0, (int)lroundf(a->uncullZoneForward),
             Glows(fire) ? 1 : 0);
        count++;
        lit += fire->lit ? 1 : 0;
        lights += fire->lightNode != nullptr ? 1 : 0;
        awake += isAwake;
        drawn += a->isDrawn ? 1 : 0;
    });
    Addf(lines, "op=list result=ok count=%d lit=%d lights=%d awake=%d drawn=%d", count, lit, lights, awake, drawn);
    return 0;
}

int32_t Pool(std::vector<std::string>& lines) {
    int32_t nodes = 0;
    int32_t fires = 0;

    for (LightNode* n = gPlayState->lightCtx.listHead; n != nullptr; n = n->next) {
        nodes++;
    }
    ForEachFire([&](const Actor*, const RsFire* fire) { fires += fire->lightNode != nullptr ? 1 : 0; });
    // `of` is the cap in force (#233): LIGHTS_BUFFER_SIZE, or what `lights pool <n>` lowered it to.
    LightsStats stats;
    Lights_GetStats(&stats);
    Addf(lines, "op=pool result=ok nodes=%d of=%d fires=%d", nodes, stats.cap, fires);
    return 0;
}

// distance [<n>|default]: sets it if given, then reports the setting and how many fires carry it.
int32_t Distance(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "default") {
            CVarClear(CVAR_RS_FIRE_DRAW_DISTANCE);
        } else {
            char* end = nullptr;
            const long value = strtol(args[1].c_str(), &end, 10);
            if (end == args[1].c_str() || *end != '\0' || value < RS_FIRE_DRAW_DISTANCE_MIN ||
                value > RS_FIRE_DRAW_DISTANCE_MAX) {
                Addf(lines, "op=distance result=error error=bad_value min=%d max=%d", RS_FIRE_DRAW_DISTANCE_MIN,
                     RS_FIRE_DRAW_DISTANCE_MAX);
                return 1;
            }
            CVarSetInteger(CVAR_RS_FIRE_DRAW_DISTANCE, (int32_t)value);
        }
        CVarSave();
        ApplyToAll();
    }
    const int32_t forward = RsFire_DrawDistance();
    int32_t fires = 0;
    int32_t applied = 0;
    ForEachFire([&](const Actor* a, const RsFire*) {
        fires++;
        applied += lroundf(a->uncullZoneForward) == forward ? 1 : 0;
    });
    Addf(lines, "op=distance result=ok forward=%d default=%d override=%d fires=%d applied=%d", forward,
         RS_FIRE_DRAW_DISTANCE, CVarGetInteger(CVAR_RS_FIRE_DRAW_DISTANCE, -1) != -1 ? 1 : 0, fires, applied);
    return 0;
}

// glow [on|off|default]: sets it if given, then reports the setting and how many lit fires' lights carry it.
int32_t Glow(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "default") {
            CVarClear(CVAR_RS_FIRE_GLOW);
        } else if (args[1] == "on" || args[1] == "off") {
            CVarSetInteger(CVAR_RS_FIRE_GLOW, args[1] == "on" ? 1 : 0);
        } else {
            Addf(lines, "op=glow result=error error=bad_value expects=on|off|default");
            return 1;
        }
        CVarSave();
        ApplyToAll();
    }
    const int32_t on = RsFire_GlowOn();
    int32_t lights = 0;
    int32_t applied = 0;
    ForEachFire([&](const Actor*, const RsFire* fire) {
        if (fire->lightNode != nullptr) {
            lights++;
            applied += Glows(fire) == (on != 0) ? 1 : 0;
        }
    });
    Addf(lines, "op=glow result=ok glow=%s default=%s override=%d lights=%d applied=%d", on ? "on" : "off",
         RS_FIRE_GLOW_DEFAULT ? "on" : "off", CVarGetInteger(CVAR_RS_FIRE_GLOW, -1) != -1 ? 1 : 0, lights, applied);
    return 0;
}

} // namespace

int32_t RsFireConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty() || args.size() > 2) {
        lines.push_back(kUsage);
        return 1;
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=%s result=error error=no_scene", args[0].c_str());
        return 1;
    }
    const std::string& sub = args[0];
    if (sub == "distance") {
        return Distance(args, lines);
    }
    if (sub == "glow") {
        return Glow(args, lines);
    }
    if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    if (sub == "list") {
        return List(lines);
    }
    if (sub == "pool") {
        return Pool(lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `rsfire` console command -----------------------------------------------
//
// `rsfire` collides with nothing in debugger/debugconsole.cpp's CMD_REGISTER list.

namespace {

const ConsoleSink::Command rsFireCommand(
    "rsfire", RsFireConsole_Run,
    "The RS torch fires in this scene (sturdy-bassoon#232, #243): list | pool | distance [<n>|default] | "
    "glow [on|off|default]. list says where each fire is, its size, whether it is lit and by what, its "
    "world flag, whether it holds a light node, and whether it is awake and drawn; pool counts the scene's "
    "light nodes against the pool's cap (`lights` for the rest). distance sets how far in front of the "
    "camera flames draw (and so how far off an arrow can light one); glow switches the halo round each lit "
    "fire. Both apply to every fire at once and are saved; default goes back to the built-in value.",
    { { "list|pool|distance|glow", Ship::ArgumentType::TEXT }, { "value", Ship::ArgumentType::TEXT, true } });

} // namespace
