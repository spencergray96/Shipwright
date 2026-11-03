#ifndef SOH_RS_ITEMS_CONSOLE_H
#define SOH_RS_ITEMS_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for the inventory (sturdy-bassoon#286, RsItems.h): the
// human `items ...` command, registered in RsItemsConsole.cpp, and `agenttest items ...` (agenttest/AgentTest.cpp),
// which writes each line as its own `[agenttest] rs_items <line>` marker. One implementation, two sinks.
//
// `<item>` is any token, counted or key ("plank", "hammer"); `items list` prints them all.
//
// `args[0]` is the subcommand:
//   list | (none)           every counted item, then every key item, a line each:
//                             item token=plank count=12 cap=999 icon=1 ground_icon=1
//                             key token=hammer held=1 category=tools flag=12 icon=0 ground_icon=0
//                           (icon=/ground_icon=: whether mods/rs-items.o2r held that icon at boot)
//                           then `op=list result=ok items=<n> keys=<n> ceremonies=<queued>`
//   give <item> [n]         a counted item: n more (default 1), the icon over Link's head - refused past the cap,
//                           `error=full`, nothing added (the console's check; the world fills to the cap instead).
//                           A key item: its flag set and its ceremony queued (`error=held` if already held).
//                           `op=give result=ok item=plank count=5 total=17`
//   take <item> [n]         a counted item: n fewer, refused `error=short` when Link has fewer. A key item: its
//                           flag cleared - a test aid, a key item is never lost in play
//   set <item> <n>          a counted item's count outright, 0..its cap, no rules
//   reset                   every count to 0 (key items untouched: `take` each)
//   float <item> [n]        the icon over Link's head and nothing else, to look at the float
//   spawn <item> [n] [once <flag>]
//                           a pickup on the floor 80 units in front of Link, past his reach: n of a counted item
//                           (default 1), coming back on every load unless `once <flag>` names its once-only world
//                           flag, a placement flag (2048-3839, or 3968-4095 for a test); or the key item.
//                           `op=spawn result=ok item=plank count=5 once_flag=-1`
//   hud [on|off]            the on-screen readout of every count and key item (CVAR_RS_ITEMS_HUD, off by default)
//   groundcount [on|off]    whether a counted pickup lying in the world shows its count (CVAR_RS_ITEMS_GROUND_COUNT,
//                           on by default); the float over Link's head always does. `op=groundcount groundcount=1`
//
// Errors are `op=<sub> result=error error=<kind>`: unknown_item, bad_value, full, short, held, no_play (a
// subcommand that needs a scene, run from the title screen), spawn_failed (the actor table refused it), usage.
//
// Returns 0 when the operation succeeded (or for a read), 1 otherwise - `rc=` on the agent loop's cmd marker is the
// pass/fail bit.
int32_t RsItemsConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_ITEMS_CONSOLE_H
