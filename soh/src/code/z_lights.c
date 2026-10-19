#include "global.h"

#include <string.h>

#include "objects/gameplay_keep/gameplay_keep.h"

#include "soh/frame_interpolation.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/savestate_serialize.h"
#include "soh/Enhancements/lights/LightsBudget.h"
#include "soh/custom/scenes/grid_tool/GridToolSceneRegistry.h"

// LIGHTS_BUFFER_SIZE is 256 (vanilla 32), in LightsBudget.h with the reasons (sturdy-bassoon#233).

typedef struct {
    /* 0x000 */ s32 numOccupied;
    /* 0x004 */ s32 searchIndex;
    /* 0x008 */ LightNode buf[LIGHTS_BUFFER_SIZE];
} LightsBuffer; // size = 0x188 at vanilla's 32

static LightsBuffer sLightsBuffer;

// The whole buffer is the savestate field, so a state's size follows LIGHTS_BUFFER_SIZE. Savestates
// live in memory only (savestates.cpp writes no file), so one taken by a build with another size can
// never reach this one (#233).
#define LIGHTS_SHIP_SAVESTATE_FIELDS(F) F(sLightsBuffer)
SHIP_SAVESTATE_DEFINE(Lights, LIGHTS_SHIP_SAVESTATE_FIELDS)

extern PlayState* gPlayState;

// The cap new inserts are held to (#233). LIGHTS_BUFFER_SIZE unless `lights pool <n>` lowered it for
// this session; never saved, so a planted 32 cannot outlive the run that planted it.
static s32 sLightsPoolCap = LIGHTS_BUFFER_SIZE;

// `lights status`'s counters (LightsBudget.h). `sLightsTick` is the tick being counted, rolled into
// `sLightsLastTick` by the first bind of the next one.
static s32 sLightsPeak;
static s32 sLightsRefused;
static LightsStats sLightsTick;
static LightsStats sLightsLastTick;
static u32 sLightsTickFrame;
static s32 sLightsTickStarted;

static void Lights_RollTick(void) {
    u32 frame;
    LightsStats fresh = { 0 };

    if (gPlayState == NULL) {
        return;
    }
    frame = gPlayState->gameplayFrames;
    if (sLightsTickStarted && frame == sLightsTickFrame) {
        return;
    }
    if (sLightsTickStarted) {
        sLightsLastTick = sLightsTick;
    }
    fresh.tick = frame;
    fresh.linkInRange = fresh.linkPoints = fresh.linkDirs = -1;
    // The gate is read once a tick, not once a bind: a CVar read and a registry lookup per drawn actor
    // would be the costliest thing in the bind.
    fresh.sunFirstGate = CVarGetInteger(CVAR_LIGHTS_SUN_FIRST, LIGHTS_SUN_FIRST_DEFAULT) &&
                         GridToolSceneRegistry_IsCustomScene(gPlayState->sceneNum);
    sLightsTick = fresh;
    sLightsTickFrame = frame;
    sLightsTickStarted = 1;
}

void Lights_GetStats(LightsStats* out) {
    *out = sLightsLastTick;
    out->occupied = sLightsBuffer.numOccupied;
    out->cap = sLightsPoolCap;
    out->peak = sLightsPeak;
    out->refused = sLightsRefused;
}

s32 Lights_SetPoolCap(s32 cap) {
    sLightsPoolCap = CLAMP(cap, 1, LIGHTS_BUFFER_SIZE);
    return sLightsPoolCap;
}

void Lights_PointSetInfo(LightInfo* info, s16 x, s16 y, s16 z, u8 r, u8 g, u8 b, s16 radius, s32 type) {
    info->type = type;
    info->params.point.x = x;
    info->params.point.y = y;
    info->params.point.z = z;
    Lights_PointSetColorAndRadius(info, r, g, b, radius);
}

