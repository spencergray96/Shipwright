/*
 * QuestPage.cpp - the pause scroll's quest page (sturdy-bassoon#111 stage 6). QuestPage.h is the
 * contract; this file is the list, its scrolling, the journal detail view and the word wrap.
 *
 * Everything here draws through RsMenu_DrawText / RsMenu_DrawBar from inside the menu's content
 * interpolation node, never through an OPEN_DISPS of its own (RsMenu.h's page seam) - so no Gfx
 * macro and no display-list pool appears in this file at all.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-20
 */

#include "QuestPage.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "RsMenu.h"
#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestIds.h"
#include "soh/Enhancements/rs/quest/QuestJournal.h"
#include "soh/Enhancements/rs/quest/QuestStore.h"

extern "C" {
#include <z64.h>
#include "macros.h"
}

// ⚠ TEMPORARY - sturdy-bassoon#121. The design lists every PRODUCTION-tier quest, and there is one;
// the debug fixtures (ids 48-52) render too until the production band has enough real quests to be
// worth a list. This is the ONE place the rows are built, so turning this off is the whole fix.
// Never ship a player build with it on: they would see `debug_smoke`.
constexpr bool kListShowsDebugTier = true;

// Test-only rows (`menu filler <n>`). The cap is a sanity bound on a console argument, not a layout
// number - nothing is sized to it.
constexpr int32_t kMaxFiller = 60;

// --- list layout, inside RsMenu_PageRect() (x 52-268, y 51-189 at stage 6) ------------------------
constexpr float kListTitleScale = 1.2f;
constexpr int16_t kListTitleY = 54;
constexpr int16_t kListRuleY = 75; // a thin rule under the title
constexpr float kRowScale = 0.75f;
constexpr int16_t kRowTop = 79;
constexpr int16_t kRowPitch = 13;
constexpr int16_t kArrowIndent = 2;  // the arrow, from the rect's left edge
constexpr int16_t kRowIndent = 14;   // the row text, from the rect's left edge
constexpr int16_t kMoreMarkInset = 10; // the "more this way" marks, from the rect's right edge

// --- journal layout, inside RsMenu_DetailRect() (x 97-224, y 63-176 at kVerticalSpan 184) --------
constexpr float kJournalTitleScale = 0.8f;
constexpr int16_t kJournalTitlePitch = 14;
constexpr float kJournalScale = 0.6f;
constexpr int16_t kJournalPitch = 10;
constexpr int16_t kJournalBlockGap = 4; // extra space above each block after the first
constexpr int16_t kJournalMarkReserve = 10; // right-hand strip kept free for the scroll marks

// Everything a status is drawn and reported as, in ONE table indexed by QuestStatus, so the row
// colour, the dump's `colour=` and the journal's status line cannot disagree. RuneScape's
// convention, with the ticket's amber for "in progress".
struct StatusStyle {
    u8 rgb[3];
    const char* colourName; // the dump's `colour=`
    const char* words;      // the journal's status line
};
constexpr StatusStyle kStatusStyles[] = {
    { { 224, 64, 52 }, "red", "Not started" },
    { { 240, 176, 32 }, "amber", "In progress" },
    { { 84, 204, 84 }, "green", "Complete" },
};
static_assert(sizeof(kStatusStyles) / sizeof(kStatusStyles[0]) == QUEST_STATUS_COUNT,
              "one style per QuestStatus - a new status needs a colour, a name and words");

// A status from the store is validated there, but a filler row's is computed here; out of range
// reads as not started rather than off the end of the table.
static const StatusStyle& StyleOf(int32_t status) {
    return kStatusStyles[status >= 0 && status < QUEST_STATUS_COUNT ? status : QUEST_STATUS_NOT_STARTED];
}
constexpr u8 kArrowColour[3] = { 255, 226, 88 }; // the menu's cursor yellow
constexpr u8 kRuleColour[3] = { 150, 140, 110 }; // the parchment frame's colour
constexpr u8 kTitleColour[3] = { 240, 232, 210 };

