#include "ActorTiers.h"

#include "global.h"
#include "z64.h"

#include <cstdio>
#include <cstdint>

/* See ActorTiers.h for what this is and why the shape is what it is. */

extern "C" uint8_t gActorTiersOn = 0;
extern "C" float gActorTiersTickScale = 1.0f;

namespace {

float sNearRadiusSq = 0.0f;
float sMidRadiusSq = 0.0f;
int32_t sPeriod = 1;
bool sMitigateB = false;
bool sDrawCull = false;

/* Counts are accumulated over the frame being walked and published when the frame number moves,
 * so the perf line always reads a complete frame rather than however far through one it landed. */
uint32_t sAccNear = 0;
uint32_t sAccMid = 0;
uint32_t sAccFar = 0;
uint32_t sPubNear = 0;
uint32_t sPubMid = 0;
uint32_t sPubFar = 0;
uint32_t sAccFrame = 0xFFFFFFFFu;

char sDescription[128] = "tiers off";

void RollFrame(uint32_t frame) {
    if (frame == sAccFrame) {
        return;
    }
    sPubNear = sAccNear;
    sPubMid = sAccMid;
    sPubFar = sAccFar;
    sAccNear = sAccMid = sAccFar = 0;
    sAccFrame = frame;
}

/*
 * Stagger key: which phase of the period an actor updates on. Derived from its address, because
 * that needs no new Actor field and costs nothing, which matters at 1800 actors.
 *
 * It has to be *hashed*, not just shifted. A field of identical actors is allocated at a constant
 * stride (En_Kanban is 0x1EC bytes), so the raw address bits are an arithmetic progression: with a
 * plain `(addr >> 4) & 0xFF`, consecutive actors advance by 30 and only two of the four phases of a
 * period-4 stagger are ever used - half the actors updating on the same frame is exactly the spike
 * the stagger exists to avoid. The Knuth multiplicative constant decorrelates the stride from the
 * period; verify with tick_max_ms against tick_ms, which is what a bad stagger shows up in.
 */
inline uint32_t StaggerKey(const struct Actor* actor) {
    const uint32_t bits = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor) >> 4);
    return (bits * 2654435761u) >> 24;
}

/*
 * Exempt = always near tier, wherever it is.
 *
 * Deliberately *not* keyed on ACTOR_FLAG_UPDATE_CULLING_DISABLED, though that is the engine's own
 * "do not cull my update" opt-out. Vanilla honours it absolutely, and a surprising number of
 * ordinary props set it - En_Kanban, the marker actor for this very experiment, is one - so
 * treating it as an exemption would make the far tier a no-op on exactly the population it is meant
 * to cull. Overriding it at distance is the point of the lever. ACTOR_FLAG_TIER_EXEMPT is the
 * fork's own, opt-in, and stays empty unless something asks for it.
 */
inline bool IsExempt(const Actor* actor) {
    return actor->id == ACTOR_PLAYER || (actor->flags & ACTOR_FLAG_TIER_EXEMPT) != 0;
}

/* Squared XZ distance to Link. Squared on purpose: the vanilla loop body's own sqrtf is part of
 * what the far tier is trying not to pay for, so the classifier must not reintroduce one. */
inline float DistSqXZToPlayer(PlayState* play, const Actor* actor) {
    const Player* player = GET_PLAYER(play);
    const float dx = actor->world.pos.x - player->actor.world.pos.x;
    const float dz = actor->world.pos.z - player->actor.world.pos.z;
    return (dx * dx) + (dz * dz);
}

} // namespace

extern "C" int32_t ActorTiers_SkipThisFrame(PlayState* play, Actor* actor) {
    RollFrame(play->state.frames);

    if (IsExempt(actor)) {
        sAccNear++;
        gActorTiersTickScale = 1.0f;
        return 0;
    }

    const float distSq = DistSqXZToPlayer(play, actor);

    if (distSq <= sNearRadiusSq) {
        sAccNear++;
        gActorTiersTickScale = 1.0f;
        return 0;
    }

    if (distSq > sMidRadiusSq) {
        sAccFar++;
        return 1;
    }

    sAccMid++;
    if (((play->state.frames + StaggerKey(actor)) % static_cast<uint32_t>(sPeriod)) != 0) {
        return 1;
    }
    /* This actor's turn: the update it is about to run stands in for the whole period. */
    gActorTiersTickScale = sMitigateB ? static_cast<float>(sPeriod) : 1.0f;
    return 0;
}

extern "C" int32_t ActorTiers_SkipDraw(PlayState* play, Actor* actor) {
    if (!sDrawCull || IsExempt(actor)) {
        return 0;
    }
    return DistSqXZToPlayer(play, actor) > sMidRadiusSq ? 1 : 0;
}

extern "C" int32_t ActorTiers_Configure(float nearRadius, float midRadius, int32_t period, int32_t mitigateB,
                                        int32_t drawCull) {
    if (nearRadius < 0.0f || midRadius < nearRadius || period < 1 || period > 60) {
        return 1;
    }
    sNearRadiusSq = nearRadius * nearRadius;
    sMidRadiusSq = midRadius * midRadius;
    sPeriod = period;
    sMitigateB = mitigateB != 0;
    sDrawCull = drawCull != 0;
    gActorTiersTickScale = 1.0f;
    gActorTiersOn = 1;
    std::snprintf(sDescription, sizeof(sDescription), "tiers on near=%.0f mid=%.0f n=%d mitb=%d drawcull=%d",
                  nearRadius, midRadius, period, sMitigateB ? 1 : 0, sDrawCull ? 1 : 0);
    return 0;
}

extern "C" void ActorTiers_Disable(void) {
    gActorTiersOn = 0;
    gActorTiersTickScale = 1.0f;
    sMitigateB = false;
    sDrawCull = false;
    sAccNear = sAccMid = sAccFar = 0;
    sPubNear = sPubMid = sPubFar = 0;
    sAccFrame = 0xFFFFFFFFu;
    std::snprintf(sDescription, sizeof(sDescription), "tiers off");
}

extern "C" void ActorTiers_GetCounts(uint32_t* nearCount, uint32_t* midCount, uint32_t* farCount) {
    *nearCount = sPubNear;
    *midCount = sPubMid;
    *farCount = sPubFar;
}

extern "C" const char* ActorTiers_Describe(void) {
    return sDescription;
}
