#ifndef SOH_RS_ANIM_PROP_ACTOR_H
#define SOH_RS_ANIM_PROP_ACTOR_H

// Rs_AnimProp (sturdy-bassoon#204 C4a; the animated-props ADR's decision 52 and its C3 addendum): one flipbook
// placement, drawn by this actor instead of by the map's baked list. The prop generator wrote the copy's own
// pose lists, IN WORLD SPACE, by the very code that writes a baked copy (generate_props.py, FLIPBOOKS): the same
// turn, rounding, position and wobble phase. So the actor turns nothing: it picks a pose and submits that pose's
// lists under gMtxClear, exactly as the room submits its own (z_room.c, func_80095AB4) - its opaque list into
// POLY_OPA_DISP and its `.xlu` into POLY_XLU_DISP. The bake replays them like any registered list (one baked
// draw per material), and the scroll, the wobble and the translucency are the lists' own: nothing here.
//
// What the copy draws from the map's list instead: with the generator's still split (on by default), every pair
// that is the same in every pose - the torch's holder - stays baked, so this actor draws only what moves.
//
// The scene's ActorEntry row (RsActorParams.h for params; the grid tool's src/shared/flipbooks.ts writes it):
//   params  the copy's index in its list's `.anim` table (RS_ANIMPROP_PARAMS)
//   rot.x   which of the room's declared prop lists (ArchiveProps_DeclareRoom's array index)
//   rot.y   the placement's yaw; unused by the draw, which is in world space already
//   rot.z   the check: 15 bits of the placement id's sha256, which the table line carries too. A mismatch is
//           a row and an archive from two exports: the actor draws nothing and reports `check`
// The row's position is the placement's origin, which the cull and the light bind use.
//
// THE CLOCK is RS's (the ADR's C3 addendum): a pose lasts its length in 20 ms client cycles; past the last pose
// the client steps back `frameStep` poses, and a step out of range (RS's -1, a sequence that plays once) stops
// it, when it draws its rest pose. A looping copy reads the bake's clock (Fast::StaticBakeClockSeconds, which
// `staticbake clock` pins, so a pinned capture holds its pose) from the table's random start; a play-once copy
// counts from its Init.
//
// WHERE THE POSE IS PICKED (#204 C4b; `animprop pick`, CVAR_RS_ANIMPROP_PICK):
//   frame (the default): Draw writes a flip head (gSPFlipBook) per pass instead of the pose's list, and the
//          interpreter picks the pose on every rendered frame, interpolated ones included, from the same clock
//          (ArchivePropLists.h, FLIP HEADS). Poses hold RS's own lengths: the torch's 100 and 80 ms.
//   tick:  Draw picks the pose and writes its list, C4a exactly. SoH draws once a 20 Hz tick and replays that
//          for every interpolated frame (#208), so a pose changes at most once a tick and holds 50 or 100 ms.
//          Kept as the fallback and the planted control.
// Draw computes the tick's pose in both modes, for `animprop list` and `status` (changes counts it); the frame
// pick's own counts are the flips' (`flip_changes`). A forced pose (`animprop pose`) holds in both.
//
// THE LIGHTS: ACTOR_FLAG_IGNORE_POINTLIGHTS, so Actor_Draw binds the scene's lights with no position, as
// Play_Draw binds the room's: the copy is lit as its baked twin is, and no point light (a fire beside it) is
// bound for it - it pays the bind walk's directional pass only (#253).
//
// THE CULL: flags otherwise 0, so a copy outside its cull volume neither updates nor draws. The volume's
// forward reach is RS_ANIMPROP_DRAW_DISTANCE, Rs_Fire's, so a torch's flame and a fire on it go together.
//
// Nothing resident beyond the actor: the copy's lists are held by ArchiveProps for the bake group (fetched on
// the first draw, and again when ArchiveProps_Generation moves).

#include "z64actor.h"
#include "soh/Enhancements/staticbake/ArchivePropLists.h"

#define RS_ANIMPROP_DRAW_DISTANCE 6000 // uncullZoneForward; Rs_Fire's (RsFire.h). Actor_Init's is 1,000

// WHERE THE POSE IS PICKED: CVAR_DEVELOPER_TOOLS("RsAnimPropPick"), 1 frame, 0 tick (RsAnimPropConsole.cpp).
#define RS_ANIMPROP_PICK_DEFAULT 1

// A forced pose for the whole session (`animprop pose`): the clock's, a pose index, or rest.
#define RS_ANIMPROP_FORCE_CLOCK (-1)
#define RS_ANIMPROP_FORCE_REST (-2)

typedef struct RsAnimProp {
    /* 0x0000 */ Actor actor;
    /* */ const ArchivePropsAnimCopy* copy; // NULL until fetched, or when there is none (`miss` says why)
    /* */ uint32_t generation;              // ArchiveProps_Generation when `copy` was fetched; 0 never
    /* */ double initClock;                 // the bake clock at Init: a play-once copy counts from it
    /* */ uint32_t changes;                 // pose changes drawn
    /* */ uint32_t draws;                   // ticks drawn
    /* */ int16_t list;                     // rot.x
    /* */ int16_t check;                    // rot.z
    /* */ int16_t copyIndex;                // params
    /* */ int16_t miss;                     // ArchivePropsAnimMiss of the last fetch
    /* */ int16_t pose;                     // the pose last drawn (copy->poseCount: rest), -1 none yet
} RsAnimProp;

#ifdef __cplusplus
extern "C" {
#endif

// Which pose a copy shows `cycle` client cycles into its play, RS's way (THE CLOCK): a pose index, or
// copy->poseCount for rest once a play-once sequence ends. Pure.
int32_t RsAnimProp_PoseAt(const ArchivePropsAnimCopy* copy, int64_t cycle);
// Whether its sequence loops: frameStep 1..poseCount.
int32_t RsAnimProp_Loops(const ArchivePropsAnimCopy* copy);
// The cycle a fetched copy's play stands at when the bake clock reads `now` (THE CLOCK). Pure.
int64_t RsAnimProp_CycleAt(const RsAnimProp* prop, double now);

// The console's side (RsAnimPropConsole.cpp): the bake clock, the forced pose, and the per-frame tripwire.
double RsAnimProp_ClockSeconds(void);
int32_t RsAnimProp_ForcedPose(void);
// 1: the pose is picked per rendered frame (flip heads), 0: in Draw, per tick (WHERE THE POSE IS PICKED).
int32_t RsAnimProp_PickFrame(void);
// `animprop plant`'s list when this copy draws it (a flip head inside a baked list), else NULL.
Gfx* RsAnimProp_PlantList(const RsAnimProp* prop);
// Once a drawn frame, from the first copy that draws in it: the #172 texture-cache tripwire (RsAnimPropConsole.h).
void RsAnimProp_FrameTick(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_ANIM_PROP_ACTOR_H
