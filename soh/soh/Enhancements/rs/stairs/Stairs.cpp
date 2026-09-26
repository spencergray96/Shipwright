#include "Stairs.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/agenttest/AgentTest.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/actors/RsActors.h"
#include "soh/Enhancements/rs/music/ZoneDirector.h"
#include "soh/Enhancements/rs/prefs/FloorText.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/frame_interpolation.h"
#include <spdlog/spdlog.h>

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
// z_camera.c, declared in no header: pulls `to` in to the first poly between `from` and `to`.
s32 Camera_BGCheck(Camera* camera, Vec3f* from, Vec3f* to);
}

// See Stairs.h for what this file owns and why the move is not the actor's.

namespace {

// --- the menu's words ---------------------------------------------------------------------------
//
// Every string a staircase menu can show, as LITERALS, one per storey index. Built by a macro
// rather than composed at registration so each is a definition string that outlives every textbox
// (RsActors.h's rule for anything the renderer is handed a pointer to) with no storage to manage.
// `{floor:N}` is expanded at READ time, so a save-slot switch changes what the menu says with
// nothing here knowing about it. Ten per phrase because the token takes exactly one digit.
#define RS_STAIR_TEN(pre, post)                                                                                        \
    {                                                                                                                  \
        pre "{floor:0}" post, pre "{floor:1}" post, pre "{floor:2}" post, pre "{floor:3}" post, pre "{floor:4}" post, \
            pre "{floor:5}" post, pre "{floor:6}" post, pre "{floor:7}" post, pre "{floor:8}" post,                    \
            pre "{floor:9}" post                                                                                       \
    }

constexpr int32_t kStoreyCount = 10;

// Two storeys: the question IS the menu, so it names where you are going and the choice confirms.
const char* const kGoUpTo[kStoreyCount] = RS_STAIR_TEN("Go up to the ", "?");
const char* const kGoDownTo[kStoreyCount] = RS_STAIR_TEN("Go down to the ", "?");
const char* const kGoUp = "Go up";
const char* const kGoDown = "Go down";

// Three or four: the body says where you ARE - in a 1x1 shaft every storey looks the same, and
// that is the one fact the options cannot carry - and each option names a destination.
const char* const kYouAreOn[kStoreyCount] = RS_STAIR_TEN("You are on the ", ".");
const char* const kUpTo[kStoreyCount] = RS_STAIR_TEN("Up to the ", "");
const char* const kDownTo[kStoreyCount] = RS_STAIR_TEN("Down to the ", "");

const char* const kCancel = "Cancel";

#undef RS_STAIR_TEN

// One staircase's menus, one screen per row, built once. The option arrays are RsDialogueOption so
// the quest-giver's renderer lays them out unchanged; `dest` is the part the renderer does not need
// and the actor does - which row picking that line moves to (-1 = Cancel).
struct StairMenus {
    const RsStairDef* def = nullptr;
    RsDialogueRule screens[RS_STAIR_MAX_ROWS] = {};
    RsDialogueOption options[RS_STAIR_MAX_ROWS][RS_STAIR_MAX_ROWS] = {};
    int32_t dest[RS_STAIR_MAX_ROWS][RS_STAIR_MAX_ROWS] = {};
};

// unique_ptr, so a registered staircase's screens never move: the renderer and the actor hold
// pointers into them for as long as a box is open.
std::array<std::unique_ptr<StairMenus>, RS_STAIR_MAX> sStairs;

RsDialogueOption MakeOption(const char* label) {
    RsDialogueOption option = {};
    option.label = label;
    option.kind = RS_DLG_ACTION_NONE;
    option.a = 0;
    option.reply = nullptr;
    // Stated, not defaulted: NpcDialogueDef.h's warning is that a value-initialised 0 is node 0.
    // Nothing here ever follows it - the stairs actor closes the box itself - but a screen that
    // reaches the shared renderer carries the same sentinels a hand-written one would.
    option.next = RS_DLG_NO_NEXT;
    option.when = nullptr;
    option.whenCount = 0;
    return option;
}

// Builds every row's screen. Assumes the definition's rows and storeys are already known good -
// the validator calls this only after those checks, then measures what it built.
void BuildMenus(const RsStairDef& def, StairMenus& out) {
    out.def = &def;
    for (int32_t row = 0; row < def.landingCount; row++) {
        RsDialogueRule& screen = out.screens[row];
        RsDialogueOption* options = out.options[row];
        int32_t* dest = out.dest[row];
        int32_t count = 0;

        if (def.landingCount == 2) {
            // Skip the menu: the other storey is the only place to go, so ask about it by name.
            const int32_t other = 1 - row;
            const bool up = other > row;
            screen.text = up ? kGoUpTo[def.landings[other].storey] : kGoDownTo[def.landings[other].storey];
            options[count] = MakeOption(up ? kGoUp : kGoDown);
            dest[count++] = other;
        } else {
            screen.text = kYouAreOn[def.landings[row].storey];
            // Highest first, like a lift panel, so the list reads in the same order as the building.
            for (int32_t other = def.landingCount - 1; other >= 0; other--) {
                if (other == row) {
                    continue;
                }
                options[count] = MakeOption(other > row ? kUpTo[def.landings[other].storey]
                                                        : kDownTo[def.landings[other].storey]);
                dest[count++] = other;
            }
        }
        options[count] = MakeOption(kCancel);
        dest[count++] = RS_STAIR_MENU_CANCEL;

        screen.when = nullptr;
        screen.whenCount = 0;
        screen.options = options;
        screen.optionCount = count;
        screen.missingOf = RS_DLG_NO_MISSING;
        screen.next = RS_DLG_NO_NEXT;
    }
}

bool IsToken(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    for (const char* c = name; *c != '\0'; c++) {
        if (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r' || *c == '%' || *c == '#' || *c == '"') {
            return false;
        }
    }
    return true;
}

// The render checks, asked of the renderer itself (RsActors.cpp) under EVERY floor convention -
// the same three questions the quest-giver's validator asks of a hand-written screen. A staircase
// menu is generated rather than authored, so what can fail here is a template that stops fitting
// once a convention's label is wider, and the storey index that picks the widest label.
bool MenuRenders(const RsDialogueRule& screen) {
    for (int32_t i = 0; i < screen.optionCount; i++) {
        if (RsText_LabelWouldOverflow(screen.options[i].label)) {
            return false;
        }
    }
    if (screen.optionCount == 2) {
        return !RsText_ChoiceWouldPaginate(&screen, 2);
    }
    return !RsText_BodyWouldWrap(&screen, screen.optionCount);
}

// --- the move -----------------------------------------------------------------------------------

#define CVAR_RS_STAIRS_FADE CVAR_ENHANCEMENT("RsStairsFadeTicks")

// The default is a HARD CUT. Both were built and compared on the castle's tower shaft (#147 ADR,
// run record 2026-09-25-issue-147-storey-actor): a 6-tick fade costs 0.7 s a move and reads as a
// door or a load for an 80-unit hop the player can see straight down; the cut is 3 ticks and is
// what RS does. The one thing the fade hid - the camera settling after the snap, which from a
// tower room started outside the tower - SeatCamera now does before the first frame is drawn.
// `stairs fade <n>` overrides it per install without a rebuild; `stairs fade default` clears the
// override.
constexpr int32_t kDefaultFadeTicks = 0;

// Ticks spent at full black after the move, before fading back in. Player's floor raycast runs on
// its next update, so the first of these is what gives `floorHeight` - and the respawn point, which
// reads live position - a floor to report; the second is margin for the camera's first real update
// from its new position. With a hard cut this is also how long Link stays frozen after landing.
constexpr int32_t kSettleTicks = 2;

// Half a storey. Within it, Link is "on" a landing's storey.
constexpr float kRowSnap = 40.0f;

enum class Phase { Idle, FadeOut, WaitRoom, Settle, FadeIn };

const char* PhaseName(Phase phase) {
    switch (phase) {
        case Phase::Idle:
            return "idle";
        case Phase::FadeOut:
            return "fade_out";
        case Phase::WaitRoom:
            return "wait_room";
        case Phase::Settle:
            return "settle";
        case Phase::FadeIn:
            return "fade_in";
    }
    return "unknown";
}

struct Move {
    Phase phase = Phase::Idle;
    int32_t stairId = -1;
    int32_t fromRow = -1;
    int32_t toRow = -1;
    int32_t fade = 0;
    int32_t ticks = 0;     // since BeginMove
    int32_t phaseTick = 0; // within the current phase
    int16_t scene = -1;
    const char* source = "";
    bool roomChange = false;
    bool roomRequested = false;
    int32_t roomFrom = -1;
};

Move sMove;
std::string sLast; // the last move's outcome, for `stairs status` - see ReportOutcome

void Marker(const char* fmt, ...) {
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    AgentTest_WriteMarker(line);
}

// A marker that is also a move's OUTCOME - landed, aborted, refused - remembered for
// `stairs status`. Remembered without its `rs_stairs ` prefix, because status prints it inside
// `last="..."`: an answer line must never read as an event to a grep for `rs_stairs stair=<n> event=`.
void ReportOutcome(const char* line) {
    static const char kPrefix[] = "rs_stairs ";
    AgentTest_WriteMarker(line);
    sLast = std::strncmp(line, kPrefix, sizeof(kPrefix) - 1) == 0 ? line + sizeof(kPrefix) - 1 : line;
}

int32_t ClampFade(int32_t ticks) {
    return ticks < 0 ? 0 : (ticks > RS_STAIR_MAX_FADE_TICKS ? RS_STAIR_MAX_FADE_TICKS : ticks);
}

void SetFill(PlayState* play, int32_t alpha) {
    play->envCtx.fillScreen = true;
    play->envCtx.screenFillColor[0] = 0;
    play->envCtx.screenFillColor[1] = 0;
    play->envCtx.screenFillColor[2] = 0;
    play->envCtx.screenFillColor[3] = static_cast<u8>(alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha));
}