// One row of the list, rebuilt every update tick from the live store - a quest started this tick
// turns amber this tick. `nodeId` is the cursor-graph id, which is also what persists the cursor.
struct QuestRow {
    int32_t questId; // -1 for a filler row
    int32_t filler;  // 1-based filler number, 0 for a real quest
    std::string token;
    std::string title;
    std::string nodeId;
    int32_t status;
};

static std::vector<QuestRow> sRows;
static int32_t sRealRows = 0;
static int32_t sFiller = 0;
static int32_t sTop = 0;
static int32_t sPageIndex = -1;
static bool sRegistered = false;

// The row A last went into. Held by identity rather than by index so a list that changes under a
// journal (filler added, say) does not silently swap which journal is on screen.
static int32_t sSelectedQuestId = -1;
static int32_t sSelectedFiller = 0;
static int32_t sJournalTop = 0;
// From the last detail frame: the input tick clamps scrolling against these, because only the
// draw knows how the journal wrapped.
static int32_t sJournalLines = 0;
static int32_t sJournalMaxTop = 0;
static int32_t sJournalDrawn = 0;
static int32_t sJournalWidth = 0;

static std::vector<RsMenuQuestRowInfo> sDrawn;
// Which menu draw frame each of the two views last drew on (RsMenuStatus::drawFrames). The dump
// reports the list's rows and the journal's numbers only when they belong to the LAST drawn frame -
// otherwise a detail view, a roll in flight or another page would still report whatever the list
// drew before it, under a field that promises it is what is on screen.
static int32_t sListDrawFrame = -1;
static int32_t sJournalDrawFrame = -1;

static int32_t VisibleRows() {
    const RsMenuRect rect = RsMenu_PageRect();
    const int32_t fit = (rect.y1 - kRowTop) / kRowPitch;
    return fit > 1 ? fit : 1;
}

static std::string PlainText(const std::vector<QuestRun>& runs) {
    std::string out;
    for (const QuestRun& run : runs) {
        out += run.text;
    }
    return out;
}

// THE ONE PLACE THE ROWS ARE BUILT, and so the one place the tier filter lives (#111's warning:
// QuestJournal_Snapshot is deliberately unfiltered). Id order is the loop order.
static void BuildRows() {
    sRows.clear();
    for (int32_t id = 0; id < QUEST_MAX; id++) {
        const QuestDef* def = Quest_GetDef(id);
        if (def == nullptr) {
            continue;
        }
        if (QUEST_ID_IS_DEBUG(id) && !kListShowsDebugTier) {
            continue;
        }
        std::vector<QuestRun> runs;
        QuestMarkup_Parse(def->title, &runs); // expanded against the floor convention; never re-expanded
        QuestRow row;
        row.questId = id;
        row.filler = 0;
        row.token = def->name;
        row.title = PlainText(runs);
        row.nodeId = "quest_" + row.token;
        row.status = QuestStore_GetStatus(id);
        sRows.push_back(row);
    }
    sRealRows = (int32_t)sRows.size();
    for (int32_t i = 1; i <= sFiller; i++) {
        char token[32];
        char title[48];
        std::snprintf(token, sizeof(token), "filler_%02d", i);
        std::snprintf(title, sizeof(title), "Filler quest %02d", i);
        QuestRow row;
        row.questId = -1;
        row.filler = i;
        row.token = token;
        row.title = title;
        row.nodeId = std::string("quest_") + token;
        // Cycling through all three, so a filler list exercises every row colour.
        row.status = (i - 1) % QUEST_STATUS_COUNT;
        sRows.push_back(row);
    }
}

static int32_t RowOfNode(const std::string& nodeId) {
    for (int32_t i = 0; i < (int32_t)sRows.size(); i++) {
        if (sRows[(size_t)i].nodeId == nodeId) {
            return i;
        }
    }
    return -1;
}

