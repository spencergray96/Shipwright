#ifndef SOH_RS_STEER_H
#define SOH_RS_STEER_H

// Taking the controls of a vehicle (sturdy-bassoon#274): the pieces the balloon shares with the boat's helm (#256),
// written once so the next vehicle does not copy them a third time. The balloon uses all of them. The boat still
// has its own copies in RsBoat.c, kept there while #262 is in flight on that file; moving it onto these is a
// follow-up, and it is where #272's fix belongs (the lock-on letterbox staying when Z is held as the controls are
// taken), so that it lands for every vehicle at once.
//
// THE HAND-OVER. Taking the controls sets PLAYER_STATE1_INPUT_DISABLED (RemoteBombchu's trick: Player zeroes its
// input copy while it is set) and the vehicle reads play->state.input[0] itself. That only works from a vehicle in
// ACTORCAT_BG, which updates before ACTORCAT_PLAYER: the tick A takes the controls Player already sees no input,
// and the tick B lets go RsSteer_LetGo masks A and B for Player, or the same B swings the sword.
//
// THE CAMERA. A sub-camera at a target the vehicle picks each tick, every coordinate eased a share of the way
// there, pulled in front of any wall, and handed back to the main camera where it was left.

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Link is free to take the controls: on nothing but his feet, empty-handed and doing nothing else. 0 while he aims
// or looks in first person, has a sword or item out, holds, carries or throws something, shields, hangs, jumps,
// falls, talks, is hurt or is in a cutscene. The boat's RsBoat_LinkCanTakeHelm, the same test.
s32 RsSteer_LinkCanTake(const Player* player);

// Hands the pad to the vehicle, and back. LetGo masks A and B for Player for a few ticks.
void RsSteer_Take(Player* player);
void RsSteer_LetGo(Player* player);

// Hurt, dead, or moved further than `range` (xz) from where he stands to steer: a bomb's blast moves Link even
// with his input off. Any of them lets go.
s32 RsSteer_Knocked(const Player* player, const Vec3f* standPos, f32 range);

// Stands Link at `pos` facing `yaw`, as a teleport: prevPos, home.pos and fallStartHeight move with him (agenttest
// goto's recipe), or the wall sweep clamps him back and a drop onto the floor reads as a fall.
void RsSteer_StandLinkAt(Player* player, const Vec3f* pos, s16 yaw);

// The stick, as the boat's helm reads it (#262 point 3b): `ahead` and `back` are how far it is pushed (dead zone 8,
// full at 70), each scaled by how near straight up or straight down it points (full within 45 degrees of straight
// up, gone at 70; 30 and 55 down), so a diagonal drives and turns and hard left or right only turns. `turn` is its
// left-right, -1..1, + is left (yaw grows counter-clockwise from above). Each 0..1 otherwise.
void RsSteer_ReadStick(const Input* input, f32* ahead, f32* back, f32* turn);

typedef struct RsSteerCam {
    s16 subCamId; // SUBCAM_NONE when the normal camera has the view
    Vec3f eye;    // where it is now, eased toward its target each tick
    Vec3f at;
} RsSteerCam;

// Takes the view over from wherever the normal camera is, so the first ticks ease out of it. `on` 0, or no
// sub-camera free (a cutscene holds them), leaves the normal camera where it is.
void RsSteerCam_Start(RsSteerCam* cam, PlayState* play, s32 on);
// One tick toward (at, eye): each coordinate `ease` (0..1) of the way there.
void RsSteerCam_Update(RsSteerCam* cam, PlayState* play, const Vec3f* at, const Vec3f* eye, f32 ease);
// Hands the view back where the sub-camera left it.
void RsSteerCam_Stop(RsSteerCam* cam, PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_STEER_H
