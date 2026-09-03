/**
 * AgentTest.cpp
 *
 * Lets an external process (an AI agent, a script, a human with Notepad) drive and observe the game
 * without touching the keyboard. Everything is file based so it is inspectable with any tool:
 *
 *   <app dir>/agent-commands.txt   input  - one console command per line, appended by the driver
 *   <app dir>/agent-log.txt        output - "[agenttest] ..." markers, appended by this file
 *
 * <app dir> is where soh.exe lives (x64/Debug for local builds). Every marker also goes through
 * SPDLOG_INFO so it lands in logs/Ship of Harkinian.log alongside the engine's own output.
 *
 * Agent mode is decided once, on the first console-logo frame, from the environment variable
 * SOH_AGENT_TEST: "1" in the process environment -> the session is on until the process exits;
 * anything else -> every hook below early-returns on one bool and nothing is ever read or
 * written. The driver sets the variable in its own shell before launching soh.exe, which
 * inherits it; nothing persists on disk or in the config. A stale agent-commands.txt is inert.
 *
 * Markers (all prefixed "[agenttest] "):
 *   session pid=<n>                      first console-logo tick, SOH_AGENT_TEST seen
 *   boot_to_play entrance=0x<hex>        console logo skipped, booting a debug save straight into play
 *   scene_loaded scene=0x<hex> entrance=0x<hex>
 *   ready scene=0x<hex> entrance=0x<hex> Link exists and Play_Init has finished; commands are consumed
 *                                        only after this, and it is re-emitted after every scene change
 *   cmd <line> rc=<n> out=<text>         one line consumed from the command file. rc=-1 means the
 *                                        command does not exist; otherwise it is the handler's return
 *                                        (0 = success). Most SoH handlers print to the ImGui console
 *                                        rather than <text>, so out= is usually empty for them.
 *   perf fps=<f> ms=<f> sub_ms=<f> draws=<n> tris=<n> flushes=<n> tick_ms=<f> tick_max_ms=<f>
 *        mem_mb=<n> actors=<n> act_near=<n> act_mid=<n> act_far=<n> nodes=<used>/<max> cell_max=<n>
 *        cell_p95=<n> cells=<occupied>/<total> heap_kb=<free>/<total> colchk_at=<peak>/<max>
 *        colchk_ac=<peak>/<max> colchk_oc=<peak>/<max> colchk_rej=<at>/<ac>/<oc>
 *        dynapoly=<peak>/<max> dynapoly_rej=<n> fog=<near>/<far> cap=<n> scene=0x<hex> frame=<n>
 *                                        every PerfInterval ticks (0 disables). fps/ms = ImGui render
 *                                        framerate (capped by the FPS setting); sub_ms = mean wall time
 *                                        one rendered frame spent inside the Fast3D interpreter, and
 *                                        draws/tris/flushes = what the last rendered frame submitted -
 *                                        the render-side figures neither ms= (capped) nor tick_ms=
 *                                        (ends before submission) can show; tick_ms = game-tick CPU
 *                                        time, avg and max over the interval; mem_mb = working set;
 *                                        actors = resident actor count (summed over the category
 *                                        lists; equals actorCtx.total since sturdy-bassoon#45
 *                                        widened that field, which used to wrap past 255);
 *                                        act_near/act_mid/act_far = how "agenttest tiers" classified
 *                                        them last frame, all zero while it is off;
 *                                        cell_max/cell_p95 = per-subdivision-cell static collision
 *                                        list lengths (see RefreshCellStats); colchk_at/ac/oc =
 *                                        worst single-frame occupancy of the three collider
 *                                        subscription lists this interval against their caps, and
 *                                        colchk_rej = subscriptions refused because a list was
 *                                        full - a correctness headroom, since a refused collider's
 *                                        hits and bumps silently do not happen (sturdy-bassoon#49);
 *                                        dynapoly = worst BgActor-slot occupancy this interval
 *                                        against BG_ACTOR_MAX and dynapoly_rej = registrations
 *                                        DynaPoly_SetBgActor refused - a refused actor exists and
 *                                        draws but has no dynapoly collision (sturdy-bassoon#55);
 *                                        fog = lightCtx fogNear
 *                                        (fog-space 0..1000) / fogFar (world units - also the view's
 *                                        far clip plane); cap = the fps target the render loop is
 *                                        actually holding to (InterpolationFPS after the vsync and
 *                                        MatchRefreshRate clamps), so an "uncapped" line proves itself
 *   room_changed from=<n> to=<n> frame=<n> pos=<x>,<y>,<z>
 *                                        the current room changed within a scene, and where Link stood on
 *                                        the tick it changed - transition-trigger latency is measured in
 *                                        units from the boundary, not in ticks
 *   roomdist <event>                     the distance-based room trigger requested or finished a room
 *                                        change (sturdy-bassoon#6 Exp 4), with the two centre distances
 *                                        that decided it
 *   state scene=0x<hex> room=<n> entrance=0x<hex> pos=<x>,<y>,<z> yaw=<n> age=<adult|child> time=0x<hex>
 *         night=<0|1> rupees=<n> rupees_pending=<n> frame=<n> cam_at=<x>,<y>,<z> cam_eye=<x>,<y>,<z>
 *         cam_setting=<n> cam_mode=<n> cam_dist=<f> name="<scene name>"
 *                                        rupees is gSaveContext.rupees and rupees_pending the
 *                                        accumulator Interface_Update drains into it one per frame
 *                                        (wallet-capped) - together they are the witness for a quest
 *                                        reward, since Rupees_ChangeBy only ever feeds the accumulator
 *                                        while a scene is loaded
 *                                        cam_* is the active camera: where it looks, where it sits,
 *                                        which CameraSettingType/mode is live, and the at-eye distance -
 *                                        the measurable form of "the camera is sitting on the floor
 *                                        looking up" (issue #38). name is last and quoted because it is
 *                                        the only field that can contain a space
 *   trace <pre|post> frame=<n> lbox=<cur>/<target> pos=... prev=... velY=... lin=... bg=0x<hex> floorH=...
 *         sf1..sf3=0x<hex> anim=0x<hex> trans=<n> rdown=<x>,<y>,<z> rdent=0x<hex>
 *                                        per-tick Player diagnostic while "agenttest trace" is active: position,
 *                                        prevPos, velocity, bgCheckFlags, floor height, state flags, anim movement
 *                                        flags, transition trigger and the void-out respawn point. "pre" is taken
 *                                        before the game tick runs, "post" after it (and after command consumption)
 *   fog mode=override near=<n> far=<n> / fog mode=scene
 *                                        echoed from "agenttest fog"; between these, the perf marker's fog=
 *                                        field carries whatever band is actually live
 *   quest <line>                         one line of QuestConsole_Run output per marker, from
 *                                        "agenttest quest ..." (sturdy-bassoon#58 P1): the Describe line
 *                                        `id=<n> name=<s> tier=<s> status=<s> steps=0x<mask>/<count>
 *                                        ordered=<0|1> available=<0|1> prereqs=<met|unmet>`, and for a
 *                                        mutating subcommand `op=<sub> id=<n> [step=<n>] result=<name>`
 *                                        first. Same renderer as the human `quest` command.
 *                                        P2 adds the journal surface: `journal <id|all> [runs]` emits
 *                                        `journal id=<n> ... visible=<n>` then `line[i]=para|item ...`
 *                                        with spans rendered as `[item:Egg]` and a checked row wrapped
 *                                        in ~tildes~; `runs` adds `run[i.r]=<style> emphasis=<name>`
 *                                        per run. `parse <text...>` is the markup probe
 *                                        (`op=parse result=ok|error error=<kind> pos=<n>`), and
 *                                        `badcheck` runs the registration gate over the malformed
 *                                        definition table (`bad[i]=<label> refused=1 problem="..."`)
 *   mark <text>                          echoed from "agenttest mark <text>"
 *   input_done [reason=scene_change]     a walk/press injection finished (or was cancelled by a scene change)
 *
 * Console command registered here:
 *   agenttest perf <ticks>                 set the perf marker interval (game ticks, 20/s); 0 disables
 *   agenttest state                        emit a state marker (scene, room, entrance, Link position and facing)
 *   agenttest goto <x> <y> <z> [yaw]       teleport Link, optionally set facing (s16 angle: 0=+Z, 16384=+X,
 *                                          -32768=-Z, -16384=-X); emits a state marker
 *   agenttest walk <frames> [sx] [sy] [buttons] [at_frame]
 *                                          hold the stick at (sx, sy) for N game frames (20/s); default 0,80 =
 *                                          full speed away from the camera. Optional buttons (comma list) land
 *                                          with a fresh press edge on the at_frame-th injected frame (default 1)
 *                                          and stay held to the end - e.g. "walk 80 0 80 A 40" rolls mid-run.
 *                                          Ends with input_done.
 *   agenttest press <BUTTONS> [frames]     hold A,B,Z,R,L,START,DUP..,CUP.. (comma list) for N frames, default 2.
 *                                          "press Z" with nothing targeted re-centres the camera behind Link.
 *   agenttest rooms                        one "transition idx= id= rooms=A,B pos= rotY=" marker per transition
 *                                          actor in the scene: where the room boundaries are
 *   agenttest time <dawn|day|dusk|night|value>  set the time of day: dayTime and skyboxTime together, plus
 *                                          nightFlag by the engine's own threshold (night when > 0xC000 or
 *                                          < 0x4555). Presets dawn=0x4000, day=0x8000, dusk=0xC001, night=0;
 *                                          value is 0..65535, decimal or 0x-hex. Emits a state marker
 *   agenttest trace <ticks>                emit a "trace" marker pair (pre/post) around each of the next N game
 *                                          ticks (0 cancels, max MAX_TRACE_TICKS). The tick the command lands in
 *                                          contributes its post only. Diagnostic for teleport/movement bugs
 *   agenttest cutscene <index> | off       arm the NEXT scene load to enter on a cutscene layer, the way a scene
 *                                          whose opening is a cutscene does. index is 0xFFF0..0xFFFF (0..15 is
 *                                          accepted as the layer number and offset for you). "off" clears both
 *                                          the queue and the live index - warping out of a cutscene leaves the
 *                                          latter armed, and the next load would silently be a cutscene entry.
 *                                          The entrance's table group must actually have that many rows or the
 *                                          load lands in a neighbouring scene - Temple of Time (0x53) has 11
 *                                          and is the safe default target
 *   agenttest fog <near> <far> | off       override the scene's fog band and far clip plane, or hand them back
 *                                          to the scene's light settings. near is fog-space (0..1000, the scene
 *                                          path clamps at 996); far is world units (100..12800) and is also the
 *                                          view's zFar - Play_Draw builds the perspective from lightCtx.fogFar -
 *                                          so pulling far in genuinely un-draws geometry past it. Rides the
 *                                          engine's own reg-editor override (R_ENV_DISABLE_DBG +
 *                                          R_ENV_FOG_NEAR/FAR in z_kankyo.c); while active, ambient/directional
 *                                          light and fog colour are frozen at their flip-time values (fine in
 *                                          flat-lit custom scenes; don't combine with "agenttest time").
 *                                          Environment_Init re-arms scene control on every scene load, so the
 *                                          override must be re-applied after each entrance - which is also the
 *                                          safety net against leaking it into a later run
 *   agenttest tiers <near> <mid> <n> [mitb] [drawcull] | off
 *                                          arm the distance-tiered actor update prototype (sturdy-bassoon#6
 *                                          Exp 2): full update inside <near>, every nth frame (staggered per
 *                                          actor) out to <mid>, skipped beyond it. mitb=1 scales the shared
 *                                          SkelAnime and velocity-integration paths by the skipped-tick count
 *                                          so throttled vanilla actors keep wall-clock speed; drawcull=1 also
 *                                          skips the per-actor draw-pass work past <mid>. "off" restores the
 *                                          vanilla path. Per-tier counts ride the perf marker's act_* fields
 *   agenttest roomdist [hysteresis] | off
 *                                          arm the distance-based room trigger (sturdy-bassoon#6 Exp 4):
 *                                          each tick, pick the room whose centre is nearest Link and drive
 *                                          Room_RequestNewRoom when it differs from the current one.
 *                                          hysteresis (world units, default 0) is how much closer the
 *                                          candidate must be before the change is taken. While armed the
 *                                          En_Holl planes stand down, so the two triggers are measured one
 *                                          at a time
 *   agenttest mark <text>                  write a marker, for bracketing checkpoints in the log
 *
 * Command-file consumption pauses while an injection is in progress, so queued lines run in order.
 * Injection details: the stick replaces the real pad's stick, buttons are OR-ed onto the real pad's
 * buttons, frames are counted only when Player_Update will read them (not paused, not mid-transition),
 * a scene change cancels the injection, frames are capped at MAX_INPUT_FRAMES, and none of it shows in
 * the ImGui Input Viewer (which renders the raw pad).
 *
 * See sturdy-bassoon/docs/reference/AGENT_TEST_LOOP.md for the driver-side protocol.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-08-21
 */

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <utility>

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <ship/Context.h>
#include <ship/debug/Console.h>
#include <fast/PerfCounters.h>
#include "soh/OTRGlobals.h"
#include "soh/util.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/actortiers/ActorTiers.h"
#include "soh/Enhancements/roomdist/RoomDist.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/Enhancements/rs/quest/QuestStore.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestConsole.h"
#include "soh/Enhancements/rs/dialogue/NpcConsole.h"
#include "AgentTest.h"
#include "soh/ShipInit.hpp"
// For SaveManager::Instance, which `save` and `loadsave` drive directly. The free
// `Save_SaveFile`/`Save_LoadFile` wrappers are declared here with C++ linkage but defined
// `extern "C"`, so calling them from a .cpp asks the linker for a symbol that is not there.
#include "soh/SaveManager.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
void Sram_InitDebugSave(void);
}

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#include <psapi.h>
#define AGENTTEST_GETPID _getpid
#else
#include <unistd.h>
#define AGENTTEST_GETPID getpid
#endif

