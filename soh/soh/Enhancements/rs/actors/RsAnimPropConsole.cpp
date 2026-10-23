#include "RsAnimPropConsole.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <fast/PerfCounters.h>
#include <fast/StaticMeshCache.h>
#include <ship/debug/Console.h>
#include <spdlog/spdlog.h>

#include "RsAnimProp.h"
#include "RsActors.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/staticbake/ArchivePropLists.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"

extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

const char* kUsage = "op=usage result=error usage=\"animprop status | list | pose <k|rest|clock> | reset\"";

// The forced pose, this session only (RsAnimProp.h's RS_ANIMPROP_FORCE_*).
int32_t sForced = RS_ANIMPROP_FORCE_CLOCK;

// THE #172 TRIPWIRE (ENGINE_BUDGETS "The texture cache"): the cache's cliff is silent, so while copies draw,
// a texture-cache eviction on every frame for a stretch with the camera still - a working set past the cap,
// not a view that moved onto new textures - writes one warning a scene. A baked draw holds its own textures,
// so copies the bake holds add nothing to the cache; a copy the bake refuses draws interpreted, and that is
// the case this catches.
constexpr int32_t kTripFrames = 40; // two seconds of 20 Hz ticks
struct Tripwire {
    uint32_t frame = 0;      // play->gameplayFrames of the last tick counted
    uint64_t evictions = 0;  // the cache's eviction total then
    Vec3f eye = {};          // the camera's eye then
    uint64_t counted = 0;    // evictions on frames a copy drew
    int32_t streak = 0;      // consecutive such frames with an eviction and the camera still
    int16_t scene = -1;
    bool tripped = false;
    bool primed = false;
} sTrip;

// A copy's lists as the console may read them: NULL when it has none, or when they were released since it fetched
// them (ArchiveProps_Generation moved) - a culled copy never draws, so never fetches again, and its pointer is freed.
const ArchivePropsAnimCopy* LiveCopy(const RsAnimProp* p) {
    return p->generation == ArchiveProps_Generation() ? p->copy : nullptr;
}

template <typename Fn> void ForEachCopy(Fn fn) {
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_RS_ANIMPROP) {
            fn(a, reinterpret_cast<RsAnimProp*>(a));
        }
    }
}

const char* ForcedName(char* buf, size_t n) {
    if (sForced == RS_ANIMPROP_FORCE_CLOCK) {
        return "clock";
    }
    if (sForced == RS_ANIMPROP_FORCE_REST) {
        return "rest";
    }
    snprintf(buf, n, "%d", (int)sForced);
    return buf;
}

int32_t Status(std::vector<std::string>& lines) {
    int32_t copies = 0, ok = 0, awake = 0, drawn = 0;
    uint64_t changes = 0, draws = 0;
    uint32_t baked = 0, rejected = 0, unbaked = 0;
    ForEachCopy([&](const Actor* a, const RsAnimProp* p) {
        copies++;
        ok += LiveCopy(p) != nullptr ? 1 : 0;
        awake += (a->flags & ACTOR_FLAG_INSIDE_CULLING_VOLUME) != 0 ? 1 : 0;
        drawn += a->isDrawn ? 1 : 0;
        changes += p->changes;
        draws += p->draws;
        if (LiveCopy(p) != nullptr) {
            ArchiveProps::AnimEntryStates(*LiveCopy(p), &baked, &rejected, &unbaked);
        }
    });
    char buf[16];
    Addf(lines,
         "op=status result=ok copies=%d ok=%d bad=%d awake=%d drawn=%d culled=%d changes=%llu draws=%llu "
         "clock=%.3f pinned=%d forced=%s lists=%u baked=%u unbaked=%u rejected=%u instance=%u copy_bytes=%u "
         "tex_evict=%llu streak=%d tripped=%d",
         copies, ok, copies - ok, awake, drawn, copies - drawn, (unsigned long long)changes,
         (unsigned long long)draws, Fast::StaticBakeClockSeconds(), Fast::StaticBakeClockIsPinned() ? 1 : 0,
         ForcedName(buf, sizeof(buf)), ArchiveProps::AnimLists(), baked, unbaked, rejected,
         (unsigned)sizeof(RsAnimProp), (unsigned)sizeof(ArchivePropsAnimCopy), (unsigned long long)sTrip.counted,
         sTrip.streak, sTrip.tripped ? 1 : 0);
    return 0;
}

std::string Joined(const int32_t* v, int32_t n) {
    std::string s;
    for (int32_t i = 0; i < n; i++) {
        s += (i ? "," : "") + std::to_string(v[i]);
    }
    return s;
}

