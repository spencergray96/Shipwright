#ifndef SOH_RS_INTERACTION_CONSOLE_H
#define SOH_RS_INTERACTION_CONSOLE_H

// The interaction surface (sturdy-bassoon#183): one implementation behind two sinks, as every rs/
// console is - the human `interaction` command and `agenttest interaction ...`, whose lines arrive
// as `rs_interaction <line>` markers. Answers are `op=...` lines or indexed rows; a trigger's own
// gameplay markers are `rs_interaction id=<n> event=...`, so a grep for one never sees the other.
//
//   status                 every resident trigger in the scene: its id, whether that id has code,
//                          where it stands, its range and focus height, how often it was checked,
//                          and whether it is in a conversation now
//   list                   every registered definition (the code table): id, name, tier, counts
//   describe <id>          one interaction: code=0|1, and the rule its gates resolve to right now
//   badcheck               plants bad definitions and proves registration refuses each
//   spawn <id> <n> [dist] [cols] [spacing]
//                          the cost bench (#117's method): N triggers for <id> in a wall in front of
//                          Link, as `propbench spawn` lays one out
//   clear                  kills every resident trigger, placed or spawned

#include <stdint.h>
#include <string>
#include <vector>

int32_t RsInteractionConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_INTERACTION_CONSOLE_H
