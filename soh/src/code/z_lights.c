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

// Read once a tick, not once a bind: a CVar read and a registry lookup per drawn actor would be the
// costliest thing in the bind. Kept apart from the stats so a report change cannot change a bind.
static s32 sLightsCountThisTick;    // a grid-tool scene: count what wants a slot before binding
static s32 sLightsSunFirstThisTick; // and the setting is on: bind the sun first when it overflows
static s32 sLightsShadowSmoothThisTick; // a grid-tool scene and the setting on: smooth the feet shadows (#250)
static s32 sLightsShadowPulseThisTick;  // and the pulse setting on: the smoothed shadows breathe (#250)
static s32 sLightsNullFastThisTick;     // a no-position bind takes the directional list (#253)

// The no-position bind's directional list (LightsBudget.h, #253): the list's directional nodes, in list order.
// Rebuilt when the list changes (sLightsListGen, bumped by every insert and remove), when a bind passes another
// head, or when a new tick starts; a node no longer directional when used rebuilds it too. -1 when the list
// holds more than fit, and the bind walks instead.
#define LIGHTS_DIR_LIST_MAX 16
static LightNode* sLightsDirNodes[LIGHTS_DIR_LIST_MAX];
static s32 sLightsDirCount;
static s32 sLightsDirValid;
static u32 sLightsListGen;
static u32 sLightsDirGen;
static u32 sLightsDirFrame;
static LightNode* sLightsDirHead;

// Lights_FindSlot's cap: the slots in a Lights group (`Lights.l.l`).
#define LIGHTS_BIND_SLOTS 7

// `lights shadow`'s ring (LightsBudget.h, #250): Link's last LIGHTS_SHADOW_RECORD_TICKS feet shadows.
static LightsShadowTick sShadowRecord[LIGHTS_SHADOW_RECORD_TICKS];
static s32 sShadowRecordHead;  // the slot the next tick takes
static s32 sShadowRecordCount; // ticks held, up to the ring's size
static LightsShadowTick* sShadowRecordOpen; // this tick's, between Link's bind and his shadow

// Link's bind opens the tick's record. A second bind of Link in one tick (none is known) overwrites it.
static void Lights_OpenShadowRecord(void) {
    u32 frame = gPlayState->state.frames;
    LightsShadowTick* rec;
    s32 last = (sShadowRecordHead + LIGHTS_SHADOW_RECORD_TICKS - 1) % LIGHTS_SHADOW_RECORD_TICKS;

    if (sShadowRecordCount > 0 && sShadowRecord[last].frame == frame) {
        rec = &sShadowRecord[last];
    } else {
        rec = &sShadowRecord[sShadowRecordHead];
        sShadowRecordHead = (sShadowRecordHead + 1) % LIGHTS_SHADOW_RECORD_TICKS;
        sShadowRecordCount = MIN(sShadowRecordCount + 1, LIGHTS_SHADOW_RECORD_TICKS);
    }
    memset(rec, 0, sizeof(*rec));
    rec->frame = frame;
    sShadowRecordOpen = rec;
}

// The last bind's node per slot, so the feet shadow can tell its lights apart from tick to tick (#250).
// Lights_ShadowCasts trusts it only for the Lights group it was taken from. Every bind records it since
// #254, vanilla scenes' too, for pairing each foot shadow with its own light's (Lights_BoundSlotKey).
static Lights* sBindLights;
static LightNode* sBindNode[LIGHTS_BIND_SLOTS];

static void Lights_OpenBindNodes(Lights* lights) {
    s32 i;

    sBindLights = lights;
    for (i = 0; i < LIGHTS_BIND_SLOTS; i++) {
        sBindNode[i] = NULL;
    }
}

// The slot a bind just filled, if it filled one: `before` is numLights before the bind. `isLink` also
// writes it into the tick's shadow record.
static void Lights_NoteShadowSlot(Lights* lights, s32 before, LightNode* node, s32 isLink) {
    if (lights->numLights <= before || before >= LIGHTS_BIND_SLOTS) {
        return;
    }
    if (lights == sBindLights) {
        sBindNode[before] = node;
    }
    if (!isLink || sShadowRecordOpen == NULL) {
        return;
    }
    sShadowRecordOpen->node[before] =
        (node->info->type == LIGHT_DIRECTIONAL) ? LIGHTS_SHADOW_NODE_DIR : (s16)(node - sLightsBuffer.buf);
    sShadowRecordOpen->numLights = lights->numLights;
}

