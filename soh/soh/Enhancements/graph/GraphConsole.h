#ifndef SOH_GRAPH_CONSOLE_H
#define SOH_GRAPH_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The `graph` console command: the frame's display-list arenas and their fail-safe (sturdy-bassoon#247).
// The behaviour is in `soh/src/code/graph.c` and Actor_DrawAll; the sizes and the fail-safe are
// described in GraphBudget.h beside this file. The human `graph ...` command joins `lines` into the
// ImGui console, and `agenttest graph ...` writes each as its own `[agenttest] graph <line>` marker.
//
// `args[0]` is the subcommand (none means `status`):
//   status          four lines, the last complete frame's:
//                   `op=status result=ok frame=<n> failsafe=on|off drawn=<n> skipped=<n> skipped_total=<n>
//                   skipped_frames=<n> repaired_frames=<n> last_cut=0x<mask> overrun_frames=<n>
//                   reserve=<words>`: actors drawn and skipped by the guard, the frames the backstop cut an
//                   arena out of (last_cut: 1 opaque, 2 translucent, 4 overlay), and the frames any arena
//                   overran at all, fail-safe on or off;
//                   then one per arena, `arena name=opa|xlu|overlay used=<n> words=<n> physical=<n> peak=<n>
//                   max_actor=<n> max_actor_id=0x<hex> after_actors=<n> after_peak=<n> markers=<n>`: words
//                   used (head and tail), the arena's size in force and its array's, the peak, the largest
//                   single actor draw and whose, what was written after the actor loop's last draw (-1 for
//                   the overlay, which the loop does not track), and Debug's open/close markers. The peaks
//                   and maxima run since the last `reset`
//   reset           zeroes the peaks, maxima and totals; prints `op=reset result=ok`
//   cap <opa|xlu|overlay> [<words>|default]
//                   shrinks an arena to `words` (64..its size) for THIS SESSION ONLY, from the next frame;
//                   then, or bare, `op=cap result=ok arena=<name> words=<n> size=<n>`. Never saved: a
//                   planted control
//   failsafe [on|off]
//                   switches the guard and the backstop for this session only, and while off puts the
//                   translucent arena flush against the overlay list, as OoT does; then, or bare, `op=failsafe
//                   result=ok failsafe=on|off`. `cap xlu 4096` + `failsafe off` is the pre-#247 game, which hangs at
//                   about 160 fires in view
//
// Returns 0 on success (and for the reads), 1 for a refused operation: an unknown subcommand, an extra
// argument or a bad value (`op=<sub> result=error error=bad_value`).
int32_t GraphConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_GRAPH_CONSOLE_H
