#ifndef SOH_RS_QUEST_DEF_H
#define SOH_RS_QUEST_DEF_H

#include <stdint.h>
#include "QuestIds.h"
#include "QuestPredicate.h"
#include "QuestJournalDef.h"

// A quest DEFINITION (sturdy-bassoon#58 P1): what a quest is, as plain C data. One file-scope
// `static const QuestDef` per quest, registered with Quest_Register (Quest.h) from a ShipInit
// function. Nothing in this struct is serialized - the save file carries only the QuestId-indexed
// {status, stepMask} entry (QuestStore.h) - so a definition may be edited freely EXCEPT for the
// meaning of its step bits once the quest has shipped into a save that matters (D3).
//
// Plain struct, no inheritance, on purpose (D5): actors stay idiomatic C and copy vanilla code
// cleanly, and nothing here needs polymorphism yet. Revisit when custom items/mechanics arrive.

// Declarative rewards (D12). Dispatched in list order by Quest_Complete AFTER status is COMPLETE.
// Rewards are not serialized, so appending a kind is never a save-format change.
//
// "grant item" is deliberately not a kind yet: no custom item exists until P3/P4, and D16 says
// nothing enters the vanilla inventory. It arrives as an appended enumerator when the collection
// API does.
typedef enum QuestRewardKind {
    QUEST_REWARD_WORLD_FLAG = 0, // a = WorldFlagId -> Flags_SetWorldFlag(a). The "unlock" primitive.
    QUEST_REWARD_RUPEES = 1,     // a = amount (s16 range) -> Rupees_ChangeBy(a). Countable, so it is
                                 // the witness the idempotency proof reads.
    QUEST_REWARD_KIND_COUNT,
} QuestRewardKind;

typedef struct QuestReward {
    QuestRewardKind kind;
    int32_t a;
} QuestReward;

// D8 escape hatch: a per-quest predicate for prerequisites the five-word vocabulary cannot say
// ("three bottles and it is night"). Nonzero = met. NON-INTROSPECTABLE by construction - a dump
// can only report that it exists and what it currently returns - so prefer `requirements` for
// anything the vocabulary can express, and grow the vocabulary before reaching for this twice.
typedef int32_t (*QuestPrereqFn)(void);

// D12 optional bespoke completion hook (cutscenes, animations). Runs after the declarative
// rewards, with status already COMPLETE, and never runs twice for one completion.
typedef void (*QuestCompleteFn)(int32_t questId);

typedef struct QuestDef {
    int32_t id;     // QuestId (QuestIds.h) - the only thing about a quest that a save file knows
    QuestTier tier; // must equal QUEST_ID_TIER(id); Quest_Register refuses otherwise (D7)
    const char* name;  // snake_case token for console lines and markers: no spaces, no '%', no '#'
    const char* title; // display text - MARKUP (QuestJournalDef.h), parsed into runs by the read API
    uint8_t ordered;   // D2: nonzero => step N may be set only when steps 0..N-1 are all set, and
                       // cleared only when no step above N is set. Zero => any order.
    int32_t stepCount; // 1..QUEST_STEP_MAX. A step >= stepCount is refused as invalid.
    const char* const* stepNames; // stepCount entries, or NULL

    // D8/D9: `requirements` are evaluated and blocking (Quest_Start refuses while any is false);
    // `hints` are display text shown before the quest starts and are never evaluated. Hints are
    // MARKUP too, so `#hint:...#` may appear inside one - the two senses of the word are different
    // scopes of the same idea and are pinned as such in CONTEXT.md.
    const QuestPredicate* requirements;
    int32_t requirementCount;
    QuestPrereqFn prereqFn; // may be NULL
    const char* const* hints;
    int32_t hintCount;

    const QuestReward* rewards;
    int32_t rewardCount;
    QuestCompleteFn onComplete; // may be NULL

    // D13: the journal, an ordered block list (QuestJournalDef.h). May be NULL with a count of 0 -
    // a quest with no journal is legal and renders as an entry with zero blocks. Not serialized,
    // so blocks may be added, reworded or reordered freely; only the STEP INDICES the checklist
    // items reference are frozen once the quest ships (D3).
    const QuestJournalBlock* journal;
    int32_t journalCount;
} QuestDef;

// The mask with the low `stepCount` bits set. A function rather than a macro so no shift-by-32
// ever appears, even in an unevaluated branch (MSVC's C4293 fires on those and /WX makes it fatal).
static inline uint32_t Quest_AllStepsMask(int32_t stepCount) {
    if (stepCount <= 0) {
        return 0u;
    }
    if (stepCount >= QUEST_STEP_MAX) {
        return 0xFFFFFFFFu;
    }
    return (1u << stepCount) - 1u;
}

#endif // SOH_RS_QUEST_DEF_H
