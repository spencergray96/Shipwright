#pragma once
// Unified header for test_level scene C data files.
#include "global.h"

// Fast64 uses these names; map them to the Shipwright equivalents from z64.h.
typedef PolygonType0 RoomShapeNormal;
typedef PolygonDlist RoomShapeDListsEntry;
#define ROOM_SHAPE_TYPE_NORMAL 0

// Macros used by Fast64 collision output that are not in Shipwright public headers.
// COLPOLY_VTX itself lives in z64bgcheck.h (via global.h above): it encodes the engine's vertex-id
// bit layout, so a copy here would silently disagree with the engine the moment that widens.
#define COLPOLY_IGNORE_NONE 0
// The 3-bit poly vtx flag field above the vertex id — mirrors z_bgcheck.c's file-local defines.
// vIA (vtxData[0]) carries the ignore flags; vIB (vtxData[1]) carries the conveyor flag.
#define COLPOLY_IGNORE_CAMERA      (1 << 0)
#define COLPOLY_IGNORE_ENTITY      (1 << 1)
#define COLPOLY_IGNORE_PROJECTILES (1 << 2)
#define COLPOLY_VIB_CONVEYOR       (1 << 0)
// SurfaceType.data[1] conveyor fields (SurfaceType_GetConveyorSpeed/Direction).
// speed 1..3 indexes sFloorConveyorSpeeds/sWaterConveyorSpeeds[speed-1] in z_player.c; a poly with
// speed set is a floor conveyor when its vIB conveyor flag is set, a water current when it is not.
// direction is in 360/64-degree units: 0 pushes +Z, 16 +X, 32 -Z, 48 -X.
#define SURFACETYPE1_CONVEYOR_SPEED(s) (((s)&7) << 18)
#define SURFACETYPE1_CONVEYOR_DIR(d)   (((d)&0x3F) << 21)
// SurfaceType.data[0]: wallType[3:0], floorType[7:4], sfxEffect[11:8], exitIdx[20:16], camIdx[31:24]
#define SURFACETYPE0(wallType, floorType, sfxEffect, exitIdx, camIdx, f, g, h) \
    (((wallType) & 0xF) | (((floorType) & 0xF) << 4) | (((sfxEffect) & 0xF) << 8) | \
     (((exitIdx) & 0x1F) << 16) | (((camIdx) & 0x1F) << 24))
// SurfaceType.data[1]: sound index at bits 27-24
#define SURFACETYPE1(sound, a, b, c, d, e, f, g) (((sound) & 0xF) << 24)
