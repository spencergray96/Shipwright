#include "RsAnimPropConsole.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include <fast/FlipBook.h>
#include <fast/PerfCounters.h>
#include <fast/StaticMeshCache.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/debug/Console.h>
#include <spdlog/spdlog.h>

#include "RsAnimProp.h"
#include "RsActors.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/staticbake/ArchivePropLists.h"
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

const char* kUsage = "op=usage result=error usage=\"animprop status | list | pose <k|rest|clock> | reset | pick "
                     "<tick|frame|default> | trace [<idx> <frames>] | selftest [plant] | plant [off]\"";

// The forced pose, this session only (RsAnimProp.h's RS_ANIMPROP_FORCE_*).
int32_t sForced = RS_ANIMPROP_FORCE_CLOCK;

// Where the pose is picked (RsAnimProp.h, WHERE THE POSE IS PICKED): this session's choice from the agent
// loop (-1: none, 0 tick, 1 frame), else the saved CVar, else frame.
#define CVAR_RS_ANIMPROP_PICK CVAR_DEVELOPER_TOOLS("RsAnimPropPick")
int32_t sPickSession = -1;

// `plant`: a list holding a flip head, registered with the bake and drawn by one copy beside its own lists, so
// the recorder must refuse it (#204 C4b). Built from that copy's flip when planted.
Gfx sPlant[2];
const RsAnimProp* sPlantCopy = nullptr;
bool sPlanted = false;

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

// The pass a copy's flip-head numbers are read from: its translucent one when it has a flip there (the torch's
// flame), else its opaque one. nullptr when it has neither.
const void* FlipHandle(const ArchivePropsAnimCopy* c) {
    if (c == nullptr) {
        return nullptr;
    }
    if (c->flipSerial[ARCHIVE_PROPS_PASS_XLU] != 0) {
        return &c->lists[0][ARCHIVE_PROPS_PASS_XLU];
    }
    return c->flipSerial[ARCHIVE_PROPS_PASS_OPA] != 0 ? &c->lists[0][ARCHIVE_PROPS_PASS_OPA] : nullptr;
}

std::string PoseName(int32_t pose, int32_t poseCount) {
    return pose < 0 ? "-" : pose == poseCount ? "rest" : std::to_string(pose);
}

