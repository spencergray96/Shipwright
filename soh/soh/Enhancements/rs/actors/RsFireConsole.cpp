#include "RsFireConsole.h"

#include <cmath>

#include <ship/debug/Console.h>

#include "RsFire.h"
#include "soh/Enhancements/console/ConsoleSink.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"

extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

const char* kUsage = "op=usage result=error usage=\"rsfire list | pool\"";

// LIGHTS_BUFFER_SIZE is static to z_lights.c; this mirrors it for the report only (#233 may change it).
constexpr int32_t kLightPool = 32;

int32_t List(std::vector<std::string>& lines) {
    int32_t count = 0;
    int32_t lit = 0;
    int32_t lights = 0;

    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].head; a != nullptr; a = a->next) {
        if (a->id != ACTOR_RS_FIRE) {
            continue;
        }
        const RsFire* fire = reinterpret_cast<const RsFire*>(a);
        Addf(lines,
             "fire idx=%d x=%d y=%d z=%d size=%d lit=%d by=%s flag=%d want_light=%d light=%d params=0x%04X", count,
             (int)lroundf(a->world.pos.x), (int)lroundf(a->world.pos.y), (int)lroundf(a->world.pos.z),
             (int)lroundf(fire->size * 100.0f), fire->lit, RsFire_LitByName(fire->litBy),
             (int)fire->flag, fire->lightWanted,
             fire->lightNode != nullptr ? 1 : 0, (unsigned)(u16)a->params);
        count++;
        lit += fire->lit ? 1 : 0;
        lights += fire->lightNode != nullptr ? 1 : 0;
    }
    Addf(lines, "fires count=%d lit=%d lights=%d", count, lit, lights);
    return 0;
}

int32_t Pool(std::vector<std::string>& lines) {
    int32_t nodes = 0;
    int32_t fires = 0;

    for (LightNode* n = gPlayState->lightCtx.listHead; n != nullptr; n = n->next) {
        nodes++;
    }
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_RS_FIRE && reinterpret_cast<const RsFire*>(a)->lightNode != nullptr) {
            fires++;
        }
    }
    Addf(lines, "pool nodes=%d of=%d fires=%d", nodes, kLightPool, fires);
    return 0;
}

} // namespace

int32_t RsFireConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty()) {
        lines.push_back(kUsage);
        return 1;
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=%s result=error error=no_scene", args[0].c_str());
        return 1;
    }
    const std::string& sub = args[0];
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
    "The RS torch fires in this scene (sturdy-bassoon#232): list | pool. list says where each fire is, "
    "its size, whether it is lit and by what, its world flag, and whether it holds a light node; pool "
    "counts the scene's light nodes against the 32 the engine has.",
    { { "list|pool", Ship::ArgumentType::TEXT } });

} // namespace