// Rando::Kaleido's follow rule (kaleido.cpp:271-293), as one function of the cursor rather than
// spread across four input branches: the view moves only as far as it must to keep the cursor's row
// on screen. Plus the clamp kaleido does not need because its list never shrinks - `menu filler 0`
// can leave the view scrolled past the new end.
static void FollowCursor(int32_t cursorRow) {
    const int32_t count = (int32_t)sRows.size();
    const int32_t visible = VisibleRows();
    if (cursorRow >= 0) {
        if (cursorRow < sTop) {
            sTop = cursorRow;
        } else if (cursorRow >= sTop + visible) {
            sTop = cursorRow - visible + 1;
        }
    }
    sTop = std::min(sTop, std::max(0, count - visible));
    sTop = std::max(sTop, 0);
}

// The row the cursor is on, or -1 when it is on a hand (or the graph is empty).
static int32_t CursorRow() {
    const RsMenuCursorNode* node = RsMenu_CursorAt(RsMenu_CursorIndex());
    return node != nullptr && node->hand < 0 ? RowOfNode(node->id) : -1;
}

static int16_t RowY(int32_t row) {
    return (int16_t)(kRowTop + (row - sTop) * kRowPitch);
}

// --- the cursor graph ------------------------------------------------------------------------------

// Every row is a node, visible or not - the graph is the list, and the view follows the cursor
// rather than the graph being cut down to the view. A row off screen carries the box it WOULD have,
// which nothing draws (this page owns its highlight) and a console line can still print.
//
// The hands lead back to the row the cursor LAST STOOD ON, the way kaleido's page arrows hand back
// mCursorPos - stepping onto a hand and straight back off it returns you where you were. When that
// row has scrolled out of view (or there is none yet) it is the top visible row instead, so a hand
// never leads to something off screen.
static int32_t sLastRow = -1;

static void QuestPageNodes(int32_t pageIndex, void* userData) {
    (void)pageIndex;
    (void)userData;
    BuildRows();
    const int32_t cursorRow = RowOfNode(RsMenu_CursorId());
    if (cursorRow >= 0) {
        sLastRow = cursorRow;
    }
    FollowCursor(cursorRow);
    const int32_t entryRow =
        sLastRow >= sTop && sLastRow < sTop + VisibleRows() && sLastRow < (int32_t)sRows.size() ? sLastRow : sTop;
    const RsMenuRect rect = RsMenu_PageRect();
    int32_t entry = -1;
    for (int32_t i = 0; i < (int32_t)sRows.size(); i++) {
        const int32_t index = RsMenu_AddCursorNode(sRows[(size_t)i].nodeId.c_str(), rect.x0, RowY(i),
                                                   (int16_t)(rect.x1 - rect.x0), kRowPitch);
        if (i == entryRow) {
            entry = index;
        }
    }
    if (entry >= 0) {
        RsMenu_SetCursorColumn(entry);
    }
}

// --- drawing -----------------------------------------------------------------------------------------

// Cut to fit, with a trailing "..." - a long title must never run under the right hand.
static std::string FitText(const std::string& text, float scale, float width) {
    if (RsMenu_TextWidth(text.c_str(), scale) <= width) {
        return text;
    }
    std::string cut = text;
    while (!cut.empty() && RsMenu_TextWidth((cut + "...").c_str(), scale) > width) {
        cut.pop_back();
    }
    return cut + "...";
}

