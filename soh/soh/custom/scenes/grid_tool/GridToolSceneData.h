#pragma once
// Shared header for all compiled-in custom scene C data files - both the ones
// tools/grid-scene-tool writes directly and the ones tools/terrain/integrate-fast64.ts
// post-processes out of a real Fast64 export. Each scene includes this instead of carrying its
// own copy, since none of it is scene-specific.
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
// SurfaceType.data[0]: wallType[3:0], floorType[7:4], sfxEffect[11:8], exitIdx[20:16], camIdx[31:24]
#define SURFACETYPE0(wallType, floorType, sfxEffect, exitIdx, camIdx, f, g, h) \
    (((wallType) & 0xF) | (((floorType) & 0xF) << 4) | (((sfxEffect) & 0xF) << 8) | \
     (((exitIdx) & 0x1F) << 16) | (((camIdx) & 0x1F) << 24))
// SurfaceType.data[1]: sfx index at bits 3:0 (see D_80119E10 in z_bgcheck.c - e.g. 8 = grass).
// Was previously shifted to bits 27:24, which SurfaceType_GetSfx/func_80041F10 never reads
// (they read data[1] & 0xF), so no footstep sound ever played and every floor silently
// defaulted to index 0 (NA_SE_PL_WALK_GROUND). Bit 27 there is also SurfaceType_IsWallDamage's
// flag, so the old shift was additionally setting that (harmless here since it's only ever
// checked against wall-type polys, but still wrong).
#define SURFACETYPE1(sound, a, b, c, d, e, f, g) ((sound) & 0xF)