void Lights_PointNoGlowSetInfo(LightInfo* info, s16 x, s16 y, s16 z, u8 r, u8 g, u8 b, s16 radius) {
    Lights_PointSetInfo(info, x, y, z, r, g, b, radius, LIGHT_POINT_NOGLOW);
}

void Lights_PointGlowSetInfo(LightInfo* info, s16 x, s16 y, s16 z, u8 r, u8 g, u8 b, s16 radius) {
    Lights_PointSetInfo(info, x, y, z, r, g, b, radius, LIGHT_POINT_GLOW);
}

void Lights_PointSetColorAndRadius(LightInfo* info, u8 r, u8 g, u8 b, s16 radius) {
    info->params.point.color[0] = r;
    info->params.point.color[1] = g;
    info->params.point.color[2] = b;
    info->params.point.radius = radius;
}

void Lights_DirectionalSetInfo(LightInfo* info, s8 x, s8 y, s8 z, u8 r, u8 g, u8 b) {
    info->type = LIGHT_DIRECTIONAL;
    info->params.dir.x = x;
    info->params.dir.y = y;
    info->params.dir.z = z;
    info->params.dir.color[0] = r;
    info->params.dir.color[1] = g;
    info->params.dir.color[2] = b;
}

// unused
void Lights_Reset(Lights* lights, u8 ambentR, u8 ambentG, u8 ambentB) {
    lights->l.a.l.col[0] = lights->l.a.l.colc[0] = ambentR;
    lights->l.a.l.col[1] = lights->l.a.l.colc[1] = ambentG;
    lights->l.a.l.col[2] = lights->l.a.l.colc[2] = ambentB;
    lights->numLights = 0;
}

/*
 * Draws every light in the provided Lights group
 */
void Lights_Draw(Lights* lights, GraphicsContext* gfxCtx) {
    Light* light;
    s32 i;

#if 1

    OPEN_DISPS(gfxCtx);

    gSPNumLights(POLY_OPA_DISP++, lights->numLights);
    gSPNumLights(POLY_XLU_DISP++, lights->numLights);

    i = 0;
    light = &lights->l.l[0];

    while (i < lights->numLights) {
        i++;
        gSPLight(POLY_OPA_DISP++, light, i);
        gSPLight(POLY_XLU_DISP++, light, i);
        light++;
    }

    i++; // abmient light is total number of lights + 1
    gSPLight(POLY_OPA_DISP++, &lights->l.a, i);
    gSPLight(POLY_XLU_DISP++, &lights->l.a, i);

    CLOSE_DISPS(gfxCtx);
#endif
}

Light* Lights_FindSlot(Lights* lights) {
    if (lights->numLights >= 7) {
        return NULL;
    } else {
        return &lights->l.l[lights->numLights++];
    }
}

void Lights_BindPoint(Lights* lights, LightParams* params, Vec3f* vec) {
    f32 xDiff;
    f32 yDiff;
    f32 zDiff;
    f32 posDiff;
    f32 scale;
    Light* light;

    if (vec != NULL) {
        xDiff = params->point.x - vec->x;
        yDiff = params->point.y - vec->y;
        zDiff = params->point.z - vec->z;
        scale = params->point.radius;
        posDiff = SQ(xDiff) + SQ(yDiff) + SQ(zDiff);

        if (posDiff < SQ(scale)) {
            light = Lights_FindSlot(lights);

            if (light != NULL) {
                posDiff = sqrtf(posDiff);
                scale = posDiff / scale;
                scale = 1 - SQ(scale);

                light->l.col[0] = light->l.colc[0] = params->point.color[0] * scale;
                light->l.col[1] = light->l.colc[1] = params->point.color[1] * scale;
                light->l.col[2] = light->l.colc[2] = params->point.color[2] * scale;

                scale = (posDiff < 1.0f) ? 120.0f : 120.0f / posDiff;

                light->l.dir[0] = xDiff * scale;
                light->l.dir[1] = yDiff * scale;
                light->l.dir[2] = zDiff * scale;
            }
        }
    }
}