void ClearFill(PlayState* play) {
    play->envCtx.fillScreen = false;
    play->envCtx.screenFillColor[3] = 0;
}

// Alpha for tick `t` of an `n`-tick fade: the cutscene fade's arithmetic (z_demo.c, the black
// case), linear in time.
int32_t FadeAlpha(int32_t t, int32_t n) {
    if (n <= 0) {
        return 255;
    }
    return (255 * t) / n;
}

// Gives Link back. Player_SetCsAction(7) ends a cutscene action from any state: in the CsAction it
// runs the exit (func_80852944), and if he never got that far Player_UpdateCommon runs it directly.
void ReleasePlayer(PlayState* play) {
    Player_SetCsAction(play, nullptr, 7);
}

// Ends a move that cannot finish - the scene changed under it, or Link vanished. Nothing is put
// back: the screen fill belongs to a PlayState that a scene change re-initialises anyway.
void Abort(const char* reason) {
    if (sMove.phase == Phase::Idle) {
        return;
    }
    char line[200];
    std::snprintf(line, sizeof(line), "rs_stairs stair=%d event=abort reason=%s phase=%s ticks=%d", sMove.stairId,
                  reason, PhaseName(sMove.phase), sMove.ticks);
    ReportOutcome(line);
    sMove = Move();
}

