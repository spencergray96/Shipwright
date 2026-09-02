#ifndef SOH_RS_QUEST_PREDICATE_H
#define SOH_RS_QUEST_PREDICATE_H

#include <stddef.h>
#include <stdint.h>
#include "QuestIds.h"
#include "QuestStore.h"

// The predicate vocabulary (sturdy-bassoon#58 decisions D10/D11). Exactly five words:
//
//     Always            true
//     QuestStatusIs     QuestStore_GetStatus(a) == b
//     QuestStepSet      QuestStore_IsStepSet(a, b)
//     AllStepsSet       every step of quest a is set (added in P2 - see below)
//     WorldFlagSet      Flags_GetWorldFlag(a)
//     Not               negates any of the above
//
// A predicate is CONSTRAINED DATA, never a callback. That is the whole point: quest prerequisites,
// dialogue rule tables and journal blocks all gate on these, and because they are plain structs
// the console can print every rule, explain why it matched, and diff two NPCs' tables. A lambda
// would turn the rule table back into an if-chain with extra indirection. Grow the vocabulary
// only when a real quest needs a word it does not have.
//
// `Not` is a flag on the predicate rather than a nested pointer. There is no And/Or, so a flag is
// fully expressive, and it keeps the struct flat plain-old-data: file-scope tables in C actors
// initialise it with a brace list, and no compound literals (which MSVC's C++ lacks) are needed.
// Conjunction is expressed by the LIST that holds the predicates (QuestDef.requirements, a journal
// block's `when`): every entry must be true. That is why no `And` word is needed.
//
// AllStepsSet was the sixth word, added in P2 (sturdy-bassoon#58 / #62) because D14's canonical
// journal block is gated on "started AND NOT all collected". `Not(all of s0..sN)` is a DISJUNCTION
// of negations, and per-predicate `negate` over QuestStepSet cannot say it - the vocabulary
// genuinely lacked the word, which is exactly the condition D10 says to grow on. It is the only
// word that reads a DEFINITION (for stepCount) rather than the raw store; see QuestPredicate.cpp.

typedef enum QuestPredicateKind {
    QUEST_PRED_ALWAYS = 0,
    QUEST_PRED_QUEST_STATUS_IS = 1, // a = QuestId, b = QuestStatus
    QUEST_PRED_QUEST_STEP_SET = 2,  // a = QuestId, b = step [0, QUEST_STEP_MAX)
    QUEST_PRED_WORLD_FLAG_SET = 3,  // a = WorldFlagId
    QUEST_PRED_ALL_STEPS_SET = 4,   // a = QuestId; b unused. Appended, never inserted - and safe to
                                    // append because predicates are DEFINITION data and are never
                                    // serialized (only QuestStore's {status, stepMask} is).
    QUEST_PRED_KIND_COUNT,
} QuestPredicateKind;

typedef struct QuestPredicate {
    QuestPredicateKind kind;
    int32_t a;
    int32_t b;
    uint8_t negate; // nonzero = Not(...)
} QuestPredicate;

// Brace-list initialisers usable at file scope in BOTH C and C++ (they expand to an aggregate
// initialiser, so they work anywhere a `QuestPredicate` is being initialised - not as rvalues):
//     static const QuestPredicate sGate = QP_WORLD_FLAG_SET(WORLD_FLAG_COOK_MET);
//     static const QuestPredicate sRules[] = { QP_NOT_QUEST_STATUS_IS(QUEST_X, QUEST_STATUS_COMPLETE), ... };
#define QP_ALWAYS() { QUEST_PRED_ALWAYS, 0, 0, 0 }
#define QP_QUEST_STATUS_IS(questId, status) { QUEST_PRED_QUEST_STATUS_IS, (questId), (status), 0 }
#define QP_QUEST_STEP_SET(questId, step) { QUEST_PRED_QUEST_STEP_SET, (questId), (step), 0 }
#define QP_WORLD_FLAG_SET(flag) { QUEST_PRED_WORLD_FLAG_SET, (flag), 0, 0 }
#define QP_ALL_STEPS_SET(questId) { QUEST_PRED_ALL_STEPS_SET, (questId), 0, 0 }
#define QP_NOT_ALWAYS() { QUEST_PRED_ALWAYS, 0, 0, 1 }
#define QP_NOT_QUEST_STATUS_IS(questId, status) { QUEST_PRED_QUEST_STATUS_IS, (questId), (status), 1 }
#define QP_NOT_QUEST_STEP_SET(questId, step) { QUEST_PRED_QUEST_STEP_SET, (questId), (step), 1 }
#define QP_NOT_WORLD_FLAG_SET(flag) { QUEST_PRED_WORLD_FLAG_SET, (flag), 0, 1 }
#define QP_NOT_ALL_STEPS_SET(questId) { QUEST_PRED_ALL_STEPS_SET, (questId), 0, 1 }

#ifdef __cplusplus
extern "C" {
#endif

// Evaluates one predicate against the live stores. Returns 1 or 0. A NULL predicate or an unknown
// kind is logged + debug-asserted and evaluates to 0; out-of-range ids/steps/flags shout through
// the store accessors they delegate to.
//
// AllStepsSet is the one exception to that shouting: an id that is in range but has no definition
// in this build evaluates to 0 QUIETLY (debug-level log, no assert). It has to, because it is the
// only word whose operand can be in range and still unresolvable, and because Quest_Describe and
// the journal read API evaluate predicates on the console's behalf - a tripped assert there would
// hang the agent loop on a Debug build, which is precisely what the Quest_Check* rule exists to
// prevent. Registration still range-checks every operand, so a typo'd id is caught up front.
int32_t QuestPredicate_Eval(const QuestPredicate* pred);

// Renders the predicate as text, e.g. `Not(QuestStepSet(0, 3))`, for console dumps and markers.
// Always NUL-terminates when `len` > 0.
void QuestPredicate_Describe(const QuestPredicate* pred, char* buf, size_t len);

#ifdef __cplusplus
}

// Builders for definitions written in C++. Same data, nicer to read:
//     constexpr auto gate = rs::quest::Not(rs::quest::QuestStatusIs(QUEST_X, QUEST_STATUS_COMPLETE));
namespace rs::quest {
constexpr QuestPredicate Always() {
    return { QUEST_PRED_ALWAYS, 0, 0, 0 };
}
constexpr QuestPredicate QuestStatusIs(int32_t questId, int32_t status) {
    return { QUEST_PRED_QUEST_STATUS_IS, questId, status, 0 };
}
constexpr QuestPredicate QuestStepSet(int32_t questId, int32_t step) {
    return { QUEST_PRED_QUEST_STEP_SET, questId, step, 0 };
}
constexpr QuestPredicate WorldFlagSet(int32_t flag) {
    return { QUEST_PRED_WORLD_FLAG_SET, flag, 0, 0 };
}
constexpr QuestPredicate AllStepsSet(int32_t questId) {
    return { QUEST_PRED_ALL_STEPS_SET, questId, 0, 0 };
}
constexpr QuestPredicate Not(QuestPredicate pred) {
    pred.negate = pred.negate ? 0 : 1;
    return pred;
}
} // namespace rs::quest
#endif

#endif // SOH_RS_QUEST_PREDICATE_H
