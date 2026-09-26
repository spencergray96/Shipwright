// Signpost text for the vert_probe scene (sturdy-bassoon#134), so the fixtures can be walked by a
// human without the run record open beside them.
//
// En_Kanban derives its message from its own params: `textId = params | 0x300`
// (`z_en_kanban.c:211`). So each sign is placed with params 0x01..0x0D and answers on text id
// 0x301..0x30D. Those ids belong to vanilla signposts, so every handler bails out unless the
// current scene is the probe - outside it the vanilla message loads exactly as before.
//
// Not COND_ID_HOOK: that macro keeps ONE `static HOOK_ID` per call site and unregisters it before
// registering again, so a loop over the table would leave only the last sign alive. Registering
// through GameInteractor directly is what lets each id keep its own hook.
#include "CustomVertProbeScene.h"
#include <soh/OTRGlobals.h>
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <variables.h>

// The engine global, declared here because the headers reachable from this file do not. It must sit
// INSIDE the extern "C" block: declared outside it, the name mangles as C++ and fails to link
// against the engine's definition.
extern PlayState* gPlayState;
}

// '&' is a line break and '^' a box break (CustomMessageManager); '#' and '%' are control-code
// markers and are deliberately absent. THREE lines of about 30 characters per box: the box holds
// four rows, and AutoFormat re-wraps, so a fourth authored line is truncated mid-word in-game.
typedef struct {
    uint8_t params;
    const char* text;
} VertProbeSign;

static const VertProbeSign sVertProbeSigns[] = {
    { 0x01, "VERTICAL TRAVERSAL PROBE&Every fixture here was&measured for issue 134."
            "^Straight ahead: the steps.&Past them: the ramps.&Beyond those: the ladders."
            "^Each ladder differs only in&where its floors sit.&Read its sign first." },

    { 0x02, "STEP LANE&Blocks of 16, 20, 24, 26,&28, 32 and 40 units."
            "^16 is WALKED: Link snaps up&with no overshoot.&18 and up are HOPPED."
            "^A hop peaks 13 above the&step. One storey of 80&needs five steps of 16." },

    { 0x03, "STEP BOUNDARY&Rises 15, 17, 18 and 19."
            "^17 is the tallest step&Link walks. At 18 he hops.&The cutoff is 18 units." },

    { 0x04, "RAMP LANE&20, 30, 45 and 55 degrees,&each rising 120 units."
            "^All four are walked up&with no slipping at all.&The landing has no wall,"
            "^so Link runs off the far&edge. That leap is not&the ramp." },

    { 0x05, "STEEP RAMPS&58, 60 and 62 degrees."
            "^58 walks. 60 and 62 block&him flat. Past 60 a face&counts as a wall." },

    { 0x06, "LADDER C1&Floors REACH the ladder."
            "^Going up he climbs past&them all and falls off&the top."
            "^A floor that reaches the&ladder is a ceiling: it&ends the climb 62 below." },

    { 0x07, "LADDER C2&A 60-unit hole in each floor."
            "^Up: climbs past every floor&with nowhere to get off."
            "^Down from an upper floor:&a full descent to the ground." },

    { 0x08, "LADDER C3&Floors on the FAR side."
            "^Climb and he steps off onto&the floor at storey 1."
            "^The dismount probe looks&26 units PAST the wall&he faces." },

    { 0x09, "LADDER C4&Proud lip, floor reaches."
            "^The lip lets him mount&going down."
            "^But that floor is a ceiling:&it ejects him at 98&and he falls." },

    { 0x0A, "LADDER C5&Proud lip, 60-unit hole."
            "^Mounting from above fails.&He steps off the edge into&the hole first." },

    { 0x0B, "LADDER C6&Same lip, 20-unit hole."
            "^He still falls in. A hole&wide enough to climb&through is wide enough"
            "^to fall into." },

    { 0x0C, "LADDER C7&THE ONE THAT WORKS."
            "^A balcony on the far side,&with the lip standing&proud of it."
            "^Up: steps onto the balcony.&Down: a full descent."
            "^This is the shape to build.&It is NOT the RS ladder&through a hole." },

    { 0x0D, "LADDER D&An exit on the landing."
            "^Climb it and you warp to&the test level."
            "^An exit cannot fire while&climbing. It fires on the&first step after landing." },
};


static void VertProbeSigns_Register() {
    for (const VertProbeSign& sign : sVertProbeSigns) {
        const char* text = sign.text;
        GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnOpenText>(
            (int32_t)(sign.params | 0x300), [text](uint16_t* textId, bool* loadFromMessageTable) {
                if (gPlayState == nullptr || gPlayState->sceneNum != SCENE_VERT_PROBE) {
                    return; // a vanilla signpost elsewhere keeps its own message
                }
                CustomMessage msg = CustomMessage(text);
                msg.AutoFormat();
                msg.LoadIntoFont();
                *loadFromMessageTable = false;
            });
    }
}

static RegisterShipInitFunc vertProbeSignsInit(VertProbeSigns_Register);