// Seats the freshly snapped camera where Camera_Normal would settle it, so a hard cut's first
// frame is already the view the next few frames keep.
//
// Pulling the eye in to the first wall along InitPlayerSettings' own 10-degree line is not enough:
// in the castle's tower that line clips the 4-unit rim of the shaft hole, 60 units back, and
// leaves the eye INSIDE the slab - the first frame then shows the roof through the ceiling. The
// camera's settled position there is lower (about 3 degrees, 98 back, against the tower's far
// wall). So three pitches are tried behind him, each pulled in by Camera_BGCheck - the same check
// Camera_Normal makes every frame - and the one with the longest clear line wins, ties going to
// the higher pitch. In the open that is the natural 10 degrees at the full 180; in a tight room it
// is whichever line reaches furthest before a wall.
void SeatCamera(Camera* camera, s16 yaw) {
    static const s16 kPitches[] = { 0x71C, 0x38E, 0 }; // 10, 5 and 0 degrees
    const s16 behind = static_cast<s16>(yaw + 0x8000);
    const f32 radius = 180.0f;
    Vec3f best = camera->eye;
    s16 bestPitch = kPitches[0];
    f32 bestDist = -1.0f;
    for (const s16 pitch : kPitches) {
        const f32 flat = radius * Math_CosS(pitch);
        Vec3f eye = { camera->at.x + flat * Math_SinS(behind), camera->at.y + radius * Math_SinS(pitch),
                      camera->at.z + flat * Math_CosS(behind) };
        Camera_BGCheck(camera, &camera->at, &eye);
        const f32 dist = Math_Vec3f_DistXYZ(&camera->at, &eye);
        if (dist > bestDist + 1.0f) {
            best = eye;
            bestDist = dist;
            bestPitch = pitch;
        }
    }
    camera->eye = best;
    camera->eyeNext = best;
    camera->inputDir.x = bestPitch;
    camera->camDir.x = bestPitch;
}

