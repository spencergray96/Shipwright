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
    "op=usage result=error usage=\"rsfire list | pool | distance [<n>|default] | glow [on|off|default] | "
    "pointlights [on|off|default] | directdraw [on|off|default] | reach [<n>|off|default] | accap [<n>|default] | "
    "freeze [on|off] | probe [<name> on|off|clear]\"";

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

// The planted probes (RsFire.h, #253), by name.
struct ProbeName {
    const char* name;
    uint32_t bit;
};
const ProbeName kProbes[] = {
    { "nosound", RS_FIRE_PROBE_NO_SOUND },
    { "nocollider", RS_FIRE_PROBE_NO_COLLIDER },
    { "noflame", RS_FIRE_PROBE_NO_FLAME },
};

// probe [<name> on|off | clear]: sets one if given, then reports them all. Session-only, never saved.
int32_t Probe(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    uint32_t probes = RsFire_GetProbes();

    if (args.size() == 2 && args[1] == "clear") {
        probes = 0;
    } else if (args.size() == 3) {
        const ProbeName* found = nullptr;
        for (const ProbeName& p : kProbes) {
            if (args[1] == p.name) {
                found = &p;
            }
        }
        if (found == nullptr || (args[2] != "on" && args[2] != "off")) {
            Addf(lines, "op=probe result=error error=bad_value expects=nosound|nocollider|noflame_on|off");
            return 1;
        }
        probes = (args[2] == "on") ? (probes | found->bit) : (probes & ~found->bit);
    } else if (args.size() != 1) {
        Addf(lines, "op=probe result=error error=bad_value expects=<name>_on|off|clear");
        return 1;
    }
    RsFire_SetProbes(probes);
    Addf(lines, "op=probe result=ok nosound=%d nocollider=%d noflame=%d", (probes & RS_FIRE_PROBE_NO_SOUND) ? 1 : 0,
         (probes & RS_FIRE_PROBE_NO_COLLIDER) ? 1 : 0, (probes & RS_FIRE_PROBE_NO_FLAME) ? 1 : 0);
    return 0;
}

// An on|off|default setting (#253's pointlights and directdraw): sets it if given, saves it, and writes it into
// every live fire. Returns 1 on a bad value, having written the error.
int32_t SetOnOff(const char* op, const char* cvar, const std::vector<std::string>& args,
                 std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "default") {
            CVarClear(cvar);
        } else if (args[1] == "on" || args[1] == "off") {
            CVarSetInteger(cvar, args[1] == "on" ? 1 : 0);
        } else {
            Addf(lines, "op=%s result=error error=bad_value expects=on|off|default", op);
            return 1;
        }
        CVarSave();
        ApplyToAll();
    }
    return 0;
}

// pointlights [on|off|default] (#253): whether a fire's own bind takes its position. Off (the default), the fire
// carries ACTOR_FLAG_IGNORE_POINTLIGHTS; `ignoring=` counts the fires that do.
int32_t PointLights(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (SetOnOff("pointlights", CVAR_RS_FIRE_POINT_LIGHTS, args, lines)) {
        return 1;
    }
    int32_t fires = 0;
    int32_t ignoring = 0;
    ForEachFire([&](const Actor* a, const RsFire*) {
        fires++;
        ignoring += (a->flags & ACTOR_FLAG_IGNORE_POINTLIGHTS) != 0 ? 1 : 0;
    });
    Addf(lines, "op=pointlights result=ok pointlights=%s default=%s override=%d fires=%d ignoring=%d",
         RsFire_PointLightsOn() ? "on" : "off", RS_FIRE_POINT_LIGHTS_DEFAULT ? "on" : "off",
         CVarGetInteger(CVAR_RS_FIRE_POINT_LIGHTS, -1) != -1 ? 1 : 0, fires, ignoring);
    return 0;
}

// directdraw [on|off|default] (#253): whether a fire draws its flame alone, outside Actor_Draw and frame
// interpolation. Read at every draw.
int32_t DirectDraw(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (SetOnOff("directdraw", CVAR_RS_FIRE_DIRECT_DRAW, args, lines)) {
        return 1;
    }
    Addf(lines, "op=directdraw result=ok directdraw=%s default=%s override=%d", RsFire_DirectDrawOn() ? "on" : "off",
         RS_FIRE_DIRECT_DRAW_DEFAULT ? "on" : "off", CVarGetInteger(CVAR_RS_FIRE_DIRECT_DRAW, -1) != -1 ? 1 : 0);
    return 0;
}