namespace {

constexpr const char* AGENT_MODE_ENV = "SOH_AGENT_TEST";
constexpr const char* COMMAND_FILE = "agent-commands.txt";
constexpr const char* MARKER_FILE = "agent-log.txt";
constexpr uint32_t POLL_INTERVAL = 10; // game ticks between command-file polls (20 ticks = 1 s)
constexpr int32_t DEFAULT_PERF_INTERVAL = 60;
constexpr int32_t MAX_INPUT_FRAMES = 20 * 60; // one minute of injected input; command channel is blocked meanwhile
constexpr int32_t MAX_TRACE_TICKS = 400;      // 20 s of trace markers, two lines per tick
// "agenttest fog" bounds. Near is fog-space (0..1000 across zNear..zFar; the engine's scene path
// clamps at 996, so 1000 = band collapsed to the far plane). Far is world units and the far clip;
// 12800 is the engine's own scene-path ceiling (z_kankyo.c) and 100 comfortably clears zNear.
constexpr int32_t FOG_NEAR_MAX = 1000;
constexpr int32_t FOG_FAR_MIN = 100;
constexpr int32_t FOG_FAR_MAX = 12800;
// "agenttest cutscene" bounds, both Play_Init's own. nextCutsceneIndex holds NONE when nothing is
// queued; anything from FIRST up is read as "this entry is a cutscene", and the scene layer it picks
// is SCENE_LAYER_CUTSCENE_FIRST + (index & LAYER_MAX).
constexpr int32_t CUTSCENE_INDEX_NONE = 0xFFEF;
constexpr int32_t CUTSCENE_INDEX_FIRST = 0xFFF0;
constexpr int32_t CUTSCENE_LAYER_MAX = 0xF;
// Staging scene for the auto-boot: the door spawn of Link's house. Small, loads fast, nothing scripted.
// The caller's real entrance comes through the command file afterwards.
constexpr int32_t BOOT_ENTRANCE = ENTR_LINKS_HOUSE_0_1;

// State
bool sAgentMode = false; // decided once at the console logo from SOH_AGENT_TEST; never re-checked

// Input injection: while sInputFramesLeft > 0, OnGameStateMainStart overwrites controller 1 with
// these values. Command consumption pauses until it finishes, so queued lines run in order.
int32_t sInputFramesLeft = 0;
int8_t sInputStickX = 0;
int8_t sInputStickY = 0;
uint16_t sInputButtons = 0;
bool sInputPressPending = false; // first injected frame also sets press.button (a fresh press)
// Mid-walk button press (walk's optional [buttons] [at_frame] args): once sInputFramesLeft counts
// down to sDeferredAtFramesLeft, these buttons join sInputButtons with a fresh press edge and stay
// held for the rest of the injection. This is how a roll is driven - A must land while running.
uint16_t sDeferredButtons = 0;
int32_t sDeferredAtFramesLeft = 0;
bool sReady = false;             // true from the first Player update after the latest OnSceneInit
int32_t sTraceTicksLeft = 0;     // while > 0, emit a trace marker pair (pre/post) around every game tick
int32_t sPerfInterval = DEFAULT_PERF_INTERVAL;
uint32_t sTickCounter = 0;
std::streamoff sConsumedBytes = 0;
GameState* sLogoState = nullptr; // non-null only during the tick the console-logo state ran

// Game-tick CPU time: OnGameStateMainStart -> OnGameFrameUpdate brackets gameState->main(), i.e. the
// whole update + display-list build for one tick. Independent of the render FPS cap.
std::chrono::steady_clock::time_point sTickStart;
bool sTickStarted = false;
double sTickSumMs = 0.0;
double sTickMaxMs = 0.0;
uint32_t sTickSamples = 0;
int16_t sLastRoom = -1;

// Fast3D's render-side counters as of the previous perf marker. They are cumulative, so the
// interval's own figures are this subtracted from the current read.
Fast::PerfCounters sLastRenderCounters = {};

std::string CommandPath() {
    return Ship::Context::GetPathRelativeToAppDirectory(COMMAND_FILE);
}

std::string MarkerPath() {
    return Ship::Context::GetPathRelativeToAppDirectory(MARKER_FILE);
}

bool AgentModeRequested() {
    const char* value = std::getenv(AGENT_MODE_ENV);
    return value != nullptr && std::string(value) == "1";
}

std::string Timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    char out[40];
    std::snprintf(out, sizeof(out), "%s.%03d", buf, static_cast<int>(ms.count()));
    return out;
}

void WriteMarker(const std::string& text) {
    SPDLOG_INFO("[agenttest] {}", text);
    std::ofstream out(MarkerPath(), std::ios::app);
    if (out) {
        out << "[" << Timestamp() << "] [agenttest] " << text << "\n";
    }
}

std::string Hex(int32_t value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%X", static_cast<unsigned>(value) & 0xFFFF);
    return buf;
}

// Single-line, printable rendering of whatever a command handler wrote to its output string.
std::string SingleLine(std::string text) {
    for (char& c : text) {
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
    }
    return text;
}

bool InNormalPlay() {
    return gPlayState != nullptr && gSaveContext.gameMode == GAMEMODE_NORMAL;
}

// Mirrors Warping.cpp's boot-to-warp-point path and Select_LoadGame: a fresh debug save, straight into Play_Init.
void BootToPlay(GameState* logoState) {
    WriteMarker("boot_to_play entrance=" + Hex(BOOT_ENTRANCE));

    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.fileNum = 0xFF; // debug save
    Sram_InitDebugSave();
    gSaveContext.magicFillTarget = gSaveContext.magic;
    gSaveContext.magic = 0;
    gSaveContext.magicCapacity = 0;
    gSaveContext.magicLevel = gSaveContext.magic;
    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);

    gSaveContext.sceneLayer = 0;
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.linkAge = LINK_AGE_ADULT;
    gSaveContext.nightFlag = 0;
    gSaveContext.skyboxTime = gSaveContext.dayTime = 0x8000;

    for (int buttonIndex = 0; buttonIndex < ARRAY_COUNT(gSaveContext.buttonStatus); buttonIndex++) {
        gSaveContext.buttonStatus[buttonIndex] = BTN_ENABLED;
    }
    gSaveContext.forceRisingButtonAlphas = 0;
    gSaveContext.nextHudVisibilityMode = 0;
    gSaveContext.hudVisibilityMode = 0;
    gSaveContext.hudVisibilityModeTimer = 0;
    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_STOP);

    gSaveContext.entranceIndex = BOOT_ENTRANCE;
    gSaveContext.respawnFlag = 0;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = ENTR_LOAD_OPENING;
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.showTitleCard = true;
    gWeatherMode = 0;

    logoState->running = false;
    SET_NEXT_GAMESTATE(logoState, Play_Init, PlayState);
}

