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
#include "soh/Enhancements/rs/actors/RsActorParams.h"
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

// The default is a 6-tick FADE each way (0.3 s out, 0.1 s held black, 0.3 s in). Both it and a hard
// cut were built and compared on the castle's tower shaft (#147 ADR, "The fade"): from filmstrips
// the cut looked cleaner once SeatCamera fixed its first frame, and the owner's play test chose the
// fade anyway - it reads better in hand. 0 is the hard cut, kept one command away:
// `stairs fade 0` overrides this per install without a rebuild; `stairs fade default` clears the
// override and comes back here.
constexpr int32_t kDefaultFadeTicks = 6;

// WALK INTO IT (#151). ON by default, provisionally, until the owner has played it on and off.
#define CVAR_RS_STAIRS_BUMP CVAR_ENHANCEMENT("RsStairsBump")
#define CVAR_RS_STAIRS_BUMP_HOLD CVAR_ENHANCEMENT("RsStairsBumpHold")
constexpr int32_t kDefaultBump = 1;
// How long Link must push into a placement before its menu opens: 2 ticks, 0.1 s. Short on purpose,
// because the hold is not what stops a graze - the aim cone is: every graze in the #151 run met the
// collider 53 to 87 degrees off-centre, outside the 30-degree cone from its first tick, and never
// started a count. What the hold decides is how nearly dead-centre a real push must be, because Link
// SLIDES round a round collider he is not pushing dead into, and an off-centre push slides out of
// the cone within a few ticks. Measured, running at the shaft from off its centre line: 2 ticks opens
// within 6 units of dead centre, 4 within 3, 8 only dead on (#151 run record, the sweep).
constexpr int32_t kDefaultBumpHold = 2;

// Ticks spent at full black after the move, before fading back in. Player's floor raycast runs on
// its next update, so the first of these is what gives `floorHeight` - and the respawn point, which
// reads live position - a floor to report; the second is margin for the camera's first real update
// from its new position. With a hard cut this is also how long Link stays frozen after landing.
constexpr int32_t kSettleTicks = 2;

// Half a storey. Within it, Link is "on" a placement's storey.
constexpr float kRowSnap = 40.0f;

// Ticks a cross-room move waits, after the new room is in, for the destination placement to
// appear. Play_Update processes the room request before Actor_UpdateAll, which spawns the room's
// setup actors before Player updates - so the placement is normally there on the tick the room
// finishes. The grace is for the order changing under us, not a known delay.
constexpr int32_t kArriveTicks = 2;

enum class Phase { Idle, FadeOut, WaitRoom, Arrive, Settle, FadeIn, ReturnRoom };