// THE MOVE ITSELF - every field #134's source dig found an in-place move must write, and the two
// `agenttest goto` does not.
void Teleport(PlayState* play, Player* player, const RsStairLanding& landing) {
    const Vec3f pos = { static_cast<f32>(landing.x), static_cast<f32>(landing.y), static_cast<f32>(landing.z) };
    const s16 yaw = static_cast<s16>(landing.yaw);

    player->actor.world.pos = pos;
    // The wall sweep runs prevPos -> world.pos as though Link walked it in one frame
    // (z_actor.c, BgCheck wall check) and would clamp him to the first poly the line crosses - a
    // slab, here. And prevPos is rewritten from home.pos at the top of every Player update
    // (z_player.c), so both move.
    player->actor.prevPos = pos;
    player->actor.home.pos = pos;
    // Otherwise a downward move reads as an ongoing fall, and a long one can void or hurt him.
    player->fallStartHeight = static_cast<s16>(pos.y);
    player->fallDistance = 0;
    player->linearVelocity = 0.0f; // speedXZ follows it (z_player.c), set too for the same frame
    player->actor.speedXZ = 0.0f;
    // `goto` does NOT do this: a move taken mid-fall would keep its downward velocity.
    player->actor.velocity.x = 0.0f;
    player->actor.velocity.y = 0.0f;
    player->actor.velocity.z = 0.0f;
    // Facing is a set of three; any one left behind wins on the next frame.
    player->actor.world.rot.y = yaw;
    player->actor.shape.rot.y = yaw;
    player->yaw = yaw;
    // A lock-on survives a teleport if the target is still inside its leash, and the staircase he
    // just used is: he would come out of the move turned to face the storey he left.
    Player_ReleaseLockOn(player);

    // The camera, snapped behind him - Play_Init's own call for "Link just appeared here". Left to
    // itself the camera would chase him up the shaft over several frames, through a slab. Only the
    // main camera: a subcamera belongs to whatever cutscene owns it.
    //
    // Then re-seated where the camera would settle. InitPlayerSettings puts the eye a fixed 180
    // behind him at a 10-degree pitch with no collision check - which, from a landing in a 4-cell
    // tower room, is OUTSIDE the tower and above the ceiling. On a hard cut the next few
    // Camera_Normal updates are on screen: the camera swooping in through the wall, then out of
    // the slab (#147 run record, the cut filmstrips).
    if (play->activeCamera == CAM_ID_MAIN) {
        Camera* camera = Play_GetCamera(play, CAM_ID_MAIN);
        Camera_InitPlayerSettings(camera, player);
        SeatCamera(camera, yaw);
    }
    // One rendered frame interpolated between the old camera and the new would draw a streak
    // through the building. Every other in-engine camera cut says the same thing.
    FrameInterpolation_DontInterpolateCamera();

    // Anything that moves Link discontinuously tells the zone music director, which otherwise
    // waits out its walking dwell before switching (ZoneDirector.h).
    RsMusic_NotifyWarped("stairs");
}

