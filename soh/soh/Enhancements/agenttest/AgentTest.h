#ifndef SOH_AGENT_TEST_H
#define SOH_AGENT_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

// Writes one line to agent-log.txt as `[agenttest] <text>`, or does NOTHING when the process was
// not launched in agent mode (SOH_AGENT_TEST). The gate is the point: gameplay code may call this
// freely without an ordinary session growing a marker channel it never asked for, and without any
// caller having to know how agent mode is detected.
//
// The text is written VERBATIM - it is never used as a format string - so a caller must build its
// own line. Keep it single-line and greppable, `key=value` fields, quoted where a value can
// contain a space; that is the contract every acceptance script depends on.
void AgentTest_WriteMarker(const char* text);

#ifdef __cplusplus
}
#endif

#endif // SOH_AGENT_TEST_H
