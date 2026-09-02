#include "QuestPredicate.h"

#include <cassert>
#include <cstdio>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/worldstate/WorldFlags.h"

static int32_t EvalPositive(const QuestPredicate* pred) {
    switch (pred->kind) {
        case QUEST_PRED_ALWAYS:
            return 1;
        case QUEST_PRED_QUEST_STATUS_IS:
            return QuestStore_GetStatus(pred->a) == pred->b;
        case QUEST_PRED_QUEST_STEP_SET:
            return QuestStore_IsStepSet(pred->a, pred->b);
        case QUEST_PRED_WORLD_FLAG_SET:
            return Flags_GetWorldFlag(pred->a) != 0;
        default:
            SPDLOG_ERROR("QuestPredicate: unknown predicate kind {} (max {})", static_cast<int>(pred->kind),
                         QUEST_PRED_KIND_COUNT - 1);
            assert(false && "unknown quest predicate kind");
            return 0;
    }
}

extern "C" int32_t QuestPredicate_Eval(const QuestPredicate* pred) {
    if (pred == nullptr) {
        SPDLOG_ERROR("QuestPredicate: eval of a NULL predicate");
        assert(false && "NULL quest predicate");
        return 0;
    }
    int32_t value = EvalPositive(pred);
    return pred->negate ? !value : value;
}

extern "C" void QuestPredicate_Describe(const QuestPredicate* pred, char* buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return;
    }
    if (pred == nullptr) {
        std::snprintf(buf, len, "<null>");
        return;
    }
    char inner[64];
    switch (pred->kind) {
        case QUEST_PRED_ALWAYS:
            std::snprintf(inner, sizeof(inner), "Always");
            break;
        case QUEST_PRED_QUEST_STATUS_IS:
            std::snprintf(inner, sizeof(inner), "QuestStatusIs(%d, %d)", pred->a, pred->b);
            break;
        case QUEST_PRED_QUEST_STEP_SET:
            std::snprintf(inner, sizeof(inner), "QuestStepSet(%d, %d)", pred->a, pred->b);
            break;
        case QUEST_PRED_WORLD_FLAG_SET:
            std::snprintf(inner, sizeof(inner), "WorldFlagSet(%d)", pred->a);
            break;
        default:
            std::snprintf(inner, sizeof(inner), "<bad kind %d>", static_cast<int>(pred->kind));
            break;
    }
    if (pred->negate) {
        std::snprintf(buf, len, "Not(%s)", inner);
    } else {
        std::snprintf(buf, len, "%s", inner);
    }
}
