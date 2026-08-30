/*
 * Distance-based room selection (sturdy-bassoon#6 Exp 4). See RoomDist.h for what this is and why.
 *
 * The whole trigger is the OnPlayerUpdate handler below. It runs at the end of Player_UpdateCommon,
 * i.e. inside Actor_UpdateAll and just before the En_Holl planes would update, so an armed
 * prototype sees exactly the position a plane would have seen on the same tick and the two
 * mechanisms' latencies are comparable without correcting for where in the tick they fired.
 *
 * The room change itself is the engine's own two-step, driven the same way En_Holl drives it:
 *
 *   tick N    Room_RequestNewRoom  - prevRoom = curRoom, curRoom.num = target, status = 1
 *   tick N    (later, in Play_Update) func_800973FC - initialises the room, status back to 0
 *   tick N+1  Room_FinishRoomChange - drops prevRoom, kills its actors, respawns transition actors
 *
 * The finish deliberately waits for the next tick rather than being pushed into the end of tick N,
 * because that is where EnHoll_WaitRoomLoaded does it and the comparison is meant to isolate *where
 * the trigger fires*, not how promptly the plumbing is pumped.
 */

#include "RoomDist.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "global.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

extern "C" uint8_t gRoomDistOn = 0;

namespace {

constexpr int32_t MAX_ROOMS = 64; // 8x8 chunks; well past anything this experiment builds

float sHysteresis = 0.0f;
int32_t sCentreCount = 0;
int16_t sCentreScene = -1;
float sCentreX[MAX_ROOMS];
float sCentreZ[MAX_ROOMS];

bool sPendingFinish = false; // a request is in flight; finish it once roomCtx.status clears
int32_t sRequests = 0;       // how many room changes this arming has driven - the flapping counter

char sDescription[160] = "roomdist off";
char sEvent[224];
bool sEventPending = false;
int32_t sEventsDropped = 0;

bool InNormalPlay() {
    return gPlayState != nullptr && gSaveContext.gameMode == GAMEMODE_NORMAL;
}

void RecordEvent(const char* fmt, ...) {
    if (sEventPending) {
        sEventsDropped++; // the poller runs every tick, so this should stay at zero
        return;
    }
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(sEvent, sizeof(sEvent), fmt, args);
    va_end(args);
    sEventPending = true;
}

/*
 * Room centres for the current scene, from the static-collision bounding box cut into an N x N
 * row-major grid (room index = row * N + col; row 0 is the -Z edge, col 0 the -X edge - the
 * north-west-first order gen_blender_scene.py's room_blocks() emits). Returns false if the scene's
 * room count is not a perfect square, in which case the prototype refuses to arm rather than
 * inventing a layout.
 */
bool BuildCentres(PlayState* play) {
    const int32_t rooms = play->numRooms;
    if (rooms < 1 || rooms > MAX_ROOMS) {
        return false;
    }
    int32_t n = 1;
    while (n * n < rooms) {
        n++;
    }
    if (n * n != rooms) {
        return false;
    }

    const float minX = play->colCtx.minBounds.x;
    const float minZ = play->colCtx.minBounds.z;
    const float spanX = (play->colCtx.maxBounds.x - minX) / static_cast<float>(n);
    const float spanZ = (play->colCtx.maxBounds.z - minZ) / static_cast<float>(n);
    for (int32_t r = 0; r < rooms; r++) {
        const int32_t row = r / n;
        const int32_t col = r % n;
        sCentreX[r] = minX + (static_cast<float>(col) + 0.5f) * spanX;
        sCentreZ[r] = minZ + (static_cast<float>(row) + 0.5f) * spanZ;
    }
    sCentreCount = rooms;
    sCentreScene = play->sceneNum;
    return true;
}

float DistXZ(float x, float z, int32_t room) {
    const float dx = x - sCentreX[room];
    const float dz = z - sCentreZ[room];
    return sqrtf(dx * dx + dz * dz);
}

void OnPlayerUpdateRoomDist() {
    if (!gRoomDistOn || !InNormalPlay()) {
        return;
    }
    PlayState* play = gPlayState;
    if (play->sceneNum != sCentreScene && !BuildCentres(play)) {
        // Scene changed under an armed prototype into one it cannot lay out. Stand down rather
        // than drive room changes off a stale table.
        RoomDist_Disable();
        RecordEvent("disarmed reason=unsupported_scene rooms=%d", play->numRooms);
        return;
    }

    RoomContext* roomCtx = &play->roomCtx;
    if (sPendingFinish) {
        if (roomCtx->status == 0) {
            Room_FinishRoomChange(play, roomCtx);
            sPendingFinish = false;
            RecordEvent("finish room=%d frame=%u", roomCtx->curRoom.num, play->state.frames);
        }
        return;
    }
    if (roomCtx->status != 0) {
        return; // somebody else's load in flight
    }

    Player* player = GET_PLAYER(play);
    const float x = player->actor.world.pos.x;
    const float z = player->actor.world.pos.z;
    const int32_t cur = roomCtx->curRoom.num;

    int32_t best = 0;
    float bestDist = DistXZ(x, z, 0);
    for (int32_t r = 1; r < sCentreCount; r++) {
        const float d = DistXZ(x, z, r);
        if (d < bestDist) {
            bestDist = d;
            best = r;
        }
    }
    if (best == cur || cur < 0 || cur >= sCentreCount) {
        return;
    }
    const float curDist = DistXZ(x, z, cur);
    if (curDist - bestDist <= sHysteresis) {
        return;
    }

    if (Room_RequestNewRoom(play, roomCtx, best)) {
        sPendingFinish = true;
        sRequests++;
        RecordEvent("request from=%d to=%d n=%d pos=%.1f,%.1f,%.1f dcur=%.1f dnew=%.1f frame=%u", cur, best,
                    sRequests, player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                    curDist, bestDist, play->state.frames);
    }
}

void RegisterRoomDist() {
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdateRoomDist);
}

} // namespace