static void QuestPageDraw(struct PlayState* play, int32_t pageIndex, void* userData) {
    (void)play;
    (void)pageIndex;
    (void)userData;
    const RsMenuRect rect = RsMenu_PageRect();
    const int16_t centreX = (int16_t)((rect.x0 + rect.x1) / 2);
    sDrawn.clear();
    sListDrawFrame = RsMenu_Status().drawFrames;

    RsMenu_DrawTextCentred("Quests", centreX, kListTitleY, kListTitleScale, kTitleColour[0], kTitleColour[1],
                           kTitleColour[2], 255);
    RsMenu_DrawBar((int16_t)(rect.x0 + 8), kListRuleY, (int16_t)(rect.x1 - 8), (int16_t)(kListRuleY + 1),
                   kRuleColour[0], kRuleColour[1], kRuleColour[2]);

    if (sRows.empty()) {
        const QuestJournalColour meta = QuestJournal_MetaColour();
        RsMenu_DrawTextCentred("No quests", centreX, kRowTop, kRowScale, meta.r, meta.g, meta.b, 255);
        return;
    }

    const int32_t cursorRow = CursorRow();
    const int32_t visible = VisibleRows();
    const int16_t textX = (int16_t)(rect.x0 + kRowIndent);
    const float textW = (float)(rect.x1 - kMoreMarkInset - 2 - textX);
    const int32_t end = std::min((int32_t)sRows.size(), sTop + visible);
    for (int32_t i = sTop; i < end; i++) {
        const QuestRow& row = sRows[(size_t)i];
        const int32_t status = row.status >= 0 && row.status < QUEST_STATUS_COUNT ? row.status : 0;
        const u8* colour = StyleOf(status).rgb;
        const int16_t y = RowY(i);
        if (i == cursorRow) {
            // Rough on purpose (#111 comment 10): a glyph, not a model. Kaleido's real arrow cursor
            // (gArrowCursorTex, kaleido.cpp:94) is the upgrade when the models come.
            RsMenu_DrawText(">", (int16_t)(rect.x0 + kArrowIndent), y, kRowScale, kArrowColour[0], kArrowColour[1],
                            kArrowColour[2], 255);
        }
        RsMenu_DrawText(FitText(row.title, kRowScale, textW).c_str(), textX, y, kRowScale, colour[0], colour[1],
                        colour[2], 255);
        RsMenuQuestRowInfo info;
        info.row = i;
        info.questId = row.questId;
        info.token = row.token;
        info.status = status;
        sDrawn.push_back(info);
    }

    // "More this way" marks, beside the first and last visible rows.
    const QuestJournalColour meta = QuestJournal_MetaColour();
    const int16_t markX = (int16_t)(rect.x1 - kMoreMarkInset);
    if (sTop > 0) {
        RsMenu_DrawText("^", markX, RowY(sTop), kRowScale, meta.r, meta.g, meta.b, 255);
    }
    if (end < (int32_t)sRows.size()) {
        RsMenu_DrawText("v", markX, RowY(end - 1), kRowScale, meta.r, meta.g, meta.b, 255);
    }
}

// --- the journal ----------------------------------------------------------------------------------
//
// NOTHING IN quest/ WRAPS TEXT, so this does. Runs are split into WORDS at spaces, and a word may
// carry more than one colour ("#item:Egg#s" is one word in two colours), so a word is a list of
// pieces. Words are laid out greedily; a word wider than the whole line is broken by character.
// Each wrapped line becomes a list of segments - same-coloured text at a pen position - which is
// what RsMenu_DrawText draws. Spaces inside a segment are kept, because RsMenu_DrawText advances the
// pen over a space without drawing it, so they cost no vertices.

struct JPiece {
    std::string text;
    QuestJournalColour colour;
};
struct JSeg {
    std::string text;
    QuestJournalColour colour;
    float x;
};
struct JLine {
    std::vector<JSeg> segs;
    float scale;
    int16_t pitch;
    int16_t gapAbove;
    bool struck;   // draw a rule through it
    float width;   // how far its last segment reaches, for the rule
};