void Lights_BindDirectional(Lights* lights, LightParams* params, Vec3f* vec) {
    Light* light = Lights_FindSlot(lights);

    if (light != NULL) {
        light->l.col[0] = light->l.colc[0] = params->dir.color[0];
        light->l.col[1] = light->l.colc[1] = params->dir.color[1];
        light->l.col[2] = light->l.colc[2] = params->dir.color[2];
        light->l.dir[0] = params->dir.x;
        light->l.dir[1] = params->dir.y;
        light->l.dir[2] = params->dir.z;
    }
}

/**
 * For every light in a provided list, try to find a free slot in the provided Lights group and bind
 * a light to it. Then apply color and positional/directional info for each light
 * based on the parameters supplied by the node.
 *
 * Note: Lights in a given list can only be binded to however many free slots are
 * available in the Lights group. This is at most 7 slots for a new group, but could be less.
 */
void Lights_BindAll(Lights* lights, LightNode* listHead, Vec3f* vec) {
    LightsBindFunc bindFuncs[] = { Lights_BindPoint, Lights_BindDirectional, Lights_BindPoint };
    LightInfo* info;
    // #233: one walk to count what wants a slot. The nearest 7 point lights are kept, sorted by
    // distance, with their list positions so the bind below can keep list order.
    LightNode* node;
    LightNode* nearest[7];
    f32 nearDistSq[7];
    s32 nearIndex[7];
    s32 numNear = 0;
    s32 numInRange = 0;
    s32 numDirs = 0;
    s32 index = 0;
    // Callers pass a fresh group, so this is 7, but the bind has never assumed it.
    s32 slotsFree = MAX(7 - lights->numLights, 0);
    s32 slotsForPoints;
    s32 i;
    s32 j;
    s32 overflow;
    s32 isLink;
    LightNode* listHead0 = listHead;

    Lights_RollTick();

    for (node = listHead; node != NULL; node = node->next, index++) {
        info = node->info;
        if (info->type == LIGHT_DIRECTIONAL) {
            numDirs++;
        } else if (vec != NULL) {
            // Lights_BindPoint's own range test, term for term, so "in range" here is exactly what
            // vanilla would bind.
            f32 xDiff = info->params.point.x - vec->x;
            f32 yDiff = info->params.point.y - vec->y;
            f32 zDiff = info->params.point.z - vec->z;
            f32 scale = info->params.point.radius;
            f32 distSq = SQ(xDiff) + SQ(yDiff) + SQ(zDiff);

            if (distSq < SQ(scale)) {
                numInRange++;
                // Insert into the sorted nearest 7. A tie keeps the earlier (newer) light, as vanilla.
                for (i = numNear; i > 0 && nearDistSq[i - 1] > distSq; i--) {
                    if (i < 7) {
                        nearest[i] = nearest[i - 1];
                        nearDistSq[i] = nearDistSq[i - 1];
                        nearIndex[i] = nearIndex[i - 1];
                    }
                }
                if (i < 7) {
                    nearest[i] = node;
                    nearDistSq[i] = distSq;
                    nearIndex[i] = index;
                    if (numNear < 7) {
                        numNear++;
                    }
                }
            }
        }
    }

    overflow = (numInRange + numDirs) > slotsFree;
    isLink = (vec != NULL) && (gPlayState != NULL) && (vec == &GET_PLAYER(gPlayState)->actor.world.pos);
    sLightsTick.binds++;
    sLightsTick.walked += index;
    sLightsTick.overflowed += overflow;
    sLightsTick.maxInRange = MAX(sLightsTick.maxInRange, numInRange);

    if (!overflow || !sLightsTick.sunFirstGate) {
        // Everything fits, or sun first is off: vanilla's loop, untouched.
        while (listHead != NULL) {
            info = listHead->info;
            bindFuncs[info->type](lights, &info->params, vec);
            listHead = listHead->next;
        }
    } else {
        // Sun first (#233). The directional lights' slots are reserved before any point light is
        // chosen, the nearest point lights fill the rest, and the layout stays vanilla's: point lights
        // first, in list order, then the directional lights, in list order. ActorShadow_DrawFeet reads
        // the last two slots as the sun, so the sun must also stay LAST.
        slotsForPoints = MAX(slotsFree - numDirs, 0);
        numNear = MIN(numNear, slotsForPoints);
        // The chosen few back into list order (at most 7, so a plain insertion sort).
        for (i = 1; i < numNear; i++) {
            LightNode* n = nearest[i];
            s32 k = nearIndex[i];
            for (j = i; j > 0 && nearIndex[j - 1] > k; j--) {
                nearest[j] = nearest[j - 1];
                nearIndex[j] = nearIndex[j - 1];
            }
            nearest[j] = n;
            nearIndex[j] = k;
        }
        for (i = 0; i < numNear; i++) {
            Lights_BindPoint(lights, &nearest[i]->info->params, vec);
        }
        for (node = listHead; node != NULL; node = node->next) {
            if (node->info->type == LIGHT_DIRECTIONAL) {
                Lights_BindDirectional(lights, &node->info->params, vec);
            }
        }
    }

    if (isLink) {
        sLightsTick.linkInRange = numInRange;
        if (overflow && sLightsTick.sunFirstGate) {
            sLightsTick.linkPoints = numNear;
            sLightsTick.linkDirs = MIN(numDirs, slotsFree);
        } else {
            // Vanilla's order, replayed for Link alone: each light in list order takes a slot while
            // one is free, so this is what the loop above bound, not what it would ideally bind.
            s32 left = slotsFree;
            sLightsTick.linkPoints = 0;
            sLightsTick.linkDirs = 0;
            for (node = listHead0; node != NULL && left > 0; node = node->next) {
                info = node->info;
                if (info->type == LIGHT_DIRECTIONAL) {
                    sLightsTick.linkDirs++;
                    left--;
                } else {
                    f32 xDiff = info->params.point.x - vec->x;
                    f32 yDiff = info->params.point.y - vec->y;
                    f32 zDiff = info->params.point.z - vec->z;
                    f32 scale = info->params.point.radius;
                    if (SQ(xDiff) + SQ(yDiff) + SQ(zDiff) < SQ(scale)) {
                        sLightsTick.linkPoints++;
                        left--;
                    }
                }
            }
        }
    }
}

