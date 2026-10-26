#ifndef SOH_RS_SWIM_STAMINA_H
#define SOH_RS_SWIM_STAMINA_H

#include <stdint.h>

// ============================================================================================
//  SWIM STAMINA  (sturdy-bassoon#257)
// ============================================================================================
//
// In OoT Link swims forever, which leaves a boat nothing to do. With this on, he swims for a BUDGET -
// any water, sea or river, one rule everywhere - and drowns when it runs out.
//
//   - BEING IN THE WATER drains it one tick per tick: swimming, diving, or sunk to the bottom in iron
//     boots with his head under (the owner, 2026-10-26). Nothing lengthens it: not the Zora tunic, not
//     a scale, not Link's age (the owner, 2026-10-25).
//   - STANDING on anything - ground, a platform, a boat's deck, a river bed with his head above the
//     surface (wading, boots or not) - refills it at once. In the air, between the two, it holds.
//   - It HOLDS while a textbox is up, while Link is in a cutscene, and while another feature holds the
//     main timer (a minigame's clock), because that clock is the only place the budget is visible and
//     drowning with no warning would be worse than a pause.
//   - It SHOWS on vanilla's hazard clock, the one heat and iron boots use, from the moment Link is in the
//     water until he stands on something: z_parameter.c starts and stops it on this file's answer to
//     VB_SHOW_SWIM_CLOCK, this file writes its seconds, and the engine never counts it down or kills Link
//     with it. With this off the VB answers false and z_parameter.c behaves as vanilla.
//   - With it on, ONE VANILLA BEHAVIOUR CHANGES: iron boots under water. The swim clock is up from the
//     moment he is in the water, so vanilla's underwater clock (8 s a heart, after a few seconds under)
//     never starts: sunk in iron boots he drowns when the swim budget runs out instead, tunic or not. The
//     heat clock is unaffected: Link cannot swim in a hot room.
//
// RUNNING OUT: Link is pulled under for RS_SWIM_SINK_TICKS, the way the Zora's River whirlpool takes
// him (ovl_En_Stream), then is put back somewhere dry. If it runs out while he is under water in an
// A-dive, the dive finishes first and he is put back the moment he surfaces, with no second sink (the
// owner, 2026-10-26). Either way he goes, in this order:
//
//   1. his REFUGE, when he has one - the boat he last steered (RsSwim_SetRefuge). Set on letting go of
//      the helm, cleared when he stands on anything but that boat, so walking around on a boat is
//      not enough and walking on land after steering spends it;
//   2. the nearest unlocked DROWN POINT in this scene (the table in SwimStamina.cpp). Port Sarim's is
//      the default and is always unlocked; the rule for any other is that it may unlock only once it is
//      as easy to walk to as Port Sarim is from the start, so drowning is never a sequence break;
//   3. otherwise vanilla's void-out: the scene's respawn point, as for a fall into the void.
//
// 1 and 2 are an in-place move (the step warp's, RsStair_BeginWarpMove: freeze, fade, place, fade
// in), and he takes the void-out's damage once he has landed. 3 is vanilla's own and charges its own.
//
// `swim` in the console reads and drives all of it (SwimConsole.h), with `rs_swim` markers.

#ifdef __cplusplus
extern "C" {
#endif

struct Actor;

// The switch. Off by default, so the feature cannot touch a vanilla area while it is being tuned.
#define CVAR_RS_SWIM_STAMINA CVAR_ENHANCEMENT("RsSwimStamina")
// THE LENGTH, in seconds. The build default is RS_SWIM_STAMINA_DEFAULT_SECONDS; the CVar overrides it
// (`swim length <s>`), clamped to [1, RS_SWIM_STAMINA_MAX_SECONDS]. Recorded in ENGINE_BUDGETS.md.
#define CVAR_RS_SWIM_STAMINA_SECONDS CVAR_ENHANCEMENT("RsSwimStaminaSeconds")
#define RS_SWIM_STAMINA_DEFAULT_SECONDS 30
#define RS_SWIM_STAMINA_MAX_SECONDS 600
// How long he sinks before he is moved, in game ticks (20 per second).
#define RS_SWIM_SINK_TICKS 30

int32_t RsSwim_Enabled(void);
void RsSwim_SetEnabled(int32_t on);
int32_t RsSwim_GetLengthSeconds(void);
void RsSwim_SetLengthSeconds(int32_t seconds); // clamped
void RsSwim_ClearLengthSeconds(void);          // back to the build default
int32_t RsSwim_LengthOverridden(void);

// The budget's ceiling in ticks, and a way to set what is left (clamped to [1, max]; for the console).
int32_t RsSwim_MaxTicks(void);
void RsSwim_SetBudgetTicks(int32_t ticks);
// The budget as the clock shows it, rounded up, so it reads 0 only when it is gone.
int32_t RsSwim_ClockSeconds(void);

// --- the refuge ---------------------------------------------------------------------------------
// Where a drowned Link goes back to, ahead of any drown point. With an `actor` (the boat), x/y/z and
// yaw are in its frame - rotated by its shape yaw and added to its position when he drowns, so a boat
// that has drifted takes him with it. With NULL they are a world position and facing, which is what
// the console's `swim refuge here` sets. Cleared when he stands on anything but `actor` (on anything
// at all for NULL), when the scene changes, or when the actor is gone.
void RsSwim_SetRefuge(struct Actor* actor, float x, float y, float z, int16_t yaw);
void RsSwim_ClearRefuge(void);

#ifdef __cplusplus
}

// C++ only, for the console.
struct RsSwimStatus {
    bool enabled;
    const char* phase; // "idle", "dive", "sinking", "moving"
    int32_t budget;
    int32_t max;
    bool swimming;
    bool grounded;
    float depth; // how far his feet are below the water's surface (Player's yDistToWater)
    const char* holdReason; // why swimming is not draining it this tick: "none", "message", "cutscene", "timer"
    bool clockOwned;        // the main timer is the swim clock
    bool refuge;
    bool refugeOnActor;
    int32_t drowns;
    const char* lastRespawn; // "none", "refuge", "point:<name>", "voidout"
};
RsSwimStatus RsSwim_GetStatus();

struct RsSwimPointStatus {
    const char* name;
    int16_t sceneId;
    int32_t rsX, rsY;
    bool unlocked;
    bool inScene; // the scene Link is in holds it
    bool placed;  // inScene and a floor was found under it
    float x, y, z;
    bool wet; // a water surface above that floor: a point in the sea, an authoring mistake
};
int32_t RsSwim_PointCount();
RsSwimPointStatus RsSwim_GetPoint(int32_t index);

// Drowns him now, whatever the budget - the console's `swim drown`. 0 when it could not (off, not in
// play, already drowning).
int32_t RsSwim_DrownNow();
#endif

#endif // SOH_RS_SWIM_STAMINA_H
