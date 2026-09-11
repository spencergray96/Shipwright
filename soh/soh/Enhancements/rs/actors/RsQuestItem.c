/*
 * RsQuestItem.c - the touch-to-collect quest item actor (sturdy-bassoon#58 P3 / #64, D16).
 *
 * Same C-under-/W3-/WX departures from vanilla idiom as RsNpc.c: no `s32 pad`, no ICHAIN, and an
 * explicit (Gfx*) on a gameplay_keep display list. Like RsNpc.c it contains no reference to
 * sceneNum and must not grow one.
 */

#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsQuestItem.h"
#include "RsActorParams.h"
#include "RsActors.h"
#include "RsItemArt.h"
#include "global.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestStore.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"

#define RS_ITEM_FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED)

// Collection radius, in world units, measured horizontally from the actor. Generous for the same
// reason RsNpc's talk range is: the agent loop arrives by teleport, and a tight radius makes "the
// step did not get set" and "I landed 6 units short" the same symptom.
#define RS_ITEM_COLLECT_RANGE 55.0f

// The pickup line names WHAT was picked up, from the quest's own step label (Quest_StepLabel,
// QuestDef.h) - so the three items of one quest say three different things without three item
// actors, and a quest that supplies no labels still gets a sentence rather than a blank.
#define RS_ITEM_PICKUP_FORMAT "You found %s.&The quest journal will&remember it."
static const char* sBadParamsText = "This item does not know&which quest it belongs to.";

void RsQuestItem_Init(Actor* thisx, PlayState* play);
void RsQuestItem_Destroy(Actor* thisx, PlayState* play);
void RsQuestItem_Update(Actor* thisx, PlayState* play);
void RsQuestItem_Draw(Actor* thisx, PlayState* play);

static void RsQuestItem_Wait(RsQuestItem* this, PlayState* play);
static void RsQuestItem_Collected(RsQuestItem* this, PlayState* play);

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
    { 18, 30, 0, { 0, 0, 0 } },
};

void RsQuestItem_Init(Actor* thisx, PlayState* play) {
    RsQuestItem* this = (RsQuestItem*)thisx;
    const QuestDef* def;

    this->questId = RS_ITEM_PARAMS_GET_QUEST(thisx->params);
    this->step = RS_ITEM_PARAMS_GET_STEP(thisx->params);
    this->valid = 0;
    this->awake = -1; // decided by the first Update, which is also what logs it

    def = Quest_GetDef(this->questId); // NULL for an invalid OR unregistered id; never asserts
    if (RS_ITEM_PARAMS_GET_RSVD(thisx->params) != 0) {
        LUSLOG_ERROR("RsQuestItem: params 0x%04X has reserved bits set", (u16)thisx->params);
    } else if (def == NULL) {
        LUSLOG_ERROR("RsQuestItem: quest %d has no definition in this build", this->questId);
    } else if (this->step < 0 || this->step >= def->stepCount) {
        LUSLOG_ERROR("RsQuestItem: quest %d has no step %d (stepCount %d)", this->questId, this->step, def->stepCount);
    } else {
        this->valid = 1;
    }

    // NOTHING IS WRITTEN HERE. The pitfall this actor exists to respect is "flags are set on
    // collection, never on spawn": a spawn that sets the step lets the player leave the zone
    // without the item and be locked out of it forever.

    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, &this->actor, &sCylinderInit);
    // No shadow yet: Actor_Draw paints shape.shadowDraw on its own, whatever RsQuestItem_Draw decides,
    // so a dormant item would otherwise leave a shadow on the floor with nothing above it. Update
    // installs ActorShadow_DrawCircle on the frame the item wakes (#98).
    ActorShape_Init(&thisx->shape, 0.0f, NULL, 9.0f);
    Actor_SetScale(thisx, 0.02f);

    // A SPRITE HAS TO BE LIFTED OFF THE FLOOR; THE FALLBACK MODEL DOES NOT. Actor_Draw translates
    // by `world.pos.y + shape.yOffset * scale.y`, and gItemDropDL's quad is centred on that origin
    // - so at yOffset 0 an item that has settled on the ground is drawn half buried in it, which is
    // exactly how it first appeared. gHeartPieceInteriorDL has its geometry modelled above its own
    // origin instead, which is why it has never needed this and why the offset is applied ONLY when
    // there is art: raising it unconditionally would move all five existing placements.
    // 450 * 0.02f = 9 units, half the sprite's 15 plus a little clearance. Vanilla's own drops do
    // the same thing with a larger, animated offset (EnItem00_Update, soh/src/code/z_en_item00.c).
    if (RsItemArt_Texture(this->questId, this->step) != NULL) {
        thisx->shape.yOffset = 450.0f;
    }

    thisx->uncullZoneDownward = 1200.0f;
    thisx->uncullZoneScale = 200.0f;
    thisx->gravity = -1.5f;

    this->actionFunc = RsQuestItem_Wait;
}

