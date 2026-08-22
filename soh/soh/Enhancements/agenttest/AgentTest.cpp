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
 * Agent mode is decided once, on the first console-logo frame: if agent-commands.txt exists then,
 * the session is on until the process exits; if not, the hook never touches the filesystem again
 * (every hook below early-returns on one bool). Create the file before launching soh.exe.
 *
 * Markers (all prefixed "[agenttest] "):
 *   session pid=<n>                      first tick the command file was seen
 *   boot_to_play entrance=0x<hex>        console logo skipped, booting a debug save straight into play
 *   scene_loaded scene=0x<hex> entrance=0x<hex>
 *   ready scene=0x<hex> entrance=0x<hex> Link exists and Play_Init has finished; commands are consumed
 *                                        only after this, and it is re-emitted after every scene change
 *   cmd <line> rc=<n> out=<text>         one line consumed from the command file. rc=-1 means the
 *                                        command does not exist; otherwise it is the handler's return
 *                                        (0 = success). Most SoH handlers print to the ImGui console
 *                                        rather than <text>, so out= is usually empty for them.
 *   perf fps=<f> ms=<f> scene=0x<hex> frame=<n>   every PerfInterval ticks (0 disables)
 *   state scene=0x<hex> room=<n> entrance=0x<hex> pos=<x>,<y>,<z> yaw=<n> age=<adult|child> frame=<n>
 *   mark <text>                          echoed from "agenttest mark <text>"
 *
 * Console command registered here:
 *   agenttest perf <ticks>   set the perf marker interval (game ticks, 20/s); 0 disables
 *   agenttest state          emit a state marker (scene, room, entrance, Link position and facing)
 *   agenttest mark <text>    write a marker, for bracketing checkpoints in the log
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

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <ship/Context.h>
#include <ship/debug/Console.h>
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

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
#define AGENTTEST_GETPID _getpid
#else
#include <unistd.h>
#define AGENTTEST_GETPID getpid
#endif

namespace {

constexpr const char* COMMAND_FILE = "agent-commands.txt";
constexpr const char* MARKER_FILE = "agent-log.txt";
constexpr uint32_t POLL_INTERVAL = 10; // game ticks between command-file polls (20 ticks = 1 s)
constexpr int32_t DEFAULT_PERF_INTERVAL = 60;
// Staging scene for the auto-boot: the door spawn of Link's house. Small, loads fast, nothing scripted.
// The caller's real entrance comes through the command file afterwards.
constexpr int32_t BOOT_ENTRANCE = ENTR_LINKS_HOUSE_0_1;

// State
bool sAgentMode = false; // decided once at the console logo; never re-checked
bool sReady = false;     // true from the first Player update after the latest OnSceneInit
int32_t sPerfInterval = DEFAULT_PERF_INTERVAL;
uint32_t sTickCounter = 0;
std::streamoff sConsumedBytes = 0;
GameState* sLogoState = nullptr; // non-null only during the tick the console-logo state ran

std::string CommandPath() {
    return Ship::Context::GetPathRelativeToAppDirectory(COMMAND_FILE);
}

std::string MarkerPath() {
    return Ship::Context::GetPathRelativeToAppDirectory(MARKER_FILE);
}

bool AgentModeActive() {
    std::error_code ec;
    return std::filesystem::exists(CommandPath(), ec);
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

        if (!InNormalPlay() || !sReady || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) {
            break;
        }
    }
}

void EmitPerf() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    const float fps = ImGui::GetIO().Framerate;
    const float ms = fps > 0.0f ? 1000.0f / fps : 0.0f;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "perf fps=%.1f ms=%.2f scene=%s frame=%u", fps, ms,
                  Hex(gPlayState->sceneNum).c_str(), gPlayState->state.frames);
    WriteMarker(buf);
}

void OnGameFrameUpdateAgentTest() {
    GameState* logoState = sLogoState;
    sLogoState = nullptr;
    sTickCounter++;

    // Console logo, first tick only: this is the one filesystem check a non-agent session ever pays.
    // It has to be the first tick because a BootSequence of FileSelect or DebugWarpScreen ends the
    // logo state right there. This runs after every OnZTitleUpdate hook of the tick, so setting the
    // next game state here wins over whichever one they set.
    if (logoState != nullptr && gPlayState == nullptr) {
        static bool decided = false;
        if (!decided) {
            decided = true;
            sAgentMode = AgentModeActive();
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
    if (sPerfInterval > 0 && sTickCounter % static_cast<uint32_t>(sPerfInterval) == 0) {
        EmitPerf();
    }
    if (sTickCounter % POLL_INTERVAL == 0) {
        ConsumeCommands();
    }
}

void OnZTitleUpdateAgentTest(void* gameState) {
    sLogoState = static_cast<GameState*>(gameState);
}

void OnSceneInitAgentTest(int16_t sceneNum) {
    sReady = false;
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
    if (args.size() >= 2 && args[1] == "state") {
        if (!InNormalPlay()) {
            if (output) {
                *output += "no scene loaded";
            }
            return 1;
        }
        Player* player = GET_PLAYER(gPlayState);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "state scene=%s room=%d entrance=%s pos=%.1f,%.1f,%.1f yaw=%d age=%s frame=%u",
                      Hex(gPlayState->sceneNum).c_str(), gPlayState->roomCtx.curRoom.num,
                      Hex(gSaveContext.entranceIndex).c_str(), player->actor.world.pos.x, player->actor.world.pos.y,
                      player->actor.world.pos.z, player->actor.shape.rot.y,
                      gSaveContext.linkAge == LINK_AGE_CHILD ? "child" : "adult", gPlayState->state.frames);
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
        *output += "usage: agenttest perf <ticks> | agenttest state | agenttest mark <text>";
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

    auto console = Ship::Context::GetRawInstance()->GetConsole();
    if (!console->HasCommand("agenttest")) {
        console->AddCommand(
            "agenttest",
            { AgentTestCommand,
              "Agent test loop: 'agenttest perf <ticks>' sets the perf marker interval (0 disables); "
              "'agenttest state' reports scene/room/entrance/position; 'agenttest mark <text>' writes a marker.",
              { { "subcommand", Ship::ArgumentType::TEXT }, { "value", Ship::ArgumentType::TEXT, true } } });
    }
}

} // namespace

static RegisterShipInitFunc initFunc(RegisterAgentTest);
