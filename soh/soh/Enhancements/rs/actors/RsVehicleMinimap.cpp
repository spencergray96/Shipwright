// The vehicles' minimap arrows (sturdy-bassoon#270): see RsVehicleMinimap.h.
//
// Each arrow goes through Minimap_DrawCompassArrow (z_map_exp.c), the helper Link's yellow arrow is drawn with, so
// the anchor, margins, position offsets and mirrored world are the vanilla arrow's own. The hook fires just before
// Link's arrow, inside Minimap_DrawCompassIcons' minimap-alpha gate, so a hidden minimap draws none of them.

#include "RsVehicleMinimap.h"

#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>

#include "RsBalloon.h"
#include "RsBoat.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/frame_interpolation.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

// The default colours. Each has a cosmetic override in the yellow and red arrows' pattern (`HUD.MinimapBoat` and
// `HUD.MinimapBalloon`, `.Changed` and `.Value`), set only from the console for now: no editor entry yet.
const Color_RGB8 kBoatColor = { 30, 120, 255 };
const Color_RGB8 kBalloonColor = { 255, 130, 0 };

// What the last minimap draw did, per vehicle, and the frame it ran (GameState frames, which count paused frames
// too, when the minimap is not drawn). Pointers are only compared, never followed.
struct Drawn {
    const Actor* vehicle;
    int32_t arrow;
};
std::vector<Drawn> sDrawn;
u32 sDrawnFrame = 0;
bool sEverDrawn = false;

// Whether an actor gets a vehicle arrow at all: every boat and balloon in the scene for now. The one place to narrow
// it when boat ownership (#263) decides which are the player's.
bool ShowsOnMinimap(const Actor* a) {
    return a->id == ACTOR_RS_BOAT || a->id == ACTOR_RS_BALLOON;
}

// Link holds this vehicle's controls: a boat's helm, a balloon's panel.
bool LinkAtControls(const Actor* a) {
    if (a->id == ACTOR_RS_BOAT) {
        return reinterpret_cast<const RsBoat*>(a)->helm != 0;
    }
    if (a->id == ACTOR_RS_BALLOON) {
        return reinterpret_cast<const RsBalloon*>(a)->controls != 0;
    }
    return false;
}

Color_RGB8 CosmeticColor(const char* changed, const char* value, Color_RGB8 def) {
    return CVarGetInteger(changed, 0) ? CVarGetColor24(value, def) : def;
}

Color_RGB8 ArrowColor(const Actor* a) {
    if (a->id == ACTOR_RS_BALLOON) {
        return CosmeticColor(CVAR_COSMETIC("HUD.MinimapBalloon.Changed"), CVAR_COSMETIC("HUD.MinimapBalloon.Value"),
                             kBalloonColor);
    }
    return CosmeticColor(CVAR_COSMETIC("HUD.MinimapBoat.Changed"), CVAR_COSMETIC("HUD.MinimapBoat.Value"), kBoatColor);
}

void DrawVehicleArrows() {
    if (gPlayState == nullptr) {
        return;
    }
    sDrawn.clear();
    sDrawnFrame = gPlayState->state.frames;
    sEverDrawn = true;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        // A killed vehicle (update cleared, still listed until the end of the tick) is gone.
        if (a->update == nullptr || !ShowsOnMinimap(a)) {
            continue;
        }
        if (LinkAtControls(a)) {
            sDrawn.push_back({ a, RS_VEHICLE_ARROW_CONTROLS });
            continue;
        }
        // The heading is the yaw the hull or basket draws with, the way it moves (sin, cos), as Link's own.
        // Each arrow is its own interpolation child, keyed by its vehicle. Unkeyed, the frames drawn between two
        // ticks pair every draw at the helper's line by its place in the order, so when one arrow goes (a boat
        // cleared, a helm taken) the next is paired with it and slides over from where it was.
        FrameInterpolation_RecordOpenChild(a, 0);
        Minimap_DrawCompassArrow(gPlayState, a->world.pos.x, a->world.pos.z, a->shape.rot.y, ArrowColor(a));
        FrameInterpolation_RecordCloseChild();
        sDrawn.push_back({ a, RS_VEHICLE_ARROW_DRAWN });
    }
}

void RegisterRsVehicleMinimap() {
    COND_HOOK(OnMinimapDrawCompassIconsUnderPlayer, true, DrawVehicleArrows);
}

RegisterShipInitFunc rsVehicleMinimapInitFunc(RegisterRsVehicleMinimap);

} // namespace

int32_t RsVehicleMinimap_Arrow(const Actor* vehicle) {
    // The draw runs once a game frame; the console may run before or after this frame's, so the last one or this
    // one is current. Older means the minimap has not been drawn since.
    if (gPlayState == nullptr || !sEverDrawn || gPlayState->state.frames - sDrawnFrame > 1) {
        return RS_VEHICLE_ARROW_NO_MINIMAP;
    }
    for (const Drawn& d : sDrawn) {
        if (d.vehicle == vehicle) {
            return d.arrow;
        }
    }
    return RS_VEHICLE_ARROW_NO_MINIMAP;
}

const char* RsVehicleMinimap_ArrowName(int32_t arrow) {
    switch (arrow) {
        case RS_VEHICLE_ARROW_DRAWN:
            return "drawn";
        case RS_VEHICLE_ARROW_CONTROLS:
            return "controls";
        default:
            return "no_minimap";
    }
}
