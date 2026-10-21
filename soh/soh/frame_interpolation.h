#pragma once

#include "include/z64math.h"

#ifdef __cplusplus

#include <unordered_map>

std::unordered_map<Mtx*, MtxF> FrameInterpolation_Interpolate(float step);

// #region SOH [sturdy-bassoon#254] The shadow probe, for `lights shadowinterp`. Between two 20 Hz ticks
// the interpolated frames replace each matrix with a lerp of the two ticks' recordings; for each matrix
// recorded inside RecordProbeBegin/End (Link's shadowDraw), the probe compares every in-between frame's
// replacement with the matrix the tick itself drew. One entry a tick that recorded any; none at 20 fps,
// where nothing is interpolated.
struct FrameInterpolationProbeTick {
    u32 frame;    // state.frames of the tick
    s16 yaw;      // the actor's shape.rot.y as it drew
    s32 matrices; // matrices probed
    s32 frames;   // interpolated frames drawn between the last tick and this one
    s32 measured; // matrix-frames compared (an in-between frame with no replacement for one is skipped)
    f32 minSize;  // the smallest in-between size against the drawn one (1 = unchanged); see probe_measure
    f32 minCos;   // the smallest cosine between an in-between long axis and the drawn one (-1 = reversed)
};

// Oldest first, at most `max`; returns the count.
int FrameInterpolation_GetProbeRecord(FrameInterpolationProbeTick* out, int max);
void FrameInterpolation_ClearProbeRecord(void);
// #endregion

extern "C" {

#endif

void FrameInterpolation_StartRecord(void);

void FrameInterpolation_StopRecord(void);

void FrameInterpolation_RecordOpenChild(const void* a, int b);

void FrameInterpolation_RecordCloseChild(void);

void FrameInterpolation_DontInterpolateCamera(void);

int FrameInterpolation_GetCameraEpoch(void);

void FrameInterpolation_RecordActorPosRotMatrix(void);

void FrameInterpolation_RecordMatrixPush(void);

void FrameInterpolation_RecordMatrixPop(void);

void FrameInterpolation_RecordMatrixPut(MtxF* src);

void FrameInterpolation_RecordMatrixMult(MtxF* mf, u8 mode);

void FrameInterpolation_RecordMatrixTranslate(f32 x, f32 y, f32 z, u8 mode);

void FrameInterpolation_RecordMatrixScale(f32 x, f32 y, f32 z, u8 mode);

void FrameInterpolation_RecordMatrixRotate1Coord(u32 coord, f32 value, u8 mode);

void FrameInterpolation_RecordMatrixRotateZYX(s16 x, s16 y, s16 z, u8 mode);

void FrameInterpolation_RecordMatrixTranslateRotateZYX(Vec3f* translation, Vec3s* rotation);

void FrameInterpolation_RecordMatrixSetTranslateRotateYXZ(f32 translateX, f32 translateY, f32 translateZ, Vec3s* rot);

void FrameInterpolation_RecordMatrixMtxFToMtx(MtxF* src, Mtx* dest);

void FrameInterpolation_RecordMatrixToMtx(Mtx* dest, char* file, s32 line);

void FrameInterpolation_RecordMatrixReplaceRotation(MtxF* mf);

void FrameInterpolation_RecordMatrixRotateAxis(f32 angle, Vec3f* axis, u8 mode);

void FrameInterpolation_RecordSkinMatrixMtxFToMtx(MtxF* src, Mtx* dest);

// sturdy-bassoon#254: opens and closes the shadow probe's scope (Link's shadowDraw in Actor_Draw).
void FrameInterpolation_RecordProbeBegin(u32 frame, s16 yaw);

void FrameInterpolation_RecordProbeEnd(void);

#ifdef __cplusplus
}
#endif
