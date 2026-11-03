#ifndef SOH_RS_BOAT_HEALTH_H
#define SOH_RS_BOAT_HEALTH_H

// Boat health (sturdy-bassoon#284): how much a boat can take, what takes it, what happens at 0 and what mends it. The
// design is sturdy-bassoon docs/decisions/2026-10-30-boat-ownership-and-fit-out.md ("Boat health"); the words are
// CONTEXT.md's; RS's rules are docs/notes/2026-11-03-osrs-sailing-hazards-and-boat-health-research.md.
//
// HP. A boat's most HP is its fit-out's (RsBoatFit_Stats): its hull's plus its keel's, RS's numbers as they are (a
// starting skiff 30 + 50 = 80, a raft 20). It never comes back on its own. What is kept is the DAMAGE, what the boat
// is missing from its most, so a better hull or keel raises the most and the HP alike: what was missing stays missing,
// and an upgrade never mends (the owner, #284).
//   - The player's own boat's damage is saved, per kind, in `rsBoats` (`damage`, and `wrecked` once it capsized): a
//     boat left damaged is damaged next time, wherever it is.
//   - A fixture's (Lake Hylia's, `boat spawn`'s, a trial's) is its actor's, none at every spawn, never saved.
//
// A HIT (RsBoatHealth_Hit) is the one way a boat loses HP. The keel's flat armour comes off every hit, whatever its
// source, and can take a hit to 0 (RS: 100 armour is one point off; ours are already whole points). A hit lands only
// while Link stands on the boat (its deck, its rail or its helm): a boat left anywhere is never damaged, as an RS boat
// with nobody aboard is not (the owner, #284). Bumping rocks or the shore never hits (RS: no damage, only the speed).
// The console's hit lands with nobody aboard, as a test aid.
//
// AT 0 THE BOAT CAPSIZES (RsBoat_Capsize, RsBoat.h): Link lets go and is left in the water where it went down, with
// the drown rules as ever (the boat is no longer his refuge); the hull sinks out of sight in RS_BOAT_SINK_TICKS. Then
// the player's own boat is STOWED and WRECKED (0 HP): a pier will not take it out until it has been repaired there,
// and says the price. A fixture is gone until its scene puts it back.
//
// MENDING. Nothing heals a boat but:
//   - a pier's repair (RsBoatDock.h, RS_BOAT_DOCK_OP_REPAIR): to full, RS_BOAT_REPAIR_RUPEES_PER_HP a point missing,
//     for a stowed boat or one docked at any pier;
//   - a BOAT HEART: a floating recovery heart, vanilla's 3D one made bigger, that heals RS_BOAT_HEART_HP. A boat
//     collects it by sailing over it (the hull's outline, RsBoat_HullContains), only with Link aboard; Link swimming
//     into it does nothing. Collected even at full HP, as vanilla's hearts are. Placed per scene in code (kBoatHearts,
//     in world units), each back on every load of its scene; `boat heart here` puts one down for a test.
// Building a better hull or keel raises the most, not the HP (the owner, #284).
//
// Console: `boat hp|damage|capsize|heart ...` (RsBoatHealth_ConsoleRun), and `boat repair` with the pier's verbs.
// Markers (agent mode): `rs_boat event=hit|hit_ignored|capsize|sunk|heal|hp_set|heart ...`.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// What hit a boat: the `hit` marker's `source=`. Appended, never reordered (nothing saves them; the console names them).
typedef enum RsBoatHitSource {
    RS_BOAT_HIT_CONSOLE,   // `boat damage`: lands with nobody aboard too (a test aid)
    RS_BOAT_HIT_HAZARD,    // a hazardous water the boat is not fitted for (#285)
    RS_BOAT_HIT_RAPIDS,    // a rapid too strong for its helm (#289)
    RS_BOAT_HIT_LIGHTNING, // a lightning cloud's strike (#289)
    RS_BOAT_HIT_ENEMY,     // a sea creature or a cannon, when there are any
    RS_BOAT_HIT_SOURCE_COUNT,
} RsBoatHitSource;

// What healed one: the `heal` marker's `source=`. Same rule.
typedef enum RsBoatHealSource {
    RS_BOAT_HEAL_CONSOLE, // `boat hp`
    RS_BOAT_HEAL_HEART,   // a boat heart
    RS_BOAT_HEAL_PIER,    // a pier's repair
    RS_BOAT_HEAL_SOURCE_COUNT,
} RsBoatHealSource;

// A pier's repair, rupees a point of HP missing. A placeholder, as the fetch fees are (RsBoatDock.h); a define because
// the pier's labels spell it (InteractionTable.cpp).
#define RS_BOAT_REPAIR_RUPEES_PER_HP 1
// What a boat heart mends. RS has no such thing; a recovery heart is a twentieth of Link's starting health, and 20 is a
// quarter of a starting skiff's. A first value to play with.
#define RS_BOAT_HEART_HP 20
// How far outside the hull a heart is still collected: half a tile, the Tempor Tantrum crates' margin (#289).
#define RS_BOAT_HEART_MARGIN 20.0f