bool PickFrame() {
    if (sPickSession >= 0) {
        return sPickSession != 0;
    }
    return CVarGetInteger(CVAR_RS_ANIMPROP_PICK, RS_ANIMPROP_PICK_DEFAULT) != 0;
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
    const Fast::FlipBookStats flips = Fast::FlipBookGetStats();
    Addf(lines,
         "op=status result=ok copies=%d ok=%d bad=%d awake=%d drawn=%d culled=%d changes=%llu draws=%llu "
         "clock=%.3f pinned=%d forced=%s lists=%u baked=%u unbaked=%u rejected=%u instance=%u copy_bytes=%u "
         "tex_evict=%llu streak=%d tripped=%d pick=%s flips=%u flip_resolves=%llu flip_changes=%llu flip_stale=%llu "
         "frames=%llu",
         copies, ok, copies - ok, awake, drawn, copies - drawn, (unsigned long long)changes,
         (unsigned long long)draws, Fast::StaticBakeClockSeconds(), Fast::StaticBakeClockIsPinned() ? 1 : 0,
         ForcedName(buf, sizeof(buf)), ArchiveProps::AnimLists(), baked, unbaked, rejected,
         (unsigned)sizeof(RsAnimProp), (unsigned)sizeof(ArchivePropsAnimCopy), (unsigned long long)sTrip.counted,
         sTrip.streak, sTrip.tripped ? 1 : 0, PickFrame() ? "frame" : "tick", flips.registered,
         (unsigned long long)flips.resolves, (unsigned long long)flips.changes, (unsigned long long)flips.stale,
         (unsigned long long)flips.frames);
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
        Fast::FlipBookEntryInfo flip;
        const bool flipped = c != nullptr && Fast::FlipBookGetEntry(FlipHandle(c), &flip);
        const std::string flipPose = flipped ? PoseName(flip.last, c->poseCount) : "-";
        Addf(lines,
             "copy idx=%d list=%d copy=%d check=%d state=%s poses=%d frames=%s cycles=%s step=%d start=%d:%d pose=%s "
             "changes=%u draws=%u awake=%d drawn=%d x=%d y=%d z=%d flip=%s flip_changes=%llu flip_resolves=%llu",
             count, (int)p->list, (int)p->copyIndex, (int)p->check, state,
             c != nullptr ? (int)c->poseCount : 0, c != nullptr ? Joined(c->frames, c->poseCount).c_str() : "-",
             c != nullptr ? Joined(c->cycles, c->poseCount).c_str() : "-", c != nullptr ? (int)c->frameStep : 0,
             c != nullptr ? (int)c->startPose : 0, c != nullptr ? (int)c->startCycle : 0, pose.c_str(),
             (unsigned)p->changes, (unsigned)p->draws, (a->flags & ACTOR_FLAG_INSIDE_CULLING_VOLUME) != 0 ? 1 : 0,
             a->isDrawn ? 1 : 0, (int)lroundf(a->world.pos.x), (int)lroundf(a->world.pos.y),
             (int)lroundf(a->world.pos.z), flipPose.c_str(), flipped ? (unsigned long long)flip.changes : 0ULL,
             flipped ? (unsigned long long)flip.resolves : 0ULL);
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
    // The flips hold the same pose, so a forced comparison in frame mode draws through the flip heads.
    Fast::FlipBookForce(sForced == RS_ANIMPROP_FORCE_REST    ? Fast::FLIPBOOK_FORCE_REST
                        : sForced == RS_ANIMPROP_FORCE_CLOCK ? Fast::FLIPBOOK_FORCE_CLOCK
                                                             : sForced);
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

// `pick tick|frame|default`: `save` (the human sink) writes the CVar, `default` clears it; from the agent loop
// it holds for this session only, `default` dropping the session's choice.
int32_t Pick(const std::vector<std::string>& args, std::vector<std::string>& lines, bool save) {
    if (args.size() != 2 || (args[1] != "tick" && args[1] != "frame" && args[1] != "default")) {
        Addf(lines, "op=pick result=error error=bad_value expects=tick|frame|default");
        return 1;
    }
    if (save) {
        if (args[1] == "default") {
            CVarClear(CVAR_RS_ANIMPROP_PICK);
        } else {
            CVarSetInteger(CVAR_RS_ANIMPROP_PICK, args[1] == "frame" ? 1 : 0);
        }
        CVarSave();
        sPickSession = -1;
    } else {
        sPickSession = args[1] == "default" ? -1 : args[1] == "frame" ? 1 : 0;
    }
    Addf(lines, "op=pick result=ok pick=%s saved=%d", PickFrame() ? "frame" : "tick",
         CVarGetInteger(CVAR_RS_ANIMPROP_PICK, -1) != -1 ? 1 : 0);
    return 0;
}

// `trace <idx> <frames>` starts one copy's trace (fast/FlipBook.h, THE TRACE); `trace` reads it: a row a
// rendered frame it drew in, then a summary whose holds are the clock between pose changes, the first and last
// (cut short by the trace's ends) left out.
int32_t TraceRun(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 3) {
        char* end = nullptr;
        const long idx = strtol(args[1].c_str(), &end, 10);
        const bool idxOk = end != args[1].c_str() && *end == '\0';
        const long frames = strtol(args[2].c_str(), &end, 10);
        if (!idxOk || end == args[2].c_str() || *end != '\0' || frames < 1 || frames > 100000) {
            Addf(lines, "op=trace result=error error=bad_value expects=<idx> <1..100000>");
            return 1;
        }
        const void* handle = nullptr;
        int32_t i = 0;
        ForEachCopy([&](Actor*, RsAnimProp* p) {
            if (i++ == idx) {
                handle = FlipHandle(LiveCopy(p));
            }
        });
        if (!Fast::FlipBookTraceStart(handle, (uint32_t)frames)) {
            Addf(lines, "op=trace result=error error=no_flip idx=%ld", idx);
            return 1;
        }
        Addf(lines, "op=trace result=ok started=1 idx=%ld frames=%ld", idx, frames);
        return 0;
    }
    if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    bool running = false;
    const std::vector<Fast::FlipBookTraceRow>& rows = Fast::FlipBookTraceRows(&running);
    std::map<int32_t, int32_t> holds; // ms -> how many
    double since = -1.0;
    int32_t changes = 0, heads = 0;
    for (size_t r = 0; r < rows.size(); r++) {
        heads += rows[r].head ? 1 : 0;
        Addf(lines, "trace frame=%llu clock=%.6f pose=%d via=%s", (unsigned long long)rows[r].frame, rows[r].clock,
             (int)rows[r].pose, rows[r].head ? "head" : "dl");
        if (r > 0 && rows[r].pose != rows[r - 1].pose) {
            if (since >= 0.0) {
                holds[(int32_t)lround((rows[r].clock - since) * 1000.0)]++;
            }
            since = rows[r].clock;
            changes++;
        }
    }
    std::string hist;
    for (const auto& kv : holds) {
        hist += (hist.empty() ? "" : ",") + std::to_string(kv.first) + ":" + std::to_string(kv.second);
    }
    const double span = rows.size() > 1 ? rows.back().clock - rows.front().clock : 0.0;
    const uint64_t frameSpan = rows.size() > 1 ? rows.back().frame - rows.front().frame : 0;
    Addf(lines,
         "op=trace result=ok rows=%u running=%d heads=%d changes=%d span_s=%.4f frame_span=%llu fps=%.2f "
         "changes_per_s=%.3f holds_ms=%s",
         (unsigned)rows.size(), running ? 1 : 0, heads, changes, span, (unsigned long long)frameSpan,
         span > 0.0 ? (double)frameSpan / span : 0.0, span > 0.0 ? changes / span : 0.0,
         hist.empty() ? "-" : hist.c_str());
    return 0;
}

