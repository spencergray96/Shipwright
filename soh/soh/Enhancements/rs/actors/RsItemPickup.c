/*
 * RsItemPickup.c - the inventory's actor (sturdy-bassoon#286): a counted pickup, a key-item pickup, or the icon that
 * bounces over Link's head when a count goes up. RsItemPickup.h says which is which and what params carry.
 *
 * Same C-under-/W3-/WX departures from vanilla idiom as RsQuestItem.c, and like it, no reference to sceneNum.
 */

#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsItemPickup.h"
#include "RsActors.h"
#include "global.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"
#include "soh/Enhancements/rs/items/RsItems.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"

extern const char* _gAmmoDigit0Tex[]; // z_parameter.c: the C buttons' ammo digits, IA8 8 x 8

// The quest item's numbers, for its reason: the agent loop arrives by teleport (RsQuestItem.c).
#define RS_PICKUP_COLLECT_RANGE 55.0f
#define RS_PICKUP_COLLECT_HEIGHT 60.0f

// The get-item id the key-item entry carries: the quest item's, for the quest item's reasons (RsQuestItem.c).
#define RS_PICKUP_GET_ITEM_ID GI_TEXT_0

// The icon is 36 x 32 model units, RuneScape's inventory sprite size; its texture is 8x that (RS_ITEM_ICON_W/H,
// 288 x 256), eight texels a unit. These scales make it about as big as the quest items' sprites: 15 units tall on
// the floor, 18 over Link's head. Held up, the camera is close: 3.4 filled the screen in the first look, 1.2 is a
// third of that.
#define RS_PICKUP_FLOOR_SCALE 0.47f
#define RS_PICKUP_FLOAT_SCALE 0.56f
#define RS_PICKUP_HELD_SCALE 1.2f // Player_DrawGetItemImpl has already scaled by 0.2
// A key item's RS model held up (sturdy-bassoon#286) is scaled so its largest span is this many model units, under
// Player's 0.2: every key item is held at one size, as vanilla's get-item models are, whatever size RS made it (a
// hammer is a third of a schematic). About the icon's width at RS_PICKUP_HELD_SCALE, which read right in Link's hands.
#define RS_PICKUP_HELD_MODEL_SIZE 50.0f
#define RS_ICON_W 36
#define RS_ICON_H 32
// A float's life: vanilla's collected drop bounces for about this long.
#define RS_PICKUP_FLOAT_TICKS 24

#define RS_PICKUP_MTX(play) Matrix_NewMtx((play)->state.gfxCtx, (char*)__FILE__, __LINE__)

static void RsItemPickup_Wait(RsItemPickup* this, PlayState* play);
static void RsItemPickup_Float(RsItemPickup* this, PlayState* play);
static void RsItemPickup_DrawHeld(PlayState* play, GetItemEntry* entry);

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

