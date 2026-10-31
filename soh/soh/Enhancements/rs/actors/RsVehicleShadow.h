#ifndef SOH_RS_VEHICLE_SHADOW_H
#define SOH_RS_VEHICLE_SHADOW_H

// Link's shadow on a vehicle he stands in (sturdy-bassoon#267): a boat (RsBoat.h, THE SHADOW there says what it looks
// like and why) and the balloon's basket (RsBalloon.h). Over one he has no feet shadows: Navi's light turned them round
// him, and they reached out past the rail. By default the vehicle draws his shadow itself, cast onto its own drawn
// faces (RS_VEHICLE_SHADOW_DRAPED, RsVehicleShadow_Draw). The faces and a height map of its deck come from its archive
// (RsVehicleDeck_FromArchive) or, for a vehicle drawn from code, from its triangles (RsVehicleDeck_FromTris). The
// settings are one set for every vehicle; `boat shadow` reads and switches them. The CVars keep their first names
// (RsBoatDeckShadow...), from when the boats had it alone.

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Link's shadow over a boat (#267): RsVehicleShadowMode (RsVehicleShadow_Mode; 0 his feet shadows, 1 the flat circle, 2
// draped over the drawn deck), its size a percent of his shadow scale. 30 is vanilla's own circle under a Link in the
// air (ActorShadow_DrawFeet).
#define CVAR_RS_VEHICLE_SHADOW CVAR_ENHANCEMENT("RsBoatDeckShadow")
#define RS_VEHICLE_SHADOW_DEFAULT RS_VEHICLE_SHADOW_DRAPED
#define CVAR_RS_VEHICLE_SHADOW_SIZE CVAR_ENHANCEMENT("RsBoatDeckShadowSize")
#define RS_VEHICLE_SHADOW_SIZE_DEFAULT 30
#define RS_VEHICLE_SHADOW_SIZE_MIN 5
#define RS_VEHICLE_SHADOW_SIZE_MAX 100
// Where the draped shadow's pieces are built (a test aid: a CVar, so SoH keeps it on exit; RsVehicleShadow_Draw).
// Relative to Link, frame interpolation glides the shadow with him between ticks but slides each piece along its face
// by up to a tick's motion, and snaps it back at the next: a jerk at the trim's edges as he inches along (the owner,
// 2026-10-30). In the boat's own space nothing slides, but the shadow moves once a tick, which shows only at a run. So
// by default it picks by his speed.
typedef enum RsVehicleShadowAnchor {
    RS_VEHICLE_SHADOW_ANCHOR_AUTO, // the hull at a walk or slower, him at a run (RS_VEHICLE_SHADOW_GLIDE_ON / _OFF)
    RS_VEHICLE_SHADOW_ANCHOR_HULL,
    RS_VEHICLE_SHADOW_ANCHOR_LINK,
} RsVehicleShadowAnchor;
#define CVAR_RS_VEHICLE_SHADOW_ANCHOR CVAR_ENHANCEMENT("RsBoatDeckShadowAnchor")

// Link's shadow over a boat (#267). His feet shadows each point away from a light, so Navi circling him turned them
// round him, and from the rail they reached out over the water. Over a boat's collision (standing, or in the air over
// it) he gets one of these instead, none of which a light turns:
typedef enum RsVehicleShadowMode {
    RS_VEHICLE_SHADOW_FEET,   // his feet shadows, as on land
    RS_VEHICLE_SHADOW_CIRCLE, // vanilla's flat circle under him, drawn by ActorShadow_DrawFeet. It sits on the
                              // collision, which steps a unit at a time as the hull rocks, and overhangs every edge
    RS_VEHICLE_SHADOW_DRAPED, // cast onto the drawn hull's own faces by the boat (RsVehicleShadow_Draw): it rocks with
                              // the hull, lies on the rail, the crown, the spine and the walls, and stops where they do
} RsVehicleShadowMode;

// The mode for `actor` this tick: RS_VEHICLE_SHADOW_FEET unless it is Link over a vehicle (CVAR_RS_VEHICLE_SHADOW).
// ActorShadow_DrawFeet draws no feet shadows for either other mode.
s32 RsVehicleShadow_Mode(PlayState* play, const Actor* actor);
// The shadow's size, a fraction of Link's shadow scale (CVAR_RS_VEHICLE_SHADOW_SIZE).
f32 RsVehicleShadow_Size(void);