// Runs every newline-terminated line appended to the command file since the last poll. Stops early
// when a command changes the game state underneath us (scene transition, quit) so the remaining
// lines run once the next scene is ready.
void ConsumeCommands() {
    const std::string path = CommandPath();
    std::error_code ec;
    const auto size = static_cast<std::streamoff>(std::filesystem::file_size(path, ec));
    if (ec) {
        return;
    }
    if (size < sConsumedBytes) {
        sConsumedBytes = 0; // file was truncated or replaced; start over
    }
    if (size == sConsumedBytes) {
        return;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return;
    }
    in.seekg(sConsumedBytes);

    std::string line;
    while (std::getline(in, line)) {
        const std::streamoff lineEnd =
            in.tellg() == std::streampos(-1) ? size : static_cast<std::streamoff>(in.tellg());
        const bool firstLineOfFile = sConsumedBytes == 0;
        sConsumedBytes = lineEnd;

        if (firstLineOfFile && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3); // UTF-8 BOM from PowerShell's -Encoding utf8
        }
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::string output;
        int32_t rc;
        auto console = Ship::Context::GetRawInstance()->GetConsole();
        // Console::Run returns 0 for an unknown command as well as for success; tell them apart here.
        if (!console->HasCommand(line.substr(0, line.find(' ')))) {
            rc = -1;
            output = "unknown command";
        } else {
            rc = console->Run(line, &output);
        }
        WriteMarker("cmd " + line + " rc=" + std::to_string(rc) + " out=" + SingleLine(output));

        if (!InNormalPlay() || !sReady || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF || sInputFramesLeft > 0) {
            break;
        }
    }
}

// Per-cell static-collision list lengths (sturdy-bassoon#26). The subdivision grid is uniform but
// scene content is not: a settlement drops hundreds of polys into one or two cells, and every
// query landing there walks that cell's lists linearly - so the scene-wide nodes= total can look
// healthy while the worst cell is pathological. Computed once per scene load (the static lookup is
// immutable after BgCheck_Allocate builds it) and carried on every perf line alongside nodes=.
constexpr uint32_t AGENT_SS_NULL = 0xFFFFFFFF; // z_bgcheck.c's SS_NULL (widened by sturdy-bassoon#22),
                                               // which is file-local there
int16_t sCellStatsScene = -1; // sceneNum the cached stats were computed for
uint32_t sCellMax = 0;        // longest per-cell node count, floor+wall+ceiling lists summed
uint32_t sCellP95 = 0;        // 95th percentile over occupied cells (empty cells cost queries nothing)
uint32_t sCellsOccupied = 0;
uint32_t sCellsTotal = 0;

uint32_t CellListLength(const CollisionContext& colCtx, uint32_t head) {
    uint32_t n = 0;
    uint32_t idx = head;
    // Bounded by the pool's used-node count so a corrupt list terminates instead of hanging.
    while (idx != AGENT_SS_NULL && n <= colCtx.polyNodes.count) {
        n++;
        idx = colCtx.polyNodes.tbl[idx].next;
    }
    return n;
}

void RefreshCellStats() {
    const CollisionContext& colCtx = gPlayState->colCtx;
    const uint32_t total = static_cast<uint32_t>(colCtx.subdivAmount.x) *
                           static_cast<uint32_t>(colCtx.subdivAmount.y) *
                           static_cast<uint32_t>(colCtx.subdivAmount.z);
    std::vector<uint32_t> occupied;
    occupied.reserve(total);
    uint32_t maxLen = 0;
    for (uint32_t i = 0; i < total; i++) {
        const StaticLookup& cell = colCtx.lookupTbl[i];
        const uint32_t n = CellListLength(colCtx, cell.floor.head) + CellListLength(colCtx, cell.wall.head) +
                           CellListLength(colCtx, cell.ceiling.head);
        if (n == 0) {
            continue;
        }
        occupied.push_back(n);
        maxLen = std::max(maxLen, n);
    }
    std::sort(occupied.begin(), occupied.end());
    sCellMax = maxLen;
    // Index of the ceil(0.95 * n)-th smallest value, i.e. at least 95% of occupied cells are at
    // or under this length.
    sCellP95 = occupied.empty() ? 0 : occupied[(occupied.size() * 95 + 99) / 100 - 1];
    sCellsOccupied = static_cast<uint32_t>(occupied.size());
    sCellsTotal = total;
}

// Resident actors, summed over the twelve actor categories. This deliberately does not read
// actorCtx.total, which was a u8 and wrapped past 255 (sturdy-bassoon#45 has since widened it to
// s32, so the two now agree - the same Actor_AddToCategory/Actor_RemoveFromCategory pair maintains
// both). Summing the s32 list lengths keeps the field honest without depending on that fix.
// Density experiments (sturdy-bassoon#6) need every figure to carry the actor count it was
// measured at, so this rides the perf line.
uint32_t ResidentActors() {
    uint32_t n = 0;
    for (size_t i = 0; i < ARRAY_COUNT(gPlayState->actorCtx.actorLists); i++) {
        n += static_cast<uint32_t>(gPlayState->actorCtx.actorLists[i].length);
    }
    return n;
}

// Resident (working set) memory in MB, or -1 where unsupported.
double ResidentMemoryMb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
#endif
    return -1.0;
}

// fps/ms are ImGui's rolling render framerate - capped by the FPS setting, so they only show
// trouble once it is already bad. tick_ms is the game tick's CPU time over this interval (avg and
// max): the number that moves with actor count and collision density. Add experiment-specific
// fields here (e.g. BgCheck timers) rather than inventing a second channel.
void EmitPerf() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    const float fps = ImGui::GetIO().Framerate;
    const float ms = fps > 0.0f ? 1000.0f / fps : 0.0f;
    const double tickAvg = sTickSamples > 0 ? sTickSumMs / sTickSamples : 0.0;
    // Static-collision node table occupancy: how many SSNodes this scene's lookup build consumed
    // out of the budget BgCheck_Allocate derived. The measurement behind the node ceiling work
    // (sturdy-bassoon#22); fixed for the life of a scene load, so any perf line carries it.
    const SSNodeList& nodes = gPlayState->colCtx.polyNodes;
    // Same lifetime as nodes=: fixed once the lookup is built, so refresh only on a scene change.
    // A same-scene reload rebuilds an identical table, so keying on sceneNum is enough.
    if (sCellStatsScene != gPlayState->sceneNum) {
        RefreshCellStats();
        sCellStatsScene = gPlayState->sceneNum;
    }

    // Zelda heap free/total, in KB. This is what BgCheck's tables actually compete with: Play_Init
    // carves them out of the two-headed arena first and then hands the *entire* remainder to
    // ZeldaArena_Init (z_play.c), so THA_GetSize is 0 from then on and there is no "arena
    // headroom" to read. Growing collision does not hit an arena wall - it shrinks the heap that
    // actors and objects allocate from, which is the number worth watching.
    u32 heapMaxFree;
    u32 heapFree;
    u32 heapAlloc;
    ZeldaArena_GetSizes(&heapMaxFree, &heapFree, &heapAlloc);
    // fog= is the live fog band / far clip (whether scene-driven or overridden by "agenttest fog"),
    // cap= the fps target the render loop is actually holding to - GetInterpolationFPS applies the
    // vsync and MatchRefreshRate clamps, so a fps reading only counts as uncapped when cap= (and
    // the display class) sit well above it. Both exist so a perf line carries the config it was
    // measured under instead of relying on the run README remembering it (sturdy-bassoon#33).
    // bld= is the build tier the exe was compiled at: "dbg" (/Od, asserts live) or "rel" (/O2,
    // NDEBUG). The two tiers are not comparable at all - Release is the only honest timing build -
    // so the tier rides on the line for the same reason cap= does (sturdy-bassoon#40).
#ifdef NDEBUG
    const char* const buildTier = "rel";
#else
    const char* const buildTier = "dbg";
