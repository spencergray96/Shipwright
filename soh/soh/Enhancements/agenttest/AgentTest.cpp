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
 *   perf fps=<f> ms=<f> tick_ms=<f> tick_max_ms=<f> mem_mb=<n> scene=0x<hex> frame=<n>
 *                                        every PerfInterval ticks (0 disables). fps/ms = ImGui render
 *                                        framerate (capped by the FPS setting); tick_ms = game-tick CPU
 *                                        time, avg and max over the interval; mem_mb = working set
 *   room_changed from=<n> to=<n> frame=<n>   the current room changed within a scene
 *   state scene=0x<hex> room=<n> entrance=0x<hex> pos=<x>,<y>,<z> yaw=<n> age=<adult|child> time=0x<hex>
 *         night=<0|1> frame=<n> name="<scene name>"
 *                                        name is last and quoted because it is the only field that can
 *                                        contain a space
 *   trace <pre|post> frame=<n> pos=... prev=... velY=... lin=... bg=0x<hex> floorH=... sf1..sf3=0x<hex>
 *         anim=0x<hex> trans=<n> rdown=<x>,<y>,<z> rdent=0x<hex>
 *                                        per-tick Player diagnostic while "agenttest trace" is active: position,
 *                                        prevPos, velocity, bgCheckFlags, floor height, state flags, anim movement
 *                                        flags, transition trigger and the void-out respawn point. "pre" is taken
 *                                        before the game tick runs, "post" after it (and after command consumption)
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
#include "soh/OTRGlobals.h"
#include "soh/util.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
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

    // Zelda heap free/total, in KB. This is what BgCheck's tables actually compete with: Play_Init
    // carves them out of the two-headed arena first and then hands the *entire* remainder to
    // ZeldaArena_Init (z_play.c), so THA_GetSize is 0 from then on and there is no "arena
    // headroom" to read. Growing collision does not hit an arena wall - it shrinks the heap that
    // actors and objects allocate from, which is the number worth watching.
    u32 heapMaxFree;
    u32 heapFree;
    u32 heapAlloc;
    ZeldaArena_GetSizes(&heapMaxFree, &heapFree, &heapAlloc);
    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "perf fps=%.1f ms=%.2f tick_ms=%.2f tick_max_ms=%.2f mem_mb=%.0f nodes=%u/%u "
                  "heap_kb=%u/%u scene=%s frame=%u",
                  fps, ms, tickAvg, sTickMaxMs, ResidentMemoryMb(), nodes.count, nodes.max,
                  heapFree / 1024, (heapFree + heapAlloc) / 1024, Hex(gPlayState->sceneNum).c_str(),
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
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "trace %s frame=%u pos=%.2f,%.2f,%.2f prev=%.2f,%.2f,%.2f velY=%.2f lin=%.2f bg=0x%X "
                  "floorH=%.1f sf1=0x%X sf2=0x%X sf3=0x%X anim=0x%X trans=%d rdown=%.1f,%.1f,%.1f rdent=%s",
                  phase, gPlayState->state.frames, player->actor.world.pos.x, player->actor.world.pos.y,
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
    const int16_t room = gPlayState->roomCtx.curRoom.num;
    if (room != sLastRoom) {
        if (sLastRoom >= 0) {
            WriteMarker("room_changed from=" + std::to_string(sLastRoom) + " to=" + std::to_string(room) +
                        " frame=" + std::to_string(gPlayState->state.frames));
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
    sTickStart = std::chrono::steady_clock::now();
    sTickStarted = true;
    if (sTraceTicksLeft > 0) {
        EmitTrace("pre");
    }
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
    // The fixed fields run to about 130 characters, so this leaves ~250 for the scene name -
    // several times the longest the scene table can produce. snprintf truncates rather than
    // overflows either way; the headroom is so the name is not what gets truncated.
    char buf[384];
    // `name` last, and quoted, because it is the only field that can contain a space - so anything
    // splitting the line on whitespace still gets every other field intact.
    std::snprintf(buf, sizeof(buf),
                  "state scene=%s room=%d entrance=%s pos=%.1f,%.1f,%.1f yaw=%d age=%s time=%s night=%d frame=%u "
                  "name=\"%s\"",
                  Hex(gPlayState->sceneNum).c_str(), gPlayState->roomCtx.curRoom.num,
                  Hex(gSaveContext.entranceIndex).c_str(), player->actor.world.pos.x, player->actor.world.pos.y,
                  player->actor.world.pos.z, player->actor.shape.rot.y,
                  gSaveContext.linkAge == LINK_AGE_CHILD ? "child" : "adult", Hex(gSaveContext.dayTime).c_str(),
                  gSaveContext.nightFlag, gPlayState->state.frames,
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
         args[1] == "time" || args[1] == "trace") &&
        !InNormalPlay()) {
        if (output) {
            *output += "no scene loaded";
        }
        return 1;
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
            "sceneflag <sceneId> [value] | save <fileNum> | loadsave <fileNum> | mark <text>";
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
              "sceneflag <sceneId> [value] | save <fileNum> | loadsave <fileNum> | mark <text>. "
              "walk/press inject controller 1 for N frames and end with an input_done marker.",
              { { "subcommand", Ship::ArgumentType::TEXT }, { "value", Ship::ArgumentType::TEXT, true } } });
    }
}

} // namespace

static RegisterShipInitFunc initFunc(RegisterAgentTest);