void Finish(PlayState* play, Player* player) {
    const RsStairLanding* landing = RsStair_GetLanding(sMove.stairId, sMove.toRow);
    const RespawnData& respawn = gSaveContext.respawn[RESPAWN_MODE_DOWN];
    char line[320];
    // THE assertion line. pos= and room= are where Link is; floor_y= and ground= prove he is
    // standing on something rather than falling past it; respawn= and respawn_room= are where a
    // void-out would put him, which is the other half of "landed on the right storey".
    std::snprintf(line, sizeof(line),
                  "rs_stairs stair=%d event=landed from_row=%d to_row=%d storey=%d pos=%.1f,%.1f,%.1f yaw=%d room=%d "
                  "floor_y=%.1f ground=%d respawn=%.1f,%.1f,%.1f respawn_room=%d fade=%d ticks=%d source=%s",
                  sMove.stairId, sMove.fromRow, sMove.toRow, landing != nullptr ? landing->storey : -1,
                  player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                  player->actor.shape.rot.y, play->roomCtx.curRoom.num, player->actor.floorHeight,
                  (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 1 : 0, respawn.pos.x, respawn.pos.y,
                  respawn.pos.z, respawn.roomIndex, sMove.fade, sMove.ticks, sMove.source);
    ReportOutcome(line);
    ClearFill(play);
    ReleasePlayer(play);
    sMove = Move();
}

void OnPlayerUpdateStairs() {
    if (sMove.phase == Phase::Idle) {
        return;
    }
    PlayState* play = gPlayState;
    if (play == nullptr || play->sceneNum != sMove.scene || GET_PLAYER(play) == nullptr) {
        Abort("scene_changed");
        return;
    }
    Player* player = GET_PLAYER(play);
    const RsStairLanding* landing = RsStair_GetLanding(sMove.stairId, sMove.toRow);
    if (landing == nullptr) {
        Abort("landing_gone"); // unreachable: BeginMove checked it and nothing unregisters
        return;
    }
    sMove.ticks++;

    switch (sMove.phase) {
        case Phase::FadeOut: {
            sMove.phaseTick++;
            if (sMove.phaseTick < sMove.fade) {
                SetFill(play, FadeAlpha(sMove.phaseTick, sMove.fade));
                return;
            }
            if (sMove.fade > 0) {
                SetFill(play, 255);
            }
            const Vec3f before = player->actor.world.pos;
            Teleport(play, player, *landing);
            sMove.roomFrom = play->roomCtx.curRoom.num;
            sMove.roomChange = landing->room != sMove.roomFrom;
            // A hard cut still goes black across a ROOM CHANGE. Until Room_FinishRoomChange the
            // destination room is not the one being drawn, so a cut would show Link standing in
            // nothing for the tick or two the load takes. Finish clears it with the rest.
            if (sMove.fade == 0 && sMove.roomChange) {
                SetFill(play, 255);
            }
            // `black=` is whether the screen is held black as the move lands, and `eye=` where the
            // snapped camera starts - both what a screenshot would have to catch on exactly the right
            // frame to prove.
            const Vec3f eye = Play_GetCamera(play, CAM_ID_MAIN)->eye;
            Marker("rs_stairs stair=%d event=moved to_row=%d storey=%d from=%.1f,%.1f,%.1f pos=%.1f,%.1f,%.1f yaw=%d "
                   "room=%d room_change=%d black=%d eye=%.1f,%.1f,%.1f ticks=%d",
                   sMove.stairId, sMove.toRow, landing->storey, before.x, before.y, before.z,
                   player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                   player->actor.shape.rot.y, sMove.roomFrom, sMove.roomChange ? 1 : 0,
                   (play->envCtx.fillScreen && play->envCtx.screenFillColor[3] == 255) ? 1 : 0, eye.x, eye.y,
                   eye.z, sMove.ticks);
            sMove.phase = sMove.roomChange ? Phase::WaitRoom : Phase::Settle;
            sMove.phaseTick = 0;
            return;
        }
        case Phase::WaitRoom: {
            // RoomDist.cpp's shape: request, wait for the load to report done, finish. The finish is
            // what kills every actor outside the new room - including, usually, the staircase that
            // opened the menu. That is why none of this lives in the actor.
            RoomContext* roomCtx = &play->roomCtx;
            if (!sMove.roomRequested) {
                if (roomCtx->status != 0) {
                    return; // somebody else's load in flight; ask when it is done
                }
                if (!Room_RequestNewRoom(play, roomCtx, landing->room)) {
                    return;
                }
                sMove.roomRequested = true;
                Marker("rs_stairs stair=%d event=room_request from=%d to=%d ticks=%d", sMove.stairId, sMove.roomFrom,
                       landing->room, sMove.ticks);
                return;
            }
            if (roomCtx->status != 0) {
                return;
            }
            Room_FinishRoomChange(play, roomCtx);
            Marker("rs_stairs stair=%d event=room from=%d to=%d ticks=%d", sMove.stairId, sMove.roomFrom,
                   roomCtx->curRoom.num, sMove.ticks);
            sMove.phase = Phase::Settle;
            sMove.phaseTick = 0;
            return;
        }
        case Phase::Settle:
            if (++sMove.phaseTick < kSettleTicks) {
                return;
            }
            // After the move AND the room change: it reads live position, yaw and the CURRENT room
            // (z_play.c), which is what a door does after its own room change. The room is what
            // decides which storey a void-out puts him back on.
            Play_SetupRespawnPoint(play, RESPAWN_MODE_DOWN, 0xDFF);
            sMove.phase = Phase::FadeIn;
            sMove.phaseTick = sMove.fade;
            if (sMove.fade > 0) {
                return;
            }
            Finish(play, player);
            return;
        case Phase::FadeIn:
            sMove.phaseTick--;
            if (sMove.phaseTick > 0) {
                SetFill(play, FadeAlpha(sMove.phaseTick, sMove.fade));
                return;
            }
            Finish(play, player);
            return;
        case Phase::Idle:
            return;
    }
}

// A scene change is the one thing that can happen to a move from outside - a death, a warp, a
// console `entrance`. Play_Init resets the screen fill itself; this forgets the move.
void OnSceneInitStairs(int16_t sceneNum) {
    Abort("scene_init");
}

void RegisterStairsHooks() {
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateStairs);
    COND_HOOK(OnSceneInit, true, OnSceneInitStairs);
}

