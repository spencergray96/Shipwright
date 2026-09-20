#ifndef SOH_RS_MENU_CONSOLE_H
#define SOH_RS_MENU_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for the mod-owned pause interface
// (sturdy-bassoon#111), the arrangement RegionConsole.h describes and for the same reason:
//
//   - the human `menu ...` command, registered here, which joins `lines` with newlines into the
//     ImGui console, and
//   - `agenttest menu ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] rs_menu <line>` marker in agent-log.txt.
//
// One implementation, two sinks, so the two can never drift.
//
// THIS IS THE MENU'S TEST SURFACE, not a convenience. The menu is a full-screen panel with no
// gameplay side effects, so without it the only evidence a run could take is a screenshot - and a
// screenshot cannot say which page the ring is on, whether the world is frozen, or whether the
// trigger has a binding at all. `dump` exists so later stages can assert state from a marker alone.
//
// PAGE NUMBERS ON THIS COMMAND ARE 1-BASED and always agree with the page's own title: `menu page 3`
// selects the page that draws `page 3`, and every line that reports a page prints `page=3` beside
// `title="page 3"`. (Internally the ring is 0-based; that index is never printed.)
//
// `args[0]` is the subcommand:
//   open                  opens the scroll. REFUSALS ARE NAMED: `error=kaleido_open` when vanilla
//                         pause is up (one menu at a time, and this is the one that yields),
//                         `error=no_play`, `error=no_pages`, `error=disabled`. Opening an already
//                         open menu is not a refusal - it reports `was_open=1` and rc=0
//   close                 closes it and restores the freeze and the HUD. Idempotent: closing a
//                         closed menu reports `was_open=0` and rc=0, because "nothing to do" is
//                         not a refused operation
//   page <n>              selects page n, 1-based. Out of range is rc=1 and never a clamp - a
//                         silent clamp would let a run assert a page it never reached. Works while
//                         closed; the ring is state, not a view
//   primary [custom|vanilla]
//                         which menu START opens, written to a CVar so it survives the session.
//                         With no argument it only reports. This is the console's job because SoH
//                         has no `set` command, so a CVar the agent loop must change needs a
//                         subcommand of its own
//   dump                  EVERYTHING a marker can carry: open/closed, current page, page count,
//                         which menu `primary` selects, the live freeze and HUD state, the
//                         trigger's binding count, the counters, and one line per registered page.
//                         `stick_frames=` is the stage-2 evidence - it counts frames on which the
//                         menu was OFFERED stick input while the world was frozen, which is what
//                         turns "Link did not move" from an untested negative into a challenged one
//
// Returns 0 when the operation succeeded (or for read-only subcommands), 1 otherwise - so `rc=` on
// the agent loop's cmd marker is the pass/fail bit.
int32_t RsMenuConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_MENU_CONSOLE_H