// `selftest [plant]`: every live copy's pose by the actor's own pick (RsAnimProp_PoseAt on RsAnimProp_CycleAt)
// against its registered flip's (Fast::FlipBookPoseAtClock), at every millisecond over two whole plays from
// two clock bases. `plant` shifts the flip's clock one client cycle, which a working test must catch.
int32_t SelfTest(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const bool plant = args.size() == 2 && args[1] == "plant";
    if (args.size() == 2 && !plant) {
        lines.push_back(kUsage);
        return 1;
    }
    const double shift = plant ? 1.0 / Fast::FLIPBOOK_CYCLES_PER_SECOND : 0.0;
    int32_t copies = 0, skipped = 0;
    uint64_t checked = 0, mismatched = 0;
    ForEachCopy([&](Actor*, RsAnimProp* p) {
        const ArchivePropsAnimCopy* c = LiveCopy(p);
        const void* handle = FlipHandle(c);
        if (handle == nullptr) {
            skipped++;
            return;
        }
        copies++;
        int64_t total = 0;
        for (int32_t k = 0; k < c->poseCount; k++) {
            total += c->cycles[k];
        }
        const int32_t ms = (int32_t)(2 * total * 20 + 40);
        for (double base : { p->initClock, 1000.0 + p->initClock }) {
            for (int32_t t = 0; t <= ms; t++) {
                const double now = base + t / 1000.0;
                const int32_t mine = RsAnimProp_PoseAt(c, RsAnimProp_CycleAt(p, now));
                const int32_t theirs = Fast::FlipBookPoseAtClock(handle, now + shift);
                checked++;
                mismatched += mine != theirs ? 1 : 0;
            }
        }
    });
    Addf(lines, "op=selftest result=%s plant=%d copies=%d skipped=%d checked=%llu mismatched=%llu",
         copies > 0 && mismatched == 0 ? "ok" : "fail", plant ? 1 : 0, copies, skipped, (unsigned long long)checked,
         (unsigned long long)mismatched);
    return copies > 0 && mismatched == 0 ? 0 : 1;
}

