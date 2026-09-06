#ifndef SOH_RS_QUEST_JOURNAL_DEF_H
#define SOH_RS_QUEST_JOURNAL_DEF_H

#include <stdint.h>
#include "QuestPredicate.h"

// The journal BLOCK MODEL (sturdy-bassoon#58 P2 / #62, decisions D13/D14/D23), as plain C data so
// it sits inside QuestDef and a C actor including QuestDef.h still compiles. The parser and the
// read API that turn this into styled runs are QuestJournal.h, which is C++.
//
// A quest's journal is an ORDERED LIST of blocks. A block is `{when, content}`: it is visible
// exactly when every predicate in `when` is true, evaluated live against the stores every time
// the journal is read. Content is either a paragraph or a checklist whose items name a step, so a
// collected step strikes its item through.
//
// D14 - ACCUMULATION IS EMERGENT, AND THERE IS DELIBERATELY NO `replaces` FIELD.
// A block gated on "started" stays true forever and so persists. A block gated on "started AND
// NOT all collected" goes false on its own and vanishes, with nothing pushing it out. If you ever
// find yourself wanting a block to explicitly supersede another, the gate on one of them is
// wrong - that is the whole reason `Not` and `AllStepsSet` are in the predicate vocabulary.
//
// D13 - why not per-state text snapshots: three items in any order is already 8 reachable step
// states times 3 statuses, and it explodes from there. Why not a text-assembling callback: opaque
// output that nothing can introspect, print or diff.

// Every display string in a QuestDef is MARKUP: `#tag:text#` spans mixed into plain prose (D23).
// A span carries one of these styles; everything outside a span is PLAIN.
//
// Tags are ANNOTATIONS, NOT REFERENCES. `#npc:the Cook#` does not resolve to an NpcId and never
// will - it is prose the author marked as npc-ish. That is the load-bearing property: it keeps
// the parser trivial, keeps every tag uniform, and makes `hint` - guidance pointing at nothing
// that exists in code - the normal case rather than an exception.
//
// FOUR TAGS, THREE STYLES: `npc` and `place` are PARSER ALIASES OF `hint` since P4 (2026-09-03).
// That is D23's exit condition firing, not an oversight. The bet was that `npc`/`place` would earn
// a distinct rendering by the time the first real quest shipped; P4 shipped and they were still
// mapped to the same emphasis as `hint`, so they collapsed - the check was reading
// QuestJournal_StyleEmphasis, exactly as the ADR said it would be.
//
// The TAGS stay accepted, which is the half of the bet that survives: an author still writes
// `#npc:the Cook#` while the sentence is being written, and un-collapsing later is
// (1) add the enumerator back here, (2) point `kTags` in QuestJournal.cpp at it, (3) give it an
// emphasis. Re-deriving which spans were npc-ish from finished prose is the expensive half, and
// that work is not being thrown away.
typedef enum QuestRunStyle {
    QUEST_RUN_PLAIN = 0, // untagged prose
    QUEST_RUN_ITEM = 1,  // a thing to obtain or carry
    QUEST_RUN_HINT = 2,  // guidance: what to wait for, who to see, where to go. The tags `hint`,
                         // `npc` and `place` all produce this style.
    QUEST_RUN_STYLE_COUNT,
} QuestRunStyle;

// How much weight a renderer should give a style. Keeping the mapping in a table rather than in
// prose is what made P4's D23 checkpoint mechanical: the question was "do npc and place still
// return the same emphasis as hint", answered by reading this one function. They did, so the
// styles collapsed above and this table is now one line per style.
//
// ITEM and HINT are on purpose the one pair that can never share an emphasis: "fetch a bucket of
// milk" and "wait until dark" are different kinds of information to a scanning player, and
// collapsing them loses the distinction the feature exists for (D23). If a future style is added,
// it earns a place here by rendering differently - that is what `npc` and `place` failed to do.
typedef enum QuestRunEmphasis {
    QUEST_EMPHASIS_NONE = 0,  // plain prose
    QUEST_EMPHASIS_KEY = 1,   // a concrete thing the player must get or hold - `item`
    QUEST_EMPHASIS_GUIDE = 2, // where to go / who to see / what to wait for - the `hint` style, and
                              // so the `hint`, `npc` and `place` tags that all produce it
    QUEST_EMPHASIS_COUNT,
} QuestRunEmphasis;

typedef enum QuestJournalBlockKind {
    QUEST_BLOCK_PARAGRAPH = 0, // `text` is the prose; no items
    QUEST_BLOCK_CHECKLIST = 1, // `text` is an optional lead-in (may be NULL); `items` are the rows
    QUEST_BLOCK_KIND_COUNT,
} QuestJournalBlockKind;

// One checklist row. `step` is the step whose set-ness strikes the row through - a REFERENCE to a
// step index, unlike the markup tags around it, which are annotations. Use -1 for a row that is
// never struck (a standing instruction inside a checklist).
typedef struct QuestJournalItem {
    const char* text; // markup; never NULL
    int32_t step;     // [0, stepCount), or -1 for "never struck"
} QuestJournalItem;

typedef struct QuestJournalBlock {
    // Visible when EVERY predicate here is true. A count of 0 means always visible - the list is
    // the conjunction, exactly as QuestDef.requirements is, so no `And` word is needed.
    const QuestPredicate* when;
    int32_t whenCount;

    QuestJournalBlockKind kind;
    const char* text; // PARAGRAPH: the prose, never NULL. CHECKLIST: lead-in, MAY be NULL.
    const QuestJournalItem* items;
    int32_t itemCount; // CHECKLIST: >= 1. PARAGRAPH: 0.
} QuestJournalBlock;

#endif // SOH_RS_QUEST_JOURNAL_DEF_H