void Lights_RecordLinkShadow(s32 foot, s32 slot, s32 lightNum, u8 alpha) {
    if (sShadowRecordOpen == NULL || foot < 0 || foot > 1 || slot < 0 || slot >= LIGHTS_SHADOW_CASTS_MAX) {
        return;
    }
    sShadowRecordOpen->alpha[foot][slot] = alpha;
    if (foot == 0) {
        sShadowRecordOpen->lightNum[slot] = lightNum;
    }
}

// #region SOH [sturdy-bassoon#254]
s32 Lights_BoundSlotKey(Lights* lights, s32 slot, const void** key, s32* node) {
    if (lights != sBindLights || slot < 0 || slot >= LIGHTS_BIND_SLOTS || sBindNode[slot] == NULL) {
        return 0;
    }
    *key = sBindNode[slot]->info;
    *node = (s32)(sBindNode[slot] - sLightsBuffer.buf);
    return 1;
}
// #endregion

s32 Lights_GetShadowRecord(LightsShadowTick* out, s32 max) {
    s32 n = MIN(max, sShadowRecordCount);
    s32 start = (sShadowRecordHead + LIGHTS_SHADOW_RECORD_TICKS - n) % LIGHTS_SHADOW_RECORD_TICKS;
    s32 i;

    for (i = 0; i < n; i++) {
        out[i] = sShadowRecord[(start + i) % LIGHTS_SHADOW_RECORD_TICKS];
    }
    return n;
}

void Lights_ClearShadowRecord(void) {
    sShadowRecordHead = 0;
    sShadowRecordCount = 0;
    sShadowRecordOpen = NULL;
}

// The feet shadows' smoothing (LightsBudget.h, #250). One slot per actor drawn with smoothed shadows,
// each holding up to LIGHTS_SHADOW_LIGHTS lights. A slot is free once its actor has missed a tick (died,
// culled, or a scene ended), so a dead actor's slot is free a tick later; an actor that takes it, or any
// actor that reappears after a missed tick, starts fresh and snaps. An actor is known by its address, id
// and params: not its home, which the player rewrites as Link moves (z_player.c). So the one way to
// inherit is an actor dying and another of the same id and params taking its address in the same tick,
// within LIGHTS_SHADOW_SNAP_DIST of it: that one would fade on from the dead one's values.
typedef struct {
    LightNode* node;  // NULL: free
    LightInfo* info;  // the node's holder's info: a node freed and re-taken by another light is another light
    f32 strength;     // the smoothed lightNum
    f32 brightness;   // the light's own colour sum, averaged over seconds: its flicker taken out
    u16 samples;      // ticks averaged into it, to LIGHTS_SHADOW_BRIGHTNESS_TICKS
    s8 dir[3];        // the last direction it was bound at with the light above the feet
    s8 bound;         // bound this tick
} LightsShadowLight;

// The brightness average: a plain mean over the first LIGHTS_SHADOW_BRIGHTNESS_TICKS ticks a light is bound,
// then a moving average over about as long. A fire's re-roll (0.5-1.0 of full, every tick) averages to a
// steady value within a second or two, and drifts by about 1% after that.
#define LIGHTS_SHADOW_BRIGHTNESS_TICKS 100

typedef struct {
    Actor* actor; // NULL: free
    s16 id;
    s16 params;
    u32 frame; // the tick it was last advanced
    Vec3f pos; // the actor's position then
    LightsShadowLight lights[LIGHTS_SHADOW_LIGHTS];
    f32 sun[2]; // the sun slots' smoothed strength, after the cut-off
} LightsShadowActor;

static LightsShadowActor sShadowActors[LIGHTS_SHADOW_ACTORS];
static f32 sShadowRate = LIGHTS_SHADOW_RATE_DEFAULT;
static f32 sShadowPulseAmount = LIGHTS_SHADOW_PULSE_AMOUNT_DEFAULT;
// Lights_SetShadowMeanColor's means, by pool node, with the info each was set for.
static f32 sShadowNodeMean[LIGHTS_BUFFER_SIZE];
static LightInfo* sShadowNodeMeanInfo[LIGHTS_BUFFER_SIZE];
static f32 sShadowPulseSeconds = LIGHTS_SHADOW_PULSE_SECONDS_DEFAULT;
static LightsShadowStats sShadowStats;

