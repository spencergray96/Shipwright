#ifndef SOH_RS_TRIAL_H
#define SOH_RS_TRIAL_H

// Rs_Trial (sturdy-bassoon#289, the crude cut): RS's first Barracuda Trial, the Tempor Tantrum, in its own scene,
// `tempor_tantrum` (a 3 x 3 world matrix of the trial's nine RS map squares, x 46-48, y 43-45, origin RS (3040, 2848)).
// The skiff collects the rank's lost supplies crates against the clock and comes back to the start. The rum, the
// rocks, the rapids, the lightning clouds and Ralph are the full prototype's, not this cut's.
//
// THE SCENE LOAD. The export lays flat ground at 0 and walls round the land (the island, the north-west coast); no
// grid-tool map has a waterbox, so on the first player update in the scene this floods it with the boat console's
// test waterbox (RsBoatConsole_AddWater) at RS_TRIAL_WATER_Y over the whole world, spawns a skiff at the start and
// the manager, and stands Link at the skiff's helm, taking it a few ticks later once he is on deck.
//
// THE MANAGER is an actor so it can draw: ACTORCAT_MISC, after the boat and Link have moved. It owns the 36 crates
// as a table of RS tiles (the cache's crate locs 59245-59280, one each) and a bit per crate, so a collected crate is
// a flag and nothing is baked. Each is drawn as a placeholder box with a pole over it, floating at the surface.
//
// A RUN (`trial start <rank>`): the skiff goes back to the start, the rank's crates go live (Swordfish 14, Shark 25,
// Marlin 36; the ids follow the ranks, docs/notes/2026-10-31-tempor-tantrum-research.md), and the clock counts ticks
// from the first tick the skiff moves. A live crate inside the skiff's hull rectangle grown by half a tile is
// collected. Every crate collected and the skiff within RS_TRIAL_FINISH_RADIUS of the start finishes it. Link
// outside the buoys' box for RS_TRIAL_OUT_TICKS fails it; coming back in time clears the count. Nothing is saved.
//
// Console: `trial ...` and `agenttest trial ...` (RsTrialConsole_Run below). Markers, agent mode only:
// `rs_trial event=<start|crate|out|in|finish|fail> ...`.

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// The sea's surface, over the export's ground at 0: deep enough for the boat (RS_BOAT_DRAFT) and for adult Link to swim
// (56), and under the top of the land's walls, which a grid-tool map's level 0 builds 80 tall. At 100 the walls were
// under water and the skiff sailed over the island.
#define RS_TRIAL_WATER_Y 60.0f
#define RS_TRIAL_OUT_TICKS 200       // 10 s at 20 Hz outside the area fails a run, as in RS
#define RS_TRIAL_FINISH_RADIUS 120.0f // 3 tiles from the start
#define RS_TRIAL_PICKUP_MARGIN 20.0f  // half a tile round the hull: RS enlarged its crates' hitbox twice after launch
#define RS_TRIAL_TARGET_SCALE 0.55f   // our skiff laps about twice as fast as RS's: RS's target times, scaled

void RsTrial_Init(Actor* thisx, PlayState* play);
void RsTrial_Destroy(Actor* thisx, PlayState* play);
void RsTrial_Update(Actor* thisx, PlayState* play);
void RsTrial_Draw(Actor* thisx, PlayState* play);

#ifdef __cplusplus
}

#include <stdint.h>
#include <string>
#include <vector>

// The one renderer behind `trial ...` and `agenttest trial ...` (markers `[agenttest] rs_trial <line>`).
// `args[0]` is the subcommand:
//   status          `op=status result=ok here=0|1 state=<idle|running|finished|failed> rank=<none|swordfish|shark|marlin>
//                   ticks=<n> moving=0|1 collected=<n> total=<n> out_ticks=<n> target_ticks=<n> boat=0|1 boat_x= boat_z=
//                   link_x= link_z= inside=0|1`. ticks: the run's clock (frozen once it ends); out_ticks: ticks Link
//                   has been outside the area, 0 inside; target_ticks: the rank's target (RS's times x RS_TRIAL_TARGET_SCALE)
//   start <swordfish|shark|marlin>
//                   puts the skiff back at the start with Link at its helm and starts a run.
//                   `op=start result=ok rank=<rank> total=<n> target_ticks=<n>`
//   reset           ends any run; no crates are drawn. `op=reset result=ok`
//   crates          one `crate n=<i> loc=<id> rank=<rank> x= z= live=0|1 collected=0|1` line per crate, then
//                   `op=crates result=ok count=36`
//   goto <n>        puts the skiff two tiles short of crate n (0-35, table order) heading straight at it, Link at the
//                   helm, stopped: a test aid, then `boat drive 1 0 <ticks>` sails through it.
//                   `op=goto result=ok n=<i> loc=<id> x= z= yaw=<binang>`
// Refusals (rc 1):
//   op=<sub> result=error error=not_here     not in the trial's scene
//   op=<sub> result=error error=no_boat      the scene has no skiff (start, goto)
//   op=goto result=error error=no_approach n=<i>  every heading at that crate crosses a wall
//   op=usage result=error usage="..."        an unknown subcommand, an unknown rank or a bad crate number
int32_t RsTrialConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif

#endif // SOH_RS_TRIAL_H
