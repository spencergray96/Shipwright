/*
 * RsSteer.c - taking the controls of a vehicle (sturdy-bassoon#274). RsSteer.h has the design.
 *
 * Same /W3 /WX rules as the other rs/ actors: explicit casts, no `s32 pad`.
 */

#include <math.h>

#include "RsSteer.h"
#include "global.h"

// z_camera.c's, not in functions.h; RemoteBombchu.cpp declares it the same way. Pulls `to` back to the first
// wall between `from` and it, so the steering camera never sits inside the scenery.
s32 Camera_BGCheck(Camera* camera, Vec3f* from, Vec3f* to);
// z_camera.c's too: sets the letterbox (and the HUD's alpha) from a camera mode's interface flags.
void Camera_UpdateInterface(s16 flags);

// Bar size 0, the HUD nibble left alone (0xF is "keep"): what a camera mode with no bars asks for, so the
// letterbox retracts the way it does when Z is let go.
#define RS_STEER_IFACE_NO_BARS 0x0F00

// The stick reads -85..85 a side. Inside the dead zone it is centred; at RS_STEER_STICK_FULL from the centre it is
// full, so the rim is reached a little before the gate. Radians: 45 and 70 degrees up, 30 and 55 down.
#define RS_STEER_STICK_DEAD 8.0f
#define RS_STEER_STICK_FULL 70.0f
#define RS_STEER_ARC_AHEAD_FULL 0.7854f
#define RS_STEER_ARC_AHEAD_END 1.2217f
#define RS_STEER_ARC_BACK_FULL 0.5236f
#define RS_STEER_ARC_BACK_END 0.9599f

s32 RsSteer_LinkCanTake(const Player* player) {
    const u32 busy = PLAYER_STATE1_HOOKSHOT_FALLING | PLAYER_STATE1_ITEM_IN_HAND | PLAYER_STATE1_INPUT_DISABLED |
                     PLAYER_STATE1_TALKING | PLAYER_STATE1_DEAD | PLAYER_STATE1_START_CHANGING_HELD_ITEM |
                     PLAYER_STATE1_READY_TO_FIRE | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_CARRYING_ACTOR |
                     PLAYER_STATE1_CHARGING_SPIN_ATTACK | PLAYER_STATE1_HANGING_OFF_LEDGE |
                     PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_JUMPING | PLAYER_STATE1_FREEFALL |
                     PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_SHIELDING |
                     PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_USING_BOOMERANG | PLAYER_STATE1_DAMAGED |
                     PLAYER_STATE1_IN_WATER | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE;

    // unk_6AD is Player's camera mode: 0 is the normal third-person view; 1 and 2 are first person, with and
    // without an item drawn. heldItemAction is what is in his hand: a drawn sword too, when A is "Put Away" and
    // vanilla's sheathe should win - the next A then takes the controls.
    return (player->stateFlags1 & busy) == 0 && player->unk_6AD == 0 && player->heldActor == NULL &&
           player->talkActor == NULL && player->heldItemAction == PLAYER_IA_NONE;
}

void RsSteer_Take(Player* player) {
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
}

void RsSteer_LetGo(Player* player) {
    player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
    // The B that let go reaches Player this same tick (the vehicle updates first): without the cooldown it swings
    // the sword.
    player->textboxBtnCooldownTimer = 4;
}

s32 RsSteer_Knocked(const Player* player, const Vec3f* standPos, f32 range) {
    return (player->stateFlags1 & (PLAYER_STATE1_DAMAGED | PLAYER_STATE1_DEAD)) != 0 ||
           Math_Vec3f_DistXZ((Vec3f*)standPos, (Vec3f*)&player->actor.world.pos) > range;
}

void RsSteer_StandLinkAt(Player* player, const Vec3f* pos, s16 yaw) {
    player->actor.world.pos = *pos;
    player->actor.prevPos = *pos;
    player->actor.home.pos = *pos;
    player->fallStartHeight = (s16)pos->y;
    player->actor.shape.rot.y = yaw;
    player->actor.world.rot.y = yaw;
    player->yaw = yaw;
    player->linearVelocity = 0.0f;
    player->actor.speedXZ = 0.0f;
}