// THE QUADS. The icon centred on the origin, texel for unit (row 0 at the top); a placeholder square inside it; and
// four digit cells along the icon's top edge from its left, a little in front of it so they never fight it for depth.
// A digit cell is 8 x 8 units but steps 6, as RS's small font sits its digits close.
static Vtx sIconVtx[4] = {
    { { { -RS_ICON_W / 2, RS_ICON_H / 2, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { RS_ICON_W / 2, RS_ICON_H / 2, 0 }, 0, { RS_ITEM_ICON_W << 5, 0 }, { 255, 255, 255, 255 } } },
    { { { RS_ICON_W / 2, -RS_ICON_H / 2, 0 },
        0,
        { RS_ITEM_ICON_W << 5, RS_ITEM_ICON_H << 5 },
        { 255, 255, 255, 255 } } },
    { { { -RS_ICON_W / 2, -RS_ICON_H / 2, 0 }, 0, { 0, RS_ITEM_ICON_H << 5 }, { 255, 255, 255, 255 } } },
};

static Vtx sPlaceholderVtx[4] = {
    { { { -12, 12, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { 12, 12, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { 12, -12, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { -12, -12, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
};

#define RS_DIGIT_CELLS 4
#define RS_DIGIT_X0 (-RS_ICON_W / 2)
#define RS_DIGIT_STEP 6
#define RS_DIGIT_TOP (RS_ICON_H / 2)
#define RS_DIGIT_Z 2
#define RS_DIGIT_VTX(cell, dx, dy)                                                                                     \
    { { { RS_DIGIT_X0 + (cell)*RS_DIGIT_STEP + (dx), RS_DIGIT_TOP - (dy), RS_DIGIT_Z },                                \
        0,                                                                                                             \
        { (dx) << 5, (dy) << 5 },                                                                                      \
        { 255, 255, 255, 255 } } }
#define RS_DIGIT_CELL(cell) \
    RS_DIGIT_VTX(cell, 0, 0), RS_DIGIT_VTX(cell, 8, 0), RS_DIGIT_VTX(cell, 8, 8), RS_DIGIT_VTX(cell, 0, 8)
static Vtx sDigitVtx[RS_DIGIT_CELLS][4] = {
    { RS_DIGIT_CELL(0) },
    { RS_DIGIT_CELL(1) },
    { RS_DIGIT_CELL(2) },
    { RS_DIGIT_CELL(3) },
};

static void RsItemPickup_Mark(RsItemPickup* this, const char* event, const char* result) {
    char line[160];
    const char* token = "?";

    if (this->valid) {
        token = this->key ? RsKeyItems_Def(this->item)->token : RsItems_Def(this->item)->token;
    }
    snprintf(line, sizeof(line), "rs_pickup event=%s item=%s key=%d count=%d once_flag=%d result=%s", event, token,
             (int)this->key, (int)this->count, (int)this->onceFlag, result);
    RsAgent_Marker(line);
}

void RsItemPickup_Init(Actor* thisx, PlayState* play) {
    RsItemPickup* this = (RsItemPickup*)thisx;
    const u16 params = (u16)thisx->params;
    s32 isFloat = (params & RS_PICKUP_PARAMS_FLOAT) != 0;

    this->item = params & RS_PICKUP_PARAMS_ITEM_MASK;
    this->key = (params & RS_PICKUP_PARAMS_KEY) != 0;
    this->count = thisx->home.rot.x > 0 ? thisx->home.rot.x : 1;
    this->onceFlag = (!this->key && thisx->home.rot.z > 0) ? thisx->home.rot.z - 1 : -1;
    this->valid = 0;

    if ((params & RS_PICKUP_PARAMS_RSVD_MASK) != 0) {
        LUSLOG_ERROR("RsItemPickup: params 0x%04X has reserved bits set", params);
    } else if (this->key ? RsKeyItems_Def(this->item) == NULL : RsItems_Def(this->item) == NULL) {
        LUSLOG_ERROR("RsItemPickup: params 0x%04X names no %s", params, this->key ? "key item" : "item");
    } else if (!this->key && this->count > RsItems_Def(this->item)->cap) {
        LUSLOG_ERROR("RsItemPickup: a pickup of %d is past the item's cap", this->count);
    } else if (this->onceFlag >= 0 && !WORLD_FLAG_IS_PLACEMENT(this->onceFlag)) {
        // A placement flag, handed out one per placement (WorldFlagIds.h, #232): a named flag would make picking up
        // planks unlock someone else's door, and the debug placement band keeps `quest debugwipe` able to reset it.
        LUSLOG_ERROR("RsItemPickup: once-only flag %d is not a placement flag", this->onceFlag);
    } else {
        this->valid = 1;
    }

    // Extra parameters, not a facing: a sprite has none.
    thisx->world.rot.x = thisx->world.rot.y = thisx->world.rot.z = 0;
    thisx->shape.rot.x = thisx->shape.rot.y = thisx->shape.rot.z = 0;

    if (isFloat) {
        // Nothing to collide with or settle: it sits over Link's head (RsItemPickup_Float). Lifted by half the icon
        // and more, in model units, so its bottom edge clears his head through the bounce: centred on the bounce
        // point it dipped behind his hair (the owner's first look). Vanilla lifts a drop the same way, by its
        // shape.yOffset (430 at the arrows' 0.035, about 15 units).
        Actor_SetScale(thisx, RS_PICKUP_FLOAT_SCALE);
        thisx->shape.yOffset = RS_ICON_H / 2 + 12;
        this->timer = RS_PICKUP_FLOAT_TICKS;
        this->actionFunc = RsItemPickup_Float;
        RsItemPickup_Mark(this, "float", "ok");
        return;
    }

    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, &this->actor, &sCylinderInit);
    // A circle shadow's size is shadowScale times the actor's scale: the quest item's 9 at its 0.02 is 0.18, so this
    // is 0.18 / 0.47. At 9 it was a dark disc twenty times too wide.
    ActorShape_Init(&thisx->shape, 0.0f, ActorShadow_DrawCircle, 0.18f / RS_PICKUP_FLOOR_SCALE);
    Actor_SetScale(thisx, RS_PICKUP_FLOOR_SCALE);
    // Lifted off the floor by half the sprite and a little, or it is drawn half buried (RsQuestItem.c's lesson).
    // In model units: Actor_Draw multiplies it by the scale, as it does the quad.
    thisx->shape.yOffset = RS_ICON_H / 2 + 2;
    thisx->uncullZoneDownward = 1200.0f;
    thisx->uncullZoneScale = 200.0f;
    thisx->gravity = -1.5f;
    this->actionFunc = RsItemPickup_Wait;
}

void RsItemPickup_Destroy(Actor* thisx, PlayState* play) {
    RsItemPickup* this = (RsItemPickup*)thisx;

    if (this->actionFunc != RsItemPickup_Float) {
        Collider_DestroyCylinder(play, &this->collider);
    }
}

// --- the key item's get-item entry -----------------------------------------------------------------------------
//
// RsQuestItem_GetItemEntryFor's fields, for its reasons (RsQuestItem.c), with the key item's text id: ITEM_NONE so
// nothing enters the vanilla inventory and the ceremony plays, gameplay_keep so Player keeps the entry, GI_TEXT_0 so
// it is taken on contact and a stray table lookup grants nothing, gi 1 so the held item draws.
GetItemEntry RsItemPickup_KeyEntry(s32 key) {
    GetItemEntry entry = GET_ITEM_NONE;

    entry.itemId = ITEM_NONE;
    entry.gi = 1;
    entry.textId = RS_TEXT_KEY_ITEM_ID(key);
    entry.objectId = OBJECT_GAMEPLAY_KEEP;
    entry.modIndex = MOD_NONE;
    entry.tableId = MOD_NONE;
    entry.getItemId = RS_PICKUP_GET_ITEM_ID;
    entry.collectable = true;
    entry.getItemFrom = ITEM_FROM_FREESTANDING;
    entry.getItemCategory = ITEM_CATEGORY_LESSER;
    entry.drawItemId = ITEM_NONE;
    entry.drawModIndex = MOD_NONE;
    entry.drawFunc = RsItemPickup_DrawHeld;
    return entry;
}

void RsItemPickup_SpawnFloat(PlayState* play, s32 item, s32 count) {
    Player* player = GET_PLAYER(play);

    if (player == NULL || RsItems_Def(item) == NULL) {
        return;
    }
    Actor_Spawn(&play->actorCtx, play, ACTOR_RS_ITEM_PICKUP, player->actor.world.pos.x, player->actor.world.pos.y,
                player->actor.world.pos.z, (s16)count, 0, 0, (s16)(RS_PICKUP_PARAMS(item) | RS_PICKUP_PARAMS_FLOAT));
}

void RsItemPickup_ShouldInit(void* actorRef, bool* should) {
    Actor* actor = (Actor*)actorRef;
    const u16 params = (u16)actor->params;
    char line[128];
    s32 item = params & RS_PICKUP_PARAMS_ITEM_MASK;
    s32 flag = -1;

    if ((params & RS_PICKUP_PARAMS_FLOAT) != 0) {
        return;
    }
    if ((params & RS_PICKUP_PARAMS_KEY) != 0) {
        if (RsKeyItems_Def(item) != NULL) {
            flag = RsKeyItems_Def(item)->worldFlag;
        }
    } else if (actor->home.rot.z > 0 && actor->home.rot.z - 1 < WORLD_FLAG_MAX) {
        flag = actor->home.rot.z - 1;
    }
    // A bad params word is left alone: it spawns and Init says what is wrong with it.
    if (flag < 0 || !Flags_GetWorldFlag(flag)) {
        return;
    }
    *should = false;
    snprintf(line, sizeof(line), "rs_pickup event=suppressed item=%d key=%d flag=%d", (int)item,
             (params & RS_PICKUP_PARAMS_KEY) != 0 ? 1 : 0, (int)flag);
    RsAgent_Marker(line);
}

// --- collecting -------------------------------------------------------------------------------------------------

static s32 RsItemPickup_LinkFree(PlayState* play) {
    Player* player = GET_PLAYER(play);

    // The quest item's gate (RsQuestItem_Wait): a prop keeps updating under a textbox, and a pickup next to a
    // talking NPC must not open a box of its own in the middle of the conversation.
    return Message_GetState(&play->msgCtx) == TEXT_STATE_NONE &&
           (player->stateFlags1 & (PLAYER_STATE1_TALKING | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_CUTSCENE)) ==
               0;
}

static void RsItemPickup_Wait(RsItemPickup* this, PlayState* play) {
    s32 result;

    // A key item handed over: Player has it, and the flag is set now, on collection (RsQuestItem_Wait's order: this
    // first, because Link is GETTING_ITEM by now and the gate below would read that as busy).
    if (this->key && Actor_HasParent(&this->actor, play)) {
        result = RsKeyItems_Grant(this->item, 0, RS_ITEMS_SRC_PICKUP);
        RsItemPickup_Mark(this, "collect", RsItems_ResultName(result));
        Actor_Kill(&this->actor);
        return;
    }
    if (!this->valid || !RsItemPickup_LinkFree(play)) {
        return;
    }
    if (this->actor.xzDistToPlayer > RS_PICKUP_COLLECT_RANGE ||
        fabsf(this->actor.yDistToPlayer) > RS_PICKUP_COLLECT_HEIGHT) {
        return;
    }
    if (this->key) {
        // An offer: Player takes it on its own update, and may decline (mid-air, carrying something).
        GiveItemEntryFromActor(&this->actor, play, RsItemPickup_KeyEntry(this->item), RS_PICKUP_COLLECT_RANGE,
                               RS_PICKUP_COLLECT_HEIGHT);
        return;
    }

    // Taken whatever Link carries, as vanilla takes a drop into a full quiver: what fits is added, the rest is lost,
    // and the float and the sound are the same either way (the owner: a box here interrupted play). `result=full`
    // on the marker says some was lost.
    result = RsItems_GainClamped(this->item, this->count, 1, RS_ITEMS_SRC_PICKUP) == this->count ? RS_ITEMS_OK
                                                                                                  : RS_ITEMS_FULL;
    RsItemPickup_Mark(this, "collect", RsItems_ResultName(result));
    if (this->onceFlag >= 0) {
        Flags_SetWorldFlag(this->onceFlag);
    }
    // Vanilla's collected drop exactly (EnItem00_Update): a SYSTEM sound at the default position, not an actor sound.
    // An actor sound is placed at the pickup and tied to it, and this actor dies on this frame, so the first cut's
    // NA_SE_SY_GET_ITEM came out quieter and clipped (the owner, 2026-11-03).
    Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    Actor_Kill(&this->actor);
}

// Over Link's head, bouncing less and less, then gone: EnItem00's collected drop (func_8001E5C8) - its 40 units
// up, 20 more for an adult, and its sine bounce scaled by the ticks left.
static void RsItemPickup_Float(RsItemPickup* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (--this->timer <= 0) {
        Actor_Kill(&this->actor);
        return;
    }
    this->actor.world.pos = player->actor.world.pos;
    this->actor.world.pos.y += 40.0f + Math_SinS(this->timer * 15000) * (this->timer * 0.3f);
    if (LINK_IS_ADULT) {
        this->actor.world.pos.y += 20.0f;
    }
}

void RsItemPickup_Update(Actor* thisx, PlayState* play) {
    RsItemPickup* this = (RsItemPickup*)thisx;

    this->actionFunc(this, play);
    if (this->actionFunc == RsItemPickup_Float || this->actor.update == NULL) {
        return;
    }
    Actor_MoveXZGravity(thisx);
    Actor_UpdateBgCheckInfo(play, thisx, 5.0f, 20.0f, 0.0f, 0x1D);
    Collider_UpdateCylinder(thisx, &this->collider);
    CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider.base);
}

// --- drawing ----------------------------------------------------------------------------------------------------

// The icon, or the placeholder square while mods/rs-items.o2r lacks it, then the count in RS's stack yellow at the
// icon's top-left, with RS's one-pixel black shadow under it, when there is more than one. Under whatever matrix the
// caller has loaded, which faces the camera.
static void RsItemPickup_DrawIcon(PlayState* play, s32 key, s32 item, s32 count, s32 ground) {
    const char* tex = key ? RsKeyItems_IconPath(item, ground) : RsItems_IconPath(item, ground);
    const s32 found = key ? RsKeyItems_IconFound(item, ground) : RsItems_IconFound(item, ground);
    char digits[8];
    s32 n;
    s32 pass;
    s32 i;

    OPEN_DISPS(play->state.gfxCtx);

    // TWO-CYCLE, AS THE SETUP LEAVES IT: fog in the first blender cycle, our surface in the second, and the
    // combiner's work in its first cycle with G_CC_PASS2 after it. Naming TEXEL0 in the second combiner cycle reads
    // the other tile there - the first cut did, and the icon drew transparent on the floor and as a flat blue square
    // in Link's hands, while the untextured placeholder was fine.
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPMatrix(POLY_OPA_DISP++, RS_PICKUP_MTX(play), G_MTX_MODELVIEW | G_MTX_LOAD);

    if (found) {
        // Never a path that did not resolve: the renderer would skip the command after it and draw garbage
        // (RsItemArt.cpp), so RsItems_IconFound is asked first, every time.
        gDPSetRenderMode(POLY_OPA_DISP++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_PASS2);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
        gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        // A TILE load, not a block: LoadBlock's count is capped at 4096 texels (G_TX_LDBLK_MAX_TXL) and would load
        // the top fourteen rows of a 288 x 256 icon. libultraship's LoadTile takes the whole rectangle.
        gDPLoadTextureTile(POLY_OPA_DISP++, (u8*)tex, G_IM_FMT_RGBA, G_IM_SIZ_16b, RS_ITEM_ICON_W, RS_ITEM_ICON_H, 0,
                           0, RS_ITEM_ICON_W - 1, RS_ITEM_ICON_H - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                           G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gSPVertex(POLY_OPA_DISP++, (uintptr_t)sIconVtx, 4, 0);
        gSP2Triangles(POLY_OPA_DISP++, 0, 2, 1, 0, 0, 3, 2, 0);
    } else {
        gDPSetRenderMode(POLY_OPA_DISP++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_PRIMITIVE, G_CC_PASS2);
        if (key) {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 230, 190, 60, 255);
        } else {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 160, 120, 70, 255);
        }
        gSPVertex(POLY_OPA_DISP++, (uintptr_t)sPlaceholderVtx, 4, 0);
        gSP2Triangles(POLY_OPA_DISP++, 0, 2, 1, 0, 0, 3, 2, 0);
    }

    if (count > 1) {
        n = snprintf(digits, sizeof(digits), "%d", (int)count);
        if (n > RS_DIGIT_CELLS) {
            n = RS_DIGIT_CELLS;
        }
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetRenderMode(POLY_OPA_DISP++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_PASS2);
        gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        // The shadow first, one unit right and down, then the yellow over it.
        for (pass = 0; pass < 2; pass++) {
            if (pass == 0) {
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 0, 0, 0, 255);
                Matrix_Push();
                Matrix_Translate(1.0f, -1.0f, -0.5f, MTXMODE_APPLY);
                gSPMatrix(POLY_OPA_DISP++, RS_PICKUP_MTX(play), G_MTX_MODELVIEW | G_MTX_LOAD);
                Matrix_Pop();
            } else {
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 0, 255);
                gSPMatrix(POLY_OPA_DISP++, RS_PICKUP_MTX(play), G_MTX_MODELVIEW | G_MTX_LOAD);
            }
            for (i = 0; i < n; i++) {
                gDPLoadTextureBlock(POLY_OPA_DISP++, (u8*)_gAmmoDigit0Tex[digits[i] - '0'], G_IM_FMT_IA, G_IM_SIZ_8b,
                                    8, 8, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                                    G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                gSPVertex(POLY_OPA_DISP++, (uintptr_t)sDigitVtx[i], 4, 0);
                gSP2Triangles(POLY_OPA_DISP++, 0, 2, 1, 0, 0, 3, 2, 0);
            }
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void RsItemPickup_Draw(Actor* thisx, PlayState* play) {
    RsItemPickup* this = (RsItemPickup*)thisx;

    if (!this->valid) {
        // Drawn anyway, as the placeholder, so a bad row is seen rather than silently missing.
        Matrix_ReplaceRotation(&play->billboardMtxF);
        RsItemPickup_DrawIcon(play, 0, -1, 0, 0);
        return;
    }
    Matrix_ReplaceRotation(&play->billboardMtxF);
    // On the floor it is the ground icon (no shadow), with its count unless `items groundcount off`; over Link's
    // head, gained, it is the inventory icon, always with its count.
    const s32 onFloor = this->actionFunc != RsItemPickup_Float;
    const s32 shown = this->key || (onFloor && !CVarGetInteger(CVAR_RS_ITEMS_GROUND_COUNT, 1)) ? 1 : this->count;
    RsItemPickup_DrawIcon(play, this->key, this->item, shown, onFloor);
}

// Link holding a key item up. Player_DrawGetItemImpl has the matrix at the held position, spinning and scaled by 0.2.
// The text id in Player's copy of the entry says which key item.
//
// Its RS model, when mods/ has it (RsKeyItems_ModelList): the item's inventory model, turned as its icon views it and
// centred, so under Player's matrix it spins about its own middle as vanilla's get-item models do. Its list carries its
// own materials, lit by the lights Player bound, so the setup is only the one every get-item draw starts from.
//
// `items hold` (sturdy-bassoon#294) stops the spin: Player turned it by gameplayFrames * 1000 about the world's upright,
// and the scale before it is uniform, so turning it back by that and on by the camera's yaw leaves it at the held side,
// turned from its +z facing the camera, wherever the ceremony's camera stands.
//
// Without one, the icon: one quad that does not face the camera by itself (gItemDropDL does, which is why
// RsQuestItem_DrawHeld replaces the rotation with identity), so it takes the billboard, and the spin goes.
static void RsItemPickup_DrawHeld(PlayState* play, GetItemEntry* entry) {
    const s32 key = RS_TEXT_IS_KEY_ITEM(entry->textId) ? RS_TEXT_KEY_ITEM_GET(entry->textId) : -1;
    Gfx* model = (Gfx*)RsKeyItems_ModelList(key);

    if (model != NULL) {
        const f32 scale = RS_PICKUP_HELD_MODEL_SIZE / RsKeyItems_ModelExtent(key);

        s16 hold;

        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        if (RsKeyItems_CeremonyHold(&hold)) {
            const s16 toCamera = Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) + 0x8000;
            Matrix_RotateY(BINANG_TO_RAD((s16)(toCamera + hold - (s16)(play->gameplayFrames * 1000))), MTXMODE_APPLY);
        }
        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, RS_PICKUP_MTX(play), G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPDisplayList(POLY_OPA_DISP++, model);
        CLOSE_DISPS(play->state.gfxCtx);
        return;
    }
    Matrix_ReplaceRotation(&play->billboardMtxF);
    Matrix_Scale(RS_PICKUP_HELD_SCALE, RS_PICKUP_HELD_SCALE, RS_PICKUP_HELD_SCALE, MTXMODE_APPLY);
    RsItemPickup_DrawIcon(play, 1, key, 1, 0);
}