LightNode* Lights_FindBufSlot() {
    LightNode* node;

    // The cap, not the buffer's size (#233): `lights pool <n>` can hold a session below it. A state
    // loaded over a lowered cap may hold more than it; inserts are refused until the count drops.
    if (sLightsBuffer.numOccupied >= sLightsPoolCap) {
        sLightsRefused++;
        return NULL;
    }

    node = &sLightsBuffer.buf[sLightsBuffer.searchIndex];

    while (node->info != NULL) {
        sLightsBuffer.searchIndex++;

        if (sLightsBuffer.searchIndex < LIGHTS_BUFFER_SIZE) {
            node++;
        } else {
            sLightsBuffer.searchIndex = 0;
            node = &sLightsBuffer.buf[0];
        }
    }

    sLightsBuffer.numOccupied++;
    sLightsPeak = MAX(sLightsPeak, sLightsBuffer.numOccupied);

    return node;
}

// return type must not be void to match
s32 Lights_FreeNode(LightNode* light) {
    if (light != NULL) {
        sLightsBuffer.numOccupied--;
        light->info = NULL;
        sLightsBuffer.searchIndex = (light - sLightsBuffer.buf) / sizeof(LightNode);
    }
}

void LightContext_Init(PlayState* play, LightContext* lightCtx) {
    LightContext_InitList(play, lightCtx);
    LightContext_SetAmbientColor(lightCtx, 80, 80, 80);
    LightContext_SetFog(lightCtx, 0, 0, 0, 996, 12800);
    memset(&sLightsBuffer, 0, sizeof(sLightsBuffer));
    sLightsPeak = 0;
    sLightsRefused = 0;
}