const char* RsBoatHealth_HitSourceName(int32_t source);  // "console", "hazard", ...; "?" out of range
int32_t RsBoatHealth_HitSourceByName(const char* name);  // -1 for none
const char* RsBoatHealth_HealSourceName(int32_t source); // "console", "heart", "pier"; "?"

// The kind's most HP and flat armour, from its fit-out (RsBoatFit_Stats). 0 for no such kind.
int32_t RsBoatHealth_Max(int32_t kind);
int32_t RsBoatHealth_Armour(int32_t kind);

// The player's boat of the kind's HP, as saved (owned or not: owning it again is the same boat), never above its most.
// 0 is wrecked.
int32_t RsBoatHealth_KindHp(int32_t kind);
// 1 while the owned kind's HP is under its most; and at 0. BoatStateIs's DAMAGED and WRECKED (RsBoatDock.h).
int32_t RsBoatHealth_KindDamaged(int32_t kind);
int32_t RsBoatHealth_KindWrecked(int32_t kind);
// Sets the owned kind's HP outright, clamped to 0..its most, and marks `hp_set`. A live boat of the kind takes it at
// once. At 0 it is wrecked: a live boat capsizes, and one out in another scene is stowed now. The pier's repair and the
// console's `boat hp` call it. 0 for a kind not owned (nothing set), else 1.
int32_t RsBoatHealth_SetKindHp(int32_t kind, int32_t hp, int32_t source);
// What a pier's repair of the kind costs now: its missing HP at RS_BOAT_REPAIR_RUPEES_PER_HP. 0 at full.
int32_t RsBoatHealth_RepairFee(int32_t kind);

struct RsBoat;
// A boat's HP now: the store's for the player's own, its actor's for a fixture.
int32_t RsBoatHealth_Of(const struct RsBoat* boat);
// The boat Link is aboard now (RsBoat_LinkAboard), not one sinking, or NULL: the one the HUD's HP bar shows.
struct RsBoat* RsBoatHealth_LinkBoat(void);
// Fills a new boat's HP: a fixture full, the player's own from the store (nothing to do). RsBoat_Init calls it.
void RsBoatHealth_InitBoat(struct RsBoat* boat);

// Hits the boat for `hit` HP before armour. Returns what it took after armour (0 when its armour takes it all), or -1
// when the hit did not land: nobody aboard (any source but the console's), already sinking, or a kind with no HP. At
// 0 it capsizes. Marks `hit` or `hit_ignored`.
int32_t RsBoatHealth_Hit(struct RsBoat* boat, int32_t hit, int32_t source);
// Heals it by `amount`, up to its most. Returns what it gained. A sinking boat gains nothing. Marks `heal`.
int32_t RsBoatHealth_Heal(struct RsBoat* boat, int32_t amount, int32_t source);

// The `rsBoats` key of one kind (RsBoatFit.cpp's section calls these inside each kind's object), and the reset every
// new game and load starts from (every kind at full).
void RsBoatHealth_SaveKind(int32_t kind);
void RsBoatHealth_LoadKind(int32_t kind);
void RsBoatHealth_Reset(void);

#ifdef __cplusplus
}

#include <string>
#include <vector>

// The health's console verbs, for RsBoatConsole_Run: returns -1 when args[0] is not one of them.
//   hp                     `op=hp result=ok` then one `hp kind=<k> owned=0|1 hp=<n> max=<n> armour=<n> wrecked=0|1
//                          fee=<rupees>` line per kind, and one `boat idx=<i> kind=<k> owned=0|1 hp=<n> max=<n>
//                          aboard=0|1 sinking=<ticks>` per live boat
//   hp <kind> <n>|full     sets the owned kind's HP outright (a live boat takes it; 0 capsizes it). `op=hp result=ok
//                          kind=<k> hp=<n> max=<n>`; error=not_owned
//   damage <n> [<source>] [<idx>]
//                          hits the boat Link is aboard (else boat idx, else the only boat) for n before armour, as
//                          `source` would (console by default, which lands with nobody aboard). `op=damage result=ok
//                          idx=<i> dealt=<n> hp=<n> max=<n>`, or result=ignored reason=<empty|sinking|no_hp>;
//                          error=no_boat
//   capsize [<idx>]        capsizes it at once, as at 0 HP. `op=capsize result=ok idx=<i>`; error=no_boat|sinking
//   heart here|list|clear  a boat heart at Link's feet on the water (this visit only), the scene's hearts, or none.
//                          `op=heart result=ok ...`, then one `heart n=<i> x= z= live=0|1` line each for list
int32_t RsBoatHealth_ConsoleRun(const std::vector<std::string>& args, std::vector<std::string>& lines);

// The grammar, for `boat`'s usage line and help text.
#define RS_BOAT_HEALTH_GRAMMAR \
    "hp [<kind> <n>|full] | damage <n> [<source>] [<idx>] | capsize [<idx>] | heart here|list|clear"
#endif

#endif // SOH_RS_BOAT_HEALTH_H