#endif
    // Render-side counters from the Fast3D interpreter (libultraship, sturdy-bassoon#40). Nothing
    // else here measures submission: ms= is 1000/ImGui's framerate and stops moving at the fps
    // cap, and tick_ms= ends before the display list is walked.
    //
    // sub_ms is the mean wall time one *rendered* frame spent inside Interpreter::Run - the
    // display-list walk plus every draw it submits. The mean is over rendered frames, of which a
    // game tick produces several (RunCommands loops InterpolationFPS/20 times over the same
    // display list), so it is not a per-tick figure and does not sum with tick_ms.
    //
    // draws=/tris=/flushes= describe the last completed rendered frame rather than an interval
    // mean, which keeps tris= directly comparable to a scene's known triangle count. draws=
    // counts the flushes that actually issued geometry (one DrawTriangles each, so: GPU draw
    // calls); flushes= counts every Flush(), so the gap between the two is state-change churn
    // that submitted nothing. Which of tris= and draws= dominates sub_ms is the question that
    // decides whether geometry caching or batching is the useful lever.
    //
    // draws_baked=/tris_baked= are the subset of that frame's draws which replayed a pre-recorded
    // static mesh instead of walking a display list (sturdy-bassoon#40 Stage 1; zero unless
    // SOH_STATIC_BAKE=1). tris_baked is *not* included in tris: baked geometry never reaches
    // GfxSpTri1, so it is neither CPU-culled nor counted there, which is exactly why turning the
    // bake on makes tris= fall for a scene that is drawing strictly more than before.
    const Fast::PerfCounters render = Fast::PerfCountersGet();
    const uint64_t renderFrames = render.frames - sLastRenderCounters.frames;
    const double subMs = renderFrames > 0 ? (render.interpMs - sLastRenderCounters.interpMs) / renderFrames : 0.0;
    sLastRenderCounters = render;

    // actors= is the resident actor count and act_near=/act_mid=/act_far= is how the distance-tier
    // prototype classified them on the last completed frame (all zero while it is off). The actor
    // axis of sturdy-bassoon#6 Exp 2: a tick_ms figure without the actor count it was taken at is
    // not evidence of anything.
    uint32_t tierNear = 0;
    uint32_t tierMid = 0;
    uint32_t tierFar = 0;
    ActorTiers_GetCounts(&tierNear, &tierMid, &tierFar);

    // colchk_at/ac/oc = the worst single-frame occupancy of the three collider subscription lists
    // over this interval, against their fixed caps, and colchk_rej = how many subscriptions were
    // refused because a list was full (sturdy-bassoon#49). This is a *correctness* headroom, not a
    // performance one: a refused collider is absent from the next tick's collision resolution, so
    // its hits, hurts and bumps silently do not happen. peak == max on any line means colliders
    // were being dropped somewhere in the interval; colchk_rej says how many. The caps ride on the
    // line rather than living only in a doc because they are plain constants that content pressure
    // may eventually raise, and an old line has to stay readable after that.
    CollisionCheckDiagWindow colChk{};
    CollisionCheck_DiagTakeWindow(&colChk);

    // dynapoly= is the worst BgActor-slot occupancy of the interval against BG_ACTOR_MAX, and
    // dynapoly_rej= how many registrations DynaPoly_SetBgActor refused (sturdy-bassoon#55).
    // Correctness headroom like colchk: a refused actor exists and draws but has no moving
    // collision, and pre-#55 its bgId aliased the scene's static collision. Occupancy persists
    // across frames (slots free only when DynaPoly_Setup collects a deleted actor), so
    // peak==current in steady state.
    DynaPolyDiagWindow dynaPoly{};
    DynaPoly_DiagTakeWindow(&gPlayState->colCtx, &dynaPoly);

    char buf[832];
    std::snprintf(buf, sizeof(buf),
                  "perf fps=%.1f ms=%.2f sub_ms=%.2f draws=%llu tris=%llu flushes=%llu draws_baked=%llu "
                  "tris_baked=%llu tick_ms=%.2f "
                  "tick_max_ms=%.2f mem_mb=%.0f actors=%u act_near=%u act_mid=%u act_far=%u nodes=%u/%u "
                  "cell_max=%u cell_p95=%u cells=%u/%u heap_kb=%u/%u colchk_at=%d/%d colchk_ac=%d/%d "
                  "colchk_oc=%d/%d colchk_rej=%u/%u/%u dynapoly=%d/%d dynapoly_rej=%u fog=%d/%d cap=%u bld=%s "
                  "scene=%s frame=%u",
                  fps, ms, subMs, (unsigned long long)render.lastDraws, (unsigned long long)render.lastTris,
                  (unsigned long long)render.lastFlushes, (unsigned long long)render.lastDrawsBaked,
                  (unsigned long long)render.lastTrisBaked, tickAvg, sTickMaxMs, ResidentMemoryMb(), ResidentActors(),
                  tierNear, tierMid, tierFar, nodes.count,
                  nodes.max, sCellMax, sCellP95, sCellsOccupied, sCellsTotal, heapFree / 1024,
                  (heapFree + heapAlloc) / 1024, colChk.peakAT, COLLISION_CHECK_AT_MAX, colChk.peakAC,
                  COLLISION_CHECK_AC_MAX, colChk.peakOC, COLLISION_CHECK_OC_MAX, colChk.rejectedAT,
                  colChk.rejectedAC, colChk.rejectedOC, dynaPoly.peak, BG_ACTOR_MAX, dynaPoly.rejected,
                  gPlayState->lightCtx.fogNear, gPlayState->lightCtx.fogFar,
                  OTRGlobals::Instance->GetInterpolationFPS(), buildTier, Hex(gPlayState->sceneNum).c_str(),
                  gPlayState->state.frames);
    WriteMarker(buf);
    sTickSumMs = 0.0;
    sTickMaxMs = 0.0;
    sTickSamples = 0;
}

// One line of everything relevant to "where is Link and why": position, the engine's previous-position
// anchor, velocity, background-check state, Player state machines, anim-driven-movement flags, whether a
// scene transition is pending, and the void-out respawn point. Taken before ("pre") and after ("post")
// each game tick while a trace is active, so a position change can be pinned to the half-frame it
// happened in.
void EmitTrace(const char* phase) {
    if (!InNormalPlay()) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    char buf[352];
    // lbox= is the letterbox: current bar size / animation target, per tick - the readout for
    // the hold-off before camera-requested bars start (sturdy-bassoon#42).
    std::snprintf(buf, sizeof(buf),
                  "trace %s frame=%u lbox=%u/%u pos=%.2f,%.2f,%.2f prev=%.2f,%.2f,%.2f velY=%.2f lin=%.2f bg=0x%X "
                  "floorH=%.1f sf1=0x%X sf2=0x%X sf3=0x%X anim=0x%X trans=%d rdown=%.1f,%.1f,%.1f rdent=%s",
                  phase, gPlayState->state.frames, ShrinkWindow_GetCurrentVal(), ShrinkWindow_GetVal(),
                  player->actor.world.pos.x, player->actor.world.pos.y,
                  player->actor.world.pos.z, player->actor.prevPos.x, player->actor.prevPos.y,
                  player->actor.prevPos.z, player->actor.velocity.y, player->linearVelocity,
                  static_cast<unsigned>(player->actor.bgCheckFlags), player->actor.floorHeight,
                  static_cast<unsigned>(player->stateFlags1), static_cast<unsigned>(player->stateFlags2),
                  static_cast<unsigned>(player->stateFlags3),
                  static_cast<unsigned>(player->skelAnime.movementFlags), gPlayState->transitionTrigger,
                  gSaveContext.respawn[RESPAWN_MODE_DOWN].pos.x, gSaveContext.respawn[RESPAWN_MODE_DOWN].pos.y,
                  gSaveContext.respawn[RESPAWN_MODE_DOWN].pos.z,
                  Hex(gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex).c_str());
    WriteMarker(buf);
}

void OnGameFrameUpdateAgentTest() {
    GameState* logoState = sLogoState;
    sLogoState = nullptr;
    sTickCounter++;
    if (sTickStarted) {
        sTickStarted = false;
        const double tickMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sTickStart).count();
        sTickSumMs += tickMs;
        sTickMaxMs = std::max(sTickMaxMs, tickMs);
        sTickSamples++;
    }

    // Console logo, first tick only: the one getenv a non-agent session ever pays. It has to be the
    // first tick because a BootSequence of FileSelect or DebugWarpScreen ends the logo state right
    // there. This runs after every OnZTitleUpdate hook of the tick, so setting the
    // next game state here wins over whichever one they set.
    if (logoState != nullptr && gPlayState == nullptr) {
        static bool decided = false;
        if (!decided) {
            decided = true;
            sAgentMode = AgentModeRequested();
            if (sAgentMode) {
                WriteMarker("session pid=" + std::to_string(AGENTTEST_GETPID()));
                BootToPlay(logoState);
            }
        }
        return;
    }

    if (!sAgentMode || !InNormalPlay() || !sReady) {
        return;
    }
    // Trigger events from the distance-based room prototype (sturdy-bassoon#6 Exp 4). Polled here
    // rather than written from RoomDist.cpp so the marker channel stays this file's, which is also
    // why RoomDist needs no file I/O of its own. At most one event per tick, and this runs every
    // tick, so nothing queues.
    {
        char event[256];
        if (RoomDist_TakeEvent(event, sizeof(event))) {
            WriteMarker(std::string("roomdist ") + event);
        }
    }
    const int16_t room = gPlayState->roomCtx.curRoom.num;
    if (room != sLastRoom) {
        if (sLastRoom >= 0) {
            // pos= is where Link stood on the tick the room actually changed - the figure a
            // transition-trigger comparison needs, since latency is measured in units from the
            // boundary rather than in ticks (sturdy-bassoon#6 Exp 4).
            Player* player = GET_PLAYER(gPlayState);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "room_changed from=%d to=%d frame=%u pos=%.1f,%.1f,%.1f", sLastRoom,
                          room, gPlayState->state.frames, player->actor.world.pos.x, player->actor.world.pos.y,
                          player->actor.world.pos.z);
            WriteMarker(buf);
        }
        sLastRoom = room;
    }
    if (sPerfInterval > 0 && sTickCounter % static_cast<uint32_t>(sPerfInterval) == 0) {
        EmitPerf();
    }
    if (sTickCounter % POLL_INTERVAL == 0 && sInputFramesLeft == 0) {
        ConsumeCommands();
    }
    // After command consumption, so the tick a goto lands in shows the freshly written position.
    if (sTraceTicksLeft > 0 && InNormalPlay()) {
        EmitTrace("post");
        sTraceTicksLeft--;
    }
}

// Fires after the pad data for this tick was read and before Player_Update consumes it.
void OnGameStateMainStartAgentTest() {
    if (!sAgentMode) {
        return;
    }
    // The trace write goes *before* the tick clock starts. It appends a line to two files, which is
    // tens of microseconds - small, but it was landing inside the timed window and inflating
    // tick_ms/tick_max_ms by ~0.1-0.15 ms on Release, where an ordinary tick is 0.4-0.5 ms. Found
    // while measuring room-transition hitches (sturdy-bassoon#6 Exp 4), where a trace-armed
    // crossing read high for a reason that had nothing to do with the crossing. The "post" trace
    // is already outside the window (OnGameFrameUpdate reads the clock before emitting anything).
    if (sTraceTicksLeft > 0) {
        EmitTrace("pre");
    }
    sTickStart = std::chrono::steady_clock::now();
    sTickStarted = true;
    if (sInputFramesLeft <= 0 || !InNormalPlay()) {
        return;
    }
    // Only frames Player_Update will actually read count: paused or mid-transition the stick is ignored.
    if (gPlayState->pauseCtx.state != 0 || gPlayState->pauseCtx.debugState != 0 ||
        gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) {
        return;
    }
    if (sDeferredButtons != 0 && sInputFramesLeft == sDeferredAtFramesLeft) {
        sInputButtons |= sDeferredButtons;
        sInputPressPending = true;
        sDeferredButtons = 0;
    }
    Input* input = &gPlayState->state.input[0];
    input->cur.stick_x = sInputStickX;
    input->cur.stick_y = sInputStickY;
    PadUtils_UpdateRelXY(input); // same dead zone and clamp a real pad gets
    input->cur.button |= sInputButtons;
    if (sInputPressPending) {
        input->press.button |= sInputButtons;
        sInputPressPending = false;
    }
    sInputFramesLeft--;
    if (sInputFramesLeft == 0) {
        WriteMarker("input_done");
    }
}