void LightContext_SetAmbientColor(LightContext* lightCtx, u8 r, u8 g, u8 b) {
    lightCtx->ambientColor[0] = r;
    lightCtx->ambientColor[1] = g;
    lightCtx->ambientColor[2] = b;
}

void LightContext_SetFog(LightContext* lightCtx, u8 r, u8 g, u8 b, s16 fogNear, s16 fogFar) {
    lightCtx->fogColor[0] = r;
    lightCtx->fogColor[1] = g;
    lightCtx->fogColor[2] = b;
    lightCtx->fogNear = fogNear;
    lightCtx->fogFar = fogFar;
}

/**
 * Allocate a new Lights group and initilize the ambient color with that provided by LightContext
 */
Lights* LightContext_NewLights(LightContext* lightCtx, GraphicsContext* gfxCtx) {
    return Lights_New(gfxCtx, lightCtx->ambientColor[0], lightCtx->ambientColor[1], lightCtx->ambientColor[2]);
}

void LightContext_InitList(PlayState* play, LightContext* lightCtx) {
    lightCtx->listHead = NULL;
}

void LightContext_DestroyList(PlayState* play, LightContext* lightCtx) {
    while (lightCtx->listHead != NULL) {
        LightContext_RemoveLight(play, lightCtx, lightCtx->listHead);
        lightCtx->listHead = lightCtx->listHead->next;
    }
}

/**
 * Insert a new light into the list pointed to by LightContext
 *
 * Note: Due to the limited number of slots in a Lights group, inserting too many lights in the
 * list may result in older entries not being bound to a Light when calling Lights_BindAll
 */
LightNode* LightContext_InsertLight(PlayState* play, LightContext* lightCtx, LightInfo* info) {
    LightNode* node;

    node = Lights_FindBufSlot();

    if (node != NULL) {
        node->info = info;
        node->prev = NULL;
        node->next = lightCtx->listHead;

        if (lightCtx->listHead != NULL) {
            lightCtx->listHead->prev = node;
        }

        lightCtx->listHead = node;
    }

    return node;
}

void LightContext_RemoveLight(PlayState* play, LightContext* lightCtx, LightNode* node) {
    if (node != NULL) {
        if (node->prev != NULL) {
            node->prev->next = node->next;
        } else {
            lightCtx->listHead = node->next;
        }

        if (node->next != NULL) {
            node->next->prev = node->prev;
        }

        Lights_FreeNode(node);
    }
}

// unused
Lights* Lights_NewAndDraw(GraphicsContext* gfxCtx, u8 ambientR, u8 ambientG, u8 ambientB, u8 numLights, u8 r, u8 g,
                          u8 b, s8 x, s8 y, s8 z) {
    Lights* lights;
    s32 i;

    lights = Graph_Alloc(gfxCtx, sizeof(Lights));

    lights->l.a.l.col[0] = lights->l.a.l.colc[0] = ambientR;
    lights->l.a.l.col[1] = lights->l.a.l.colc[1] = ambientG;
    lights->l.a.l.col[2] = lights->l.a.l.colc[2] = ambientB;
    lights->numLights = numLights;

    for (i = 0; i < numLights; i++) {
        lights->l.l[i].l.col[0] = lights->l.l[i].l.colc[0] = r;
        lights->l.l[i].l.col[1] = lights->l.l[i].l.colc[1] = g;
        lights->l.l[i].l.col[2] = lights->l.l[i].l.colc[2] = b;
        lights->l.l[i].l.dir[0] = x;
        lights->l.l[i].l.dir[1] = y;
        lights->l.l[i].l.dir[2] = z;
    }

    Lights_Draw(lights, gfxCtx);

    return lights;
}

