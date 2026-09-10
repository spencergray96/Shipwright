#ifndef SOH_RS_MUSIC_CONSOLE_H
#define SOH_RS_MUSIC_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for the zone director
// (sturdy-bassoon#90 P0), the same arrangement QuestConsole.h uses and for the same reason:
//
//   - the human `rsmusic ...` command, registered here, which joins `lines` with newlines into
//     the ImGui console, and
//   - `agenttest music ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] rs_music <line>` marker in agent-log.txt.
//
// One implementation, two sinks, so the two can never drift. Every line is single-line and
// greppable (`key=value` fields, no spaces inside a value).
//
// This exists because #90's verification is a P0 deliverable rather than a nice-to-have: the
// agent test loop screenshots and reads FPS, and IT CANNOT HEAR ANYTHING. Without a
// machine-readable signal every phase of this feature needs a human at the keyboard to close.
// (It is not a substitute for one - whether it sounds right is still a human call.)
//
// `args[0]` is the subcommand:
//   status                the live director state, in grouped lines - director, zone, the track
//                         and its queue position, tunables, counters. Grouped rather than one because the
//                         ImGui console does not wrap and one 200-character line means dragging
//                         the window out to full width to read it. Every LINE is still
//                         single-line key=value, which is what the marker channel needs;
//                         "greppable" and "one line in total" were never the same requirement
//   where                 Link's position in BOTH frames plus the zone that wins there. The
//                         "check a rect against where he actually is" tool
//   zones                 one line per table entry: priority, scene binding, rects, tracks
//   scenes                the per-scene opt-in list and each scene's RS coordinate anchor
//   bags                  every zone's shuffle bag: the shuffled order, how far through it the
//                         zone is, and what it played last. The `advance` marker already makes a
//                         whole bag cycle reconstructable from the log without polling, which is
//                         what a run asserts on; this is for reading a bag at a moment, and for
//                         seeing the refill's back-to-back guard in the order itself
//   firstvisit            every zone with a one-shot opener: its world flag and whether that
//                         flag is spent yet. The assertable form of "the opener was NOT consumed
//                         by clipping the boundary, and WAS consumed on activation"
//   on | off              flip the master CVar (persisted)
//   dwell <seconds>       set the dwell, fade-out, fade-in CVars. Seconds, floating point.
//   fadeout <seconds>     A fade is an 8-bit field in units of 1/30 s, so anything past 8.5 s
//   fadein <seconds>      clamps - the reported unit count is what the engine will actually get
//   baseline              bookmark the transition count, so a run can assert "and then NOTHING
//                         happened" against it. It does NOT zero the counter and there is no way
//                         to: an unresettable counter makes "the count did not move" a stronger
//                         claim. It was called `reset` through P0, which described neither what
//                         it did nor what it was for, and cost a human real confusion
//
// Returns 0 when the operation succeeded (or for read-only subcommands), 1 otherwise - so `rc=` on
// the agent loop's cmd marker is the pass/fail bit.
int32_t MusicConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_MUSIC_CONSOLE_H
