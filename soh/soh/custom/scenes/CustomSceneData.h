#pragma once
// Shared header for every compiled-in custom scene's C data files - the ones
// tools/grid-scene-tool writes directly, the ones tools/terrain/integrate-fast64.ts post-processes
// out of a real Fast64 export, and the hand-maintained test_level.
//
// It exists because Fast64-style scene C uses names and macros that Shipwright does not publish,
// and every scene needs the same set. They lived in two near-identical copies
// (grid_tool/GridToolSceneData.h and test_level/test_level_scene_data.h) until those copies drifted
// apart on SURFACETYPE1 and only one of them was right - issue #41. One definition now, so that
// cannot happen again; both old headers are shims that include this.
#include "global.h"

// Fast64 uses decomp's names for these; map them to the Shipwright equivalents from z64.h.
typedef PolygonType0 RoomShapeNormal;
typedef PolygonDlist RoomShapeDListsEntry;
#define ROOM_SHAPE_TYPE_NORMAL 0
// `Spawn` and `BgCamInfo` appear only in a genuine Fast64 export - the grid tool's own writers
// emit neither - but they are the same kind of rename as the two above.
typedef EntranceEntry Spawn;
typedef CamData BgCamInfo;

// Fast64 writes actor and transition-actor rotations in degrees through decomp's DEG_TO_BINANG,
// which Shipwright does not define. 0x8000 binang is half a turn, so a full turn is 0x10000 and
// anything at or past 180 degrees lands outside s16 - the double cast is decomp's own, and is
// what makes that wrap explicit rather than a narrowing conversion MSVC would reject.
#ifndef DEG_TO_BINANG
#define DEG_TO_BINANG(degreesf) (s16)(s32)((degreesf) * (0x8000 / 180.0f))
#endif

// EnvLightSettings.fogNear packs the blend rate into its top bits. Fast64 defines this macro in
// the scene header it generates, which this fork replaces, so it is redeclared here verbatim.
#ifndef BLEND_RATE_AND_FOG_NEAR
#define BLEND_RATE_AND_FOG_NEAR(blendRate, fogNear) (s16)((((blendRate) / 4) << 10) | (fogNear))
#endif

// Macros used by Fast64-style collision output that are not in Shipwright public headers.
// COLPOLY_VTX itself lives in z64bgcheck.h (via global.h above): it encodes the engine's vertex-id
// bit layout, so a copy here would silently disagree with the engine the moment that widens.
#define COLPOLY_IGNORE_NONE 0
// The 3-bit poly vtx flag field above the vertex id - mirrors z_bgcheck.c's file-local defines.
// vIA (vtxData[0]) carries the ignore flags; vIB (vtxData[1]) carries the conveyor flag.
#define COLPOLY_IGNORE_CAMERA      (1 << 0)
#define COLPOLY_IGNORE_ENTITY      (1 << 1)
#define COLPOLY_IGNORE_PROJECTILES (1 << 2)
#define COLPOLY_VIB_CONVEYOR       (1 << 0)

// SurfaceType is two u32 words of bitfields. Every field below is placed where this fork's own
// accessors in z_bgcheck.c read it - the bit ranges are that file's shifts and masks, not decomp's
// documentation - and the argument order is the one Fast64 emits positionally. The static asserts
// at the bottom of this header check both halves against those same shifts and masks.
//
// The pre-#41 versions of these macros accepted all eight arguments and honoured one to five of
// them, in the wrong bits. Everything the tree emits today is zero except SURFACETYPE1's material
// nibble, so only test_level's collision actually changed when this was fixed.
//
// Deliberately not #ifndef-guarded, unlike DEG_TO_BINANG and BLEND_RATE_AND_FOG_NEAR above. Those
// two can legitimately arrive from a Fast64-generated header; a second definition of these two is
// the #41 bug itself, and should be a loud redefinition error rather than a silent no-op.

