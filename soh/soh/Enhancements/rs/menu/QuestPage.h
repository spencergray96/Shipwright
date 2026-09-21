#ifndef SOH_RS_MENU_QUEST_PAGE_H
#define SOH_RS_MENU_QUEST_PAGE_H

#include <stdint.h>
#include <string>
#include <vector>

// The quest page of the pause scroll (sturdy-bassoon#111 stage 6): the first page in the ring, and
// the first one with anything to select.
//
// LEVEL 0 IS THE LIST. One row per quest, in QUEST ID ORDER, coloured by status - red not started,
// amber in progress, green complete - and nothing else: no step indicator, because the concept
// does not exist in the quest data and RuneScape does not show one. A larger title sits above the
// rows and an arrow sits to the left of the row the cursor is on. The list scrolls DISCRETELY,
// kaleido-style (Rando::Kaleido, Enhancements/kaleido.cpp:252-360 - its logic copied, not its
// drawing): pressing down on the bottom visible row shifts every row up one, C-left/C-right page by
// a screenful. The rows are a COLUMN in the menu's cursor graph (RsMenu_SetCursorColumn): up/down
// walk the rows, left/right from any row reach the hands, and the hands lead back to the row the
// cursor last stood on (the top visible row, if that one has scrolled away) - kaleido's rule too.
//
// LEVEL 1 IS THE JOURNAL. A on a row runs the menu's down-a-level animation and then draws that
// quest's journal, upright, inside the vertical parchment (RsMenu_DetailRect): the title, a status
// line, then every visible block, word-wrapped to the page width and coloured by run emphasis with
// the journal's own table (QuestJournal_RunColour - the one the ImGui overlay uses). A journal
// taller than the page SCROLLS BY LINE with up/down, the same discrete rule as the list, with a
// marker at the top or bottom edge while there is more that way. B goes back up.
//
// WHICH QUESTS ARE ROWS - READ THIS BEFORE SHIPPING. The design says "every production-tier quest",
// and there is exactly ONE (Cook's Assistant, id 0); ids 48-52 are debug fixtures. Stage 6 took the
// route of letting the DEBUG TIER RENDER TOO, TEMPORARILY, rather than reserving production ids for
// placeholder quests - production ids are permanent save-format commitments and the list's display
// order forever (QuestIds.h), which is a content decision, not a menu one. So QuestPage.cpp carries
// one switch, kListShowsDebugTier, in the one function that builds the rows, and it must be turned
// off before a player ever sees `debug_smoke` (sturdy-bassoon issue linked beside the switch).
//
// SCROLLING NEEDS MORE ROWS THAN THAT, so there is a test-only row source: `menu filler <n>` appends
// n synthetic rows after the real ones (statuses cycling not-started / in-progress / complete, each
// with a synthetic journal long enough to scroll). Not a CVar, the house rule for diagnostics - a
// session that wedges cannot leave filler rows in the owner's config - and never on by default.

struct RsMenuQuestRowInfo {
    int32_t row;       // 0-based position in the whole list
    int32_t questId;   // -1 for a filler row
    std::string token; // the quest's definition name, or filler_NN; no spaces
    int32_t status;    // QuestStatus
};

// Everything `menu dump` says about this page. `drawn` is what the LAST DRAWN FRAME put on screen,
// in screen order, which is what a screenshot is asserted against - and EMPTY when that frame did
// not draw the list (a detail view, a roll or a level change in flight, another page). Likewise
// `journalDrawn` is 0 unless the last frame drew the journal. The rest is live state. (What the
// journal costs in glyphs is the menu's own `section=draw glyphs=`; per-view counters are stage 7's.)
struct RsMenuQuestPageStatus {
    int32_t rows;        // the whole list, real + filler
    int32_t realRows;
    int32_t fillerRows;
    int32_t visible;     // how many rows fit on the page
    int32_t top;         // 0-based index of the first visible row
    int32_t cursorRow;   // 0-based row the cursor is on, -1 when it is on a hand
    bool showsDebugTier;
    std::vector<RsMenuQuestRowInfo> drawn;
    // The journal (level 1). `selected*` is the row A last went into.
    int32_t selectedRow;
    int32_t selectedQuestId;
    std::string selectedToken;
    int32_t journalLines;   // wrapped lines in the whole journal, title and status included
    int32_t journalTop;     // first wrapped line on screen
    int32_t journalDrawn;   // wrapped lines the last detail frame drew
    int32_t journalMaxTop;  // how far it can scroll; 0 means it fits
    int32_t journalWidth;   // the wrap width, in game units
};
RsMenuQuestPageStatus RsMenuQuestPage_Status();

// 0..kMaxFiller; false (and nothing changed) outside it.
bool RsMenuQuestPage_SetFiller(int32_t count);
int32_t RsMenuQuestPage_MaxFiller();

const char* RsMenuQuestPage_StatusColourName(int32_t status); // "red", "amber", "green"

// Registers the page, once. Called by RsMenu.cpp's ShipInit, first, so it is page 1 of the ring.
void RsMenuQuestPage_Register();

#endif // SOH_RS_MENU_QUEST_PAGE_H
