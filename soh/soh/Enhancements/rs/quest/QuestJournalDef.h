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
typedef enum QuestRunStyle {
    QUEST_RUN_PLAIN = 0, // untagged prose
    QUEST_RUN_ITEM = 1,  // a thing to obtain or carry
    QUEST_RUN_NPC = 2,   // a character to find or speak to
    QUEST_RUN_PLACE = 3, // a location to go
    QUEST_RUN_HINT = 4,  // guidance that maps to no code object at all ("wait until it gets dark")
    QUEST_RUN_STYLE_COUNT,
} QuestRunStyle;

// How much weight a renderer should give a style. This is where D23's deferred-value bet on `npc`
// and `place` LIVES, rather than in prose: today they share GUIDE with `hint` and buy nothing
// functional, and the ADR's exit condition is "if P4 ships and they still render identically to
// hint, collapse them". With the mapping in a table, that check is reading one function instead of
// re-reading every journal entry.
//
// ITEM and HINT are on purpose the one pair that can never share an emphasis: "fetch a bucket of
// milk" and "wait until dark" are different kinds of information to a scanning player, and
// collapsing them loses the distinction the feature exists for (D23).
typedef enum QuestRunEmphasis {
    QUEST_EMPHASIS_NONE = 0,  // plain prose
    QUEST_EMPHASIS_KEY = 1,   // a concrete thing the player must get or hold - `item`
    QUEST_EMPHASIS_GUIDE = 2, // where to go / who to see / what to wait for - `npc`, `place`, `hint`
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