void RsQuestItem_Destroy(Actor* thisx, PlayState* play) {
    RsQuestItem* this = (RsQuestItem*)thisx;

    Collider_DestroyCylinder(play, &this->collider);
}

static void RsQuestItem_Wait(RsQuestItem* this, PlayState* play) {
    char line[128];
    char pickup[128];
    Player* player = GET_PLAYER(play);
    s32 result;

    // An ACTORCAT_PROP actor keeps updating while Link is in a textbox (D_80116068[ACTORCAT_PROP]
    // does not list PLAYER_STATE1_TALKING), so without this gate an item lying near a quest-giver
    // would open its own textbox in the middle of a conversation - reassigning msgCtx->talkActor
    // and stepping on the reply the NPC is showing. Wait until the screen is clear.
    if (Message_GetState(&play->msgCtx) != TEXT_STATE_NONE ||
        (player->stateFlags1 & (PLAYER_STATE1_TALKING | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_CUTSCENE)) != 0) {
        return;
    }
    if (this->actor.xzDistToPlayer > RS_ITEM_COLLECT_RANGE || fabsf(this->actor.yDistToPlayer) > 60.0f) {
        return;
    }

    if (this->valid) {
        // Check-then-write, the P1 rule: an ordered-quest violation is bug class inside
        // Quest_SetStep (log + debug assert), and a tripped assert hangs the agent loop. The check
        // proves the same refusal without it, and a refusal is reported, not swallowed.
        result = Quest_CheckSetStep(this->questId, this->step);
        if (result == QUEST_OK) {
            result = Quest_SetStep(this->questId, this->step);
        }
        // Composed, so it has to be COPIED rather than pointed at: this actor is killed as soon as
        // the box starts closing and its instance memory goes back to the arena, while the text is
        // still being read (Message_OpenText re-enters from the draw path on a language switch).
        // Quest_StepLabel itself returns a definition string, but the sentence around it is local.
        snprintf(pickup, sizeof(pickup), RS_ITEM_PICKUP_FORMAT, Quest_StepLabel(this->questId, this->step));
        RsText_SetDirectCopy(pickup);
    } else {
        result = QUEST_ERR_BAD_DEF;
        RsText_SetDirect(sBadParamsText);
    }

    snprintf(line, sizeof(line), "rs_item quest=%d step=%d event=collect result=%s", this->questId, this->step,
             Quest_ResultName(result));
    RsAgent_Marker(line);

    Audio_PlayActorSound2(&this->actor, NA_SE_SY_GET_ITEM);
    Message_StartTextbox(play, RS_TEXT_DIRECT, &this->actor);
    this->collider.base.ocFlags1 &= ~OC1_ON;
    this->actionFunc = RsQuestItem_Collected;
}

// --- dormant until the quest is in progress (sturdy-bassoon#98) ------------------------------------
//
// The owner's rule: this mod has no economy of ordinary items, so a quest item means nothing outside
// its quest, and collecting one early only updated the journal of a quest nobody had offered. So an
// item is OFFERED only while its quest is IN_PROGRESS. A step already set never gets here at all
// (the ShouldActorInit hook in RsActors.cpp), and a COMPLETE quest has every step set - so in
// practice "not offered" means NOT_STARTED, or a quest reset while Link stands in the scene.
//
// Per frame, not per spawn, and that is the whole point: the Cook stands in the same scene as his
// ingredients, and an item decided at scene load would stay missing after the player accepts until
// they left and came back. The item is never killed for this, only put to sleep, so every live
// actor count the acceptance drivers assert is unchanged.
//
// A params word this build cannot honour stays awake, exactly as before: the item should be seen
// and say so on touch, not silently never appear. That branch also never calls QuestStore_GetStatus,
// which asserts on an invalid id - and an assert here hangs the agent loop.
static s32 RsQuestItem_IsOffered(RsQuestItem* this) {
    if (!this->valid) {
        return 1;
    }
    return QuestStore_GetStatus(this->questId) == QUEST_STATUS_IN_PROGRESS;
}

// Logged on every change rather than once, so a run can prove the gate was CHALLENGED - dormant at
// load, then awake with no reload - instead of only that an item was eventually collected.
static void RsQuestItem_UpdateOffer(RsQuestItem* this) {
    char line[128];
    const s32 offered = RsQuestItem_IsOffered(this);

    if (offered == this->awake) {
        return;
    }
    this->awake = offered;
    if (offered) {
        this->actor.shape.shadowDraw = ActorShadow_DrawCircle;
    } else {
        this->actor.shape.shadowDraw = NULL;
    }
    snprintf(line, sizeof(line), "rs_item quest=%d step=%d event=%s status=%s", this->questId, this->step,
             offered ? "awake" : "dormant",
             this->valid ? Quest_StatusName(QuestStore_GetStatus(this->questId)) : "invalid_params");
    RsAgent_Marker(line);
}