RegisterShipInitFunc stairsHooksInitFunc(RegisterStairsHooks);

} // namespace

// --- registry -----------------------------------------------------------------------------------

extern "C" const char* RsStair_ResultName(int32_t result) {
    switch (result) {
        case RS_STAIR_OK:
            return "ok";
        case RS_STAIR_ERR_BAD_ID:
            return "bad_id";
        case RS_STAIR_ERR_BAD_ROW:
            return "bad_row";
        case RS_STAIR_ERR_BUSY:
            return "busy";
        case RS_STAIR_ERR_NO_PLAY:
            return "no_play";
        case RS_STAIR_ERR_WRONG_SCENE:
            return "wrong_scene";
        case RS_STAIR_ERR_BAD_ROOM:
            return "bad_room";
        default:
            return "unknown";
    }
}

extern "C" const char* RsStair_ProblemName(int32_t problem) {
    switch (problem) {
        case RS_STAIR_PROBLEM_NONE:
            return "none";
        case RS_STAIR_PROBLEM_NULL_DEF:
            return "null_def";
        case RS_STAIR_PROBLEM_BAD_ID:
            return "bad_id";
        case RS_STAIR_PROBLEM_BAD_NAME:
            return "bad_name";
        case RS_STAIR_PROBLEM_ROW_COUNT:
            return "row_count";
        case RS_STAIR_PROBLEM_NULL_LANDINGS:
            return "null_landings";
        case RS_STAIR_PROBLEM_BAD_STOREY:
            return "bad_storey";
        case RS_STAIR_PROBLEM_STOREY_ORDER:
            return "storey_order";
        case RS_STAIR_PROBLEM_BAD_ROOM:
            return "bad_room";
        case RS_STAIR_PROBLEM_MENU_OVERFLOWS:
            return "menu_overflows";
        case RS_STAIR_PROBLEM_ID_TAKEN:
            return "id_taken";
        default:
            return "unknown";
    }
}

extern "C" int32_t RsStair_DefProblem(const RsStairDef* def, int32_t* where) {
    int32_t unused = -1;
    int32_t* at = where != nullptr ? where : &unused;
    *at = -1;

    if (def == nullptr) {
        return RS_STAIR_PROBLEM_NULL_DEF;
    }
    if (!RS_STAIR_ID_IS_VALID(def->id)) {
        return RS_STAIR_PROBLEM_BAD_ID;
    }
    if (!IsToken(def->name)) {
        return RS_STAIR_PROBLEM_BAD_NAME;
    }
    if (def->landingCount < 2 || def->landingCount > RS_STAIR_MAX_ROWS) {
        return RS_STAIR_PROBLEM_ROW_COUNT;
    }
    if (def->landings == nullptr) {
        return RS_STAIR_PROBLEM_NULL_LANDINGS;
    }
    for (int32_t row = 0; row < def->landingCount; row++) {
        const RsStairLanding& landing = def->landings[row];
        *at = row;
        if (landing.storey < 0 || landing.storey >= kStoreyCount) {
            return RS_STAIR_PROBLEM_BAD_STOREY;
        }
        if (row > 0 && landing.storey <= def->landings[row - 1].storey) {
            return RS_STAIR_PROBLEM_STOREY_ORDER;
        }
        if (landing.room < 0 || landing.room > 255) {
            return RS_STAIR_PROBLEM_BAD_ROOM;
        }
    }
    // Every storey is known good, so the menus can be built - and then measured, which is the only
    // check that needs them built.
    StairMenus menus;
    BuildMenus(*def, menus);
    for (int32_t row = 0; row < def->landingCount; row++) {
        *at = row;
        if (!MenuRenders(menus.screens[row])) {
            return RS_STAIR_PROBLEM_MENU_OVERFLOWS;
        }
    }
    *at = -1;
    const RsStairDef* existing = RsStair_GetDef(def->id);
    if (existing != nullptr && existing != def) {
        return RS_STAIR_PROBLEM_ID_TAKEN;
    }
    return RS_STAIR_PROBLEM_NONE;
}