void RsSteer_ReadStick(const Input* input, f32* ahead, f32* back, f32* turn) {
    const f32 stickX = (f32)input->cur.stick_x;
    const f32 stickY = (f32)input->cur.stick_y;
    const f32 amount = CLAMP((sqrtf(stickX * stickX + stickY * stickY) - RS_STEER_STICK_DEAD) /
                                 (RS_STEER_STICK_FULL - RS_STEER_STICK_DEAD),
                             0.0f, 1.0f);

    *ahead = 0.0f;
    *back = 0.0f;
    if (stickY > 0.0f) {
        *ahead = amount * CLAMP((RS_STEER_ARC_AHEAD_END - fabsf(atan2f(stickX, stickY))) /
                                    (RS_STEER_ARC_AHEAD_END - RS_STEER_ARC_AHEAD_FULL),
                                0.0f, 1.0f);
    } else if (stickY < 0.0f) {
        *back = amount * CLAMP((RS_STEER_ARC_BACK_END - fabsf(atan2f(stickX, -stickY))) /
                                   (RS_STEER_ARC_BACK_END - RS_STEER_ARC_BACK_FULL),
                               0.0f, 1.0f);
    }
    *turn = CLAMP(-stickX / RS_STEER_STICK_FULL, -1.0f, 1.0f);
}

void RsSteerCam_Start(RsSteerCam* cam, PlayState* play, s32 on) {
    Camera* main;

    cam->subCamId = SUBCAM_NONE;
    if (!on) {
        return;
    }
    cam->subCamId = Play_CreateSubCamera(play);
    if (cam->subCamId == SUBCAM_NONE) {
        return;
    }
    main = Play_GetCamera(play, CAM_ID_MAIN);
    cam->eye = main->eye;
    cam->at = main->at;
    Play_ChangeCameraStatus(play, CAM_ID_MAIN, CAM_STAT_WAIT);
    Play_ChangeCameraStatus(play, cam->subCamId, CAM_STAT_ACTIVE);
    Play_CameraSetAtEye(play, cam->subCamId, &cam->at, &cam->eye);
    // #272: a fresh sub-camera is CAM_SET_FREE0, whose interface flags (0xFF00) leave the letterbox as it is,
    // and the main camera stops setting it once it waits. Taken with Z held, the lock-on bars froze on for
    // the whole time at the controls. So retract them here, as releasing Z would; letting go hands the view
    // back to the main camera, whose own mode puts them back only if Link is still Z-targeting.
    Camera_UpdateInterface(RS_STEER_IFACE_NO_BARS);
}

void RsSteerCam_Update(RsSteerCam* cam, PlayState* play, const Vec3f* at, const Vec3f* eye, f32 ease) {
    Vec3f pulled;

    if (cam->subCamId == SUBCAM_NONE) {
        return;
    }
    Math_ApproachF(&cam->at.x, at->x, ease, 400.0f);
    Math_ApproachF(&cam->at.y, at->y, ease, 400.0f);
    Math_ApproachF(&cam->at.z, at->z, ease, 400.0f);
    Math_ApproachF(&cam->eye.x, eye->x, ease, 400.0f);
    Math_ApproachF(&cam->eye.y, eye->y, ease, 400.0f);
    Math_ApproachF(&cam->eye.z, eye->z, ease, 400.0f);
    pulled = cam->eye;
    Camera_BGCheck(Play_GetCamera(play, cam->subCamId), &cam->at, &pulled);
    Play_CameraSetAtEye(play, cam->subCamId, &cam->at, &pulled);
}

void RsSteerCam_Stop(RsSteerCam* cam, PlayState* play) {
    if (cam->subCamId == SUBCAM_NONE) {
        return;
    }
    Play_CameraSetAtEye(play, CAM_ID_MAIN, &cam->at, &cam->eye);
    Play_ChangeCameraStatus(play, CAM_ID_MAIN, CAM_STAT_ACTIVE);
    Play_ClearCamera(play, cam->subCamId);
    cam->subCamId = SUBCAM_NONE;
}