const char* PhaseName(Phase phase) {
    switch (phase) {
        case Phase::Idle:
            return "idle";
        case Phase::FadeOut:
            return "fade_out";
        case Phase::WaitRoom:
            return "wait_room";
        case Phase::Arrive:
            return "arrive";
        case Phase::Settle:
            return "settle";
        case Phase::FadeIn:
            return "fade_in";
        case Phase::ReturnRoom:
            return "return_room";
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
    int32_t roomFrom = -1; // the room Link was in when the move began
    int32_t roomTo = -1;   // the destination storey's room
    bool roomRequested = false;
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

int32_t ClampTicks(int32_t ticks, int32_t lo, int32_t hi) {
    return ticks < lo ? lo : (ticks > hi ? hi : ticks);
}

int32_t ClampFade(int32_t ticks) {
    return ClampTicks(ticks, 0, RS_STAIR_MAX_FADE_TICKS);
}

int32_t ClampBumpHold(int32_t ticks) {
    return ClampTicks(ticks, 1, RS_STAIR_MAX_BUMP_HOLD);
}

// Whether a setting's CVar holds an override. A sentinel default rather than CVarExists:
// consolevariablebridge.h declares that one, but this libultraship does not define it, and the only
// symptom is an unresolved external at link.
int32_t CVarIsSet(const char* name) {
    constexpr int32_t kUnset = INT32_MIN;
    return CVarGetInteger(name, kUnset) != kUnset ? 1 : 0;
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

// Ends a move that cannot finish in a scene that carries on: Link has not moved, so the screen
// comes back and he is given back where he stood. `returned` says whether the move had to load the
// room he started in again first.
void CancelInPlace(PlayState* play, const char* reason, int32_t returned) {
    char line[200];
    std::snprintf(line, sizeof(line), "rs_stairs stair=%d event=abort reason=%s to_row=%d returned=%d room=%d ticks=%d",
                  sMove.stairId, reason, sMove.toRow, returned, play->roomCtx.curRoom.num, sMove.ticks);
    ReportOutcome(line);
    ClearFill(play);
    ReleasePlayer(play);
    sMove = Move();
}

// The live placement standing on (staircase, row), or null - not placed, or in a room that is not
// loaded. Staircases are ACTORCAT_PROP (RsActors.cpp), so that one list is the whole search; an
// actor already killed this frame (update == NULL) does not count.
Actor* FindPlacement(PlayState* play, int32_t stairId, int32_t row) {
    for (Actor* actor = play->actorCtx.actorLists[ACTORCAT_PROP].head; actor != nullptr; actor = actor->next) {
        if (actor->id == ACTOR_RS_STAIRS && actor->update != nullptr &&
            RS_STAIR_PARAMS_GET_ID(actor->params) == stairId && RS_STAIR_PARAMS_GET_ROW(actor->params) == row) {
            return actor;
        }
    }
    return nullptr;
}

// WHERE LINK LANDS, from the placement rather than a table: `landForward` in front of it, on its
// floor, facing the way it faces. `home` rather than `world`, because home is exactly what the scene
// authored (Actor_Spawn copies the ActorEntry into it) and nothing ever moves it.
bool LandingFromPlacement(PlayState* play, const RsStairDef& def, int32_t row, Vec3f* pos, s16* yaw) {
    const Actor* placement = FindPlacement(play, def.id, row);
    if (placement == nullptr) {
        return false;
    }
    const s16 facing = placement->home.rot.y;
    const f32 forward = static_cast<f32>(def.landForward);
    pos->x = placement->home.pos.x + forward * Math_SinS(facing);
    pos->y = placement->home.pos.y;
    pos->z = placement->home.pos.z + forward * Math_CosS(facing);
    *yaw = facing;
    return true;
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
void Teleport(PlayState* play, Player* player, const Vec3f& pos, s16 yaw) {
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

// Puts Link down in front of the destination storey's placement and moves on to Settle. False,
// with nothing moved, when that placement is not in the actor list.
bool Land(PlayState* play, Player* player) {
    const RsStairDef* def = RsStair_GetDef(sMove.stairId);
    const RsStairLanding* landing = RsStair_GetLanding(sMove.stairId, sMove.toRow);
    Vec3f pos;
    s16 yaw = 0;
    if (def == nullptr || landing == nullptr || !LandingFromPlacement(play, *def, sMove.toRow, &pos, &yaw)) {
        return false;
    }
    const Vec3f before = player->actor.world.pos;
    Teleport(play, player, pos, yaw);
    // `black=` is whether the screen is held black as the move lands, and `eye=` where the snapped
    // camera starts - both what a screenshot would have to catch on exactly the right frame to
    // prove. `room=` is the room Link is in as he lands: on a cross-room move the new room is
    // already in by then.
    const Vec3f eye = Play_GetCamera(play, CAM_ID_MAIN)->eye;
    Marker("rs_stairs stair=%d event=moved to_row=%d storey=%d from=%.1f,%.1f,%.1f pos=%.1f,%.1f,%.1f yaw=%d "
           "room=%d room_change=%d black=%d eye=%.1f,%.1f,%.1f ticks=%d",
           sMove.stairId, sMove.toRow, landing->storey, before.x, before.y, before.z, player->actor.world.pos.x,
           player->actor.world.pos.y, player->actor.world.pos.z, player->actor.shape.rot.y, play->roomCtx.curRoom.num,
           sMove.roomTo != sMove.roomFrom ? 1 : 0,
           (play->envCtx.fillScreen && play->envCtx.screenFillColor[3] == 255) ? 1 : 0, eye.x, eye.y, eye.z,
           sMove.ticks);
    sMove.phase = Phase::Settle;
    sMove.phaseTick = 0;
    return true;
}

// One step of a room change: request `room` when nothing else is loading, then finish it once it is
// in. True on the tick the change completes. RoomDist.cpp's shape. The finish is what kills every
// actor outside the new room - usually including the staircase that opened the menu, which is why
// none of this lives in the actor.
bool StepRoomChange(PlayState* play, int32_t room) {
    RoomContext* roomCtx = &play->roomCtx;
    if (!sMove.roomRequested) {
        if (roomCtx->status != 0) {
            return false; // somebody else's load in flight; ask when it is done
        }
        const int32_t from = roomCtx->curRoom.num;
        if (!Room_RequestNewRoom(play, roomCtx, room)) {
            return false;
        }
        sMove.roomRequested = true;
        Marker("rs_stairs stair=%d event=room_request from=%d to=%d ticks=%d", sMove.stairId, from, room, sMove.ticks);
        return false;
    }
    if (roomCtx->status != 0) {
        return false;
    }
    const int32_t from = roomCtx->prevRoom.num;
    Room_FinishRoomChange(play, roomCtx);
    Marker("rs_stairs stair=%d event=room from=%d to=%d ticks=%d", sMove.stairId, from, roomCtx->curRoom.num,
           sMove.ticks);
    sMove.roomRequested = false;
    return true;
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

// THE CONTROLLER. One tick per Player update:
//
//   FadeOut --same room--> land --> Settle --> FadeIn --> landed
//      `--other room--> WaitRoom --> Arrive --> land --'
//                                      `--no placement--> ReturnRoom --> abort, returned=1
//
// A cross-room move loads the room BEFORE it lands, because the placement it lands in front of is
// in that room's actor list and does not exist until the room does. The screen is black for all of
// it, fade or cut.
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
    sMove.ticks++;

    switch (sMove.phase) {
        case Phase::FadeOut:
            sMove.phaseTick++;
            if (sMove.phaseTick < sMove.fade) {
                SetFill(play, FadeAlpha(sMove.phaseTick, sMove.fade));
                return;
            }
            // A hard cut still goes black across a ROOM CHANGE. Until Room_FinishRoomChange the
            // destination room is not the one being drawn, so a cut would show the load. Finish or
            // CancelInPlace clears it.
            if (sMove.fade > 0 || sMove.roomTo != sMove.roomFrom) {
                SetFill(play, 255);
            }
            if (sMove.roomTo != sMove.roomFrom) {
                sMove.phase = Phase::WaitRoom;
                sMove.phaseTick = 0;
                return;
            }
            // Same room: BeginMove saw the placement, so this fails only if it was killed since.
            if (!Land(play, player)) {
                CancelInPlace(play, "no_placement", 0);
            }
            return;
        case Phase::WaitRoom:
            if (StepRoomChange(play, sMove.roomTo)) {
                sMove.phase = Phase::Arrive;
                sMove.phaseTick = 0;
            }
            return;
        case Phase::Arrive:
            if (Land(play, player)) {
                return;
            }
            if (++sMove.phaseTick < kArriveTicks) {
                return;
            }
            // The room is in and the storey has no placement - an authoring mistake, and one only a
            // room load could reveal. Link has not moved, so load his own room back and give him back
            // where he stood, rather than leave him standing in a room that is not drawn.
            Marker("rs_stairs stair=%d event=no_placement to_row=%d room=%d ticks=%d", sMove.stairId, sMove.toRow,
                   play->roomCtx.curRoom.num, sMove.ticks);
            sMove.phase = Phase::ReturnRoom;
            sMove.phaseTick = 0;
            return;
        case Phase::ReturnRoom:
            if (StepRoomChange(play, sMove.roomFrom)) {
                CancelInPlace(play, "no_placement", 1);
            }
            return;
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
        case RS_STAIR_ERR_NO_PLACEMENT:
            return "no_placement";
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
        case RS_STAIR_PROBLEM_LAND_FORWARD:
            return "land_forward";
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
    if (def->landForward < RS_STAIR_MIN_LAND_FORWARD) {
        return RS_STAIR_PROBLEM_LAND_FORWARD;
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

extern "C" int32_t RsStair_PlacedLanding(int32_t stairId, int32_t row, float* x, float* y, float* z, int16_t* yaw) {
    const RsStairDef* def = RsStair_GetDef(stairId);
    Vec3f pos;
    s16 facing = 0;
    if (def == nullptr || gPlayState == nullptr || !LandingFromPlacement(gPlayState, *def, row, &pos, &facing)) {
        return 0;
    }
    *x = pos.x;
    *y = pos.y;
    *z = pos.z;
    *yaw = facing;
    return 1;
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
        const Actor* placement = FindPlacement(gPlayState, stairId, row);
        if (placement == nullptr) {
            continue; // not in a loaded room, so not the storey he is standing on
        }
        const float dist = std::fabs(placement->home.pos.y - y);
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
    return CVarIsSet(CVAR_RS_STAIRS_FADE);
}

extern "C" int32_t RsStair_BumpEnabled(void) {
    return CVarGetInteger(CVAR_RS_STAIRS_BUMP, kDefaultBump) != 0 ? 1 : 0;
}

extern "C" void RsStair_SetBumpEnabled(int32_t on) {
    CVarSetInteger(CVAR_RS_STAIRS_BUMP, on != 0 ? 1 : 0);
    CVarSave();
}

extern "C" void RsStair_ClearBumpEnabled(void) {
    CVarClear(CVAR_RS_STAIRS_BUMP);
    CVarSave();
}

extern "C" int32_t RsStair_BumpEnabledOverridden(void) {
    return CVarIsSet(CVAR_RS_STAIRS_BUMP);
}

extern "C" int32_t RsStair_GetBumpHold(void) {
    return ClampBumpHold(CVarGetInteger(CVAR_RS_STAIRS_BUMP_HOLD, kDefaultBumpHold));
}

extern "C" void RsStair_SetBumpHold(int32_t ticks) {
    CVarSetInteger(CVAR_RS_STAIRS_BUMP_HOLD, ClampBumpHold(ticks));
    CVarSave();
}

extern "C" void RsStair_ClearBumpHold(void) {
    CVarClear(CVAR_RS_STAIRS_BUMP_HOLD);
    CVarSave();
}

extern "C" int32_t RsStair_BumpHoldOverridden(void) {
    return CVarIsSet(CVAR_RS_STAIRS_BUMP_HOLD);
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
    } else if (landing->room == gPlayState->roomCtx.curRoom.num &&
               FindPlacement(gPlayState, stairId, toRow) == nullptr) {
        // Knowable now only when the destination is in the loaded room; in another room the move
        // finds out after loading it, and goes back (the controller's Arrive phase).
        result = RS_STAIR_ERR_NO_PLACEMENT;
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
    sMove.roomFrom = gPlayState->roomCtx.curRoom.num;
    sMove.roomTo = landing->room;

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