extern "C" int32_t RsStair_Register(const RsStairDef* def) {
    int32_t where = -1;
    const int32_t problem = RsStair_DefProblem(def, &where);
    if (problem != RS_STAIR_PROBLEM_NONE) {
        // BUG CLASS, as RsNpc_Register and Quest_Register are (QUEST_SYSTEM.md, "Two loudness
        // classes"): a malformed or duplicate definition is a mistake in the source, so it is an
        // error, a debug assert and a refusal. The kind and the row, never the prose. Nothing the
        // agent loop drives can reach this - `stairs badcheck` asks RsStair_DefProblem, which is
        // silent - so the assert fires only for a table that is actually broken, at boot.
        char line[160];
        std::snprintf(line, sizeof(line), "rs_stairs stair=%d event=refused problem=%s row=%d",
                      def != nullptr ? def->id : -1, RsStair_ProblemName(problem), where);
        AgentTest_WriteMarker(line);
        SPDLOG_ERROR("RsStairs: register stair {}: {} at row {}", def != nullptr ? def->id : -1,
                     RsStair_ProblemName(problem), where);
        assert(false && "staircase definition failed validation");
        return problem;
    }
    if (sStairs[def->id] != nullptr) {
        return 0; // the same pointer again: a ShipInit re-run
    }
    auto menus = std::make_unique<StairMenus>();
    BuildMenus(*def, *menus);
    sStairs[def->id] = std::move(menus);
    return 0;
}

extern "C" const RsStairDef* RsStair_GetDef(int32_t stairId) {
    if (!RS_STAIR_ID_IS_VALID(stairId) || sStairs[stairId] == nullptr) {
        return nullptr;
    }
    return sStairs[stairId]->def;
}

extern "C" int32_t RsStair_IsRegistered(int32_t stairId) {
    return RsStair_GetDef(stairId) != nullptr ? 1 : 0;
}

extern "C" const RsStairLanding* RsStair_GetLanding(int32_t stairId, int32_t row) {
    const RsStairDef* def = RsStair_GetDef(stairId);
    if (def == nullptr || row < 0 || row >= def->landingCount) {
        return nullptr;
    }
    return &def->landings[row];
}

extern "C" int32_t RsStair_RowNearestPlayer(int32_t stairId) {
    const RsStairDef* def = RsStair_GetDef(stairId);
    if (def == nullptr || gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr) {
        return -1;
    }
    const float y = GET_PLAYER(gPlayState)->actor.world.pos.y;
    int32_t best = -1;
    float bestDist = kRowSnap;
    for (int32_t row = 0; row < def->landingCount; row++) {
        const float dist = std::fabs(static_cast<float>(def->landings[row].y) - y);
        if (dist <= bestDist) {
            bestDist = dist;
            best = row;
        }
    }
    return best;
}

extern "C" const RsDialogueRule* RsStair_Screen(int32_t stairId, int32_t row) {
    const RsStairDef* def = RsStair_GetDef(stairId);
    if (def == nullptr || row < 0 || row >= def->landingCount) {
        return nullptr;
    }
    return &sStairs[stairId]->screens[row];
}

extern "C" int32_t RsStair_MenuDestination(int32_t stairId, int32_t row, int32_t choiceIndex) {
    const RsDialogueRule* screen = RsStair_Screen(stairId, row);
    if (screen == nullptr || choiceIndex < 0 || choiceIndex >= screen->optionCount) {
        return RS_STAIR_MENU_NO_OPTION;
    }
    return sStairs[stairId]->dest[row][choiceIndex];
}

extern "C" int32_t RsStair_GetFadeTicks(void) {
    const int32_t ticks = CVarGetInteger(CVAR_RS_STAIRS_FADE, kDefaultFadeTicks);
    return ClampFade(ticks);
}

extern "C" void RsStair_SetFadeTicks(int32_t ticks) {
    CVarSetInteger(CVAR_RS_STAIRS_FADE, ClampFade(ticks));
    CVarSave();
}

extern "C" void RsStair_ClearFadeTicks(void) {
    CVarClear(CVAR_RS_STAIRS_FADE);
    CVarSave();
}

