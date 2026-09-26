/*
 * RsStairs.c - the staircase actor (sturdy-bassoon#147). RsStairs.h says what it is.
 *
 * Copied from RsNpc.c's talk loop, with its two deliberate departures from vanilla idiom (no `s32
 * pad` locals, no ICHAIN), for the same reason: soh/soh C files get /W3 /WX.
 *
 * What differs from a quest-giver, and why:
 *   - NO GRAVITY. Every placement above the ground floor stands over a hole - that is the point of
 *     it - and would fall down the shaft to the bottom.
 *   - TALK IS GATED TO ITS OWN STOREY. Three placements share one column, and the talk offer picks
 *     the nearest in XZ, which is all three at once; a lock-on skips the range checks altogether and
 *     can reach through the hole. So the offer is refused outright when Link is not within
 *     RS_STAIRS_TALK_Y of this placement's height.
 *   - ACTORCAT_PROP, so Link idles rather than plays the NPC talk animation at a hole in the floor.
 *
 * No `sceneNum` here either. The one scene check a staircase needs - "are these landings in the
 * scene I am standing in" - is the mover's (Stairs.cpp), where it refuses a move rather than
 * teleporting Link to another scene's coordinates.
 */

#include <math.h>
#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsStairs.h"
#include "RsActorParams.h"
#include "RsActors.h"
#include "global.h"
#include "soh/Enhancements/rs/stairs/Stairs.h"

// Talk range. XZ reaches the landing (40 from the shaft centre) with margin, so Link can turn round
// and go straight back; Y is under half a storey, so a placement never answers from the floor above
// or below.
#define RS_STAIRS_TALK_XZ 70.0f
#define RS_STAIRS_TALK_Y 30.0f

// How far a placement may sit from its row's landing height before it is called a mistake. The
// placement and the landing are on the same storey by definition; a gap of half a storey means the
// params name the wrong row.
#define RS_STAIRS_ROW_TOLERANCE 40.0f

void RsStairs_Init(Actor* thisx, PlayState* play);
void RsStairs_Destroy(Actor* thisx, PlayState* play);
void RsStairs_Update(Actor* thisx, PlayState* play);

static void RsStairs_Wait(RsStairs* this, PlayState* play);
static void RsStairs_Talk(RsStairs* this, PlayState* play);

// Solid and immovable. Radius 20 fills a 40x40 hole: Link's own cylinder is 12, so his centre can
// get no closer than 32 to the shaft's, and the hole's corners are 28.3 out - he cannot step in.
static ColliderCylinderInit sCylinderInit = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_NONE,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_2,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00000000, 0x00, 0x00 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_NONE,
        OCELEM_ON,
    },
    { 20, 60, 0, { 0, 0, 0 } },
};

void RsStairs_Init(Actor* thisx, PlayState* play) {
    RsStairs* this = (RsStairs*)thisx;
    const RsStairLanding* landing;
    char line[192];

    this->stairId = RS_STAIR_PARAMS_GET_ID(thisx->params);
    this->row = RS_STAIR_PARAMS_GET_ROW(thisx->params);

    // Loud, never fatal and never an assert - RsNpc's rule. A broken placement still stands there
    // and its menu says what is wrong (RsActors.cpp renders the diagnostic), which is a mistake you
    // can see rather than a staircase that silently is not there.
    // Each mistake also gets a marker, so a run sees it without reading the engine log.
    if (RS_STAIR_PARAMS_GET_RSVD(thisx->params) != 0) {
        LUSLOG_ERROR("RsStairs: params 0x%04X has reserved bits set (stair %d)", (u16)thisx->params, this->stairId);
        snprintf(line, sizeof(line), "rs_stairs stair=%d event=bad_placement row=%d reason=reserved_bits params=0x%04X",
                 this->stairId, this->row, (unsigned)(u16)thisx->params);
        RsAgent_Marker(line);
    }
    landing = RsStair_GetLanding(this->stairId, this->row);
    if (landing == NULL) {
        LUSLOG_ERROR("RsStairs: stair %d has no row %d in this build", this->stairId, this->row);
        snprintf(line, sizeof(line), "rs_stairs stair=%d event=bad_placement row=%d reason=no_such_row",
                 this->stairId, this->row);
        RsAgent_Marker(line);
    } else if (fabsf((f32)landing->y - thisx->home.pos.y) > RS_STAIRS_ROW_TOLERANCE) {
        // The row is STATED in params rather than inferred from height (RsActorParams.h), and this
        // is the check that makes stating it safe.
        snprintf(line, sizeof(line), "rs_stairs stair=%d event=bad_placement row=%d reason=height y=%.1f landing_y=%d",
                 this->stairId, this->row, thisx->home.pos.y, (int)landing->y);
        RsAgent_Marker(line);
    }

    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, &this->actor, &sCylinderInit);
    ActorShape_Init(&thisx->shape, 0.0f, NULL, 0.0f);

    thisx->targetMode = 0; // the shortest lock-on range (70), for the same reason as the talk gate
    thisx->colChkInfo.mass = MASS_IMMOVABLE;
    thisx->textId = RS_TEXT_STAIR_ID(this->stairId, this->row);

    this->actionFunc = RsStairs_Wait;
}