// The draped shadow's last draw, for `boat shadow` (agent mode).
typedef struct RsVehicleShadowStats {
    u32 frame;    // state.frames of the draw; 0 before the first
    s32 kind;     // the boat's kind
    f32 radius;   // world units
    f32 feetOver; // Link's feet over the deck under him; -999 when no drawn deck is under him (nothing drawn)
    s32 lit;      // corners drawn with any alpha
    s32 glide;    // 1: built relative to him (gliding), 0: on the hull
    s32 faces;    // hull faces it fell on (clipped to its square, with any shade)
    s32 dropped;  // faces left out past RS_VEHICLE_SHADOW_MAX_VTX (a big size over many small faces)
    s32 tris;     // triangles drawn
    // Since `boat shadow reset`: draws, draws held back (no drawn deck under him, or his feet under it: climbing), and
    // the lowest feetOver of a draw (999 before one)
    s32 drawn;
    s32 held;
    f32 lowestDrawn;
} RsVehicleShadowStats;
extern RsVehicleShadowStats gRsVehicleShadow;
// `boat shadow reset`: zeroes drawn and held, and lowestDrawn back to 999.
void RsVehicleShadow_StatsReset(void);
// The draped shadow's last RS_VEHICLE_SHADOW_TRACE ticks it was asked to draw (one row each: drawn, held back, or not
// drawn with Link aboard last tick), oldest first from gRsVehicleShadowTraceNext (a ring),
// for `boat shadow trace`: where it took him to be (model space), the deck and fade reference it chose, the jump's
// fade, his speed, and what it drew.
#define RS_VEHICLE_SHADOW_TRACE 256
typedef struct RsVehicleShadowTraceRow {
    u32 frame;
    f32 x, y, z;  // his place as the shadow took it (his feet mid-climb), model space
    f32 deck;     // the deck he is over
    f32 under;    // the fade's reference
    f32 strength; // the jump's fade
    f32 speed;    // his linearVelocity
    s16 tris;     // triangles drawn; -1 held back, -2 not drawn with him aboard last tick
    u8 climbing;
    u8 glide; // built relative to him
} RsVehicleShadowTraceRow;
extern RsVehicleShadowTraceRow gRsVehicleShadowTrace[RS_VEHICLE_SHADOW_TRACE];
extern s32 gRsVehicleShadowTraceNext;

// A kind's drawn deck as a height map in its list's model space (#267), for Link's draped shadow: the highest up-facing
// face of its hull, keel and trim over a grid of knots (not the sails, mast or helm: Link never stands on them). Built
// by RsBoatModel.cpp from the kind's archive the first time it is asked for, so it follows whatever the archive draws
// and nothing derived from RS is committed (the clean-distribution ADR).
#define RS_VEHICLE_DECK_STEP 2   // units between knots
#define RS_VEHICLE_DECK_NONE 255 // no drawn face over the knot
typedef struct RsVehicleDeckHeights {
    s16 x0; // the first knot, model space
    s16 z0;
    s16 w;           // knots across (x)
    s16 h;           // knots along (z)
    s16 base;        // a knot's height is base + cell / 2
    const u8* cells; // row by row along z; RS_VEHICLE_DECK_NONE where no drawn face is
} RsVehicleDeckHeights;

// A face of the drawn hull the draped shadow falls on (#267): the hull, keel and trim's faces that do not point down
// (the slanted walls and upright faces too), in the list's model space, with the unit normal and the extent across it.
typedef struct RsVehicleDeckFace {
    f32 v[3][3];
    f32 vn[3][3]; // each corner's normal, averaged over the faces that share the corner
    f32 n[3];
    f32 lo[2]; // x, z
    f32 hi[2];
} RsVehicleDeckFace;

// A vehicle's deck for the draped shadow: its map and its faces, model space.
typedef struct RsVehicleDeck {
    RsVehicleDeckHeights map;
    const RsVehicleDeckFace* faces;
    s32 numFaces;
} RsVehicleDeck;

// The deck of the vehicle whose display list is `listPath` (its archive's `.../props`), read from the archive's copies
// the first time it is asked for, leaving out the copies named in `skipCopies` (NULL-terminated; NULL for none), or
// NULL when the archive is not mounted or has no deck (RsVehicleDeck.cpp).
const RsVehicleDeck* RsVehicleDeck_FromArchive(const char* listPath, const char* const* skipCopies);
// The deck made of `count` triangles (model space, each wound so its front faces out: normal = (b - a) x (c - a)),
// built the first time `key` is asked for: a vehicle drawn from code, as the balloon's placeholder basket is.
const RsVehicleDeck* RsVehicleDeck_FromTris(const char* key, const f32 (*tris)[3][3], s32 count);

// A vehicle Link's shadow is drawn on, as its own Draw hands it over (last, after the vehicle: its own render state).
typedef struct RsVehicleShadowSource {
    Actor* vehicle; // its matrix is Actor_Draw's (world.pos plus yOffset, shape.rot, scale 1)
    s32 bgId;       // its collision
    s32 linkAboard; // Link stood on it last tick
    const RsVehicleDeck* deck;
    f32 modelY; // its model space's origin in the actor's space
    f32 modelZ;
    s32 kind; // for the stats: a boat's kind, or RS_VEHICLE_SHADOW_KIND_BASKET
} RsVehicleShadowSource;
#define RS_VEHICLE_SHADOW_KIND_BASKET (-1)

// Link's shadow on this vehicle in RS_VEHICLE_SHADOW_DRAPED, when he is over it.
void RsVehicleShadow_Draw(PlayState* play, const RsVehicleShadowSource* src);

#ifdef __cplusplus
}
#endif

#endif