void CancelInput(const char* reason) {
    if (sInputFramesLeft > 0) {
        sInputFramesLeft = 0;
        sInputPressPending = false;
        sDeferredButtons = 0;
        WriteMarker(std::string("input_done reason=") + reason);
    }
}

void StartInput(int32_t frames, int8_t stickX, int8_t stickY, uint16_t buttons, uint16_t deferredButtons = 0,
                int32_t deferredAtFrame = 0) {
    sInputStickX = stickX;
    sInputStickY = stickY;
    sInputButtons = buttons;
    sInputPressPending = buttons != 0;
    // deferredAtFrame is 1-based from the start of the injection; convert to the frames-left
    // value OnGameStateMainStart counts down through.
    sDeferredButtons = deferredButtons;
    sDeferredAtFramesLeft = frames - (deferredAtFrame - 1);
    sInputFramesLeft = frames;
}

// "A,Z,CUP" -> button mask; returns false on an unknown name.
bool ParseButtons(const std::string& text, uint16_t* mask) {
    static const std::vector<std::pair<const char*, uint16_t>> names = {
        { "A", BTN_A },         { "B", BTN_B },           { "Z", BTN_Z },     { "R", BTN_R },
        { "L", BTN_L },         { "START", BTN_START },   { "DUP", BTN_DUP }, { "DDOWN", BTN_DDOWN },
        { "DLEFT", BTN_DLEFT }, { "DRIGHT", BTN_DRIGHT }, { "CUP", BTN_CUP }, { "CDOWN", BTN_CDOWN },
        { "CLEFT", BTN_CLEFT }, { "CRIGHT", BTN_CRIGHT },
    };
    *mask = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(',', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string name = text.substr(start, end - start);
        for (char& c : name) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        bool found = false;
        for (const auto& [candidate, bit] : names) {
            if (name == candidate) {
                *mask |= bit;
                found = true;
            }
        }
        if (!found) {
            return false;
        }
        start = end + 1;
    }
    return true;
}

// Whole-string decimal integer; "40abc", "0x4000" and "" are rejected.
bool ParseInt(const std::string& text, int32_t* value) {
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        *value = parsed;
        return true;
    } catch (...) { return false; }
}

// Whole-string integer in 0..65535, decimal or 0x-prefixed hex - dayTime values are quoted in hex
// everywhere. The base is picked explicitly: base 0 would silently read a leading zero as octal.
bool ParseU16(const std::string& text, int32_t* value) {
    try {
        const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
        size_t consumed = 0;
        const long parsed = std::stol(hex ? text.substr(2) : text, &consumed, hex ? 16 : 10);
        if (consumed != (hex ? text.size() - 2 : text.size()) || parsed < 0 || parsed > 0xFFFF) {
            return false;
        }
        *value = static_cast<int32_t>(parsed);
        return true;
    } catch (...) { return false; }
}

// Whole-string integer in 0..0xFFFFFFFF, decimal or 0x-prefixed hex. Same explicit-base reasoning
// as ParseU16; wider because a scene flag word is 32 bits and tests want to write a recognisable
// one like 0xDEADBEEF.
bool ParseU32(const std::string& text, uint32_t* value) {
    try {
        const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
        size_t consumed = 0;
        const unsigned long long parsed = std::stoull(hex ? text.substr(2) : text, &consumed, hex ? 16 : 10);
        if (consumed != (hex ? text.size() - 2 : text.size()) || parsed > 0xFFFFFFFFull) {
            return false;
        }
        *value = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) { return false; }
}

