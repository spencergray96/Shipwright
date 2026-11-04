#ifndef SOH_RS_BOAT_HAZARD_H
#define SOH_RS_BOAT_HAZARD_H

// Hazardous waters (sturdy-bassoon#285): sea a boat may sail into, and is punished in, unless it is fitted with the one
// part that protects against it, RS's Iron Boots pattern. The design is sturdy-bassoon
// docs/decisions/2026-10-30-boat-ownership-and-fit-out.md ("Hazardous waters"); RS's rules and words are
// docs/notes/2026-11-03-osrs-sailing-hazards-and-boat-health-research.md.
//
// A HAZARD KIND is a bit of the tiers' `waters` mask (RsBoatTier, RsBoatFit.h). A tier carries every bit it protects
// against, so RsBoatFit_Stats's OR is the boat's protection:
//   - stormy: sails from oak up (RS's storm resistance 1);
//   - kelp: a helm from adamant up;
//   - crystal: a keel from adamant up (so never the raft);
//   - fetid: the inoculation station, a facility on the skiff and sloop;
//   - icy: the eternal brazier, a core part on every kind.
// The rapids' four strengths and lightning have bits too, set on the helm and sail tiers, but nothing reads them: they
// are objects, not waters, and #289's.
//
// NOTHING BLOCKS. Every kind lets the boat in and punishes it, as RS's do; a waterway that should be closed is authored
// closed, with walls or collision (the owner, #285, withdrawing the `block` gate the design first had).
//
// A boat is IN a hazard when its helm's point (RsBoat_HelmPos) is inside one of the scene's hazard rectangles of that
// kind: one point fixed on the hull, so one clean edge, and the point a hazard that hurts Link would read too. In one
// it is not protected against, every tick:
//   - its top speed is a share of the hull's (RsBoatSpeedMod's `share`, RsBoat.h): the strongest of every kind it is
//     in, reached at the boat's own drag;
//   - with Link aboard, each kind counts up and HITS it (RsBoatHealth_Hit, RS_BOAT_HIT_HAZARD) every `every`
//     seconds, the first a whole interval after entering. Icy's hit grows by `step` a hit, to `cap`. Leaving resets
//     the count. A hit that capsizes the boat ends its hazards: a sinking hull marks no `hazard_leave`.
//   - the HUD lists the kind's words under the HP bar while Link is aboard, until the boat leaves that water.
// Bumping rocks or the shore is never a hit (#284).
//
// The rectangles are per scene in code, beside the scene's water (RsSceneWater.cpp), each drawn as a sheet tinted by
// its kind; `boat hazard add` puts one down for this visit. Each kind's numbers are its row's, tunable live and saved
// (`boat hazard tune`, a CVar a knob, cleared for the row's).
//
// Console: `boat hazard ...` (RsBoatHazard_ConsoleRun). Markers (agent mode): `rs_boat event=hazard_enter|hazard_leave
// |hazard_hit ...`, beside the hit's own `rs_boat event=hit ... source=hazard`.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The hazard waters, in the order of their bits. Appended, never reordered: a kind's bit is in every tier row.
typedef enum RsBoatHazardKind {
    RS_BOAT_HAZARD_STORMY,
    RS_BOAT_HAZARD_KELP,
    RS_BOAT_HAZARD_CRYSTAL,
    RS_BOAT_HAZARD_FETID,
    RS_BOAT_HAZARD_ICY,
    RS_BOAT_HAZARD_COUNT,
} RsBoatHazardKind;

// The `waters` bits: a hazard kind's, then the objects' (#289), which nothing reads yet.
#define RS_BOAT_WATER_STORMY (1u << RS_BOAT_HAZARD_STORMY)
#define RS_BOAT_WATER_KELP (1u << RS_BOAT_HAZARD_KELP)
#define RS_BOAT_WATER_CRYSTAL (1u << RS_BOAT_HAZARD_CRYSTAL)
#define RS_BOAT_WATER_FETID (1u << RS_BOAT_HAZARD_FETID)
#define RS_BOAT_WATER_ICY (1u << RS_BOAT_HAZARD_ICY)
#define RS_BOAT_WATER_LIGHTNING (1u << 8)        // camphor sails up: a strike does no damage (it still slows)
#define RS_BOAT_WATER_RAPIDS_GENTLE (1u << 9)    // an iron helm up rides gentle rapids
#define RS_BOAT_WATER_RAPIDS_STRONG (1u << 10)   // mithril up
#define RS_BOAT_WATER_RAPIDS_POWERFUL (1u << 11) // rune up
#define RS_BOAT_WATER_RAPIDS_DEADLY (1u << 12)   // dragon