void RsStairs_Destroy(Actor* thisx, PlayState* play) {
    RsStairs* this = (RsStairs*)thisx;

    Collider_DestroyCylinder(play, &this->collider);
}

static void RsStairs_Wait(RsStairs* this, PlayState* play) {
    const RsDialogueRule* screen;
    char line[160];

    if (Actor_ProcessTalkRequest(&this->actor, play)) {
        screen = RsStair_Screen(this->stairId, this->row);
        snprintf(line, sizeof(line), "rs_stairs stair=%d event=open row=%d options=%d", this->stairId, this->row,
                 screen != NULL ? (int)screen->optionCount : 0);
        RsAgent_Marker(line);
        this->actionFunc = RsStairs_Talk;
        return;
    }
    // Not while a move is in flight - this placement's or any other's - so a second menu cannot open
    // under the fade.
    if (RsStair_IsMoving()) {
        return;
    }
    if (fabsf(this->actor.yDistToPlayer) > RS_STAIRS_TALK_Y) {
        return;
    }
    Actor_OfferTalkExchange(&this->actor, play, RS_STAIRS_TALK_XZ, RS_STAIRS_TALK_Y, EXCH_ITEM_NONE);
}

static void RsStairs_Talk(RsStairs* this, PlayState* play) {
    char line[192];
    u8 state = Message_GetState(&play->msgCtx);
    s32 choice;
    s32 dest;
    s32 result;

    // Closed from outside - see RsNpc_Talk for why this matters.
    if (state == TEXT_STATE_NONE) {
        this->actionFunc = RsStairs_Wait;
        return;
    }

    if (state == TEXT_STATE_CHOICE) {
        if (!Message_ShouldAdvance(play)) {
            return;
        }
        if (play->msgCtx.textId != this->actor.textId) {
            return; // somebody else's box
        }
        // No gating on a staircase menu, so the visible row IS the declared option.
        choice = play->msgCtx.choiceIndex;
        dest = RsStair_MenuDestination(this->stairId, this->row, choice);
        if (dest == RS_STAIR_MENU_NO_OPTION) {
            return;
        }
        // BeginMove before the box closes, not after: it puts Link in a cutscene action, and
        // Player_Action_Talk takes that up as the box closes, so he goes straight from talking to
        // still - no frame of walking in between.
        result = dest != RS_STAIR_MENU_CANCEL ? RsStair_BeginMove(this->stairId, this->row, dest, "menu") : RS_STAIR_OK;
        snprintf(line, sizeof(line), "rs_stairs stair=%d event=choice row=%d index=%d to_row=%d result=%s",
                 this->stairId, this->row, (int)choice, (int)dest,
                 dest != RS_STAIR_MENU_CANCEL ? RsStair_ResultName(result) : "cancel");
        RsAgent_Marker(line);
        Message_CloseTextbox(play);
        this->actionFunc = RsStairs_Wait;
        return;
    }

    // A statement - only ever the diagnostic for a broken placement.
    if (state == TEXT_STATE_DONE && Message_ShouldAdvance(play)) {
        this->actionFunc = RsStairs_Wait;
    }
}

void RsStairs_Update(Actor* thisx, PlayState* play) {
    RsStairs* this = (RsStairs*)thisx;

    this->actionFunc(this, play);

    // The attention arrow rides above the focus, so it sits at head height over the hole.
    Actor_SetFocus(thisx, 40.0f);
    Collider_UpdateCylinder(thisx, &this->collider);
    CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider.base);
}