// A fading light whose strength falls below this draws under an alpha of 1 (0.00005 x 255 x 78 < 1): drop it.
#define LIGHTS_SHADOW_DROP_BELOW 78.0f
// ActorShadow_DrawFoot's clamp: a strength of 1 / 0.00005 or more draws at the full alpha.
#define LIGHTS_SHADOW_FULL 20000.0f

static void Lights_ResetShadowTable(void) {
    memset(sShadowActors, 0, sizeof(sShadowActors));
    sShadowStats.snaps = sShadowStats.full = sShadowStats.nobind = sShadowStats.cut = 0;
}

// Advanced this tick or the last. Anything older, or from a later frame (a frames restart, a savestate
// loaded from earlier), is free.
static s32 Lights_ShadowSlotLive(LightsShadowActor* e, u32 frame) {
    return e->actor != NULL && (e->frame == frame || e->frame + 1 == frame);
}

void Lights_GetShadowStats(LightsShadowStats* out) {
    u32 frame = (gPlayState != NULL) ? gPlayState->state.frames : 0;
    s32 i;

    *out = sShadowStats;
    out->gate = sLightsShadowSmoothThisTick;
    out->rate = sShadowRate;
    out->pulseGate = sLightsShadowPulseThisTick;
    out->pulseAmount = sShadowPulseAmount;
    out->pulseSeconds = sShadowPulseSeconds;
    out->actors = 0;
    for (i = 0; i < LIGHTS_SHADOW_ACTORS; i++) {
        out->actors += Lights_ShadowSlotLive(&sShadowActors[i], frame);
    }
}

void Lights_SetShadowMeanColor(LightNode* node, LightInfo* info, f32 colorSum) {
    s32 index;

    if (node == NULL) {
        return;
    }
    index = node - sLightsBuffer.buf;
    if (index >= 0 && index < LIGHTS_BUFFER_SIZE) {
        sShadowNodeMean[index] = colorSum;
        sShadowNodeMeanInfo[index] = info;
    }
}

void Lights_SetShadowPulse(f32 amount, f32 seconds) {
    sShadowPulseAmount = CLAMP(amount, 0.0f, 0.9f);
    sShadowPulseSeconds = CLAMP(seconds, 0.5f, 30.0f);
}

// The pulse's factor for one light this tick: from 1 (the shadow at its full strength) down to 1 - amount and
// back, on a sine. Lightening only, and applied after the draw's clamp (LIGHTS_SHADOW_FULL), so a shadow already
// at full dark breathes too: a pulse round the clamped value would be clamped flat. The phase runs with
// state.frames (20 a second) and is offset by the light's pool node times the golden angle, so neighbouring
// nodes spread evenly round the cycle and a light keeps its place in it whichever slot it binds to.
static f32 Lights_ShadowPulse(LightNode* node, u32 frame) {
    f32 cycle = (f32)frame / (20.0f * sShadowPulseSeconds);
    f32 phase = 2.0f * M_PI * (cycle - floorf(cycle)) + 2.3999632f * (f32)(node - sLightsBuffer.buf);

    return 1.0f - sShadowPulseAmount * (0.5f + 0.5f * sinf(phase));
}

f32 Lights_SetShadowRate(f32 rate) {
    sShadowRate = CLAMP(rate, 0.01f, 1.0f);
    return sShadowRate;
}

// The actor's slot, claiming a free one if it has none. `*snap` is set when it must snap.
static LightsShadowActor* Lights_ShadowActorSlot(Actor* actor, u32 frame, s32* snap) {
    LightsShadowActor* free = NULL;
    LightsShadowActor* e;
    s32 i;

    *snap = 0;
    for (i = 0; i < LIGHTS_SHADOW_ACTORS; i++) {
        e = &sShadowActors[i];
        if (Lights_ShadowSlotLive(e, frame)) {
            if (e->actor == actor && e->id == actor->id && e->params == actor->params) {
                if (e->frame + 1 == frame &&
                    Math_Vec3f_DistXYZ(&e->pos, &actor->world.pos) > LIGHTS_SHADOW_SNAP_DIST) {
                    *snap = 1;
                }
                return e;
            }
        } else if (free == NULL) {
            free = e;
        }
    }
    if (free == NULL) {
        return NULL;
    }
    memset(free, 0, sizeof(*free));
    free->actor = actor;
    free->id = actor->id;
    free->params = actor->params;
    free->frame = frame - 1; // so the caller advances it this tick
    *snap = 1;
    return free;
}