bool ParseFloat(const std::string& text, float* value) {
    try {
        size_t consumed = 0;
        const float parsed = std::stof(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        *value = parsed;
        return true;
    } catch (...) { return false; }
}

// Optional argument: absent -> fallback, present but malformed -> false.
bool ParseIntArg(const std::vector<std::string>& args, size_t index, int32_t fallback, int32_t* value) {
    if (index >= args.size()) {
        *value = fallback;
        return true;
    }
    return ParseInt(args[index], value);
}

void OnZTitleUpdateAgentTest(void* gameState) {
    sLogoState = static_cast<GameState*>(gameState);
}

void OnSceneInitAgentTest(int16_t sceneNum) {
    sReady = false;
    sLastRoom = -1;
    CancelInput("scene_change");
    if (sAgentMode) {
        WriteMarker("scene_loaded scene=" + Hex(sceneNum) + " entrance=" + Hex(gSaveContext.entranceIndex));
    }
}

void OnPlayerUpdateAgentTest() {
    if (sReady || !InNormalPlay()) {
        return;
    }
    sReady = true;
    if (sAgentMode) {
        WriteMarker("ready scene=" + Hex(gPlayState->sceneNum) + " entrance=" + Hex(gSaveContext.entranceIndex));
    }
}

// Requires InNormalPlay().
std::string StateLine() {
    Player* player = GET_PLAYER(gPlayState);
    Camera* camera = GET_ACTIVE_CAM(gPlayState);
    // The fixed fields run to about 230 characters, so this leaves ~280 for the scene name -
    // several times the longest the scene table can produce. snprintf truncates rather than
    // overflows either way; the headroom is so the name is not what gets truncated.
    char buf[512];
    // `name` last, and quoted, because it is the only field that can contain a space - so anything
    // splitting the line on whitespace still gets every other field intact.
    std::snprintf(buf, sizeof(buf),
                  "state scene=%s room=%d entrance=%s pos=%.1f,%.1f,%.1f yaw=%d age=%s time=%s night=%d "
                  "rupees=%d rupees_pending=%d frame=%u "
                  "cam_at=%.1f,%.1f,%.1f cam_eye=%.1f,%.1f,%.1f cam_setting=%d cam_mode=%d cam_dist=%.1f "
                  "name=\"%s\"",
                  Hex(gPlayState->sceneNum).c_str(), gPlayState->roomCtx.curRoom.num,
                  Hex(gSaveContext.entranceIndex).c_str(), player->actor.world.pos.x, player->actor.world.pos.y,
                  player->actor.world.pos.z, player->actor.shape.rot.y,
                  gSaveContext.linkAge == LINK_AGE_CHILD ? "child" : "adult", Hex(gSaveContext.dayTime).c_str(),
                  gSaveContext.nightFlag, gSaveContext.rupees, gSaveContext.rupeeAccumulator, gPlayState->state.frames,
                  camera->at.x, camera->at.y, camera->at.z,
                  camera->eye.x, camera->eye.y, camera->eye.z, camera->setting, camera->mode, camera->dist,
                  SohUtils::GetSceneName(gPlayState->sceneNum).c_str());
    return buf;
}

// The shared tail of every subcommand that changes or reports where Link is: emit a state marker
// and mirror it into the command's output.
int32_t EmitState(std::string* output) {
    const std::string line = StateLine();
    WriteMarker(line);
    if (output) {
        *output += line;
    }
    return 0;
}

int32_t AgentTestCommand(std::shared_ptr<Ship::Console> console, const std::vector<std::string>& args,
                         std::string* output) {
    if (args.size() >= 3 && args[1] == "perf") {
        try {
            sPerfInterval = std::stoi(args[2]);
        } catch (...) {
            if (output) {
                *output += "perf interval must be an integer number of game ticks";
            }
            return 1;
        }
        if (output) {
            *output += "perf interval set to " + std::to_string(sPerfInterval) + " ticks";
        }
        return 0;
    }
    if (args.size() >= 2 &&
        (args[1] == "state" || args[1] == "goto" || args[1] == "walk" || args[1] == "press" || args[1] == "rooms" ||
         args[1] == "time" || args[1] == "trace" || args[1] == "fog" || args[1] == "uncull") &&
        !InNormalPlay()) {
        if (output) {
            *output += "no scene loaded";
        }
        return 1;
    }
    // ORs ACTOR_FLAG_UPDATE_CULLING_DISABLED onto every resident actor except the player
    // (sturdy-bassoon#50). Most props evaluate their AC/OC distance gates only when their update
    // runs, and their updates are camera-frustum-culled - so a measurement ring's subscription
    // count would otherwise depend on where the camera points. This removes exactly that
    // confound; the distance gates themselves still apply. One-shot over actors alive right now:
    // re-issue after every spawn batch. Nothing persists - a scene load spawns fresh actors
    // without the flag.
    if (args.size() >= 2 && args[1] == "uncull") {
        uint32_t n = 0;
        for (size_t i = 0; i < ARRAY_COUNT(gPlayState->actorCtx.actorLists); i++) {
            if (i == ACTORCAT_PLAYER) {
                continue;
            }
            for (Actor* actor = gPlayState->actorCtx.actorLists[i].head; actor != nullptr; actor = actor->next) {
                actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
                n++;
            }
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "uncull n=%u", n);
        WriteMarker(buf);
        if (output) {
            *output += buf;
        }
        return 0;
    }
    if (args.size() >= 2 && args[1] == "state") {
        return EmitState(output);
    }
    if (args.size() >= 5 && args[1] == "goto") {
        Player* player = GET_PLAYER(gPlayState);
        Vec3f pos;
        int32_t yaw = player->actor.shape.rot.y;
        if (!ParseFloat(args[2], &pos.x) || !ParseFloat(args[3], &pos.y) || !ParseFloat(args[4], &pos.z) ||
            !ParseIntArg(args, 5, yaw, &yaw) || yaw < -32768 || yaw > 32767) {
            if (output) {
                *output += "goto needs numeric x y z and an optional yaw in -32768..32767";
            }
            return 1;
        }
        player->actor.world.pos = pos;
        // Writing world.pos alone is not a teleport: the engine sweeps the wall check along
        // prevPos -> world.pos as if Link moved there in one frame (BgCheck_CheckWallImpl), clamping
        // him to the first poly that line crosses. The hookshot landing syncs prevPos for the same
        // reason (Player_Action_80850AEC); the player needs home.pos moved too, because
        // Player_UpdateCommon rewrites prevPos from home.pos at the top of every update, and
        // fallStartHeight so a large Y jump does not read as an ongoing fall.
        player->actor.prevPos = pos;
        player->actor.home.pos = pos;
        player->fallStartHeight = static_cast<int16_t>(pos.y);
        if (args.size() >= 6) {
            player->actor.world.rot.y = static_cast<int16_t>(yaw);
            player->actor.shape.rot.y = static_cast<int16_t>(yaw);
            player->yaw = static_cast<int16_t>(yaw);
        }
        player->linearVelocity = 0.0f;
        return EmitState(output);
    }
    if (args.size() >= 3 && args[1] == "walk") {
        int32_t frames = 0;
        int32_t sx = 0;
        int32_t sy = 0;
        if (!ParseInt(args[2], &frames) || frames <= 0 || frames > MAX_INPUT_FRAMES || !ParseIntArg(args, 3, 0, &sx) ||
            !ParseIntArg(args, 4, 80, &sy)) {
            if (output) {
                *output += "walk needs frames in 1.." + std::to_string(MAX_INPUT_FRAMES) + " and integer stick values";
            }
            return 1;
        }
        sx = std::clamp(sx, -85, 85);
        sy = std::clamp(sy, -85, 85);
        // Optional mid-walk press: [buttons] [at_frame]. The buttons land with a fresh press edge on
        // the at_frame-th injected frame (1-based, default 1) and stay held to the end of the walk -
        // "walk 80 0 80 A 40" is a roll at full run speed, which sequential walk-then-press cannot do.
        uint16_t deferredMask = 0;
        int32_t deferredAt = 1;
        if (args.size() >= 6) {
            if (!ParseButtons(args[5], &deferredMask)) {
                if (output) {
                    *output += "unknown button; use A,B,Z,R,L,START,DUP,DDOWN,DLEFT,DRIGHT,CUP,CDOWN,CLEFT,CRIGHT";
                }
                return 1;
            }
            if (!ParseIntArg(args, 6, 1, &deferredAt) || deferredAt < 1 || deferredAt > frames) {
                if (output) {
                    *output += "walk press frame must be 1..frames";
                }
                return 1;
            }
        }
        StartInput(frames, static_cast<int8_t>(sx), static_cast<int8_t>(sy), 0, deferredMask, deferredAt);
        if (output) {
            *output += "walking " + std::to_string(frames) + " frames, stick " + std::to_string(sx) + "," +
                       std::to_string(sy);
            if (deferredMask != 0) {
                *output += ", pressing " + args[5] + " at frame " + std::to_string(deferredAt);
            }
            *output += "; wait for input_done";
        }
        return 0;
    }
    if (args.size() >= 3 && args[1] == "press") {
        uint16_t mask = 0;
        if (!ParseButtons(args[2], &mask)) {
            if (output) {
                *output += "unknown button; use A,B,Z,R,L,START,DUP,DDOWN,DLEFT,DRIGHT,CUP,CDOWN,CLEFT,CRIGHT";
            }
            return 1;
        }
        int32_t frames = 2;
        if (!ParseIntArg(args, 3, 2, &frames) || frames < 1 || frames > MAX_INPUT_FRAMES) {
            if (output) {
                *output += "press frames must be 1.." + std::to_string(MAX_INPUT_FRAMES);
            }
            return 1;
        }
        StartInput(frames, 0, 0, mask);
        if (output) {
            *output += "holding " + args[2] + " for " + std::to_string(frames) + " frames; wait for input_done";
        }
        return 0;
    }
    if (args.size() >= 2 && args[1] == "rooms") {
        // The scene's transition actors: where room boundaries are and which rooms they join.
        const TransitionActorContext& ctx = gPlayState->transiActorCtx;
        for (int i = 0; i < ctx.numActors; i++) {
            const TransitionActorEntry& t = ctx.list[i];
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "transition idx=%d id=0x%04X rooms=%d,%d pos=%d,%d,%d rotY=%d params=0x%04X", i,
                          static_cast<unsigned>(t.id) & 0xFFFF, t.sides[0].room, t.sides[1].room, t.pos.x, t.pos.y,
                          t.pos.z, t.rotY, static_cast<unsigned>(t.params) & 0xFFFF);
            WriteMarker(buf);
        }
        if (output) {
            *output += std::to_string(ctx.numActors) + " transition actors in room list; see transition markers";
        }
        return 0;
    }
    if (args.size() >= 3 && args[1] == "time") {
        static const std::vector<std::pair<const char*, int32_t>> presets = {
            { "dawn", 0x4000 }, { "day", 0x8000 }, { "dusk", 0xC001 }, { "night", 0x0000 }
        };
        int32_t value = -1;
        for (const auto& [name, presetValue] : presets) {
            if (args[2] == name) {
                value = presetValue;
            }
        }
        if (value < 0 && !ParseU16(args[2], &value)) {
            if (output) {
                *output += "time needs dawn|day|dusk|night or a value in 0..65535 (0x-hex ok)";
            }
            return 1;
        }
        gSaveContext.skyboxTime = gSaveContext.dayTime = static_cast<u16>(value);
        // The same threshold Environment_Update re-derives nightFlag from every frame; set it here too so
        // anything reading IS_NIGHT this frame agrees with the new clock.
        gSaveContext.nightFlag = (gSaveContext.dayTime > 0xC000 || gSaveContext.dayTime < 0x4555) ? 1 : 0;
        return EmitState(output);
    }
    if (args.size() >= 3 && args[1] == "trace") {
        int32_t ticks = 0;
        if (!ParseInt(args[2], &ticks) || ticks < 0 || ticks > MAX_TRACE_TICKS) {
            if (output) {
                *output += "trace needs a tick count in 0.." + std::to_string(MAX_TRACE_TICKS) + " (0 cancels)";
            }
            return 1;
        }
        sTraceTicksLeft = ticks;
        if (output) {
            *output += "tracing " + std::to_string(ticks) + " ticks (pre/post markers per tick)";
        }
        return 0;
    }
    // Scene entry is either plain or a cutscene entry, and the two want different letterbox
    // behaviour (sturdy-bassoon#43) - but every entrance cutscene in sEntranceCutsceneTable is
    // gated on an EventChkInf flag the debug save already has set, so no console warp can reach
    // one. Play_Init reads nextCutsceneIndex into cutsceneIndex and treats >= 0xFFF0 as "this
    // entry is a cutscene", which is the switch this writes. Deliberately not gated on
    // InNormalPlay - it only touches the save context, and applies to the next load either way.
    if (args.size() >= 3 && args[1] == "cutscene") {
        if (args[2] == "off") {
            // Both halves. nextCutsceneIndex is the queue Play_Init reads - but a cutscene left
            // early (warping out of one) never clears cutsceneIndex, and Play_Init would still
            // read that as a cutscene entry on the next load.
            gSaveContext.nextCutsceneIndex = CUTSCENE_INDEX_NONE;
            gSaveContext.cutsceneIndex = 0;
            if (output) {
                *output += "next scene load enters plain";
            }
            return 0;
        }
        int32_t index = 0;
        if (!ParseU16(args[2], &index) || (index > CUTSCENE_LAYER_MAX && index < CUTSCENE_INDEX_FIRST)) {
            if (output) {
                *output += "cutscene needs a layer in 0..15, an index in 0xFFF0..0xFFFF, or off";
            }
            return 1;
        }
        if (index <= CUTSCENE_LAYER_MAX) {
            index += CUTSCENE_INDEX_FIRST;
        }
        gSaveContext.nextCutsceneIndex = static_cast<u16>(index);
        if (output) {
            *output += "next scene load enters with cutsceneIndex=" + Hex(index);
        }
        return 0;
    }
    // Fog-band / far-clip override for draw-cost experiments (sturdy-bassoon#33): lightCtx.fogFar is
    // both where fog saturates and the far plane Play_Draw builds the perspective from, so pulling it
    // in un-draws everything past it - the rasterization share of draw cost - while the submitted
    // display list stays identical. Implemented on the engine's own reg-editor path rather than by
    // writing lightCtx directly, which Environment_Update would overwrite next frame: while
    // R_ENV_DISABLE_DBG is false, Environment_Update copies fog (and the light colours it mirrored
    // into the regs while scene control was live) *from* the regs instead of the scene's light
    // settings. Environment_Init sets R_ENV_DISABLE_DBG back to true on every scene load, so the
    // override never leaks past an entrance - and must be re-applied after one.
    if (args.size() >= 3 && args[1] == "fog") {
        if (args[2] == "off") {
            R_ENV_DISABLE_DBG = true;
            WriteMarker("fog mode=scene");
            if (output) {
                *output += "fog and far clip handed back to the scene's light settings";
            }
            return 0;
        }
        int32_t fogNear = 0;
        int32_t fogFar = 0;
        if (args.size() < 4 || !ParseInt(args[2], &fogNear) || !ParseInt(args[3], &fogFar) || fogNear < 0 ||
            fogNear > FOG_NEAR_MAX || fogFar < FOG_FAR_MIN || fogFar > FOG_FAR_MAX) {
            if (output) {
                *output += "fog needs <near 0.." + std::to_string(FOG_NEAR_MAX) + "> <far " +
                           std::to_string(FOG_FAR_MIN) + ".." + std::to_string(FOG_FAR_MAX) +
                           ">, or off. near is fog-space (996 = fog collapsed to the far plane); far is world "
                           "units and the far clip";
            }
            return 1;
        }
        R_ENV_FOG_NEAR = static_cast<s16>(fogNear);
        R_ENV_FOG_FAR = static_cast<s16>(fogFar);
        R_ENV_DISABLE_DBG = false;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "fog mode=override near=%d far=%d", fogNear, fogFar);
        WriteMarker(buf);
        if (output) {
            *output += buf;
        }
        return 0;
    }
    // Distance-tiered actor updating (sturdy-bassoon#6 Exp 2). A console subcommand rather than a
    // CVar or a rebuild, so every tier configuration can be A/B'd inside one game session against
    // one scene load - which is the only way the differences here (tenths of a millisecond on
    // Release) are measurable at all. "off" restores the vanilla path exactly.
    //
    //   agenttest tiers <near> <mid> <n> [mitb] [drawcull]
    //
    // near/mid are XZ world-unit radii (near = full update, near..mid = update every nth frame
    // staggered per actor, beyond mid = skipped); n is that period. mitb arms mitigation (b): the
    // shared SkelAnime and velocity-integration paths scale by the skipped-tick count so throttled
    // vanilla actors keep wall-clock-correct speed. drawcull additionally skips the per-actor
    // draw-pass work beyond the mid radius, which is a different cost from anything in
    // Actor_UpdateAll and is measured separately.
    if (args.size() >= 2 && args[1] == "tiers") {
        if (args.size() >= 3 && args[2] == "off") {
            ActorTiers_Disable();
            WriteMarker(ActorTiers_Describe());
            if (output) {
                *output += ActorTiers_Describe();
            }
            return 0;
        }
        float nearRadius = 0.0f;
        float midRadius = 0.0f;
        int32_t period = 0;
        int32_t mitigateB = 0;
        int32_t drawCull = 0;
        if (args.size() < 5 || !ParseFloat(args[2], &nearRadius) || !ParseFloat(args[3], &midRadius) ||
            !ParseInt(args[4], &period) || !ParseIntArg(args, 5, 0, &mitigateB) ||
            !ParseIntArg(args, 6, 0, &drawCull) || ActorTiers_Configure(nearRadius, midRadius, period, mitigateB,
                                                                       drawCull) != 0) {
            if (output) {
                *output += "tiers needs <near radius> <mid radius> <n 1..60> [mitb 0|1] [drawcull 0|1], "
                           "with mid >= near >= 0; or off";
            }
            return 1;
        }
        WriteMarker(ActorTiers_Describe());
        if (output) {
            *output += ActorTiers_Describe();
        }
        return 0;
    }
    // Distance-based room selection (sturdy-bassoon#6 Exp 4). Same reasoning as `tiers`: a console
    // subcommand rather than a CVar or a rebuild, so both triggers can be walked in one session
    // against one scene load. While armed, En_Holl's planes stand down (see EnHoll_Update), so this
    // is an either/or rather than a both.
    //
    //   agenttest roomdist [hysteresis] | off
    //
    // hysteresis is how much closer (world units) the candidate room's centre must be before the
    // change is taken; the switch then lands hysteresis/2 units past the boundary. Default 0 is the
    // naive nearest-centre rule, which is the configuration worth measuring for flapping first.
    if (args.size() >= 2 && args[1] == "roomdist") {
        if (args.size() >= 3 && args[2] == "off") {
            RoomDist_Disable();
            WriteMarker(RoomDist_Describe());
            if (output) {
                *output += RoomDist_Describe();
            }
            return 0;
        }
        if (!InNormalPlay()) {
            if (output) {
                *output += "no scene loaded";
            }
            return 1;
        }
        float hysteresis = 0.0f;
        if ((args.size() >= 3 && !ParseFloat(args[2], &hysteresis)) || hysteresis < 0.0f) {
            if (output) {
                *output += "roomdist needs an optional non-negative hysteresis in world units, or off";
            }
            return 1;
        }
        const int32_t rc = RoomDist_Configure(hysteresis);
        if (rc != 0) {
            if (output) {
                *output += rc == 2 ? "roomdist needs a scene whose room count is a perfect square (it derives "
                                     "an NxN grid of room centres from the collision bounds)"
                                   : "roomdist could not be armed";
            }
            return 1;
        }
        WriteMarker(RoomDist_Describe());
        if (output) {
            *output += RoomDist_Describe();
        }
        return 0;
    }
    // Reads or writes one scene's saved flag word. The point is `sceneFlags` itself: it is indexed
    // straight by scene id with no bounds check anywhere in the engine, so it silently ran off its
    // own end once custom scenes pushed the table past 124 entries (sturdy-bassoon#30). Nothing
    // else can observe these - they are not console-readable and the only in-game way to move one
    // is to open a chest - so a save round-trip cannot be tested without this.
    if (args.size() >= 3 && args[1] == "sceneflag") {
        uint32_t sceneId = 0;
        if (!ParseU32(args[2], &sceneId) || sceneId >= ARRAY_COUNT(gSaveContext.sceneFlags)) {
            if (output) {
                *output += "sceneflag needs a scene id in 0.." +
                           std::to_string(ARRAY_COUNT(gSaveContext.sceneFlags) - 1) + " (0x-hex ok)";
            }
            return 1;
        }
        SavedSceneFlags* flags = &gSaveContext.sceneFlags[sceneId];
        if (args.size() >= 4) {
            uint32_t value = 0;
            if (!ParseU32(args[3], &value)) {
                if (output) {
                    *output += "sceneflag value must be in 0..0xFFFFFFFF (0x-hex ok)";
                }
                return 1;
            }
            flags->chest = value;
            flags->swch = value;
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "sceneflag scene=0x%X chest=0x%08X swch=0x%08X slots=%d",
                      sceneId, flags->chest, flags->swch,
                      static_cast<int>(ARRAY_COUNT(gSaveContext.sceneFlags)));
        WriteMarker(buf);
        if (output) {
            *output += buf;
        }
        return 0;
    }
    // Reads or writes one project world flag (sturdy-bassoon#54). Same argument as sceneflag:
    // there are no custom actors yet, so nothing in-game can touch the store, and a save
    // round-trip cannot be demonstrated without a console-level probe.
    //   worldflag count        -> emits how many flags are set and the capacity
    //   worldflag <n>          -> reads flag n
    //   worldflag <n> <0|1>    -> clears/sets flag n, then reads it back through the real getter
    if (args.size() >= 3 && args[1] == "worldflag") {
        char buf[128];
        if (args[2] == "count") {
            std::snprintf(buf, sizeof(buf), "worldflag count=%d max=%d", WorldFlags_CountSet(), WORLD_FLAG_MAX);
            WriteMarker(buf);
            if (output) {
                *output += buf;
            }
            return 0;
        }
        int32_t flag = 0;
        if (!ParseInt(args[2], &flag) || flag < 0 || flag >= WORLD_FLAG_MAX) {
            if (output) {
                *output += "worldflag needs `count` or a flag in 0.." + std::to_string(WORLD_FLAG_MAX - 1);
            }
            return 1;
        }
        if (args.size() >= 4) {
            int32_t value = 0;
            if (!ParseInt(args[3], &value) || (value != 0 && value != 1)) {
                if (output) {
                    *output += "worldflag value must be 0 or 1";
                }
                return 1;
            }
            if (value) {
                Flags_SetWorldFlag(flag);
            } else {
                Flags_UnsetWorldFlag(flag);
            }
        }
        std::snprintf(buf, sizeof(buf), "worldflag flag=%d value=%d max=%d", flag, Flags_GetWorldFlag(flag) ? 1 : 0,
                      WORLD_FLAG_MAX);
        WriteMarker(buf);
        if (output) {
            *output += buf;
        }
        return 0;
    }
    // Reads or writes one entry of the project quest store (sturdy-bassoon#58 P0). Store-level only:
    // there are no quest definitions yet, so this is the console-level probe that lets the save
    // round-trip be demonstrated, exactly as `worldflag` was for #54. P1's `agenttest quest ...`
    // markers are the definition-aware surface; this one stays raw on purpose.
    //   queststore count                 -> touched-entry count and the capacity/bands
    //   queststore <id>                  -> reads status + step mask for quest id
    //   queststore <id> <status> <mask>  -> writes both (mask accepts 0x hex), reads back via getters
    if (args.size() >= 3 && args[1] == "queststore") {
        char buf[160];
        if (args[2] == "count") {
            std::snprintf(buf, sizeof(buf), "queststore touched=%d max=%d debug_first=%d", QuestStore_CountTouched(),
                          QUEST_MAX, QUEST_ID_DEBUG_FIRST);
            WriteMarker(buf);
            if (output) {
                *output += buf;
            }
            return 0;
        }
        int32_t questId = 0;
        if (!ParseInt(args[2], &questId) || !QUEST_ID_IS_VALID(questId)) {
            if (output) {
                *output += "queststore needs `count` or a quest id in 0.." + std::to_string(QUEST_MAX - 1);
            }
            return 1;
        }
        if (args.size() >= 4) {
            int32_t status = 0;
            uint32_t mask = 0;
            if (args.size() < 5 || !ParseInt(args[3], &status) || status < 0 || status >= QUEST_STATUS_COUNT ||
                !ParseU32(args[4], &mask)) {
                if (output) {
                    *output += "queststore <id> <status 0.." + std::to_string(QUEST_STATUS_COUNT - 1) +
                               "> <stepMask, 0x hex ok>";
                }
                return 1;
            }
            QuestStore_SetStatus(questId, status);
            QuestStore_SetStepMask(questId, mask);
        }
        std::snprintf(buf, sizeof(buf), "queststore id=%d status=%d steps=0x%08X tier=%s", questId,
                      QuestStore_GetStatus(questId), QuestStore_GetStepMask(questId),
                      Quest_TierName(QUEST_ID_TIER(questId)));
        WriteMarker(buf);
        if (output) {
            *output += buf;
        }
        return 0;
    }
    // Evaluates one predicate from the vocabulary against the live stores (sturdy-bassoon#58 P0),
    // so the five words can be proven in-game before any quest, NPC or journal uses them.
    //   questpred <kind> <a> <b> <negate>   kind: 0 Always, 1 QuestStatusIs, 2 QuestStepSet,
    //                                             3 WorldFlagSet, 4 AllStepsSet (P2)
    // AllStepsSet is the one kind whose `a` can be in range and still name a quest this build does
    // not define; it answers 0 quietly in that case, so the probe cannot assert on any input.
    if (args.size() >= 6 && args[1] == "questpred") {
        int32_t kind = 0;
        int32_t a = 0;
        int32_t b = 0;
        int32_t negate = 0;
        if (!ParseInt(args[2], &kind) || kind < 0 || kind >= QUEST_PRED_KIND_COUNT || !ParseInt(args[3], &a) ||
            !ParseInt(args[4], &b) || !ParseInt(args[5], &negate) || (negate != 0 && negate != 1)) {
            if (output) {
                *output += "questpred <kind 0.." + std::to_string(QUEST_PRED_KIND_COUNT - 1) + "> <a> <b> <negate 0|1>";
            }
            return 1;
        }
        QuestPredicate pred = { static_cast<QuestPredicateKind>(kind), a, b, static_cast<uint8_t>(negate) };
        char desc[96];
        QuestPredicate_Describe(&pred, desc, sizeof(desc));
        char buf[160];
        std::snprintf(buf, sizeof(buf), "questpred %s value=%d", desc, QuestPredicate_Eval(&pred));
        WriteMarker(buf);
        if (output) {
            *output += buf;
        }
        return 0;
    }
    // Writes gSaveContext to Save/file<n+1>.sav. In normal play this only happens at a save point
    // or an owl statue, neither of which an agent can reach - and a save that is never written
    // cannot be shown to round-trip.
    //
    // The file number is explicit rather than defaulting to gSaveContext.fileNum, which the boot
    // sequence leaves at 0xFF - and SaveManager::SaveSection returns silently for 0xFF and 0xFE
    // (debug save and boss rush), so defaulting would make this a no-op that reports success.
    //
    // The range is the save menu's three slots. Anything higher writes the .sav fine and then
    // asserts in SaveFileThreaded's InitMeta, which indexes `fileMetaInfo` - a
    // std::array<SaveFileMetaInfo, MaxFiles> - by the same number. Refused here so the message says
    // so, rather than surfacing as "array subscript out of range" from inside <array>.
    //
    // The write is threaded, so the file appears shortly after this returns rather than during it.
    if (args.size() >= 3 && args[1] == "save") {
        uint32_t fileNum = 0;
        if (!ParseU32(args[2], &fileNum) || fileNum >= SaveManager::MaxFiles) {
            if (output) {
                *output += "save needs a file number in 0.." + std::to_string(SaveManager::MaxFiles - 1) +
                           " - the save menu's slots, and what SaveManager::fileMetaInfo is sized for. "
                           "File n is written to Save/file<n+1>.sav";
            }
            return 1;
        }
        SaveManager::Instance->SaveFile(static_cast<int>(fileNum));
        char buf[128];
        std::snprintf(buf, sizeof(buf), "saving fileNum=%u -> Save/file%u.sav slots=%d (threaded)", fileNum,
                      fileNum + 1, static_cast<int>(ARRAY_COUNT(gSaveContext.sceneFlags)));
        WriteMarker(buf);
        if (output) {
            *output += buf;
        }
        return 0;
    }
    // Reads a .sav back into gSaveContext, so `sceneflag` can observe what actually survived the
    // round trip. This is the deserialisation half only - it does not reload the scene, which is
    // what makes it usable mid-run.
    if (args.size() >= 3 && args[1] == "loadsave") {
        uint32_t fileNum = 0;
        if (!ParseU32(args[2], &fileNum) || fileNum >= SaveManager::MaxFiles) {
            if (output) {
                *output += "loadsave needs a file number in 0.." + std::to_string(SaveManager::MaxFiles - 1) +
                           ", reading Save/file<n+1>.sav";
            }
            return 1;
        }
        if (!std::filesystem::exists(Ship::Context::GetPathRelativeToAppDirectory("Save") + "/file" +
                                     std::to_string(fileNum + 1) + ".sav")) {
            if (output) {
                *output += "no Save/file" + std::to_string(fileNum + 1) + ".sav to load";
            }
            return 1;
        }
        SaveManager::Instance->LoadFile(static_cast<int>(fileNum));
        char buf[128];
        std::snprintf(buf, sizeof(buf), "loaded fileNum=%u slots=%d", fileNum,
                      static_cast<int>(ARRAY_COUNT(gSaveContext.sceneFlags)));
        WriteMarker(buf);
        if (output) {
            *output += buf;
        }
        return 0;
    }
    // The definition-aware quest surface (sturdy-bassoon#58 P1 / D18). Same parser and renderer as
    // the human `quest` console command - QuestConsole_Run - so the two cannot drift; the only
    // difference is the sink: every line becomes its own `quest <line>` marker (greppable), and the
    // lines are also joined into `out=` with " | ". rc is 0 only when the operation succeeded, so a
    // refused write (prereq_unmet, order_violation, already_complete ...) is rc=1 on every tier -
    // pre-validated through the Quest_Check* calls, never via an assert.
    if (args.size() >= 3 && args[1] == "quest") {
        const std::vector<std::string> sub(args.begin() + 2, args.end());
        std::vector<std::string> lines;
        const int32_t rc = QuestConsole_Run(sub, lines);
        for (const std::string& line : lines) {
            WriteMarker("quest " + line); // the marker is written verbatim - it is not a format string
            if (output) {
                if (!output->empty()) {
                    *output += " | ";
                }
                // ConsoleWindow hands a handler's `output` to vsnprintf as the FORMAT string when
                // the command is typed, so '%' must be doubled here exactly as the human `quest`
                // sink does it (QuestConsole.cpp). Today nothing can produce one - definition
                // strings and `quest parse` input both refuse '%' - so this only ever escapes a
                // future line that grows one, which is the point of having it.
                for (char c : line) {
                    *output += c;
                    if (c == '%') {
                        *output += '%';
                    }
                }
            }
        }
        return rc;
    }
    // The NPC dialogue surface (sturdy-bassoon#58 P3 / D18). Same arrangement as `quest` above:
    // one implementation (RsNpcConsole_Run) behind two sinks, so the human command and the agent
    // markers cannot drift. Read-only - nothing here writes quest or world state; picking a
    // dialogue option is what writes, and only an actor in a real conversation does that.
    if (args.size() >= 3 && args[1] == "npc") {
        const std::vector<std::string> sub(args.begin() + 2, args.end());
        std::vector<std::string> lines;
        const int32_t rc = RsNpcConsole_Run(sub, lines);
        for (const std::string& line : lines) {
            WriteMarker("npc " + line); // written verbatim - it is not a format string
            if (output) {
                if (!output->empty()) {
                    *output += " | ";
                }
                // ConsoleWindow hands a handler's `output` to vsnprintf as the FORMAT string when
                // the command is typed, so '%' is doubled exactly as the human `npc` sink does it.
                for (char c : line) {
                    *output += c;
                    if (c == '%') {
                        *output += '%';
                    }
                }
            }
        }
        return rc;
    }
    if (args.size() >= 2 && args[1] == "mark") {
        std::string text;
        for (size_t i = 2; i < args.size(); i++) {
            text += (i > 2 ? " " : "") + args[i];
        }
        WriteMarker("mark " + text);
        return 0;
    }
    if (output) {
        *output +=
            "usage: agenttest perf <ticks> | state | goto <x> <y> <z> [yaw] | "
            "walk <frames> [stick_x] [stick_y] [buttons] [at_frame] | "
            "press <BUTTONS> [frames] | rooms | time <dawn|day|dusk|night|value> | trace <ticks> | "
            "cutscene <index>|off | fog <near> <far>|off | tiers <near> <mid> <n> [mitb] [drawcull]|off | "
            "roomdist [hysteresis]|off | uncull | sceneflag <sceneId> [value] | worldflag count|<n> [0|1] | "
            "queststore count|<id> [status mask] | questpred <kind> <a> <b> <negate> | "
              "quest list|dump <id>|start <id>|setstep <id> <n>|clearstep <id> <n>|check <id> <n>|complete <id>|"
              "journal <id|all> [runs]|parse <text...>|badcheck|"
              "force <id>|reset <id>|debugwipe | "
              "npc list|dump <id>|resolve <id>|actors|badcheck | "
            "save <fileNum> | loadsave <fileNum> | mark <text>";
    }
    return 1;
}

