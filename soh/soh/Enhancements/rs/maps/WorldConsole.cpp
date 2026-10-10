#include "WorldConsole.h"

#include <cstdio>
#include <cstdlib>
#include <ship/debug/Console.h>

#include "SceneMaps.h"
#include "WorldContext.h"
#include "soh/Enhancements/console/ConsoleSink.h"

extern "C" {
#include <z64.h>
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

std::string Describe() {
    const int32_t ctx = RsWorld_Get();
    return "ctx=" + std::string(RsWorld_Name(ctx)) + " value=" + std::to_string(ctx) + " source=" + RsWorld_Source();
}

// "unset", "solo", or a scene id in hex (0xA2) or decimal. Refused here, not in RsWorld_Set, so the
// line can say why; Set refuses the same values anyway.
bool ParseContext(const std::string& word, int32_t* out) {
    if (word == "unset") {
        *out = RS_WORLD_CONTEXT_UNSET;
        return true;
    }
    if (word == "solo") {
        *out = RS_GEN_WORLD_SOLO;
        return true;
    }
    char* end = nullptr;
    const long value = std::strtol(word.c_str(), &end, 0);
    if (word.empty() || end == nullptr || *end != '\0' || value < 0 || value >= SCENE_ID_MAX) {
        return false;
    }
    *out = static_cast<int32_t>(value);
    return RsWorld_IsValid(*out) != 0;
}

bool ParseMap(const std::vector<std::string>& args, int32_t* map) {
    if (args.size() < 2) {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(args[1].c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || value < 1 || value > 0xFFFF) {
        return false;
    }
    *map = static_cast<int32_t>(value);
    return true;
}

const char* kUsage = "usage: worldctx get | set <unset|solo|0xNN> | pick <map> | scenes <map>";

} // namespace

int32_t RsWorldConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
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
        int32_t ctx = 0;
        if (args.size() < 2 || !ParseContext(args[1], &ctx)) {
            lines.push_back("op=set result=error error=bad_context");
            return 1;
        }
        const std::string from = RsWorld_Name(RsWorld_Get());
        RsWorld_Set(ctx, "console");
        lines.push_back("op=set from=" + from + " " + Describe());
        return 0;
    }
    if (sub == "pick") {
        int32_t map = 0;
        if (!ParseMap(args, &map)) {
            lines.push_back("op=pick result=error error=bad_map");
            return 1;
        }
        const int32_t here = gPlayState != nullptr ? gPlayState->sceneNum : -1;
        int32_t rank = RS_MAPS_PICK_NONE;
        const int32_t scene = RsMaps_PickScene(map, here, RsWorld_Get(), &rank);
        if (scene < 0) {
            Addf(lines, "op=pick map=%d result=error error=no_scene ctx=%s", map, RsWorld_Name(RsWorld_Get()));
            return 1;
        }
        Addf(lines, "op=pick map=%d scene=0x%X entrance=0x%X rank=%s ctx=%s", map, scene, RsMaps_SceneEntrance(scene),
             RsMaps_PickRankName(rank), RsWorld_Name(RsWorld_Get()));
        return 0;
    }
    if (sub == "scenes") {
        int32_t map = 0;
        if (!ParseMap(args, &map)) {
            lines.push_back("op=scenes result=error error=bad_map");
            return 1;
        }
        int32_t scenes[16];
        const int32_t count = RsMaps_ScenesHolding(map, scenes, ARRAY_COUNT(scenes));
        Addf(lines, "op=scenes map=%d count=%d", map, count);
        for (int32_t i = 0; i < count && i < static_cast<int32_t>(ARRAY_COUNT(scenes)); i++) {
            const int32_t world = RsMaps_SceneWorld(scenes[i]);
            Addf(lines, "scene[%d]=0x%X world=%s entrance=0x%X", i, scenes[i], RsMaps_WorldName(world),
                 RsMaps_SceneEntrance(scenes[i]));
        }
        return 0;
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `worldctx` console command ----------------------------------------------

namespace {

const ConsoleSink::Command worldctxCommand(
    "worldctx", RsWorldConsole_Run,
    "The save's world context (sturdy-bassoon#173 F5): get | set <unset|solo|0xNN> | pick <map> | scenes "
    "<map>. A chunk map is in its solo scene and in the stitched overworld; when a staircase or warp tile "
    "takes Link to it from a basement, this decides which loads. Entering a chunk scene sets it; set it "
    "here to test the other world. pick says what a trip to a map would load from here.",
    { { "get|set|pick|scenes", Ship::ArgumentType::TEXT }, { "argument", Ship::ArgumentType::TEXT, true } });

} // namespace