extern "C" int32_t RoomDist_Configure(float hysteresis) {
    if (hysteresis < 0.0f || !InNormalPlay()) {
        return 1;
    }
    if (!BuildCentres(gPlayState)) {
        return 2;
    }
    sHysteresis = hysteresis;
    sPendingFinish = false;
    sRequests = 0;
    gRoomDistOn = 1;
    std::snprintf(sDescription, sizeof(sDescription),
                  "roomdist on hysteresis=%.0f rooms=%d grid=%dx%d bounds=%.0f,%.0f..%.0f,%.0f", hysteresis,
                  sCentreCount, static_cast<int>(sqrtf(static_cast<float>(sCentreCount)) + 0.5f),
                  static_cast<int>(sqrtf(static_cast<float>(sCentreCount)) + 0.5f), gPlayState->colCtx.minBounds.x,
                  gPlayState->colCtx.minBounds.z, gPlayState->colCtx.maxBounds.x, gPlayState->colCtx.maxBounds.z);
    return 0;
}

extern "C" void RoomDist_Disable(void) {
    gRoomDistOn = 0;
    sPendingFinish = false;
    std::snprintf(sDescription, sizeof(sDescription), "roomdist off requests=%d", sRequests);
}

extern "C" const char* RoomDist_Describe(void) {
    return sDescription;
}

extern "C" int32_t RoomDist_TakeEvent(char* buf, uint32_t size) {
    if (!sEventPending || buf == nullptr || size == 0) {
        return 0;
    }
    std::snprintf(buf, size, "%s%s", sEvent, sEventsDropped > 0 ? " (events dropped)" : "");
    sEventPending = false;
    return 1;
}

static RegisterShipInitFunc initFuncRoomDist(RegisterRoomDist);