// ShipInit functions run again whenever a config or preset is loaded, so COND_HOOK (which unregisters
// its previous hook first) keeps every hook registered exactly once.
void RegisterAgentTest() {
    COND_HOOK(OnGameFrameUpdate, true, OnGameFrameUpdateAgentTest);
    COND_HOOK(OnZTitleUpdate, true, OnZTitleUpdateAgentTest);
    COND_HOOK(OnSceneInit, true, OnSceneInitAgentTest);
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateAgentTest);
    COND_HOOK(OnGameStateMainStart, true, OnGameStateMainStartAgentTest);

    auto console = Ship::Context::GetRawInstance()->GetConsole();
    if (!console->HasCommand("agenttest")) {
        console->AddCommand(
            "agenttest",
            { AgentTestCommand,
              "Agent test loop: perf <ticks> | state | goto <x> <y> <z> [yaw] | "
              "walk <frames> [stick_x] [stick_y] [buttons] [at_frame] | "
              "press <BUTTONS> [frames] | rooms | time <dawn|day|dusk|night|value> | trace <ticks> | "
              "cutscene <index>|off | fog <near> <far>|off | tiers <near> <mid> <n> [mitb] [drawcull]|off | "
              "roomdist [hysteresis]|off | uncull | sceneflag <sceneId> [value] | worldflag count|<n> [0|1] | "
              "queststore count|<id> [status mask] | questpred <kind> <a> <b> <negate> | "
              "quest list|dump <id>|start <id>|setstep <id> <n>|clearstep <id> <n>|check <id> <n>|complete <id>|"
              "force <id>|reset <id>|debugwipe | "
              "npc list|dump <id>|resolve <id>|actors|badcheck | "
              "save <fileNum> | loadsave <fileNum> | mark <text>. walk/press inject controller 1 for N frames and end "
              "with an input_done marker.",
              { { "subcommand", Ship::ArgumentType::TEXT }, { "value", Ship::ArgumentType::TEXT, true } } });
    }
}

} // namespace

static RegisterShipInitFunc initFunc(RegisterAgentTest);

// The marker channel, opened up to gameplay code (sturdy-bassoon#58 P3). Gated on sAgentMode for
// the reason every other WriteMarker caller is: outside agent mode there is no agent-log.txt to
// append to and no one reading it, and an ordinary session should not grow a marker stream it
// never asked for. This is what lets an actor make an in-game conversation ASSERTABLE - a
// screenshot shows a textbox, a marker names the rule that produced it.
extern "C" void AgentTest_WriteMarker(const char* text) {
    if (!sAgentMode || text == nullptr) {
        return;
    }
    WriteMarker(text);
}
