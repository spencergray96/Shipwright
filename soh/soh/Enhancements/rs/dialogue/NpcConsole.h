#ifndef SOH_RS_NPC_CONSOLE_H
#define SOH_RS_NPC_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for NPCs (sturdy-bassoon#58 D18), the
// same arrangement QuestConsole.h describes:
//
//   - the human `npc ...` command, registered here, which joins `lines` with newlines into the
//     ImGui console, and
//   - `agenttest npc ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] npc <line>` marker in agent-log.txt.
//
// One implementation, two sinks, so the two can never drift. Every line is single-line and
// greppable (`key=value` fields, quoted only where a value can contain a space).
//
// `args[0]` is the subcommand:
//   list                  one RsNpc_Describe line per registered NPC
//   dump <npcId>          the Describe line, then per rule: when[i]= with its live value,
//                         match=, text=, opt[i.j]= - the D11 introspection prize made concrete
//   resolve <npcId>       the first-match-wins answer alone, with the matched rule's text
//   actors                every live RS actor instance in the loaded scene, with the rule each
//                         one currently resolves to - the two-placements proof
//   badcheck              RsNpc_DefProblem over the malformed table; every entry must be refused
// Returns 0 when the read succeeded, 1 otherwise - so `rc=` on the agent-loop cmd marker is the
// pass/fail bit. Nothing here writes: picking an option is what writes, and only an actor does it.
int32_t RsNpcConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_NPC_CONSOLE_H