// SurfaceType.data[0]
//   bgCamIndex     [7:0]   SurfaceType_GetCamDataIndex
//   exitIndex      [12:8]  SurfaceType_GetSceneExitIndex
//   floorType      [17:13] SurfaceType_GetFloorType
//   unk18          [20:18] func_80041D70
//   wallType       [25:21] func_80041D94 (indexes D_80119D90 for the wall flags)
//   floorProperty  [29:26] func_80041E80 / func_80041EA4
//   isSoft         [30]    func_80041EC8
//   isHorseBlocked [31]    SurfaceType_IsHorseBlocked
#define SURFACETYPE0(bgCamIndex, exitIndex, floorType, unk18, wallType, floorProperty, isSoft,     \
                     isHorseBlocked)                                                               \
    (((u32)(bgCamIndex) & 0xFFu) | (((u32)(exitIndex) & 0x1Fu) << 8) |                             \
     (((u32)(floorType) & 0x1Fu) << 13) | (((u32)(unk18) & 0x07u) << 18) |                         \
     (((u32)(wallType) & 0x1Fu) << 21) | (((u32)(floorProperty) & 0x0Fu) << 26) |                  \
     (((u32)(isSoft) & 0x01u) << 30) | (((u32)(isHorseBlocked) & 0x01u) << 31))

// SurfaceType.data[1]
//   material          [3:0]   func_80041F10 / SurfaceType_GetSfx - indexes D_80119E10 in
//                             z_bgcheck.c for the footstep sound (e.g. 8 = grass); out-of-range
//                             values fall back to NA_SE_PL_WALK_GROUND
//   floorEffect       [5:4]   SurfaceType_GetFloorEffect (1 = slope/sliding, 2 = transition)
//   lightSetting      [10:6]  SurfaceType_GetLightSettingIndex
//   echo              [16:11] SurfaceType_GetEcho
//   canHookshot       [17]    SurfaceType_IsHookshotSurface
//   conveyorSpeed     [20:18] SurfaceType_GetConveyorSpeed - 1..3 indexes
//                             sFloorConveyorSpeeds/sWaterConveyorSpeeds[speed-1] in z_player.c; a
//                             poly with speed set is a floor conveyor when its vIB conveyor flag is
//                             set and a water current when it is not
//   conveyorDirection [26:21] SurfaceType_GetConveyorDirection - 360/64-degree units: 0 pushes +Z,
//                             16 +X, 32 -Z, 48 -X
//   isWallDamage      [27]    SurfaceType_IsWallDamage
#define SURFACETYPE1(material, floorEffect, lightSetting, echo, canHookshot, conveyorSpeed,        \
                     conveyorDirection, isWallDamage)                                              \
    (((u32)(material) & 0x0Fu) | (((u32)(floorEffect) & 0x03u) << 4) |                             \
     (((u32)(lightSetting) & 0x1Fu) << 6) | (((u32)(echo) & 0x3Fu) << 11) |                        \
     (((u32)(canHookshot) & 0x01u) << 17) | (((u32)(conveyorSpeed) & 0x07u) << 18) |               \
     (((u32)(conveyorDirection) & 0x3Fu) << 21) | (((u32)(isWallDamage) & 0x01u) << 27))

#ifdef __cplusplus
// Round-trip guard (issue #41). Each line packs a distinct value through the macro and reads it
// back with the same shift and mask the engine's accessor uses, so a bad edit to a macro above
// fails the build instead of the scene going quietly wrong in-game. Every probe field sets the top
// bit of its own width, so a field that lands one bit narrow or one bit shifted cannot still
// satisfy its own assert.
//
// Its limit is worth stating plainly rather than trusting: these are hand-mirrored copies of the
// accessors' shifts and masks, not calls to them - the real accessors take a live CollisionContext
// and cannot run at compile time. So this catches a mistake on *this* side of the mirror and would
// not notice z_bgcheck.c changing underneath it. When you touch either side, re-read the other.
//
// This is C++-only because the header is included by ~70 .c files as well, and MSVC's C mode does
// not reliably take `static_assert`. The ~25 Custom*Scene.cpp files that include it are enough to
// make the check run on every build.
#define SURFACETYPE_ASSERT(which, expr, expected, what)                                            \
    static_assert((expr) == (expected), "SURFACETYPE" which " field misplaced: " what)

