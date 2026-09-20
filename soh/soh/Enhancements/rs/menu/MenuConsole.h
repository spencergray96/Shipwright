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
//   view [ownvp|bracket|inherit]
//                         STAGE-4 DIAGNOSTIC. How the scroll geometry gets a viewport and a
//                         projection. `ownvp` is what ships - our own Vp + guOrtho in the pool.
//                         `bracket` adds the level-2 `View` bracket research recommended, and
//                         `inherit` is that bracket ALONE. The two exist so the rejected
//                         alternative can be run rather than argued about: `inherit` draws
//                         pixel-identically to `ownvp`, which is how "the bracket never reaches
//                         OVERLAY_DISP" was measured rather than only read. NOT a CVar (see
//                         RsMenuViewMode in RsMenu.h) - it resets to `ownvp` every launch
//   probe [on|off]        STAGE-4 INSTRUMENT. Drives one stepped per-tick offset into BOTH the
//                         scroll geometry (through a Matrix_ op, which frame-interpolates) and a
//                         green probe string (through a texture rectangle's baked y, which does
//                         not), and recolours the rolls magenta so a pixel scan cannot pick up the
//                         world. Reports `step=`/`phase=`/`dy=`, which is the 20 Hz lattice a
//                         screenshot is measured against: a drawn position that is not a whole
//                         multiple of `step` from the park position came from the renderer.
//                         SOH_2D_DRAWING.md's "two-channel probe". Also not a CVar
//   dump                  EVERYTHING a marker can carry: open/closed, current page, page count,
//                         which menu `primary` selects, the live freeze and HUD state, the
//                         trigger's binding count, the counters, and one line per registered page.
//                         `stick_frames=` is the stage-2 evidence - it counts frames on which the
//                         menu was OFFERED stick input while the world was frozen, which is what
//                         turns "Link did not move" from an untested negative into a challenged one.
//                         `section=view` adds the two above plus `epoch=` - frame interpolation's
//                         camera epoch, the one number that says whether something has quietly
//                         switched interpolation off for the rest of the frame - and `pause_mode=`,
//                         the register that would have exempted it
//
// Returns 0 when the operation succeeded (or for read-only subcommands), 1 otherwise - so `rc=` on
// the agent loop's cmd marker is the pass/fail bit.
int32_t RsMenuConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_MENU_CONSOLE_H