extern "C" int32_t RsStair_FadeTicksOverridden(void) {
    // A sentinel default rather than CVarExists: consolevariablebridge.h declares that one, but this
    // libultraship does not define it, and the only symptom is an unresolved external at link.
    constexpr int32_t kUnset = INT32_MIN;
    return CVarGetInteger(CVAR_RS_STAIRS_FADE, kUnset) != kUnset ? 1 : 0;
}

extern "C" int32_t RsStair_IsMoving(void) {
    return sMove.phase != Phase::Idle ? 1 : 0;
}

extern "C" int32_t RsStair_BeginMove(int32_t stairId, int32_t fromRow, int32_t toRow, const char* source) {
    int32_t result = RS_STAIR_OK;
    const RsStairDef* def = RsStair_GetDef(stairId);
    const RsStairLanding* landing = RsStair_GetLanding(stairId, toRow);

    if (def == nullptr) {
        result = RS_STAIR_ERR_BAD_ID;
    } else if (landing == nullptr) {
        result = RS_STAIR_ERR_BAD_ROW;
    } else if (sMove.phase != Phase::Idle) {
        result = RS_STAIR_ERR_BUSY;
    } else if (gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr) {
        result = RS_STAIR_ERR_NO_PLAY;
    } else if (gPlayState->sceneNum != def->sceneId) {
        result = RS_STAIR_ERR_WRONG_SCENE;
    } else if (landing->room >= gPlayState->numRooms) {
        result = RS_STAIR_ERR_BAD_ROOM;
    }
    if (result != RS_STAIR_OK) {
        char line[200];
        std::snprintf(line, sizeof(line), "rs_stairs stair=%d event=refused result=%s from_row=%d to_row=%d source=%s",
                      stairId, RsStair_ResultName(result), fromRow, toRow, source != nullptr ? source : "");
        if (result == RS_STAIR_ERR_BUSY) {
            AgentTest_WriteMarker(line); // must not overwrite the outcome of the move it bounced off
        } else {
            ReportOutcome(line);
        }
        return result;
    }

    sMove = Move();
    sMove.phase = Phase::FadeOut;
    sMove.stairId = stairId;
    sMove.fromRow = fromRow;
    sMove.toRow = toRow;
    sMove.fade = RsStair_GetFadeTicks();
    sMove.scene = gPlayState->sceneNum;
    sMove.source = source != nullptr ? source : "";

    // Freeze him, with NO cutscene actor: CsAction 1 turns Link to face its csActor every frame
    // (func_80851314), which would overwrite the landing's facing. From a conversation this is
    // picked up by Player_Action_Talk as the box closes; from idle, by the next action handler pass.
    Player_SetCsAction(gPlayState, nullptr, 1);

    const RsStairLanding* from = RsStair_GetLanding(stairId, fromRow);
    Marker("rs_stairs stair=%d event=move_begin from_row=%d to_row=%d from_storey=%d to_storey=%d fade=%d source=%s",
           stairId, fromRow, toRow, from != nullptr ? from->storey : -1, landing->storey, sMove.fade, sMove.source);
    return RS_STAIR_OK;
}

// --- C++ surface --------------------------------------------------------------------------------

RsStairStatus RsStair_GetStatus() {
    RsStairStatus status;
    status.moving = sMove.phase != Phase::Idle;
    status.phase = PhaseName(sMove.phase);
    status.stairId = sMove.stairId;
    status.fromRow = sMove.fromRow;
    status.toRow = sMove.toRow;
    status.ticks = sMove.ticks;
    status.fadeTicks = sMove.fade;
    status.source = sMove.source;
    status.last = sLast;
    return status;
}

int32_t RsStair_ListIds(int32_t* out, int32_t max) {
    int32_t count = 0;
    for (int32_t id = 0; id < RS_STAIR_MAX && count < max; id++) {
        if (sStairs[id] != nullptr) {
            out[count++] = id;
        }
    }
    return count;
}

std::string RsStair_ComposeBody(int32_t stairId, int32_t row) {
    const RsDialogueRule* screen = RsStair_Screen(stairId, row);
    return screen != nullptr ? RsFloorText_Compose(screen->text) : std::string();
}

std::string RsStair_ComposeLabel(int32_t stairId, int32_t row, int32_t index) {
    const RsDialogueRule* screen = RsStair_Screen(stairId, row);
    if (screen == nullptr || index < 0 || index >= screen->optionCount) {
        return std::string();
    }
    return RsFloorText_Compose(screen->options[index].label);
}
