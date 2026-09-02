#ifndef SOH_RS_QUEST_JOURNAL_H
#define SOH_RS_QUEST_JOURNAL_H

#include <stdint.h>
#include <string>
#include <vector>

#include "QuestJournalDef.h"

// The journal READ API (sturdy-bassoon#58 P2 / #62, decisions D15/D23): the markup parser, and
// the one builder both consumers call.
//
// C++ ONLY, no extern "C" shim - a deliberate departure from D5's habit. D5's shim exists so
// idiomatic-C actors can call the quest API; no actor renders a journal, and every consumer of
// this file (QuestConsole, Quest.cpp's registration gate, the eventual ImGui overlay) is C++.
// The DEFINITION side stays plain C in QuestJournalDef.h, which is the half actors include.
//
// THE RETURN TYPE IS RUNS, NEVER STRINGS (D23). This is settled here, before anything consumes
// it, because it is the return type and not a formatting detail: handing back std::string and
// retrofitting styling later changes every consumer's signature. It is also what lets a console
// with no colour at all validate the model - printing `%rEgg%w` would prove nothing, whereas
// printing `[item:Egg]` proves the runs are right.

// --- markup ------------------------------------------------------------------------------------
//
// Grammar: `#tag:text#` spans mixed into plain prose. Tags are exactly `item`, `npc`, `place`,
// `hint`, lowercase, untrimmed. The tag/text split is at the FIRST ':' inside the span, so a colon
// in the prose is fine ("#hint:go north: then east#").
//
// THERE IS NO ESCAPE FOR A LITERAL '#'. That is the point: it makes a stray '#' unambiguously an
// error instead of a guess, and a mis-tagged span can never quietly render as literal prose - the
// silent-failure class this project keeps stamping out. (If an escape is ever needed it must be
// something other than `##`, which already means two adjacent spans.)
//
// Scanning is a single left-to-right pass and the FIRST error wins; `pos` is the byte offset the
// scanner was standing on when it gave up.
enum QuestMarkupError {
    QUEST_MARKUP_OK = 0,
    QUEST_MARKUP_UNCLOSED = 1,    // a '#' with no later '#'. This is also the stray-'#' case.
    QUEST_MARKUP_MISSING_TAG = 2, // `#Egg#` - no ':' in the span. The bare in-tree CustomMessage
                                  // form, so it is the likeliest author mistake of all.
    QUEST_MARKUP_UNKNOWN_TAG = 3, // `#thing:x#`, and also `#ITEM:x#` / `#item :x#` (exact match)
    QUEST_MARKUP_EMPTY_TAG = 4,   // `#:x#`
    QUEST_MARKUP_EMPTY_TEXT = 5,  // `#item:#` - an empty run is always a typo
    QUEST_MARKUP_BAD_CHAR = 6,    // '%', '"', or any of \n \r \t - see below
    QUEST_MARKUP_NULL_TEXT = 7,   // a NULL where display text was required
    QUEST_MARKUP_ERROR_COUNT,
};

// Why BAD_CHAR refuses those four:
//   '%'  the ImGui console hands a command handler's output to vsnprintf as the FORMAT string
//        (ConsoleWindow::Append), so a stray '%' reads whatever is next on the stack.
//   '"'  every console line here is `key="value"`; an embedded quote breaks the grep contract
//        that both surfaces and every acceptance script depend on. Apostrophes are fine.
//   \n \r \t  lines must stay single-line and greppable.
struct QuestMarkupResult {
    QuestMarkupError error;
    int32_t pos; // byte offset of the failure; 0 when error == QUEST_MARKUP_OK
};

struct QuestRun {
    std::string text;
    QuestRunStyle style;
};

// Scans without allocating. Used by the registration gate, so it must stay silent: it LOGS
// NOTHING and asserts nothing - it reports. Quest_Register is what shouts.
QuestMarkupResult QuestMarkup_Validate(const char* text);

// Validate-then-emit. On ANY error `out` is left EMPTY - there is no literal-prose fallback
// anywhere in this file, which is what makes a malformed span impossible to miss. Adjacent plain
// text is coalesced, and a run is never empty. Also silent.
QuestMarkupResult QuestMarkup_Parse(const char* text, std::vector<QuestRun>* out);

const char* QuestMarkup_ErrorName(QuestMarkupError error); // "unclosed", "unknown_tag", ...
const char* QuestJournal_StyleName(QuestRunStyle style);   // "plain", "item", "npc", ...
QuestRunEmphasis QuestJournal_StyleEmphasis(QuestRunStyle style);
const char* QuestJournal_EmphasisName(QuestRunEmphasis emphasis); // "none", "key", "guide"

// --- the resolved journal ------------------------------------------------------------------------

enum QuestJournalLineKind {
    QUEST_LINE_PARAGRAPH = 0,  // a paragraph block, or a checklist's lead-in
    QUEST_LINE_CHECK_ITEM = 1, // one checklist row
};

struct QuestJournalLine {
    QuestJournalLineKind kind;
    std::vector<QuestRun> runs;
    int32_t blockIndex; // which block in the definition produced this line
    int32_t step;       // CHECK_ITEM: the step it tracks, or -1. PARAGRAPH: always -1.
    bool checked;       // CHECK_ITEM: that step is set right now
};

struct QuestJournalEntry {
    int32_t questId;
    const char* name; // the definition's token; borrowed, valid as long as the build is
    int32_t status;
    std::vector<QuestRun> title;
    std::vector<QuestJournalLine> lines;
    int32_t blockCount;   // blocks in the definition
    int32_t visibleCount; // blocks whose `when` is true right now
};

// D15, the per-quest getter. False for an invalid or unregistered id, with `out` untouched; no
// assert, so a console surface can call it on anything. Every predicate is evaluated live, so two
// calls a frame apart can legitimately differ - that IS the accumulation model.
//
// A block whose markup fails to parse cannot get here: Quest_Register refuses the definition. If
// one somehow does, its line renders as a single QUEST_RUN_PLAIN run reading `<markup error: ...>`
// rather than as the raw prose - still impossible to mistake for authored text.
bool QuestJournal_Build(int32_t questId, QuestJournalEntry* out);

// D15, the snapshot builder. EVERY registered quest, in id order, unfiltered - a quest with no
// visible block comes back as an entry with `visibleCount == 0` and no lines. Filtering ("only
// started") is a display policy and belongs to the caller; baking it in here would be one more
// rule to get wrong, and the console can drop empties itself.
std::vector<QuestJournalEntry> QuestJournal_Snapshot();

// --- rendering helpers (shared by both console sinks, D18) -------------------------------------

// Runs joined into one line, spans shown as `[item:Egg]`: what proves the run list is right on a
// surface with no colour. Plain runs are emitted bare.
std::string QuestJournal_RenderInline(const std::vector<QuestRun>& runs);

#endif // SOH_RS_QUEST_JOURNAL_H