// A kind's live numbers: its row's, unless `boat hazard tune` set a knob.
typedef enum RsBoatHazardKnob {
    RS_BOAT_HAZARD_KNOB_SHARE, // the top speed's share in it, 0.05..1
    RS_BOAT_HAZARD_KNOB_HIT,   // a hit, before armour (icy's first)
    RS_BOAT_HAZARD_KNOB_EVERY, // seconds between hits
    RS_BOAT_HAZARD_KNOB_STEP,  // what each hit adds to the next (icy's 2; 0 elsewhere)
    RS_BOAT_HAZARD_KNOB_CAP,   // the most a growing hit reaches (icy's 20; 0: no cap)
    RS_BOAT_HAZARD_KNOB_COUNT,
} RsBoatHazardKnob;

const char* RsBoatHazard_KindName(int32_t kind); // "stormy", ...; "?" out of range
int32_t RsBoatHazard_KindByName(const char* name); // -1 for none
float RsBoatHazard_Knob(int32_t kind, int32_t knob);
// The kinds' names in `mask`, comma-separated, or "none". For markers and the console.
void RsBoatHazard_MaskNames(uint32_t mask, char* out, int32_t size);

// The hazard kinds at a point of the loaded scene, a bit each.
uint32_t RsBoatHazard_KindsAt(float x, float z);

struct RsBoat;
struct PlayState;
// The boat's hazards this tick: what it is in, its speed share, and the hits. RsBoat_Update calls it before reading its
// top speed, every tick it is afloat.
void RsBoatHazard_Tick(struct RsBoat* boat, struct PlayState* play);
// The kinds the boat is in and not protected against, as of its last tick: the HUD's alerts.
uint32_t RsBoatHazard_Harmful(const struct RsBoat* boat);
// A kind's words: RS's, naming the missing part.
const char* RsBoatHazard_Alert(int32_t kind);

// The rectangles' sheets, tinted by kind. RsSceneWater's frame-end draw calls it after the water's own, before any
// faded boat.
void RsBoatHazard_DrawSheets(void);

#ifdef __cplusplus
}

#include <string>
#include <vector>

// The hazards' console verbs, for RsBoatConsole_Run: returns -1 when args[0] is not `hazard`.
//   hazard here            `op=hazard result=ok x= z= kinds=<k,...|none>` at Link's feet, then one `boat kind=
//                          owned= helm_x= helm_z= in=<k,...> harmful=<k,...> protects=<k,...> share=<x100>` line for
//                          the boat he is aboard
//   hazard at <x> <z>      the kinds at a point: `op=hazard result=ok x= z= kinds=...`
//   hazard list            one `area n= kind= x0= z0= x1= z1= from=scene|console` line each, then one `kind name=
//                          share= hit= every= step= cap=` line each, then one `protects boat=<k> waters=...` each
//   hazard add <kind> here [<half>]
//                          a square of that kind round Link's feet, `half` (200) each way, until the scene reloads;
//                          it acts at once, and its sheet shows once the water under its centre is found
//   hazard clear           takes the console's squares away
//   hazard tune [default]  the knobs, or every one back to its row's
//   hazard tune <kind> default | <kind> <knob> <value>
//                          `op=hazard result=ok kind= share= hit= every= step= cap=`
int32_t RsBoatHazard_ConsoleRun(const std::vector<std::string>& args, std::vector<std::string>& lines);

#define RS_BOAT_HAZARD_GRAMMAR                                                                                   \
    "hazard here | hazard at <x> <z> | hazard list | hazard add <kind> here [<half>] | hazard clear | "          \
    "hazard tune [default] | hazard tune <kind> default|<knob> <value>"
#endif

#endif // SOH_RS_BOAT_HAZARD_H
