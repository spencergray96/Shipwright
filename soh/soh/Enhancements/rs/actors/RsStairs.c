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
 *   - WALK INTO IT (#151). Pushing into the collider opens the menu with no A press - see the
 *     section below.
 *
 * No `sceneNum` here either. The one scene check a staircase needs - "are these landings in the
 * scene I am standing in" - is the mover's (Stairs.cpp), where it refuses a move rather than
 * teleporting Link to another scene's coordinates.
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h> // vsnprintf, for the agent-loop markers

#include "RsStairs.h"
#include "RsActorParams.h"
#include "RsActors.h"
#include "global.h"
#include "soh/Enhancements/rs/RsAssert.h"
#include "soh/Enhancements/rs/stairs/Stairs.h"

// Talk range. XZ reaches the landing (40 from the shaft centre) with margin, so Link can turn round
// and go straight back; Y is under half a storey, so a placement never answers from the floor above
// or below.
#define RS_STAIRS_TALK_XZ 70.0f
#define RS_STAIRS_TALK_Y 30.0f

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

static void RsStairs_Marker(const char* fmt, ...) {
    char line[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    RsAgent_Marker(line);
}

// --- walk into it (sturdy-bassoon#151) -------------------------------------------------------------
//
// The second way in, beside target-and-talk: push into the placement and its menu opens. The
// collider already fills the hole, so Link pushing against it IS him trying to step into the shaft.
// When the push has held for the hold (RsStair_GetBumpHold), this placement's talk offer goes out
// with ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED - vanilla's own talk-without-A, the one NPCs that call out
// to Link use - and Player takes it on his next update as though A had been pressed.
//
// A PUSH is four things on one tick, all read fresh:
//   - touching: the placement's cylinder met Link's in this tick's OC pass (OC2_HIT_PLAYER) - which
//     Play_Update runs before the actors update, on the colliders last tick registered;
//   - the stick held, at least RS_STAIRS_BUMP_STICK of the 60 Player reads;
//   - the stick pointed into the shaft: within RS_STAIRS_BUMP_CONE of the line to its centre;
//   - Link FACING it, within the same cone - so backing into it (a Z-parallel back-step) is not one.
// What stops a GRAZE is the cone together with how Link moves: he slides round a round collider he
// is not pushing dead into, sideways by his speed times the sine of the angle off-centre every tick,
// so an off-centre push swings out of the cone, and a graze - brushing past, walking along the
// landing - is outside it from the first tick of contact. The hold decides how nearly dead-centre a
// push must be to last it out; it is not a timer against grazes.
//
// And what may stop a push counting, checked only once there IS one, so a `bump_ignored` marker is
// always a guard that was actually challenged (a move in flight, a textbox, airborne, carrying,
// locked on to something else, the setting off) - see RsStairs_GuardWhy.
//
// THE LATCH is the re-open guard, and it is set two ways, both reported as `bump_latch`:
//   - `reason=talk`: any conversation with this placement ending - Cancel, a pick, the box closed
//     from outside, whether the bump or A opened it;
//   - `reason=move`: a storey move running while Link stands on this storey with the stick held. That
//     is the placement he is put down beside - he lands facing away from it with the camera seated
//     behind him, on the shaft side, so a stick held toward the camera through the move turns him
//     straight back into it - and the one he stood at, if the move has to give him back.
// A latched placement counts nothing until the push is LET GO: the stick released, Link clear of the
// placement (beyond its landing, RsStairs_ClearDist), or off this storey. Two things that look like
// letting go and are not:
//   - losing contact. Standing still against the collider flickers in and out of it (OC compares
//     positions in whole units), and the conversation's own back-step takes Link off it anyway.
//   - the stick turning off the shaft. The stick is read in world terms, through the camera, and the
//     camera moves under a held stick: the box closes with the talk camera off to one side, so for a
//     tick or two the same held push points somewhere else, then swings back into the shaft as the
//     camera settles. Lifting the latch on that reopened the menu under a push that never let go
//     (#151 run record, the cancel leg).

// Of the 60 Player reads (func_80077D10 clamps there): a deliberate push, not a thumb resting on the
// stick. Dropping under it is also "let go" for the latch.
#define RS_STAIRS_BUMP_STICK 15.0f
// How far beyond its landing Link must be to be clear of a placement, which lifts the latch. Beyond
// the LANDING, not beyond contact, so that being put down on it by a move never counts as stepping
// clear of it; and the landing is past where a conversation's back-step leaves him (vanilla steps
// Link back from a talk partner nearer than 40).
#define RS_STAIRS_BUMP_CLEAR_BEYOND 10.0f
// 30 degrees either side of the line to the shaft's centre, for the stick and for Link's facing. At
// contact Link's centre is 32 from the shaft's (collider 20 plus his 12), where a line that misses
// the collider altogether is 39 degrees off - so the cone keeps a margin inside it.
#define RS_STAIRS_BUMP_CONE 0x1555
// Ticks apart that end a CONTACT, the unit `bump_ignored` reports in: again only when the refusing
// reason changes within one contact, not once per tick of a contact that flickers.
#define RS_STAIRS_BUMP_APART 3

typedef enum RsStairsBumpWhy {
    RS_BUMP_OK,
    // Not a push into it. Quiet unless it ends a count, except `aim` and `facing`, which are
    // reported as `bump_ignored` when Link is touching it with the stick held
    RS_BUMP_CONTACT,  // not touching it
    RS_BUMP_RELEASED, // the stick is not held
    RS_BUMP_AIM,      // the stick is not pointed into the shaft: brushing past, walking along the landing
    RS_BUMP_FACING,   // Link is not facing the shaft: backing into it
    // A push into it that may not count
    RS_BUMP_DISABLED,  // the setting is off (`stairs bump off`)
    RS_BUMP_MOVING,    // a storey move is in flight
    RS_BUMP_BUSY,      // a textbox is open, or Link is in a cutscene
    RS_BUMP_AIRBORNE,  // Player takes a talk offer only on the ground (or a horse, or swimming - neither
                       // is possible at a staircase)
    RS_BUMP_CARRYING,  // A throws what he carries; an auto-accepted offer would talk instead, so refuse it
    RS_BUMP_LOCKED_ON, // locked on to another actor: Player refuses the offer anyway
    RS_BUMP_LATCHED,   // the push that was running when this placement's menu closed, or through a move
    // Ends a count only
    RS_BUMP_STOREY, // Link left this placement's storey
    RS_BUMP_TALK,   // a conversation opened some other way (A) before the hold
    RS_BUMP_WHY_COUNT,
} RsStairsBumpWhy;

static const char* const sBumpWhyNames[] = {
    "ok",       "contact",  "released",  "aim",     "facing", "disabled", "moving", "busy",
    "airborne", "carrying", "locked_on", "latched", "storey", "talk",
};
RS_STATIC_ASSERT(ARRAY_COUNT(sBumpWhyNames) == RS_BUMP_WHY_COUNT, "one marker name per RsStairsBumpWhy");

// What Link is doing to this placement this tick.
typedef struct RsStairsPush {
    s32 touching;
    f32 stick;   // magnitude, 0..60
    s16 aimOff;  // where the stick points, minus the direction to the shaft's centre
    s16 faceOff; // where Link faces, minus the same
} RsStairsPush;

static s32 RsStairs_InCone(s16 off) {
    return ABS(off) <= RS_STAIRS_BUMP_CONE;
}

static s32 RsStairs_Degrees(s16 off) {
    return (s32)((f32)off * (360.0f / 65536.0f));
}

static f32 RsStairs_ClearDist(RsStairs* this) {
    const RsStairDef* def = RsStair_GetDef(this->stairId);
    return (def != NULL ? (f32)def->landForward : (f32)RS_STAIR_MIN_LAND_FORWARD) + RS_STAIRS_BUMP_CLEAR_BEYOND;
}

static void RsStairs_ReadPush(RsStairs* this, PlayState* play, Player* player, RsStairsPush* push) {
    const s16 toShaft = (s16)(this->actor.yawTowardsPlayer + 0x8000);
    s16 stickAngle = 0;

    push->touching = (this->collider.base.ocFlags2 & OC2_HIT_PLAYER) ? 1 : 0;
    // The stick exactly as Player reads it (Player_ProcessControlStick): the same helper, which
    // applies the mirrored-world flip, turned by the same camera. Read off the pad rather than off
    // Link's motion, because the latch must see a stick still held while Link cannot move - through
    // the box closing, and through a move's freeze.
    func_80077D10(&push->stick, &stickAngle, &play->state.input[0]);
    push->aimOff = (s16)((s16)(stickAngle + Camera_GetInputDirYaw(GET_ACTIVE_CAM(play))) - toShaft);
    push->faceOff = (s16)(player->actor.shape.rot.y - toShaft);
}

static RsStairsBumpWhy RsStairs_PushWhy(const RsStairsPush* push) {
    if (!push->touching) {
        return RS_BUMP_CONTACT;
    }
    if (push->stick < RS_STAIRS_BUMP_STICK) {
        return RS_BUMP_RELEASED;
    }
    if (!RsStairs_InCone(push->aimOff)) {
        return RS_BUMP_AIM;
    }
    if (!RsStairs_InCone(push->faceOff)) {
        return RS_BUMP_FACING;
    }
    return RS_BUMP_OK;
}

// Asked only of a push, in this order, so the reason reported is the first guard that refused it.
// `moving` comes before `busy` because a move holds Link in a cutscene action, and the move is the
// reason worth naming.
static RsStairsBumpWhy RsStairs_GuardWhy(RsStairs* this, PlayState* play, Player* player) {
    if (!RsStair_BumpEnabled()) {
        return RS_BUMP_DISABLED;
    }
    if (RsStair_IsMoving()) {
        return RS_BUMP_MOVING;
    }
    if (Message_GetState(&play->msgCtx) != TEXT_STATE_NONE || Player_InCsMode(play)) {
        return RS_BUMP_BUSY;
    }
    if (!(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        return RS_BUMP_AIRBORNE;
    }
    if (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
        return RS_BUMP_CARRYING;
    }
    if (player->focusActor != NULL && player->focusActor != &this->actor) {
        return RS_BUMP_LOCKED_ON;
    }
    if (this->bump.latched) {
        return RS_BUMP_LATCHED;
    }
    return RS_BUMP_OK;
}

static void RsStairs_BumpLatch(RsStairs* this, const char* reason) {
    if (this->bump.latched) {
        return;
    }
    this->bump.latched = 1;
    this->bump.ignoredWhy = -1; // a push refused by the latch is a new thing to report
    RsStairs_Marker("rs_stairs stair=%d event=bump_latch row=%d reason=%s", this->stairId, this->row, reason);
}

static void RsStairs_BumpRearm(RsStairs* this, const char* reason) {
    if (!this->bump.latched) {
        return;
    }
    this->bump.latched = 0;
    RsStairs_Marker("rs_stairs stair=%d event=bump_rearm row=%d reason=%s", this->stairId, this->row, reason);
}

// Ends a count that did not open the menu. `ignoredWhy` takes the reason, so the same one is not
// reported again as `bump_ignored` on the next tick of the same contact.
static void RsStairs_BumpAbandon(RsStairs* this, RsStairsBumpWhy why, const RsStairsPush* push) {
    if (this->bump.count == 0) {
        return;
    }
    RsStairs_Marker("rs_stairs stair=%d event=bump_abandon row=%d reason=%s count=%d hold=%d offered=%d aim=%d "
                    "face=%d stick=%d",
                    this->stairId, this->row, sBumpWhyNames[why], this->bump.count, RsStair_GetBumpHold(),
                    this->bump.offered, RsStairs_Degrees(push->aimOff), RsStairs_Degrees(push->faceOff),
                    (s32)push->stick);
    this->bump.count = 0;
    this->bump.offered = 0;
    this->bump.ignoredWhy = (s8)why;
}

// One tick of walk-into. Returns 1 when the hold has been reached and this tick's talk offer is to
// be auto-accepted.
static s32 RsStairs_Bump(RsStairs* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    RsStairsBump* bump = &this->bump;
    const s32 hold = RsStair_GetBumpHold();
    RsStairsPush push;
    RsStairsBumpWhy why;

    RsStairs_ReadPush(this, play, player, &push);

    if (push.touching) {
        bump->ticksApart = 0;
    } else if (bump->ticksApart < RS_STAIRS_BUMP_APART && ++bump->ticksApart == RS_STAIRS_BUMP_APART) {
        bump->ignoredWhy = -1; // this contact is over
    }

    // Off this storey there is nothing to push, and a latch left from here has nothing to guard.
    if (fabsf(this->actor.yDistToPlayer) > RS_STAIRS_TALK_Y) {
        RsStairs_BumpRearm(this, "left");
        RsStairs_BumpAbandon(this, RS_BUMP_STOREY, &push);
        return 0;
    }
    // Only a placement Link is not already clear of: another staircase on the same storey has nothing
    // to guard, and latching it would only lift it again on the same tick.
    if (RsStair_IsMoving() && push.stick >= RS_STAIRS_BUMP_STICK &&
        this->actor.xzDistToPlayer <= RsStairs_ClearDist(this)) {
        RsStairs_BumpLatch(this, "move");
    }
    if (bump->latched) {
        if (push.stick < RS_STAIRS_BUMP_STICK) {
            RsStairs_BumpRearm(this, "released");
        } else if (this->actor.xzDistToPlayer > RsStairs_ClearDist(this)) {
            RsStairs_BumpRearm(this, "clear");
        }
    }

    why = RsStairs_PushWhy(&push);
    if (why == RS_BUMP_OK) {
        why = RsStairs_GuardWhy(this, play, player);
    }
    if (why != RS_BUMP_OK) {
        if (bump->count > 0) {
            RsStairs_BumpAbandon(this, why, &push);
        } else if (push.touching && push.stick >= RS_STAIRS_BUMP_STICK && why != bump->ignoredWhy) {
            // Touching it and trying to move, and it did not count: the marker that proves a
            // negative was a real challenge rather than Link never reaching the collider.
            bump->ignoredWhy = (s8)why;
            RsStairs_Marker("rs_stairs stair=%d event=bump_ignored row=%d reason=%s aim=%d face=%d stick=%d",
                            this->stairId, this->row, sBumpWhyNames[why], RsStairs_Degrees(push.aimOff),
                            RsStairs_Degrees(push.faceOff), (s32)push.stick);
        }
        return 0;
    }

    if (bump->count == 0) {
        RsStairs_Marker("rs_stairs stair=%d event=bump_start row=%d hold=%d aim=%d face=%d stick=%d", this->stairId,
                        this->row, hold, RsStairs_Degrees(push.aimOff), RsStairs_Degrees(push.faceOff),
                        (s32)push.stick);
    }
    if (bump->count < hold) {
        bump->count++;
    }
    if (bump->count < hold) {
        return 0;
    }
    if (!bump->offered) {
        bump->offered = 1;
        RsStairs_Marker("rs_stairs stair=%d event=bump_offer row=%d count=%d hold=%d", this->stairId, this->row,
                        bump->count, hold);
    }
    return 1;
}

// --- the actor -------------------------------------------------------------------------------------

void RsStairs_Init(Actor* thisx, PlayState* play) {
    RsStairs* this = (RsStairs*)thisx;

    this->stairId = RS_STAIR_PARAMS_GET_ID(thisx->params);
    this->row = RS_STAIR_PARAMS_GET_ROW(thisx->params);
    this->bump.ignoredWhy = -1;

    // Loud, never fatal and never an assert - RsNpc's rule. A broken placement still stands there
    // and its menu says what is wrong (RsActors.cpp renders the diagnostic), which is a mistake you
    // can see rather than a staircase that silently is not there.
    // Each mistake also gets a marker, so a run sees it without reading the engine log.
    if (RS_STAIR_PARAMS_GET_RSVD(thisx->params) != 0) {
        LUSLOG_ERROR("RsStairs: params 0x%04X has reserved bits set (stair %d)", (u16)thisx->params, this->stairId);
        RsStairs_Marker("rs_stairs stair=%d event=bad_placement row=%d reason=reserved_bits params=0x%04X",
                        this->stairId, this->row, (unsigned)(u16)thisx->params);
    }
    // Nothing to compare the placement's position against: the placement IS where its storey's
    // landing is measured from (StairDef.h), so the only thing it can get wrong is which row it names.
    if (RsStair_GetLanding(this->stairId, this->row) == NULL) {
        LUSLOG_ERROR("RsStairs: stair %d has no row %d in this build", this->stairId, this->row);
        RsStairs_Marker("rs_stairs stair=%d event=bad_placement row=%d reason=no_such_row", this->stairId, this->row);
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
    RsStairsPush push;
    const char* via;
    s32 autoAccept;

    // Cleared at the top of every tick, and set again below only on a tick the hold is reached.
    // Player updates before props, so it reads a flag set here on its NEXT update; one left standing
    // would open the menu on whatever next brought Link into talk range - walking past, or landing
    // from a move.
    this->actor.flags &= ~ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED;

    if (Actor_ProcessTalkRequest(&this->actor, play)) {
        // `via=` says which way in opened it: the auto-accepted offer, or A.
        via = this->bump.offered ? "bump" : "talk";
        if (!this->bump.offered) {
            RsStairs_ReadPush(this, play, GET_PLAYER(play), &push);
            RsStairs_BumpAbandon(this, RS_BUMP_TALK, &push);
        }
        this->bump.count = 0;
        this->bump.offered = 0;
        screen = RsStair_Screen(this->stairId, this->row);
        RsStairs_Marker("rs_stairs stair=%d event=open row=%d options=%d via=%s", this->stairId, this->row,
                        screen != NULL ? (int)screen->optionCount : 0, via);
        this->actionFunc = RsStairs_Talk;
        return;
    }
    autoAccept = RsStairs_Bump(this, play);
    // Not while a move is in flight - this placement's or any other's - so a second menu cannot open
    // under the fade.
    if (RsStair_IsMoving()) {
        return;
    }
    if (fabsf(this->actor.yDistToPlayer) > RS_STAIRS_TALK_Y) {
        return;
    }
    if (autoAccept) {
        this->actor.flags |= ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED;
    }
    Actor_OfferTalkExchange(&this->actor, play, RS_STAIRS_TALK_XZ, RS_STAIRS_TALK_Y, EXCH_ITEM_NONE);
}

// Back to waiting, LATCHED: the push that opened this box - or that moved its cursor - may still be
// held, and must be let go before it counts again. See the walk-into section.
static void RsStairs_EndTalk(RsStairs* this) {
    this->bump.ignoredWhy = -1;
    RsStairs_BumpLatch(this, "talk");
    this->actionFunc = RsStairs_Wait;
}

static void RsStairs_Talk(RsStairs* this, PlayState* play) {
    u8 state = Message_GetState(&play->msgCtx);
    s32 choice;
    s32 dest;
    s32 result;

    // Closed from outside - see RsNpc_Talk for why this matters.
    if (state == TEXT_STATE_NONE) {
        RsStairs_EndTalk(this);
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
        RsStairs_Marker("rs_stairs stair=%d event=choice row=%d index=%d to_row=%d result=%s", this->stairId,
                        this->row, (int)choice, (int)dest,
                        dest != RS_STAIR_MENU_CANCEL ? RsStair_ResultName(result) : "cancel");
        Message_CloseTextbox(play);
        RsStairs_EndTalk(this);
        return;
    }

    // A statement - only ever the diagnostic for a broken placement.
    if (state == TEXT_STATE_DONE && Message_ShouldAdvance(play)) {
        RsStairs_EndTalk(this);
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
