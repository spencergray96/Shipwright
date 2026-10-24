#ifndef SOH_AGENT_TEST_H
#define SOH_AGENT_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

// Records one line as `[agenttest] <text>`. In agent mode (SOH_AGENT_TEST) it is a marker: written to
// agent-log.txt, where an acceptance script can wait on it, and to the engine log. Outside agent mode
// it goes to the engine log ALONE (sturdy-bassoon#97), so a human session keeps a record of what the
// game did without growing a marker file nobody is reading. No caller has to know which mode it is.
//
// Call it for EVENTS - a textbox opened, an item woke, a quest completed - never once per frame. It
// runs in every session, so a per-frame caller would flood an ordinary player's log.
//
// The text is written VERBATIM - it is never used as a format string - so a caller must build its
// own line. Keep it single-line and greppable, `key=value` fields, quoted where a value can
// contain a space; that is the contract every acceptance script depends on.
void AgentTest_WriteMarker(const char* text);

// One rendered frame has been submitted (RunCommands, OTRGlobals.cpp; sturdy-bassoon#253). The perf marker's
// frame_max_ms= is the longest gap between two of these over its interval, and frame_late= how many gaps ran
// past 1.2x the cap's frame: "does it hold the cap" needs the worst frame, which a mean fps cannot show (a
// frame that also runs the 20 Hz tick costs the tick on top). One clock read a frame, in every session.
void AgentTest_NoteFrameEnd(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_AGENT_TEST_H
