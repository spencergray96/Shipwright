#ifndef SOH_RS_QUEST_CONSOLE_H
#define SOH_RS_QUEST_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces (sturdy-bassoon#58 D18):
//
//   - the human `quest ...` command, registered here, which joins `lines` with newlines into the
//     ImGui console, and
//   - `agenttest quest ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] quest <line>` marker in agent-log.txt.
//
// One implementation, two sinks, so the two can never drift. Every line is single-line and
// greppable (`key=value` fields, quoted only where a value can contain a space).
//
// `args[0]` is the subcommand:
//   list                       one Quest_Describe line per registered quest
//   dump <id>                  the Describe line, then step[i]= / req[i]= / prereq_fn= / reward[i]= / hint[i]=
//   start <id>                 Quest_Start
//   setstep <id> <step>        Quest_SetStep      (pre-validated via Quest_CheckSetStep, so a
//   clearstep <id> <step>      Quest_ClearStep     refusal is rc=1 on every tier and never asserts)
//   check <id> <step>          the Quest_CheckSetStep answer alone, no write
//   complete <id>              Quest_Complete
//   force <id>                 Quest_ForceComplete
//   reset <id>                 Quest_Reset
//   debugwipe                  Quest_DebugWipe
// Mutating subcommands emit `op=<sub> id=<n> [step=<n>] result=<Quest_ResultName>` then the
// Describe line. Returns 0 when the operation succeeded (or for read-only subcommands), 1 otherwise
// - so `rc=` on the agent-loop cmd marker is the pass/fail bit.
int32_t QuestConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_QUEST_CONSOLE_H