static bool SameColour(const QuestJournalColour& a, const QuestJournalColour& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

// Wraps one source line (a run list, all at one scale) into `out`. `prefix` is drawn on the first
// wrapped line only and every continuation is indented by its width - a hanging indent, so a
// checklist item's wrapped tail lines up under its own text rather than under the bullet.
static void WrapRuns(const std::vector<QuestRun>& runs, const std::string& prefix, bool struck, float scale,
                     int16_t pitch, int16_t gapAbove, float width, std::vector<JLine>& out) {
    // 1. Words, as colour pieces.
    std::vector<std::vector<JPiece>> words;
    bool inWord = false;
    for (const QuestRun& run : runs) {
        const QuestJournalColour colour = QuestJournal_RunColour(QuestJournal_StyleEmphasis(run.style), struck);
        for (const char c : run.text) {
            if (c == ' ') {
                inWord = false;
                continue;
            }
            if (!inWord) {
                words.emplace_back();
                inWord = true;
            }
            std::vector<JPiece>& word = words.back();
            if (word.empty() || !SameColour(word.back().colour, colour)) {
                word.push_back({ std::string(), colour });
            }
            word.back().text += c;
        }
    }

    const float space = RsMenu_TextWidth(" ", scale);
    const float indent = RsMenu_TextWidth(prefix.c_str(), scale);
    JLine line;
    line.scale = scale;
    line.pitch = pitch;
    line.gapAbove = gapAbove;
    line.struck = struck;
    line.width = 0.0f;
    float pen = 0.0f;
    if (!prefix.empty()) {
        line.segs.push_back({ prefix, QuestJournal_RunColour(QUEST_EMPHASIS_NONE, struck), 0.0f });
        pen = indent;
    }
    bool lineHasWord = false;
    // A space owed between the previous word and the next piece. Held rather than drawn, so a word
    // that wraps does not carry a leading space onto its new line.
    bool pendingSpace = false;

    auto emit = [&](const std::string& text, const QuestJournalColour& colour) {
        // Glue onto the last segment when it is the same colour, so a line is a few segments rather
        // than one per word - fewer SetPrimColor words, same glyphs. The space goes into the
        // segment's text, which RsMenu_DrawText advances over exactly as `pen` did.
        if (!line.segs.empty() && SameColour(line.segs.back().colour, colour)) {
            if (pendingSpace) {
                line.segs.back().text += ' ';
            }
            line.segs.back().text += text;
        } else {
            line.segs.push_back({ text, colour, pen });
        }
        pendingSpace = false;
        pen += RsMenu_TextWidth(text.c_str(), scale);
        line.width = pen;
    };
    auto breakLine = [&]() {
        out.push_back(line);
        line.segs.clear();
        line.gapAbove = 0;
        line.width = 0.0f;
        pen = indent;
        lineHasWord = false;
        pendingSpace = false;
    };

    for (const std::vector<JPiece>& word : words) {
        float wordW = 0.0f;
        for (const JPiece& piece : word) {
            wordW += RsMenu_TextWidth(piece.text.c_str(), scale);
        }
        if (lineHasWord && pen + space + wordW > width) {
            breakLine();
        }
        if (lineHasWord) {
            pen += space;
            pendingSpace = true;
        }
        if (pen + wordW <= width) {
            for (const JPiece& piece : word) {
                emit(piece.text, piece.colour);
            }
        } else {
            // Wider than a whole line: break it by character.
            for (const JPiece& piece : word) {
                for (const char c : piece.text) {
                    const std::string ch(1, c);
                    const float w = RsMenu_TextWidth(ch.c_str(), scale);
                    if (pen + w > width && pen > indent) {
                        breakLine();
                    }
                    emit(ch, piece.colour);
                }
            }
        }
        lineHasWord = true;
    }
    out.push_back(line);
}

static QuestRun Run(const char* text, QuestRunStyle style) {
    QuestRun run;
    run.text = text;
    run.style = style;
    return run;
}

// A filler row's journal: synthetic, deliberately long enough to scroll, and carrying every run
// colour so a screenshot of it checks the colour table too. Not markup, so it cannot fail to parse.
static void BuildFillerJournal(int32_t filler, QuestJournalEntry* entry) {
    char title[48];
    std::snprintf(title, sizeof(title), "Filler quest %02d", filler);
    entry->questId = -1;
    entry->name = "filler";
    entry->status = (filler - 1) % QUEST_STATUS_COUNT;
    entry->title = { Run(title, QUEST_RUN_PLAIN) };
    entry->lines.clear();

    QuestJournalLine line;
    line.blockIndex = 0;
    line.step = -1;
    line.checked = false;
    line.kind = QUEST_LINE_PARAGRAPH;
    line.runs = { Run("A test row made by the menu filler command, so the list has more rows than fit and this "
                      "journal has more lines than the page.",
                      QUEST_RUN_PLAIN) };
    entry->lines.push_back(line);
    line.runs = { Run("Bring the ", QUEST_RUN_PLAIN),
                  Run("bucket of milk", QUEST_RUN_ITEM),
                  Run(" and the ", QUEST_RUN_PLAIN),
                  Run("pot of flour", QUEST_RUN_ITEM),
                  Run(" to the ", QUEST_RUN_PLAIN),
                  Run("cook in the castle kitchen", QUEST_RUN_HINT),
                  Run(".", QUEST_RUN_PLAIN) };
    entry->lines.push_back(line);
    line.kind = QUEST_LINE_CHECK_ITEM;
    const char* steps[] = { "Collect an egg from the farm.", "Milk a dairy cow.", "Grind wheat at the mill." };
    for (int32_t s = 0; s < 3; s++) {
        line.runs = { Run(steps[s], QUEST_RUN_PLAIN) };
        line.step = s;
        line.checked = s == 0;
        entry->lines.push_back(line);
    }
    line.kind = QUEST_LINE_PARAGRAPH;
    line.step = -1;
    line.checked = false;
    for (int32_t p = 1; p <= 3; p++) {
        char text[160];
        std::snprintf(text, sizeof(text),
                      "Scroll test paragraph %d. Press down to move the page one line at a time, and up to come "
                      "back.",
                      p);
        line.runs = { Run(text, QUEST_RUN_PLAIN) };
        entry->lines.push_back(line);
    }
}

// The whole journal as wrapped lines: title, status, then every visible block.
static void BuildJournalLines(const QuestJournalEntry& entry, float width, std::vector<JLine>& out) {
    out.clear();
    WrapRuns(entry.title, "", false, kJournalTitleScale, kJournalTitlePitch, 0, width, out);
    WrapRuns({ Run(StyleOf(entry.status).words, QUEST_RUN_PLAIN) }, "", false, kJournalScale, kJournalPitch, 0, width,
             out);
    // The status line is meta text, not authored prose, so it takes the meta colour rather than
    // the plain one.
    for (JSeg& seg : out.back().segs) {
        seg.colour = QuestJournal_MetaColour();
    }
    if (entry.lines.empty()) {
        WrapRuns({ Run("Nothing yet.", QUEST_RUN_PLAIN) }, "", false, kJournalScale, kJournalPitch, kJournalBlockGap,
                 width, out);
        for (JSeg& seg : out.back().segs) {
            seg.colour = QuestJournal_MetaColour();
        }
        return;
    }
    int32_t lastBlock = -1;
    for (const QuestJournalLine& line : entry.lines) {
        // A gap above each new block; checklist rows of one block sit tight together.
        const int16_t gap = line.blockIndex != lastBlock || line.kind == QUEST_LINE_PARAGRAPH ? kJournalBlockGap : 0;
        lastBlock = line.blockIndex;
        const bool struck = line.kind == QUEST_LINE_CHECK_ITEM && line.checked;
        WrapRuns(line.runs, line.kind == QUEST_LINE_CHECK_ITEM ? "- " : "", struck, kJournalScale, kJournalPitch, gap,
                 width, out);
    }
}

// How far down the journal can be scrolled: the smallest first line from which everything left
// still fits the page height. Walked back from the end, because line heights differ.
static int32_t JournalMaxTop(const std::vector<JLine>& lines, int32_t height) {
    int32_t used = 0;
    int32_t top = (int32_t)lines.size();
    while (top > 0) {
        const JLine& line = lines[(size_t)(top - 1)];
        const int32_t h = line.pitch + (top - 1 == 0 ? 0 : line.gapAbove);
        if (used + h > height) {
            break;
        }
        used += h;
        top--;
    }
    return top;
}

static void QuestPageDetailDraw(struct PlayState* play, int32_t pageIndex, void* userData) {
    (void)play;
    (void)pageIndex;
    (void)userData;
    const RsMenuRect rect = RsMenu_DetailRect();
    const float width = (float)(rect.x1 - rect.x0 - kJournalMarkReserve);
    const int32_t height = rect.y1 - rect.y0;

    // Resolved LIVE, every frame - predicates are evaluated per call and two frames may differ,
    // which is the journal's accumulation model working as designed (QuestJournal.h, D15).
    QuestJournalEntry entry;
    bool have = false;
    if (sSelectedFiller > 0) {
        BuildFillerJournal(sSelectedFiller, &entry);
        have = true;
    } else if (sSelectedQuestId >= 0) {
        have = QuestJournal_Build(sSelectedQuestId, &entry);
    }
    std::vector<JLine> lines;
    if (have) {
        BuildJournalLines(entry, width, lines);
    } else {
        WrapRuns({ Run("No quest selected.", QUEST_RUN_PLAIN) }, "", false, kJournalScale, kJournalPitch, 0, width,
                 lines);
    }

    sJournalLines = (int32_t)lines.size();
    sJournalMaxTop = JournalMaxTop(lines, height);
    sJournalTop = std::min(std::max(sJournalTop, 0), sJournalMaxTop);
    sJournalWidth = (int32_t)width;
    sJournalDrawFrame = RsMenu_Status().drawFrames;
    sJournalDrawn = 0;

    float y = (float)rect.y0;
    int32_t last = sJournalTop - 1;
    for (int32_t i = sJournalTop; i < (int32_t)lines.size(); i++) {
        const JLine& line = lines[(size_t)i];
        const float gap = i == sJournalTop ? 0.0f : (float)line.gapAbove;
        if (y + gap + (float)line.pitch > (float)rect.y1 + 0.5f) {
            break;
        }
        y += gap;
        for (const JSeg& seg : line.segs) {
            RsMenu_DrawText(seg.text.c_str(), (int16_t)(rect.x0 + seg.x), (int16_t)y, line.scale, seg.colour.r,
                            seg.colour.g, seg.colour.b, 255);
        }
        if (line.struck && !line.segs.empty() && line.width > line.segs.front().x) {
            // The rule through a ticked step, which the ImGui overlay draws too. From the line's
            // first glyph rather than the margin, so a wrapped tail's rule starts under its text and
            // not across the hanging indent. Half a glyph cell down is roughly mid x-height.
            const QuestJournalColour c = QuestJournal_RunColour(QUEST_EMPHASIS_NONE, true);
            const int16_t ry = (int16_t)(y + 16.0f * line.scale * 0.5f);
            RsMenu_DrawBar((int16_t)(rect.x0 + line.segs.front().x), ry, (int16_t)(rect.x0 + line.width),
                           (int16_t)(ry + 1), c.r, c.g, c.b);
        }
        y += (float)line.pitch;
        sJournalDrawn++;
        last = i;
    }

    const QuestJournalColour meta = QuestJournal_MetaColour();
    const int16_t markX = (int16_t)(rect.x1 - kJournalMarkReserve + 2);
    if (sJournalTop > 0) {
        RsMenu_DrawText("^", markX, rect.y0, kJournalScale, meta.r, meta.g, meta.b, 255);
    }
    if (last + 1 < (int32_t)lines.size()) {
        RsMenu_DrawText("v", markX, (int16_t)(rect.y1 - kJournalPitch), kJournalScale, meta.r, meta.g, meta.b, 255);
    }
}

// --- selection and input -----------------------------------------------------------------------------

static bool QuestPageSelect(int32_t pageIndex, const RsMenuCursorNode* node, void* userData) {
    (void)pageIndex;
    (void)userData;
    const int32_t row = node != nullptr ? RowOfNode(node->id) : -1;
    if (row < 0) {
        return false;
    }
    sSelectedQuestId = sRows[(size_t)row].questId;
    sSelectedFiller = sRows[(size_t)row].filler;
    sJournalTop = 0;
    return true;
}

static void QuestPageInput(int32_t pageIndex, int32_t level, uint16_t press, int32_t navY, void* userData) {
    (void)pageIndex;
    (void)userData;
    if (level != 0) {
        // The journal scrolls by line, clamped against the last frame's wrap.
        if (navY != 0) {
            sJournalTop = std::min(std::max(sJournalTop + navY, 0), sJournalMaxTop);
        }
        return;
    }
    // C-left / C-right page the list by a screenful - Rando::Kaleido's paging (kaleido.cpp:295-322).
    // Only from a row; on a hand there is no row to page from.
    const int32_t row = CursorRow();
    if (row < 0 || sRows.empty()) {
        return;
    }
    int32_t target = row;
    if (CHECK_BTN_ALL(press, BTN_CLEFT)) {
        target = std::max(0, row - VisibleRows());
    } else if (CHECK_BTN_ALL(press, BTN_CRIGHT)) {
        target = std::min((int32_t)sRows.size() - 1, row + VisibleRows());
    }
    if (target != row) {
        RsMenu_SetCursorById(sRows[(size_t)target].nodeId.c_str());
    }
}

// --- public ----------------------------------------------------------------------------------------

RsMenuQuestPageStatus RsMenuQuestPage_Status() {
    RsMenuQuestPageStatus status;
    status.rows = (int32_t)sRows.size();
    status.realRows = sRealRows;
    status.fillerRows = sFiller;
    status.visible = VisibleRows();
    status.top = sTop;
    status.cursorRow = RsMenu_CurrentPage() == sPageIndex ? CursorRow() : -1;
    status.showsDebugTier = kListShowsDebugTier;
    // Only what the last drawn frame drew (see sListDrawFrame); empty when the list was not on it.
    const int32_t lastFrame = RsMenu_Status().drawFrames;
    if (sListDrawFrame == lastFrame) {
        status.drawn = sDrawn;
    }
    const bool journalOnScreen = sJournalDrawFrame == lastFrame;
    status.selectedRow = -1;
    for (int32_t i = 0; i < (int32_t)sRows.size(); i++) {
        const QuestRow& row = sRows[(size_t)i];
        if (row.questId == sSelectedQuestId && row.filler == sSelectedFiller) {
            status.selectedRow = i;
            status.selectedToken = row.token;
        }
    }
    status.selectedQuestId = sSelectedQuestId;
    status.journalLines = sJournalLines;
    status.journalTop = sJournalTop;
    status.journalDrawn = journalOnScreen ? sJournalDrawn : 0;
    status.journalMaxTop = sJournalMaxTop;
    status.journalWidth = sJournalWidth;
    return status;
}

bool RsMenuQuestPage_SetFiller(int32_t count) {
    if (count < 0 || count > kMaxFiller) {
        return false;
    }
    sFiller = count;
    BuildRows();
    FollowCursor(-1);
    return true;
}

int32_t RsMenuQuestPage_MaxFiller() {
    return kMaxFiller;
}

const char* RsMenuQuestPage_StatusColourName(int32_t status) {
    return StyleOf(status).colourName;
}

void RsMenuQuestPage_Register() {
    if (sRegistered) {
        return;
    }
    sRegistered = true;
    RsMenuPage page;
    page.id = "quests";
    page.title = "Quests";
    page.draw = QuestPageDraw;
    page.nodes = QuestPageNodes;
    page.userData = nullptr;
    page.select = QuestPageSelect;
    page.detailDraw = QuestPageDetailDraw;
    page.input = QuestPageInput;
    page.ownsItemHighlight = true;
    sPageIndex = RsMenu_RegisterPageStruct(page);
}