// Hidden and inert, but still alive: killing an actor that owns the OPEN textbox would leave
// msgCtx->talkActor dangling. Wait for the box to go away, then go.
static void RsQuestItem_Collected(RsQuestItem* this, PlayState* play) {
    u8 state = Message_GetState(&play->msgCtx);

    if (state == TEXT_STATE_NONE || state == TEXT_STATE_CLOSING) {
        Actor_Kill(&this->actor);
    }
}

void RsQuestItem_Update(Actor* thisx, PlayState* play) {
    RsQuestItem* this = (RsQuestItem*)thisx;

    // Only while waiting: a collected item's textbox is already open, and a quest reset underneath
    // it must not put it to sleep before it has despawned. `awake` is still 1 in that state, because
    // nothing dormant can have been collected.
    if (this->actionFunc == RsQuestItem_Wait) {
        RsQuestItem_UpdateOffer(this);
    }
    if (this->awake == 1) {
        this->actionFunc(this, play);
    }

    // Gravity and floor checks run even while dormant, so an item that wakes is already resting on
    // the ground rather than dropping into place in front of the player.
    Actor_MoveXZGravity(thisx);
    Actor_UpdateBgCheckInfo(play, thisx, 5.0f, 20.0f, 0.0f, 0x1D);

    if (this->actionFunc == RsQuestItem_Wait && this->awake == 1) {
        Collider_UpdateCylinder(thisx, &this->collider);
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider.base);
    }
}

void RsQuestItem_Draw(Actor* thisx, PlayState* play) {
    RsQuestItem* this = (RsQuestItem*)thisx;
    const char* tex;

    if (this->actionFunc != RsQuestItem_Wait) {
        return; // collected: nothing to draw while the pickup textbox finishes
    }
    if (this->awake != 1) {
        return; // dormant (#98), or the init frame before Update has decided
    }

    // NULL for any (quest, step) with no art of its own - every debug fixture, and any quest added
    // before its sprites are - which is what keeps this change invisible to everything but the
    // three items it is for.
    tex = this->valid ? RsItemArt_Texture(this->questId, this->step) : NULL;

    OPEN_DISPS(play->state.gfxCtx);

    if (tex != NULL) {
        // VANILLA'S OWN DROP SPRITE, with our texture in it. This is the non-bombchu branch of
        // EnItem00_DrawCollectible (soh/src/code/z_en_item00.c) copied exactly: gItemDropDL is the
        // flat billboarded quad every recovery heart, deku nut and magic jar in the game is drawn
        // on, and it reads its texture from SEGMENT 0x08. So the whole of "draw a collectible" is
        // two commands, and the size, the billboarding, the render mode and the alpha cutout are
        // all vanilla's rather than ours to get wrong.
        //
        // Written this way after the hand-rolled quad it replaced drew NOTHING AT ALL: that path
        // was copied from the *bombchu* branch beside this one, which is reachable only through an
        // enhancement and is evidently not exercised. See the P6 record - the lesson is to copy the
        // path the game runs thousands of times per playthrough, not the one that merely looks
        // closest.
        //
        // gItemDropDL expects a 32x32 RGBA16 texture, which is what every entry of vanilla's
        // sItemDropTex is and what RS_ITEM_ART_SIZE / the .rgb5a1.png suffix exist to guarantee.
        // Passing an archive path through a segment is exactly what vanilla does here too - post
        // Torch, sItemDropTex holds "__OTR__..." strings - so this still needs no object-bank
        // entry, the property RsNpc_Draw's note is about.
        // gItemDropDL PUTS TEXTURE ROW 0 AT THE BOTTOM, so a normally-oriented image renders upside
        // down on it. That is not a bug in the display list: vanilla's own drop textures are stored
        // flipped to suit it, which is why the one branch beside this one that loads a normally
        // -oriented image (the bombchu inventory icon) follows it with exactly this line. Doing it
        // here rather than flipping the PNGs keeps the artwork the right way up on disk, which is
        // the whole point of the asset being a PNG someone can open.
        Matrix_Scale(1.0f, -1.0f, 1.0f, MTXMODE_APPLY);

        POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
        POLY_OPA_DISP = Gfx_SetupDL_66(POLY_OPA_DISP);

        // Matrix_NewMtx by hand rather than MATRIX_NEWMTX: the macro passes __FILE__, a
        // `const char[]`, into a `char*` parameter. soh/src builds at /w and gets away with it;
        // everything under soh/soh is /W3 /WX, so the cast is not optional here. RsNpc_Draw and
        // the fallback below do the same, and gSPSegment's cast below is the same story.
        gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                  G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)tex);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gItemDropDL);
    } else {
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                  G_MTX_MODELVIEW | G_MTX_LOAD);
        // gameplay_keep again, so no scene needs an object list entry (see the note in RsNpc_Draw).
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gHeartPieceInteriorDL);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