// A light's strength term, as ActorShadow_DrawFeet reads it: its colour sum times its height above the feet.
static s32 Lights_ShadowStrength(Light* light) {
    if (light->l.dir[1] <= 0) {
        return 0;
    }
    return (light->l.col[0] + light->l.col[1] + light->l.col[2]) * ABS(light->l.dir[1]);
}

// The actor's entry for bound slot j's light, or NULL.
static LightsShadowLight* Lights_FindShadowLight(LightsShadowActor* e, s32 j) {
    s32 i;

    for (i = 0; i < LIGHTS_SHADOW_LIGHTS; i++) {
        if (e->lights[i].node == sBindNode[j] && e->lights[i].info == sBindNode[j]->info) {
            return &e->lights[i];
        }
    }
    return NULL;
}

// Moves the actor's lights one tick: the bound ones toward their strength now, the rest toward 0.
static void Lights_AdvanceShadowLights(LightsShadowActor* e, Lights* lights, s32 numPoints, s32 snap) {
    LightsShadowLight* found[LIGHTS_BIND_SLOTS];
    LightsShadowLight* sl;
    LightsShadowLight* weakest;
    Light* light;
    LightParams* params;
    f32 now;
    s32 index;
    s32 target;
    s32 i;
    s32 j;

    // Every light still bound is marked first, so a joining light below can never take one of them.
    for (i = 0; i < LIGHTS_SHADOW_LIGHTS; i++) {
        e->lights[i].bound = 0;
    }
    for (j = 0; j < numPoints; j++) {
        found[j] = Lights_FindShadowLight(e, j);
        if (found[j] != NULL) {
            found[j]->bound = 1;
        }
    }
    for (j = 0; j < numPoints; j++) {
        light = &lights->l.l[j];
        params = &sBindNode[j]->info->params;
        // This tick's flicker, divided out: the bound colour is the light's colour now times the distance
        // falloff, so the strength at the averaged brightness is the bound one scaled by average / now.
        now = (f32)(params->point.color[0] + params->point.color[1] + params->point.color[2]);
        sl = found[j];
        if (sl == NULL) {
            // A joining light takes a free entry, or else the weakest fading one, which is cut short. At
            // most 5 lights are bound beside the sun's 2 slots, so a fading entry is always there to take.
            weakest = NULL;
            for (i = 0; i < LIGHTS_SHADOW_LIGHTS; i++) {
                if (e->lights[i].node == NULL) {
                    weakest = &e->lights[i];
                    break;
                }
                if (!e->lights[i].bound && (weakest == NULL || e->lights[i].strength < weakest->strength)) {
                    weakest = &e->lights[i];
                }
            }
            if (weakest->node != NULL) {
                sShadowStats.cut++;
            }
            sl = weakest;
            sl->node = sBindNode[j];
            sl->info = sBindNode[j]->info;
            // A known mean starts the average settled; otherwise it starts from this tick's roll.
            index = sBindNode[j] - sLightsBuffer.buf;
            if (sShadowNodeMeanInfo[index] == sBindNode[j]->info && sShadowNodeMean[index] > 0.0f) {
                sl->brightness = sShadowNodeMean[index];
                sl->samples = LIGHTS_SHADOW_BRIGHTNESS_TICKS;
            } else {
                sl->brightness = now;
                sl->samples = 1;
            }
        } else {
            sl->samples = MIN(sl->samples + 1, LIGHTS_SHADOW_BRIGHTNESS_TICKS);
            sl->brightness += (now - sl->brightness) / sl->samples;
        }
        target = (now > 0.0f) ? Lights_ShadowStrength(light) * (sl->brightness / now) : 0;
        if (found[j] == NULL) {
            sl->strength = snap ? target : 0.0f;
        } else {
            sl->strength = snap ? target : sl->strength + sShadowRate * (target - sl->strength);
        }
        if (light->l.dir[1] > 0) {
            sl->dir[0] = light->l.dir[0];
            sl->dir[1] = light->l.dir[1];
            sl->dir[2] = light->l.dir[2];
        }
        sl->bound = 1;
    }
    for (i = 0; i < LIGHTS_SHADOW_LIGHTS; i++) {
        sl = &e->lights[i];
        if (sl->node == NULL || sl->bound) {
            continue;
        }
        sl->strength = snap ? 0.0f : sl->strength * (1.0f - sShadowRate);
        if (sl->strength < LIGHTS_SHADOW_DROP_BELOW) {
            sl->node = NULL;
        }
    }
}

