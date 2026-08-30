#ifndef SOH_ENHANCEMENTS_ACTOR_TIERS_H
#define SOH_ENHANCEMENTS_ACTOR_TIERS_H

/*
 * Distance-tiered actor updating - the Exp 2 prototype for sturdy-bassoon#6 (Option B / lever 4).
 *
 * Three tiers, by XZ distance from Link:
 *   near  full update every frame, exactly the vanilla path
 *   mid   *update throttling*: update every Nth frame, staggered per actor so the skipped work
 *         spreads evenly across frames instead of every actor landing on the same one
 *   far   *update culling*: the whole per-actor body is skipped
 *
 * What "skipped" buys is more than the actor's own update(). Vanilla OoT already gates
 * `actor->update()` on ACTOR_FLAG_INSIDE_CULLING_VOLUME (set in Actor_DrawAll from the culling
 * volume test), so a distant actor's update function is *already* not called. What is still paid
 * for every resident actor, every frame, is the fixed part of the Actor_UpdateAll loop body: the
 * prevPos copy, the distance (sqrtf) and yaw (atan2s) to Link, the freeze timer, and
 * CollisionCheck_ResetDamage. That fixed cost is what this skips, and it is the cost that scales
 * with residency rather than with visibility.
 *
 * OoT has no delta time, so a throttled actor naively runs at 1/N speed. Mitigation (b) from #6
 * scales the two *shared* systems - SkelAnime playback and velocity integration - by the number of
 * ticks the update stands in for, so reused vanilla actors still walk and animate at the right
 * wall-clock rate at distance. Bespoke `this->timer--` logic still drifts; that is what the
 * exemption flag is for.
 *
 * Everything here is inert until `agenttest tiers` arms it. `gActorTiersOn` is the only thing the
 * engine's hot paths test when it is off.
 */

#include <stdint.h>

struct Actor;
struct PlayState;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Per-actor opt-out for anything time-sensitive: set this in Actor.flags and the actor is treated
 * as near tier wherever it stands. Bit 31 is free - the engine's own flags stop at bit 28.
 *
 * ACTOR_FLAG_UPDATE_CULLING_DISABLED is deliberately *not* treated as an exemption. It is the
 * engine's own "never cull my update" opt-out and vanilla honours it absolutely, but plenty of
 * ordinary props set it (En_Kanban, a signpost, does) - so honouring it here would make the far
 * tier a no-op on the population it is meant to cull. Overriding it at distance is the lever.
 */
#define ACTOR_FLAG_TIER_EXEMPT (1u << 31)

/* The hot-path gate. 0 = the engine runs exactly the code it ran before this file existed. */
extern uint8_t gActorTiersOn;

/*
 * Mitigation (b): how many game ticks the update currently running stands in for. Exactly 1.0f
 * whenever the prototype is off or the actor is near tier, so the two shared-system hooks are a
 * single float compare in the default configuration.
 */
extern float gActorTiersTickScale;

/*
 * Called once per actor per frame from Actor_UpdateAll, for actors that reached the update branch.
 * Returns nonzero to skip this actor's whole loop body this frame. Also classifies the actor for
 * the act_near=/act_mid=/act_far= perf fields and sets gActorTiersTickScale for the update that is
 * about to run.
 */
int32_t ActorTiers_SkipThisFrame(struct PlayState* play, struct Actor* actor);

/* Called once per actor per frame from Actor_DrawAll. Nonzero = skip the per-actor draw-pass work
 * (projection, culling test, draw) for a far-tier actor. Off unless the draw half was armed
 * explicitly: it is a separate lever from update tiering and is measured separately. */
int32_t ActorTiers_SkipDraw(struct PlayState* play, struct Actor* actor);

/* `agenttest tiers <near> <mid> <n> [mitb] [drawcull]` / `agenttest tiers off`. Radii are XZ world
 * units; period is the mid tier's N. Returns 0 on success. */
int32_t ActorTiers_Configure(float nearRadius, float midRadius, int32_t period, int32_t mitigateB,
                             int32_t drawCull);
void ActorTiers_Disable(void);

/* Last completed frame's per-tier counts, for the perf line. */
void ActorTiers_GetCounts(uint32_t* nearCount, uint32_t* midCount, uint32_t* farCount);

/* One-line description of the live configuration, for the marker and the command's output. */
const char* ActorTiers_Describe(void);

#ifdef __cplusplus
}
#endif

#endif /* SOH_ENHANCEMENTS_ACTOR_TIERS_H */
