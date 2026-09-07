#ifndef SOH_RS_QUEST_OVERLAY_H
#define SOH_RS_QUEST_OVERLAY_H

#include <stdint.h>

// The on-screen quest journal overlay (sturdy-bassoon#58 P5 / #74, decision D18).
//
// The one surface with no console equivalent. The console proves the journal's RUNS are right
// (`quest journal <id> runs` prints every run's style and emphasis), but nothing before this
// RENDERED an emphasis: `item` and `hint` were distinct as data and identical on every screen. The
// overlay is the read API's intended consumer - it calls QuestJournal_Build / QuestJournal_Snapshot
// every ImGui frame and draws each run in its emphasis colour, so the D23 requirement that `item`
// and `hint` render differently is finally a visible fact rather than a table entry.
//
// It is a DEBUG overlay: an ImGui window, re-resolved from the global stores every frame (which is
// what makes it show a step striking through the instant an item is collected), switched from the
// `quest` console command so the switch is on both sinks (`quest overlay ...` for a human,
// `agenttest quest overlay ...` for the loop). Nothing persists: the state below is process-lifetime
// only, and every launch starts with the overlay off. The custom quest-journal UI is still out of
// scope for #58; this draws the data that UI will read, with no design in it.
//
// C++ only, no extern "C", for the same reason QuestJournal.h gives - no actor draws a journal.

#define QUEST_OVERLAY_TRACK_ALL (-1)

struct QuestOverlayState {
    bool enabled;
    int32_t track;        // a QuestId, or QUEST_OVERLAY_TRACK_ALL: every registered quest with a visible block
    int32_t drawnEntries; // what the LAST ImGui frame actually rendered - how a console leg proves the
    int32_t drawnLines;   // overlay drew something, since a marker cannot see a pixel
};

void QuestOverlay_SetEnabled(bool enabled);
void QuestOverlay_SetTrack(int32_t questId); // QUEST_OVERLAY_TRACK_ALL or a QuestId the caller has validated
QuestOverlayState QuestOverlay_Get();

#endif // SOH_RS_QUEST_OVERLAY_H