s32 Lights_ShadowCasts(Actor* actor, Lights* lights, LightsShadowCast* casts) {
    LightsShadowActor* e;
    LightsShadowLight* sl;
    LightsShadowTick* rec = NULL;
    Light* sun;
    u32 frame;
    s32 numPoints = lights->numLights - 2;
    s32 snap;
    s32 n = 0;
    s32 sum = 0;
    s32 i;

    if (!sLightsShadowSmoothThisTick || gPlayState == NULL) {
        return -1;
    }
    // The bind must be this group's, every point slot's node known, and the sun's two slots after them.
    if (lights != sBindLights || numPoints < 0) {
        sShadowStats.nobind++;
        return -1;
    }
    for (i = 0; i < numPoints; i++) {
        if (sBindNode[i] == NULL) {
            sShadowStats.nobind++;
            return -1;
        }
    }
    frame = gPlayState->state.frames;
    e = Lights_ShadowActorSlot(actor, frame, &snap);
    if (e == NULL) {
        sShadowStats.full++;
        return -1;
    }
    // Once a tick: a second draw of the actor in the same tick reads the same values.
    if (e->frame != frame || snap) {
        sShadowStats.snaps += snap;
        Lights_AdvanceShadowLights(e, lights, numPoints, snap);
        // The sun: vanilla's cut-off by 8x the point lights' strength, from the smoothed sum, and then
        // smoothed itself. The x8 turns a point light's steady change as the actor walks into a swing of
        // the sun shadow several times as fast; smoothing the result fades that too. Held to the range
        // the draw can show (0 to the clamp at 1), so a saturated sun does not lag on its way down.
        for (i = 0; i < LIGHTS_SHADOW_LIGHTS; i++) {
            if (e->lights[i].node != NULL && e->lights[i].dir[1] > 0) {
                sum += (s32)e->lights[i].strength;
            }
        }
        for (i = 0; i < 2; i++) {
            f32 target = CLAMP(Lights_ShadowStrength(&lights->l.l[numPoints + i]) - sum * 8, 0, LIGHTS_SHADOW_FULL);

            e->sun[i] = snap ? target : e->sun[i] + sShadowRate * (target - e->sun[i]);
        }
        e->frame = frame;
        e->pos = actor->world.pos;
    }

    if (GET_PLAYER(gPlayState) != NULL && actor == &GET_PLAYER(gPlayState)->actor) {
        rec = sShadowRecordOpen;
    }
    for (i = 0; i < LIGHTS_SHADOW_LIGHTS; i++) {
        sl = &e->lights[i];
        if (sl->node == NULL) {
            continue;
        }
        memset(&casts[n], 0, sizeof(casts[n]));
        casts[n].light.l.dir[0] = sl->dir[0];
        casts[n].light.l.dir[1] = sl->dir[1];
        casts[n].light.l.dir[2] = sl->dir[2];
        casts[n].lightNum = (sl->dir[1] > 0) ? (s32)sl->strength : 0;
        casts[n].key = sl->info; // #254
        casts[n].keyNode = (s32)(sl->node - sLightsBuffer.buf);
        if (sLightsShadowPulseThisTick) {
            casts[n].lightNum =
                (s32)(MIN(casts[n].lightNum, LIGHTS_SHADOW_FULL) * Lights_ShadowPulse(sl->node, frame));
        }
        if (rec != NULL) {
            rec->node[n] = (s16)(sl->node - sLightsBuffer.buf);
        }
        n++;
    }
    // The sun's two slots, on their bound direction at the smoothed strength.
    for (i = 0; i < 2; i++) {
        sun = &lights->l.l[numPoints + i];
        casts[n].light = *sun;
        casts[n].lightNum = (sun->l.dir[1] > 0) ? (s32)e->sun[i] : 0;
        casts[n].key = NULL; // #254: the sun's two are paired by their place, not a light
        casts[n].keyNode = 0;
        if (rec != NULL && sBindNode[numPoints + i] != NULL) {
            // Vanilla's layout puts the sun last; only a sun-first-off overflow puts a point light here.
            rec->node[n] = (sBindNode[numPoints + i]->info->type == LIGHT_DIRECTIONAL)
                               ? LIGHTS_SHADOW_NODE_DIR
                               : (s16)(sBindNode[numPoints + i] - sLightsBuffer.buf);
        }
        n++;
    }
    if (rec != NULL) {
        rec->numLights = n;
        rec->smoothed = 1;
    }
    return n;
}