// reach [<n>|off|default] (#253): how far from Link a fire's collider registers; off is 0, no gate. Read every
// tick. `in_reach=` counts the awake fires inside it now.
int32_t Reach(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "default") {
            CVarClear(CVAR_RS_FIRE_COLLIDER_REACH);
        } else if (args[1] == "off") {
            CVarSetInteger(CVAR_RS_FIRE_COLLIDER_REACH, 0);
        } else {
            char* end = nullptr;
            const long value = strtol(args[1].c_str(), &end, 10);
            if (end == args[1].c_str() || *end != '\0' || value < 1 || value > RS_FIRE_COLLIDER_REACH_MAX) {
                Addf(lines, "op=reach result=error error=bad_value min=1 max=%d expects=<n>|off|default",
                     RS_FIRE_COLLIDER_REACH_MAX);
                return 1;
            }
            CVarSetInteger(CVAR_RS_FIRE_COLLIDER_REACH, (int32_t)value);
        }
        CVarSave();
    }
    const int32_t reach = RsFire_ColliderReach();
    Actor* link = &GET_PLAYER(gPlayState)->actor;
    int32_t awake = 0;
    int32_t inReach = 0;
    ForEachFire([&](const Actor* a, const RsFire*) {
        if ((a->flags & ACTOR_FLAG_INSIDE_CULLING_VOLUME) == 0) {
            return;
        }
        awake++;
        inReach += (reach == 0 || Actor_WorldDistXYZToActor(link, const_cast<Actor*>(a)) < (f32)reach) ? 1 : 0;
    });
    Addf(lines, "op=reach result=ok reach=%d default=%d override=%d awake=%d in_reach=%d", reach,
         RS_FIRE_COLLIDER_REACH_DEFAULT, CVarGetInteger(CVAR_RS_FIRE_COLLIDER_REACH, -1) != -1 ? 1 : 0, awake,
         inReach);
    return 0;
}

// freeze [on|off] (#253): SoH's frame advance (Developer Tools), so no actor updates while the game keeps
// drawing: a flame's scroll and flicker stand still, and two captures of one view can be compared pixel for
// pixel. A planted control; off before anything that must move.
int32_t Freeze(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] != "on" && args[1] != "off") {
            Addf(lines, "op=freeze result=error error=bad_value expects=on|off");
            return 1;
        }
        gPlayState->frameAdvCtx.enabled = (args[1] == "on") ? 1 : 0;
    }
    Addf(lines, "op=freeze result=ok freeze=%s", gPlayState->frameAdvCtx.enabled ? "on" : "off");
    return 0;
}

// accap [<n>|default] (#253): the AC list's cap for this session, every collider's and not only the fires'.
// A planted control for the old 192; never saved.
int32_t AcCap(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2) {
        if (args[1] == "default") {
            CollisionCheck_SetACCap(COLLISION_CHECK_AC_MAX);
        } else {
            char* end = nullptr;
            const long value = strtol(args[1].c_str(), &end, 10);
            if (end == args[1].c_str() || *end != '\0' || value < 1 || value > COLLISION_CHECK_AC_MAX) {
                Addf(lines, "op=accap result=error error=bad_value min=1 max=%d", COLLISION_CHECK_AC_MAX);
                return 1;
            }
            CollisionCheck_SetACCap((s32)value);
        }
    }
    Addf(lines, "op=accap result=ok cap=%d size=%d", CollisionCheck_GetACCap(), COLLISION_CHECK_AC_MAX);
    return 0;
}

} // namespace

int32_t RsFireConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty() || args.size() > 3 || (args.size() == 3 && args[0] != "probe")) {
        lines.push_back(kUsage);
        return 1;
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=%s result=error error=no_scene", args[0].c_str());
        return 1;
    }
    const std::string& sub = args[0];
    if (sub == "probe") {
        return Probe(args, lines);
    }
    if (sub == "distance") {
        return Distance(args, lines);
    }
    if (sub == "glow") {
        return Glow(args, lines);
    }
    if (sub == "pointlights") {
        return PointLights(args, lines);
    }
    if (sub == "directdraw") {
        return DirectDraw(args, lines);
    }
    if (sub == "reach") {
        return Reach(args, lines);
    }
    if (sub == "accap") {
        return AcCap(args, lines);
    }
    if (sub == "freeze") {
        return Freeze(args, lines);
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
    "fire. Both apply to every fire at once and are saved; default goes back to the built-in value. #253: "
    "pointlights on binds point lights for a fire's own (unlit) flame, as before; directdraw on draws each flame "
    "alone, outside Actor_Draw and frame interpolation, so it turns to the camera once a tick; reach sets how "
    "far from Link a fire's collider registers (off registers every fire in view). All three are saved. accap "
    "lowers the AC list's cap for this session, freeze stops actor updates (frame advance) so captures compare "
    "pixel for pixel, and probe switches off one of a fire's costs for this session "
    "only - nosound, nocollider, noflame - as planted controls for timing the fire field.",
    { { "list|pool|distance|glow|pointlights|directdraw|reach|accap|freeze|probe", Ship::ArgumentType::TEXT },
      { "value", Ship::ArgumentType::TEXT, true },
      { "on|off", Ship::ArgumentType::TEXT, true } });

} // namespace