// One packed word carrying a full-width value in every data[0] field.
#define SURFACETYPE0_PROBE SURFACETYPE0(0xA5u, 0x15u, 0x1Bu, 0x05u, 0x1Au, 0x09u, 1u, 1u)
SURFACETYPE_ASSERT("0", SURFACETYPE0_PROBE & 0xFFu, 0xA5u, "bgCamIndex [7:0]");
SURFACETYPE_ASSERT("0", SURFACETYPE0_PROBE >> 8 & 0x1Fu, 0x15u, "exitIndex [12:8]");
SURFACETYPE_ASSERT("0", SURFACETYPE0_PROBE >> 13 & 0x1Fu, 0x1Bu, "floorType [17:13]");
SURFACETYPE_ASSERT("0", SURFACETYPE0_PROBE >> 18 & 0x07u, 0x05u, "unk18 [20:18]");
SURFACETYPE_ASSERT("0", SURFACETYPE0_PROBE >> 21 & 0x1Fu, 0x1Au, "wallType [25:21]");
SURFACETYPE_ASSERT("0", SURFACETYPE0_PROBE >> 26 & 0x0Fu, 0x09u, "floorProperty [29:26]");
SURFACETYPE_ASSERT("0", SURFACETYPE0_PROBE >> 30 & 0x01u, 1u, "isSoft [30]");
SURFACETYPE_ASSERT("0", SURFACETYPE0_PROBE >> 31 & 0x01u, 1u, "isHorseBlocked [31]");

// One packed word carrying a full-width value in every data[1] field. Bits [31:28] are unused, so
// the whole word is asserted too - that catches a field spilling past its mask into them.
#define SURFACETYPE1_PROBE SURFACETYPE1(0x0Du, 0x03u, 0x1Bu, 0x2Au, 1u, 0x06u, 0x33u, 1u)
SURFACETYPE_ASSERT("1", SURFACETYPE1_PROBE & 0x0Fu, 0x0Du, "material [3:0]");
SURFACETYPE_ASSERT("1", SURFACETYPE1_PROBE >> 4 & 0x03u, 0x03u, "floorEffect [5:4]");
SURFACETYPE_ASSERT("1", SURFACETYPE1_PROBE >> 6 & 0x1Fu, 0x1Bu, "lightSetting [10:6]");
SURFACETYPE_ASSERT("1", SURFACETYPE1_PROBE >> 11 & 0x3Fu, 0x2Au, "echo [16:11]");
SURFACETYPE_ASSERT("1", SURFACETYPE1_PROBE >> 17 & 0x01u, 1u, "canHookshot [17]");
SURFACETYPE_ASSERT("1", SURFACETYPE1_PROBE >> 18 & 0x07u, 0x06u, "conveyorSpeed [20:18]");
SURFACETYPE_ASSERT("1", SURFACETYPE1_PROBE >> 21 & 0x3Fu, 0x33u, "conveyorDirection [26:21]");
SURFACETYPE_ASSERT("1", SURFACETYPE1_PROBE >> 27 & 0x01u, 1u, "isWallDamage [27]");
SURFACETYPE_ASSERT("1", SURFACETYPE1_PROBE >> 28, 0u, "something spilled into unused bits [31:28]");

// The one flag the engine also documents by value, on SurfaceType itself in z64bgcheck.h
// ("0x0800_0000 = wall damage"). Pin the macro to that literal.
SURFACETYPE_ASSERT("1", SURFACETYPE1(0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u), 0x08000000u,
                     "isWallDamage does not match the documented 0x0800_0000");

#undef SURFACETYPE0_PROBE
#undef SURFACETYPE1_PROBE
#undef SURFACETYPE_ASSERT
#endif // __cplusplus