static void Lights_RollTick(void) {
    u32 frame;
    LightsStats fresh = { 0 };

    if (gPlayState == NULL) {
        return;
    }
    // state.frames, not gameplayFrames: the latter stands still while the game is paused or held by
    // IREG(72), and a frozen key would freeze the report with it.
    frame = gPlayState->state.frames;
    if (sLightsTickStarted && frame == sLightsTickFrame) {
        return;
    }
    if (sLightsTickStarted) {
        sLightsLastTick = sLightsTick;
    }
    fresh.tick = frame;
    fresh.linkInRange = fresh.linkPoints = fresh.linkDirs = -1;
    sLightsCountThisTick = GridToolSceneRegistry_IsCustomScene(gPlayState->sceneNum);
    sLightsSunFirstThisTick = sLightsCountThisTick && CVarGetInteger(CVAR_LIGHTS_SUN_FIRST, LIGHTS_SUN_FIRST_DEFAULT);
    sLightsShadowSmoothThisTick =
        sLightsCountThisTick && CVarGetInteger(CVAR_LIGHTS_SHADOW_SMOOTH, LIGHTS_SHADOW_SMOOTH_DEFAULT);
    sLightsShadowPulseThisTick =
        sLightsShadowSmoothThisTick && CVarGetInteger(CVAR_LIGHTS_SHADOW_PULSE, LIGHTS_SHADOW_PULSE_DEFAULT);
    sLightsNullFastThisTick = CVarGetInteger(CVAR_LIGHTS_NULL_BIND_FAST, LIGHTS_NULL_BIND_FAST_DEFAULT) != 0;
    fresh.sunFirstGate = sLightsSunFirstThisTick;
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
    if (lights->numLights >= LIGHTS_BIND_SLOTS) {
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

// Lights_BindPoint's range test, term for term, so "in range" here is exactly what vanilla binds (#233).
static s32 Lights_PointInRange(LightParams* params, Vec3f* vec, f32* distSq) {
    f32 xDiff = params->point.x - vec->x;
    f32 yDiff = params->point.y - vec->y;
    f32 zDiff = params->point.z - vec->z;
    f32 scale = params->point.radius;

    *distSq = SQ(xDiff) + SQ(yDiff) + SQ(zDiff);
    return *distSq < SQ(scale);
}

// Link's own bind under vanilla's rule, for `lights status`: each light in list order takes a slot while
// one is free, so this is what vanilla's loop bound, not what it would ideally bind. Returns the nodes visited.
static s32 Lights_CountVanillaBind(LightNode* node, Vec3f* vec, s32 slotsFree) {
    s32 visited = 0;
    f32 distSq;

    sLightsTick.linkPoints = 0;
    sLightsTick.linkDirs = 0;
    for (; node != NULL && slotsFree > 0; node = node->next, visited++) {
        if (node->info->type == LIGHT_DIRECTIONAL) {
            sLightsTick.linkDirs++;
            slotsFree--;
        } else if (Lights_PointInRange(&node->info->params, vec, &distSq)) {
            sLightsTick.linkPoints++;
            slotsFree--;
        }
    }
    return visited;
}

// The directional list for `head`, rebuilt if stale (#253). False when the list holds more directional
// lights than it keeps: the caller walks.
static s32 Lights_RefreshDirNodes(LightNode* head) {
    LightNode* node;
    s32 i;

    if (sLightsDirValid && sLightsDirGen == sLightsListGen && sLightsDirHead == head &&
        sLightsDirFrame == sLightsTickFrame) {
        if (sLightsDirCount < 0) {
            return 0;
        }
        for (i = 0; i < sLightsDirCount; i++) {
            if (sLightsDirNodes[i]->info == NULL || sLightsDirNodes[i]->info->type != LIGHT_DIRECTIONAL) {
                break;
            }
        }
        if (i == sLightsDirCount) {
            return sLightsDirCount >= 0;
        }
    }
    sLightsDirCount = 0;
    for (node = head; node != NULL; node = node->next) {
        if (node->info->type == LIGHT_DIRECTIONAL) {
            if (sLightsDirCount >= LIGHTS_DIR_LIST_MAX) {
                sLightsDirCount = -1;
                break;
            }
            sLightsDirNodes[sLightsDirCount++] = node;
        }
        sLightsTick.walked++;
    }
    sLightsDirValid = 1;
    sLightsDirGen = sLightsListGen;
    sLightsDirHead = head;
    sLightsDirFrame = sLightsTickFrame;
    return sLightsDirCount >= 0;
}

// `lights nullbind verify` (#253): beside every fast bind, the walk it replaced binds a copy of the group as it
// came in, and any difference in what bound - the count, or a slot's colour or direction - is counted. `plant`
// drops the fast bind's last light first, so the check is seen to catch one. Session-only.
static s32 sLightsNullVerify = LIGHTS_NULL_VERIFY_OFF;

void Lights_SetNullBindVerify(s32 mode) {
    sLightsNullVerify = mode;
}

s32 Lights_GetNullBindVerify(void) {
    return sLightsNullVerify;
}

static void Lights_VerifyNullBind(Lights* fast, Lights* ref, LightNode* listHead) {
    LightsBindFunc bindFuncs[] = { Lights_BindPoint, Lights_BindDirectional, Lights_BindPoint };
    LightNode* node;
    s32 i;
    s32 same;

    if (sLightsNullVerify == LIGHTS_NULL_VERIFY_PLANT && fast->numLights > 0) {
        fast->numLights--;
    }
    for (node = listHead; node != NULL; node = node->next) {
        bindFuncs[node->info->type](ref, &node->info->params, NULL);
    }
    same = ref->numLights == fast->numLights;
    for (i = 0; same && i < fast->numLights; i++) {
        same = memcmp(fast->l.l[i].l.col, ref->l.l[i].l.col, 3) == 0 &&
               memcmp(fast->l.l[i].l.colc, ref->l.l[i].l.colc, 3) == 0 &&
               memcmp(fast->l.l[i].l.dir, ref->l.l[i].l.dir, 3) == 0;
    }
    sLightsTick.nullVerified++;
    sLightsTick.nullMismatch += same ? 0 : 1;
}

/**
 * For every light in a provided list, try to find a free slot in the provided Lights group and bind
 * a light to it. Then apply color and positional/directional info for each light
 * based on the parameters supplied by the node.
 *
 * Note: Lights in a given list can only be binded to however many free slots are
 * available in the Lights group. This is at most 7 slots for a new group, but could be less.
 *
 * #233: in grid-tool scenes the list is walked once more first, to count what wants a slot. When it
 * all fits, or sun first is off, vanilla's loop binds as it always has. When it does not fit and sun
 * first is on, the directional lights' slots are reserved, the NEAREST point lights take the rest, and
 * the layout stays vanilla's: point lights first, in list order, then the directional lights.
 * ActorShadow_DrawFeet reads the last two slots as the sun, so the sun must stay last. Vanilla scenes
 * run vanilla's loop and nothing else but two counters.
 */
void Lights_BindAll(Lights* lights, LightNode* listHead, Vec3f* vec) {
    LightsBindFunc bindFuncs[] = { Lights_BindPoint, Lights_BindDirectional, Lights_BindPoint };
    LightInfo* info;
    LightNode* listStart = listHead;
    LightNode* node;
    // The nearest point lights in range, sorted by distance, with their list positions.
    LightNode* nearest[LIGHTS_BIND_SLOTS];
    f32 nearDistSq[LIGHTS_BIND_SLOTS];
    s32 nearIndex[LIGHTS_BIND_SLOTS];
    s32 numNear = 0;
    s32 numInRange = 0;
    s32 numDirs = 0;
    s32 index = 0;
    // Callers pass a fresh group, so this is all 7, but the bind has never assumed it.
    s32 slotsFree = MAX(LIGHTS_BIND_SLOTS - lights->numLights, 0);
    s32 slotsForPoints;
    s32 overflow;
    s32 isLink;
    s32 before;
    f32 distSq;
    s32 i;
    s32 j;

    Lights_RollTick();
    sLightsTick.binds++;

    isLink = (vec != NULL) && (gPlayState != NULL) && (GET_PLAYER(gPlayState) != NULL) &&
             (vec == &GET_PLAYER(gPlayState)->actor.world.pos);
    if (isLink) {
        Lights_OpenShadowRecord();
    }

    // #254: which node fills each slot, for every bind (it was only for the smoothing, #250).
    Lights_OpenBindNodes(lights);

    // #253: no position, so only the directional lights can bind, in list order, as either walk below would bind
    // them. The count's overflow is the one figure it would have changed, so it is kept.
    if (vec == NULL) {
        sLightsTick.nullBinds++;
        if (sLightsNullFastThisTick && Lights_RefreshDirNodes(listStart)) {
            Lights ref;

            if (sLightsNullVerify != LIGHTS_NULL_VERIFY_OFF) {
                ref = *lights;
            }
            for (i = 0; i < sLightsDirCount; i++) {
                before = lights->numLights;
                Lights_BindDirectional(lights, &sLightsDirNodes[i]->info->params, vec);
                Lights_NoteShadowSlot(lights, before, sLightsDirNodes[i], 0);
            }
            if (sLightsNullVerify != LIGHTS_NULL_VERIFY_OFF) {
                Lights_VerifyNullBind(lights, &ref, listStart);
            }
            sLightsTick.walked += sLightsDirCount;
            sLightsTick.nullFast++;
            if (sLightsCountThisTick) {
                sLightsTick.overflowed += sLightsDirCount > slotsFree;
            }
            return;
        }
    }

    if (!sLightsCountThisTick) {
        // A vanilla scene: vanilla's loop.
        while (listHead != NULL) {
            info = listHead->info;
            before = lights->numLights;
            bindFuncs[info->type](lights, &info->params, vec);
            Lights_NoteShadowSlot(lights, before, listHead, isLink); // #254: every actor's slots, not only Link's
            listHead = listHead->next;
            sLightsTick.walked++;
        }
        return;
    }

    for (node = listStart; node != NULL; node = node->next, index++) {
        info = node->info;
        if (info->type == LIGHT_DIRECTIONAL) {
            numDirs++;
        } else if (vec != NULL && Lights_PointInRange(&info->params, vec, &distSq)) {
            numInRange++;
            // Insert into the sorted nearest. A tie keeps the earlier (newer) light, as vanilla would.
            for (i = numNear; i > 0 && nearDistSq[i - 1] > distSq; i--) {
                if (i < LIGHTS_BIND_SLOTS) {
                    nearest[i] = nearest[i - 1];
                    nearDistSq[i] = nearDistSq[i - 1];
                    nearIndex[i] = nearIndex[i - 1];
                }
            }
            if (i < LIGHTS_BIND_SLOTS) {
                nearest[i] = node;
                nearDistSq[i] = distSq;
                nearIndex[i] = index;
                numNear = MIN(numNear + 1, LIGHTS_BIND_SLOTS);
            }
        }
    }
    sLightsTick.walked += index;

    overflow = (numInRange + numDirs) > slotsFree;
    sLightsTick.overflowed += overflow;
    sLightsTick.maxInRange = MAX(sLightsTick.maxInRange, numInRange);

    if (!overflow || !sLightsSunFirstThisTick) {
        while (listHead != NULL) {
            info = listHead->info;
            before = lights->numLights;
            bindFuncs[info->type](lights, &info->params, vec);
            Lights_NoteShadowSlot(lights, before, listHead, isLink);
            listHead = listHead->next;
            sLightsTick.walked++;
        }
        if (isLink) {
            sLightsTick.linkInRange = numInRange;
            sLightsTick.walked += Lights_CountVanillaBind(listStart, vec, slotsFree);
        }
        return;
    }

    // Sun first.
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
        before = lights->numLights;
        Lights_BindPoint(lights, &nearest[i]->info->params, vec);
        Lights_NoteShadowSlot(lights, before, nearest[i], isLink);
    }
    for (node = listStart; node != NULL; node = node->next) {
        if (node->info->type == LIGHT_DIRECTIONAL) {
            before = lights->numLights;
            Lights_BindDirectional(lights, &node->info->params, vec);
            Lights_NoteShadowSlot(lights, before, node, isLink);
        }
        sLightsTick.walked++;
    }
    if (isLink) {
        sLightsTick.linkInRange = numInRange;
        sLightsTick.linkPoints = numNear;
        sLightsTick.linkDirs = MIN(numDirs, slotsFree);
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
    sLightsDirValid = 0; // #253: every node is gone
    sLightsPeak = 0;
    sLightsRefused = 0;
    // A new scene restarts state.frames, so its first tick could share the last one's number.
    sLightsTickStarted = 0;
    // And every actor arrives fresh: their shadows snap, and the nodes the table knew are gone (#250).
    Lights_ResetShadowTable();
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
        sLightsListGen++; // #253: the directional list is stale
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
        sLightsListGen++; // #253: the directional list is stale
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