int32_t List(std::vector<std::string>& lines) {
    int32_t count = 0;
    ForEachCopy([&](const Actor* a, const RsAnimProp* p) {
        const ArchivePropsAnimCopy* c = LiveCopy(p);
        const char* state = p->copy != nullptr && c == nullptr ? "stale" : ArchiveProps_AnimMissName(p->miss);
        std::string pose =
            p->pose < 0 ? "-" : (c != nullptr && p->pose == c->poseCount ? "rest" : std::to_string(p->pose));
        Addf(lines,
             "copy idx=%d list=%d copy=%d check=%d state=%s poses=%d frames=%s cycles=%s step=%d start=%d:%d pose=%s "
             "changes=%u draws=%u awake=%d drawn=%d x=%d y=%d z=%d",
             count, (int)p->list, (int)p->copyIndex, (int)p->check, state,
             c != nullptr ? (int)c->poseCount : 0, c != nullptr ? Joined(c->frames, c->poseCount).c_str() : "-",
             c != nullptr ? Joined(c->cycles, c->poseCount).c_str() : "-", c != nullptr ? (int)c->frameStep : 0,
             c != nullptr ? (int)c->startPose : 0, c != nullptr ? (int)c->startCycle : 0, pose.c_str(),
             (unsigned)p->changes, (unsigned)p->draws, (a->flags & ACTOR_FLAG_INSIDE_CULLING_VOLUME) != 0 ? 1 : 0,
             a->isDrawn ? 1 : 0, (int)lroundf(a->world.pos.x), (int)lroundf(a->world.pos.y),
             (int)lroundf(a->world.pos.z));
        count++;
    });
    Addf(lines, "op=list result=ok count=%d", count);
    return 0;
}

int32_t Pose(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() != 2) {
        lines.push_back(kUsage);
        return 1;
    }
    if (args[1] == "clock") {
        sForced = RS_ANIMPROP_FORCE_CLOCK;
    } else if (args[1] == "rest") {
        sForced = RS_ANIMPROP_FORCE_REST;
    } else {
        char* end = nullptr;
        const long k = strtol(args[1].c_str(), &end, 10);
        if (end == args[1].c_str() || *end != '\0' || k < 0 || k >= ARCHIVE_PROPS_ANIM_POSES_MAX) {
            Addf(lines, "op=pose result=error error=bad_value expects=0..%d|rest|clock",
                 ARCHIVE_PROPS_ANIM_POSES_MAX - 1);
            return 1;
        }
        sForced = (int32_t)k;
    }
    char buf[16];
    Addf(lines, "op=pose result=ok forced=%s", ForcedName(buf, sizeof(buf)));
    return 0;
}

int32_t Reset(std::vector<std::string>& lines) {
    int32_t copies = 0;
    ForEachCopy([&](Actor*, RsAnimProp* p) {
        p->changes = 0;
        p->draws = 0;
        copies++;
    });
    Addf(lines, "op=reset result=ok copies=%d", copies);
    return 0;
}

} // namespace

extern "C" double RsAnimProp_ClockSeconds(void) {
    return Fast::StaticBakeClockSeconds();
}

extern "C" int32_t RsAnimProp_ForcedPose(void) {
    return sForced;
}

extern "C" void RsAnimProp_FrameTick(PlayState* play) {
    if (play == nullptr || (sTrip.primed && sTrip.frame == play->gameplayFrames)) {
        return;
    }
    const uint64_t evictions = Fast::PerfCountersGet().texEvictions;
    const Vec3f eye = play->view.eye;
    if (sTrip.scene != play->sceneNum) {
        sTrip = Tripwire{};
        sTrip.scene = play->sceneNum;
    } else if (sTrip.primed) {
        const bool still = eye.x == sTrip.eye.x && eye.y == sTrip.eye.y && eye.z == sTrip.eye.z;
        const uint64_t more = evictions - sTrip.evictions;
        sTrip.counted += more;
        sTrip.streak = (more > 0 && still && play->gameplayFrames == sTrip.frame + 1) ? sTrip.streak + 1 : 0;
        if (sTrip.streak >= kTripFrames && !sTrip.tripped) {
            sTrip.tripped = true;
            SPDLOG_WARN("[rs_animprop] the texture cache evicted on each of {} frames with the camera still while "
                        "flipbook copies drew: its working set is past the cap (ENGINE_BUDGETS \"The texture "
                        "cache\", sturdy-bassoon#172)",
                        kTripFrames);
            char line[128];
            snprintf(line, sizeof(line), "rs_animprop event=tex_evict frames=%d evictions=%llu", kTripFrames,
                     (unsigned long long)sTrip.counted);
            RsAgent_Marker(line);
        }
    }
    sTrip.frame = play->gameplayFrames;
    sTrip.evictions = evictions;
    sTrip.eye = eye;
    sTrip.primed = true;
}

int32_t RsAnimPropConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.empty() || args.size() > 2) {
        lines.push_back(kUsage);
        return 1;
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=%s result=error error=no_scene", args[0].c_str());
        return 1;
    }
    const std::string& sub = args[0];
    if (sub == "pose") {
        return Pose(args, lines);
    }
    if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    if (sub == "status") {
        return Status(lines);
    }
    if (sub == "list") {
        return List(lines);
    }
    if (sub == "reset") {
        return Reset(lines);
    }
    lines.push_back(kUsage);
    return 1;
}

// --- the human sink: the `animprop` console command ---------------------------------------------
//
// `animprop` collides with nothing in debugger/debugconsole.cpp's CMD_REGISTER list.

namespace {

const ConsoleSink::Command animPropCommand(
    "animprop", RsAnimPropConsole_Run,
    "The flipbook props in this scene, each drawn by its own Rs_AnimProp (sturdy-bassoon#204): status | list | "
    "pose <k|rest|clock> | reset. status counts the copies, how many found their lists, drew, were culled, and "
    "their pose changes; list says each copy's clock and the pose it drew last. pose holds every copy on one pose "
    "(rest: its baked still's own triangles) for a comparison, and clock lets them play again; reset zeroes the "
    "pose-change counts, for a rate.",
    { { "status|list|pose|reset", Ship::ArgumentType::TEXT }, { "value", Ship::ArgumentType::TEXT, true } });

} // namespace