Lights* Lights_New(GraphicsContext* gfxCtx, u8 ambientR, u8 ambientG, u8 ambientB) {
    Lights* lights;

    lights = Graph_Alloc(gfxCtx, sizeof(Lights));

    lights->l.a.l.col[0] = lights->l.a.l.colc[0] = ambientR;
    lights->l.a.l.col[1] = lights->l.a.l.colc[1] = ambientG;
    lights->l.a.l.col[2] = lights->l.a.l.colc[2] = ambientB;
    lights->numLights = 0;

    return lights;
}

void Lights_GlowCheckPrepare(PlayState* play) {
    LightNode* node;
    LightPoint* params;
    Vec3f pos;
    Vec3f multDest;
    f32 wDest;
    f32 wX;
    f32 wY;

    node = play->lightCtx.listHead;

    while (node != NULL) {
        params = &node->info->params.point;

        if (node->info->type == LIGHT_POINT_GLOW) {
            f32 x, y;
            u32 shrink;
            uint32_t height;

            pos.x = params->x;
            pos.y = params->y;
            pos.z = params->z;
            Actor_ProjectPos(play, &pos, &multDest, &wDest);
            wX = multDest.x * wDest;
            wY = multDest.y * wDest;

            x = wX * 160 + 160;
            y = wY * 120 + 120;
            shrink = ShrinkWindow_GetCurrentVal();

            if ((multDest.z > 1.0f) && y >= shrink && y <= SCREEN_HEIGHT - shrink) {
                OTRGetPixelDepthPrepare(x, y);
            }
        }
        node = node->next;
    }
}

void Lights_GlowCheck(PlayState* play) {
    LightNode* node;
    LightPoint* params;
    Vec3f pos;
    Vec3f multDest;
    f32 wDest;
    f32 wX;
    f32 wY;
    s32 wZ;
    s32 zBuf;

    node = play->lightCtx.listHead;

    while (node != NULL) {
        params = &node->info->params.point;

        if (node->info->type == LIGHT_POINT_GLOW) {
            f32 x, y;
            u32 shrink;
            uint32_t height;

            pos.x = params->x;
            pos.y = params->y;
            pos.z = params->z;
            Actor_ProjectPos(play, &pos, &multDest, &wDest);
            params->drawGlow = false;
            wX = multDest.x * wDest;
            wY = multDest.y * wDest;

            x = wX * 160 + 160;
            y = wY * 120 + 120;
            shrink = ShrinkWindow_GetCurrentVal();

            if ((multDest.z > 1.0f) && y >= shrink && y <= SCREEN_HEIGHT - shrink) {
                wZ = (s32)((multDest.z * wDest) * 16352.0f) + 16352;
                zBuf = OTRGetPixelDepth(x, y) * 4;

                if (wZ < (zBuf >> 3)) {
                    params->drawGlow = true;
                }
            }
        }
        node = node->next;
    }
}

void Lights_DrawGlow(PlayState* play) {
    s32 pad;
    LightNode* node;

    node = play->lightCtx.listHead;

    OPEN_DISPS(play->state.gfxCtx);

    POLY_XLU_DISP = func_800947AC(POLY_XLU_DISP++);
    gDPSetAlphaDither(POLY_XLU_DISP++, G_AD_NOISE);
    gDPSetColorDither(POLY_XLU_DISP++, G_CD_MAGICSQ);
    gSPDisplayList(POLY_XLU_DISP++, gGlowCircleTextureLoadDL);

    while (node != NULL) {
        LightInfo* info;
        LightPoint* params;
        f32 scale;
        s32 pad[4];

        info = node->info;
        params = &info->params.point;

        if ((info->type == LIGHT_POINT_GLOW) && (params->drawGlow)) {
            scale = SQ(params->radius) * 0.0000026f;

            FrameInterpolation_RecordOpenChild(node, 0);
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, params->color[0], params->color[1], params->color[2], 50);
            Matrix_Translate(params->x, params->y, params->z, MTXMODE_NEW);
            Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, gGlowCircleDL);
            FrameInterpolation_RecordCloseChild();
        }

        node = node->next;
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