// `plant [off]` (#204 C4b): the first live copy also draws sPlant, a registered list holding its flip head, so
// the recorder meets a flip head inside a recording. Reports that entry's bake state and refusal.
int32_t Plant(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args.size() == 2 && args[1] == "off") {
        sPlanted = false;
        sPlantCopy = nullptr;
        Addf(lines, "op=plant result=ok planted=0");
        return 0;
    }
    if (args.size() != 1) {
        lines.push_back(kUsage);
        return 1;
    }
    if (!sPlanted) {
        const RsAnimProp* first = nullptr;
        ForEachCopy([&](Actor*, RsAnimProp* p) {
            if (first == nullptr && FlipHandle(LiveCopy(p)) != nullptr) {
                first = p;
            }
        });
        if (first == nullptr) {
            Addf(lines, "op=plant result=error error=no_flip");
            return 1;
        }
        const ArchivePropsAnimCopy* c = LiveCopy(first);
        const void* handle = FlipHandle(c);
        const s32 pass = handle == &c->lists[0][ARCHIVE_PROPS_PASS_XLU] ? ARCHIVE_PROPS_PASS_XLU : ARCHIVE_PROPS_PASS_OPA;
        gSPFlipBook(&sPlant[0], handle, c->flipSerial[pass]);
        gSPEndDisplayList(&sPlant[1]);
        Fast::StaticBakeRegister(sPlant);
        sPlantCopy = first;
        sPlanted = true;
    }
    const Fast::StaticBakeEntryInfo e = Fast::StaticBakeGetEntry(sPlant);
    const char* state = e.state == Fast::StaticBakeEntryState::Baked      ? "baked"
                        : e.state == Fast::StaticBakeEntryState::Rejected ? "rejected"
                        : e.state == Fast::StaticBakeEntryState::Unbaked  ? "unbaked"
                                                                          : "not_registered";
    Addf(lines, "op=plant result=ok planted=1 state=%s reason=\"%s\"", state,
         e.rejectReason != nullptr ? e.rejectReason : "-");
    return 0;
}

int32_t Run(const std::vector<std::string>& args, std::vector<std::string>& lines, bool save) {
    if (args.empty() || args.size() > 3) {
        lines.push_back(kUsage);
        return 1;
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=%s result=error error=no_scene", args[0].c_str());
        return 1;
    }
    const std::string& sub = args[0];
    if (sub == "trace") {
        return TraceRun(args, lines);
    }
    if (args.size() > 2) {
        lines.push_back(kUsage);
        return 1;
    }
    if (sub == "pose") {
        return Pose(args, lines);
    }
    if (sub == "pick") {
        return Pick(args, lines, save);
    }
    if (sub == "selftest") {
        return SelfTest(args, lines);
    }
    if (sub == "plant") {
        return Plant(args, lines);
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

} // namespace

extern "C" int32_t RsAnimProp_PickFrame(void) {
    return PickFrame() ? 1 : 0;
}

extern "C" Gfx* RsAnimProp_PlantList(const RsAnimProp* prop) {
    return sPlanted && prop == sPlantCopy ? sPlant : nullptr;
}

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
    return Run(args, lines, true);
}

int32_t RsAnimPropConsole_RunSession(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    return Run(args, lines, false);
}

// --- the human sink: the `animprop` console command ---------------------------------------------
//
// `animprop` collides with nothing in debugger/debugconsole.cpp's CMD_REGISTER list.

namespace {

const ConsoleSink::Command animPropCommand(
    "animprop", RsAnimPropConsole_Run,
    "The flipbook props in this scene, each drawn by its own Rs_AnimProp (sturdy-bassoon#204): status | list | "
    "pose <k|rest|clock> | reset | pick <tick|frame|default> | trace [<idx> <frames>] | selftest [plant] | plant "
    "[off]. status counts the copies, how many found their lists, drew, were culled, and their pose changes; list "
    "says each copy's clock and the pose it drew last. pose holds every copy on one pose (rest: its baked still's "
    "own triangles) for a comparison, and clock lets them play again; reset zeroes the pose-change counts, for a "
    "rate. pick: where the pose is picked, per rendered frame (flip heads, the default) or per tick in Draw "
    "(sturdy-bassoon#204 C4b); saved. trace logs which pose one copy drew each rendered frame; selftest checks the "
    "two picks agree; plant draws a flip head inside a baked list, which the recorder must refuse.",
    { { "status|list|pose|reset|pick|trace|selftest|plant", Ship::ArgumentType::TEXT },
      { "value", Ship::ArgumentType::TEXT, true },
      { "frames", Ship::ArgumentType::TEXT, true } });

} // namespace
