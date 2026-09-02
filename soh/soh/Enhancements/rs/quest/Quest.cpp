#include "Quest.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <spdlog/spdlog.h>

#include "WorldFlagIds.h"
#include "soh/Enhancements/worldstate/WorldFlags.h"

// Rupees_ChangeBy is C; the same include shape debugconsole.cpp and AgentTest.cpp use.
extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
}

// --- registry ---------------------------------------------------------------------------------
//
// Pointers to file-scope definitions, indexed by QuestId. A zero-initialised POD array, so there
// is no static-initialisation-order hazard with the RegisterShipInitFunc objects that call
// Quest_Register - and registration itself never touches the store, so it does not matter whether
// it runs before or after SaveManager::InitFile zeroes the entries.
static std::array<const QuestDef*, QUEST_MAX> sDefs = {};

static void Shout(const char* op, int32_t questId, const char* what) {
    SPDLOG_ERROR("Quest: {} quest {}: {}", op, questId, what);
}

// Bug-class refusal: log, debug-assert, and hand the code back for the caller to return.
#define QUEST_BUG(op, questId, what, result) \
    do {                                     \
        Shout(op, questId, what);            \
        assert(false && what);               \
        return (result);                     \
    } while (0)

static bool StringIsClean(const char* s, bool allowSpaces) {
    if (s == nullptr) {
        return false;
    }
    for (const char* p = s; *p != '\0'; p++) {
        // '%' is refused because the ImGui console hands a handler's output to vsnprintf as the
        // FORMAT string (ConsoleWindow::SendInfoMessage); a stray '%' would corrupt the human sink.
        if (*p == '%' || (!allowSpaces && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))) {
            return false;
        }
    }
    return true;
}

static const char* ValidateDef(const QuestDef* def) {
    if (!QUEST_ID_IS_VALID(def->id)) {
        return "id out of range";
    }
    if (def->tier != QUEST_ID_TIER(def->id)) {
        return "tier does not match the id's band";
    }
    if (!StringIsClean(def->name, false)) {
        return "name is NULL, or contains whitespace or '%'";
    }
    if (!StringIsClean(def->title, true)) {
        return "title is NULL or contains '%'";
    }
    if (def->stepCount < 1 || def->stepCount > QUEST_STEP_MAX) {
        return "stepCount outside [1, QUEST_STEP_MAX]";
    }
    if (def->stepNames != nullptr) {
        for (int32_t i = 0; i < def->stepCount; i++) {
            if (!StringIsClean(def->stepNames[i], false)) {
                return "a stepName is NULL, or contains whitespace or '%'";
            }
        }
    }
    if (def->requirementCount < 0 || (def->requirementCount > 0 && def->requirements == nullptr)) {
        return "requirements list is NULL with a nonzero count";
    }
    for (int32_t i = 0; i < def->requirementCount; i++) {
        const QuestPredicate& p = def->requirements[i];
        if (p.kind < 0 || p.kind >= QUEST_PRED_KIND_COUNT) {
            return "a requirement has an unknown predicate kind";
        }
    }
    if (def->hintCount < 0 || (def->hintCount > 0 && def->hints == nullptr)) {
        return "hints list is NULL with a nonzero count";
    }
    for (int32_t i = 0; i < def->hintCount; i++) {
        if (!StringIsClean(def->hints[i], true)) {
            return "a hint is NULL or contains '%'";
        }
    }
    if (def->rewardCount < 0 || (def->rewardCount > 0 && def->rewards == nullptr)) {
        return "rewards list is NULL with a nonzero count";
    }
    for (int32_t i = 0; i < def->rewardCount; i++) {
        const QuestReward& r = def->rewards[i];
        switch (r.kind) {
            case QUEST_REWARD_WORLD_FLAG:
                if (r.a < 0 || r.a >= WORLD_FLAG_MAX) {
                    return "a world-flag reward is outside the store";
                }
                if ((WORLD_FLAG_IS_DEBUG(r.a) != 0) != (def->tier == QUEST_TIER_DEBUG)) {
                    return "a world-flag reward is in the other tier's band";
                }
                break;
            case QUEST_REWARD_RUPEES:
                if (r.a < -32768 || r.a > 32767 || r.a == 0) {
                    return "a rupee reward is zero or outside s16";
                }
                break;
            default:
                return "a reward has an unknown kind";
        }
    }
    return nullptr;
}

extern "C" int32_t Quest_Register(const QuestDef* def) {
    if (def == nullptr) {
        QUEST_BUG("register", -1, "NULL definition", QUEST_ERR_BAD_DEF);
    }
    const char* problem = ValidateDef(def);
    if (problem != nullptr) {
        SPDLOG_ERROR("Quest: register quest {} ({}): {}", def->id, def->name ? def->name : "<null>", problem);
        assert(false && "quest definition failed validation");
        return QUEST_ERR_BAD_DEF;
    }
    const QuestDef* existing = sDefs[def->id];
    if (existing == def) {
        return QUEST_OK; // ShipInit re-run; already ours
    }
    if (existing != nullptr) {
        SPDLOG_ERROR("Quest: register quest {} ({}): id already owned by '{}'", def->id, def->name, existing->name);
        assert(false && "duplicate quest id");
        return QUEST_ERR_DUPLICATE;
    }
    sDefs[def->id] = def;
    SPDLOG_INFO("Quest: registered {} '{}' tier={} steps={} ordered={} requirements={} rewards={}", def->id,
                def->name, Quest_TierName(def->tier), def->stepCount, def->ordered ? 1 : 0, def->requirementCount,
                def->rewardCount);
    return QUEST_OK;
}

extern "C" const QuestDef* Quest_GetDef(int32_t questId) {
    if (!QUEST_ID_IS_VALID(questId)) {
        return nullptr;
    }
    return sDefs[questId];
}

extern "C" int32_t Quest_IsRegistered(int32_t questId) {
    return Quest_GetDef(questId) != nullptr;
}

extern "C" int32_t Quest_RegisteredCount(void) {
    int32_t count = 0;
    for (const QuestDef* def : sDefs) {
        if (def != nullptr) {
            count++;
        }
    }
    return count;
}

// Resolves an id to its definition, shouting (bug class) if it has none. Returns the QuestResult
// through `result` so callers can hand it straight back.
static const QuestDef* Resolve(const char* op, int32_t questId, int32_t* result) {
    if (!QUEST_ID_IS_VALID(questId)) {
        *result = QUEST_ERR_INVALID_ID;
        Shout(op, questId, "id out of range");
        assert(false && "quest id out of range");
        return nullptr;
    }
    const QuestDef* def = sDefs[questId];
    if (def == nullptr) {
        *result = QUEST_ERR_NOT_REGISTERED;
        Shout(op, questId, "no definition registered");
        assert(false && "quest not registered");
        return nullptr;
    }
    *result = QUEST_OK;
    return def;
}

// --- read half ----------------------------------------------------------------------------------

extern "C" int32_t Quest_GetStatus(int32_t questId) {
    int32_t rc;
    return Resolve("get status", questId, &rc) ? QuestStore_GetStatus(questId) : QUEST_STATUS_NOT_STARTED;
}

extern "C" uint32_t Quest_GetStepMask(int32_t questId) {
    int32_t rc;
    return Resolve("get steps", questId, &rc) ? QuestStore_GetStepMask(questId) : 0u;
}

extern "C" int32_t Quest_IsStepSet(int32_t questId, int32_t step) {
    int32_t rc;
    const QuestDef* def = Resolve("is step set", questId, &rc);
    if (def == nullptr) {
        return 0;
    }
    if (step < 0 || step >= def->stepCount) {
        Shout("is step set", questId, "step outside the definition's stepCount");
        assert(false && "quest step outside stepCount");
        return 0;
    }
    return QuestStore_IsStepSet(questId, step);
}

static bool AllStepsSet(const QuestDef* def) {
    const uint32_t all = Quest_AllStepsMask(def->stepCount);
    return (QuestStore_GetStepMask(def->id) & all) == all;
}

extern "C" int32_t Quest_AllStepsSet(int32_t questId) {
    int32_t rc;
    const QuestDef* def = Resolve("all steps set", questId, &rc);
    return def != nullptr && AllStepsSet(def);
}

static bool PrereqsMet(const QuestDef* def) {
    for (int32_t i = 0; i < def->requirementCount; i++) {
        if (!QuestPredicate_Eval(&def->requirements[i])) {
            return false;
        }
    }
    return def->prereqFn == nullptr || def->prereqFn() != 0;
}

extern "C" int32_t Quest_PrereqsMet(int32_t questId) {
    int32_t rc;
    const QuestDef* def = Resolve("prereqs met", questId, &rc);
    return def != nullptr && PrereqsMet(def);
}

extern "C" int32_t Quest_IsAvailable(int32_t questId) {
    int32_t rc;
    const QuestDef* def = Resolve("is available", questId, &rc);
    return def != nullptr && QuestStore_GetStatus(questId) == QUEST_STATUS_NOT_STARTED && PrereqsMet(def);
}

// The Check* family: the same lookups, but a bad id / unregistered id / bad step is answered with
// its code and NO assert, so a console can pre-validate on a Debug build.
static const QuestDef* Peek(int32_t questId, int32_t* result) {
    if (!QUEST_ID_IS_VALID(questId)) {
        *result = QUEST_ERR_INVALID_ID;
        return nullptr;
    }
    const QuestDef* def = sDefs[questId];
    *result = def ? QUEST_OK : QUEST_ERR_NOT_REGISTERED;
    return def;
}

static int32_t CheckStart(const QuestDef* def) {
    if (QuestStore_GetStatus(def->id) != QUEST_STATUS_NOT_STARTED) {
        return QUEST_ERR_WRONG_STATUS;
    }
    return PrereqsMet(def) ? QUEST_OK : QUEST_ERR_PREREQ_UNMET;
}

// D2, set: step N on an ordered quest needs bits [0, N) all set. Clear: no bit above N may be set.
// Both are expressed through Quest_AllStepsMask so no shift-by-32 exists for step 31.
static int32_t CheckSetStep(const QuestDef* def, int32_t step) {
    if (step < 0 || step >= def->stepCount) {
        return QUEST_ERR_INVALID_STEP;
    }
    if (QuestStore_GetStatus(def->id) == QUEST_STATUS_COMPLETE) {
        return QUEST_ERR_WRONG_STATUS;
    }
    if (def->ordered) {
        const uint32_t below = Quest_AllStepsMask(step);
        if ((QuestStore_GetStepMask(def->id) & below) != below) {
            return QUEST_ERR_ORDER_VIOLATION;
        }
    }
    return QUEST_OK;
}

static int32_t CheckClearStep(const QuestDef* def, int32_t step) {
    if (step < 0 || step >= def->stepCount) {
        return QUEST_ERR_INVALID_STEP;
    }
    if (QuestStore_GetStatus(def->id) == QUEST_STATUS_COMPLETE) {
        return QUEST_ERR_WRONG_STATUS;
    }
    if (def->ordered) {
        const uint32_t upToAndIncluding = Quest_AllStepsMask(step + 1);
        if ((QuestStore_GetStepMask(def->id) & ~upToAndIncluding) != 0) {
            return QUEST_ERR_ORDER_VIOLATION;
        }
    }
    return QUEST_OK;
}

static int32_t CheckComplete(const QuestDef* def) {
    const int32_t status = QuestStore_GetStatus(def->id);
    if (status == QUEST_STATUS_COMPLETE) {
        return QUEST_ALREADY_COMPLETE;
    }
    if (status != QUEST_STATUS_IN_PROGRESS) {
        return QUEST_ERR_WRONG_STATUS;
    }
    return AllStepsSet(def) ? QUEST_OK : QUEST_ERR_STEPS_INCOMPLETE;
}

extern "C" int32_t Quest_CheckStart(int32_t questId) {
    int32_t rc;
    const QuestDef* def = Peek(questId, &rc);
    return def ? CheckStart(def) : rc;
}

extern "C" int32_t Quest_CheckSetStep(int32_t questId, int32_t step) {
    int32_t rc;
    const QuestDef* def = Peek(questId, &rc);
    return def ? CheckSetStep(def, step) : rc;
}

extern "C" int32_t Quest_CheckClearStep(int32_t questId, int32_t step) {
    int32_t rc;
    const QuestDef* def = Peek(questId, &rc);
    return def ? CheckClearStep(def, step) : rc;
}

extern "C" int32_t Quest_CheckComplete(int32_t questId) {
    int32_t rc;
    const QuestDef* def = Peek(questId, &rc);
    return def ? CheckComplete(def) : rc;
}

// --- write half ---------------------------------------------------------------------------------

static bool IsBugClass(int32_t result) {
    switch (result) {
        case QUEST_ERR_ORDER_VIOLATION:
        case QUEST_ERR_INVALID_ID:
        case QUEST_ERR_NOT_REGISTERED:
        case QUEST_ERR_INVALID_STEP:
        case QUEST_ERR_BAD_DEF:
        case QUEST_ERR_DUPLICATE:
            return true;
        default:
            return false;
    }
}

// Every refused write passes through here: bug class shouts and asserts, outcome class is quiet.
static int32_t Refuse(const char* op, const QuestDef* def, int32_t step, int32_t result) {
    if (IsBugClass(result)) {
        SPDLOG_ERROR("Quest: {} refused on quest {} ({}) step {}: {} (steps=0x{:08X} status={})", op, def->id,
                     def->name, step, Quest_ResultName(result), QuestStore_GetStepMask(def->id),
                     Quest_StatusName(QuestStore_GetStatus(def->id)));
        assert(false && "quest write refused (bug class - see log)");
    } else {
        SPDLOG_DEBUG("Quest: {} on quest {} ({}) -> {}", op, def->id, def->name, Quest_ResultName(result));
    }
    return result;
}

extern "C" int32_t Quest_Start(int32_t questId) {
    int32_t rc;
    const QuestDef* def = Resolve("start", questId, &rc);
    if (def == nullptr) {
        return rc;
    }
    rc = CheckStart(def);
    if (rc != QUEST_OK) {
        return Refuse("start", def, -1, rc);
    }
    QuestStore_SetStatus(questId, QUEST_STATUS_IN_PROGRESS);
    SPDLOG_INFO("Quest: started {} ({})", questId, def->name);
    return QUEST_OK;
}

extern "C" int32_t Quest_SetStep(int32_t questId, int32_t step) {
    int32_t rc;
    const QuestDef* def = Resolve("set step", questId, &rc);
    if (def == nullptr) {
        return rc;
    }
    rc = CheckSetStep(def, step);
    if (rc != QUEST_OK) {
        return Refuse("set step", def, step, rc);
    }
    QuestStore_SetStep(questId, step);
    SPDLOG_INFO("Quest: {} ({}) step {} set -> 0x{:08X}", questId, def->name, step, QuestStore_GetStepMask(questId));
    return QUEST_OK;
}

extern "C" int32_t Quest_ClearStep(int32_t questId, int32_t step) {
    int32_t rc;
    const QuestDef* def = Resolve("clear step", questId, &rc);
    if (def == nullptr) {
        return rc;
    }
    rc = CheckClearStep(def, step);
    if (rc != QUEST_OK) {
        return Refuse("clear step", def, step, rc);
    }
    QuestStore_ClearStep(questId, step);
    SPDLOG_INFO("Quest: {} ({}) step {} cleared -> 0x{:08X}", questId, def->name, step,
                QuestStore_GetStepMask(questId));
    return QUEST_OK;
}

static void DispatchReward(const QuestDef* def, const QuestReward& reward, int32_t index) {
    switch (reward.kind) {
        case QUEST_REWARD_WORLD_FLAG:
            Flags_SetWorldFlag(reward.a);
            SPDLOG_INFO("Quest: {} ({}) reward[{}] world flag {} set", def->id, def->name, index, reward.a);
            break;
        case QUEST_REWARD_RUPEES:
            // s16 range was checked at registration. In play this feeds gSaveContext.rupeeAccumulator,
            // which Interface_Update drains one per frame into rupees (wallet-capped); with no
            // PlayState it adds directly.
            Rupees_ChangeBy(static_cast<s16>(reward.a));
            SPDLOG_INFO("Quest: {} ({}) reward[{}] rupees {:+d}", def->id, def->name, index, reward.a);
            break;
        default:
            // Unreachable after ValidateDef; kept loud rather than silently skipped.
            SPDLOG_ERROR("Quest: {} ({}) reward[{}] has unknown kind {}", def->id, def->name, index,
                         static_cast<int>(reward.kind));
            assert(false && "unknown quest reward kind");
            break;
    }
}

// D12. The ONLY path to COMPLETE. Status is written first, so a re-entrant call - the same frame,
// or after a save/load that restored COMPLETE - returns QUEST_ALREADY_COMPLETE before any reward
// is looked at. Rewards then dispatch in list order, then the optional callback.
static int32_t CompleteInternal(const QuestDef* def, const char* op) {
    if (QuestStore_GetStatus(def->id) == QUEST_STATUS_COMPLETE) {
        return Refuse(op, def, -1, QUEST_ALREADY_COMPLETE);
    }
    QuestStore_SetStatus(def->id, QUEST_STATUS_COMPLETE);
    SPDLOG_INFO("Quest: {} ({}) COMPLETE via {}; dispatching {} reward(s)", def->id, def->name, op,
                def->rewardCount);
    for (int32_t i = 0; i < def->rewardCount; i++) {
        DispatchReward(def, def->rewards[i], i);
    }
    if (def->onComplete != nullptr) {
        def->onComplete(def->id);
    }
    return QUEST_OK;
}

extern "C" int32_t Quest_Complete(int32_t questId) {
    int32_t rc;
    const QuestDef* def = Resolve("complete", questId, &rc);
    if (def == nullptr) {
        return rc;
    }
    rc = CheckComplete(def);
    if (rc != QUEST_OK) {
        return Refuse("complete", def, -1, rc);
    }
    return CompleteInternal(def, "complete");
}

extern "C" int32_t Quest_ForceComplete(int32_t questId) {
    int32_t rc;
    const QuestDef* def = Resolve("force complete", questId, &rc);
    if (def == nullptr) {
        return rc;
    }
    if (QuestStore_GetStatus(questId) == QUEST_STATUS_COMPLETE) {
        return Refuse("force complete", def, -1, QUEST_ALREADY_COMPLETE);
    }
    QuestStore_SetStepMask(questId, Quest_AllStepsMask(def->stepCount));
    return CompleteInternal(def, "force");
}

extern "C" int32_t Quest_Reset(int32_t questId) {
    int32_t rc;
    const QuestDef* def = Resolve("reset", questId, &rc);
    if (def == nullptr) {
        return rc;
    }
    QuestStore_Reset(questId);
    SPDLOG_INFO("Quest: {} ({}) reset", questId, def->name);
    return QUEST_OK;
}

extern "C" void Quest_DebugWipe(int32_t* questsWiped, int32_t* flagsCleared) {
    int32_t quests = 0;
    for (int32_t id = QUEST_ID_DEBUG_FIRST; id < QUEST_MAX; id++) {
        if (QuestStore_GetStatus(id) != QUEST_STATUS_NOT_STARTED || QuestStore_GetStepMask(id) != 0) {
            quests++;
        }
        QuestStore_Reset(id);
    }
    int32_t flags = 0;
    for (int32_t flag = WORLD_FLAG_DEBUG_FIRST; flag < WORLD_FLAG_MAX; flag++) {
        if (Flags_GetWorldFlag(flag)) {
            flags++;
            Flags_UnsetWorldFlag(flag);
        }
    }
    SPDLOG_INFO("Quest: debugwipe cleared {} quest entries in [{}, {}) and {} world flags in [{}, {})", quests,
                QUEST_ID_DEBUG_FIRST, QUEST_MAX, flags, WORLD_FLAG_DEBUG_FIRST, WORLD_FLAG_MAX);
    if (questsWiped) {
        *questsWiped = quests;
    }
    if (flagsCleared) {
        *flagsCleared = flags;
    }
}

// --- rendering ----------------------------------------------------------------------------------

extern "C" const char* Quest_ResultName(int32_t result) {
    switch (result) {
        case QUEST_OK:
            return "ok";
        case QUEST_ALREADY_COMPLETE:
            return "already_complete";
        case QUEST_ERR_PREREQ_UNMET:
            return "prereq_unmet";
        case QUEST_ERR_WRONG_STATUS:
            return "wrong_status";
        case QUEST_ERR_STEPS_INCOMPLETE:
            return "steps_incomplete";
        case QUEST_ERR_ORDER_VIOLATION:
            return "order_violation";
        case QUEST_ERR_INVALID_ID:
            return "invalid_id";
        case QUEST_ERR_NOT_REGISTERED:
            return "not_registered";
        case QUEST_ERR_INVALID_STEP:
            return "invalid_step";
        case QUEST_ERR_BAD_DEF:
            return "bad_def";
        case QUEST_ERR_DUPLICATE:
            return "duplicate";
        default:
            return "<bad result>";
    }
}

extern "C" const char* Quest_StatusName(int32_t status) {
    switch (status) {
        case QUEST_STATUS_NOT_STARTED:
            return "not_started";
        case QUEST_STATUS_IN_PROGRESS:
            return "in_progress";
        case QUEST_STATUS_COMPLETE:
            return "complete";
        default:
            return "<bad status>";
    }
}

extern "C" const char* Quest_TierName(int32_t tier) {
    switch (tier) {
        case QUEST_TIER_PROD:
            return "prod";
        case QUEST_TIER_DEBUG:
            return "debug";
        default:
            return "<bad tier>";
    }
}

extern "C" const char* Quest_RewardKindName(int32_t kind) {
    switch (kind) {
        case QUEST_REWARD_WORLD_FLAG:
            return "world_flag";
        case QUEST_REWARD_RUPEES:
            return "rupees";
        default:
            return "<bad kind>";
    }
}

extern "C" void Quest_Describe(int32_t questId, char* buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return;
    }
    if (!QUEST_ID_IS_VALID(questId)) {
        std::snprintf(buf, len, "id=%d invalid=1", questId);
        return;
    }
    const QuestDef* def = sDefs[questId];
    const int32_t status = QuestStore_GetStatus(questId);
    const uint32_t mask = QuestStore_GetStepMask(questId);
    if (def == nullptr) {
        std::snprintf(buf, len, "id=%d name=- registered=0 tier=%s status=%s steps=0x%08X", questId,
                      Quest_TierName(QUEST_ID_TIER(questId)), Quest_StatusName(status), mask);
        return;
    }
    const bool met = PrereqsMet(def);
    std::snprintf(buf, len, "id=%d name=%s tier=%s status=%s steps=0x%08X/%d ordered=%d available=%d prereqs=%s",
                  questId, def->name, Quest_TierName(def->tier), Quest_StatusName(status), mask, def->stepCount,
                  def->ordered ? 1 : 0, (status == QUEST_STATUS_NOT_STARTED && met) ? 1 : 0, met ? "met" : "unmet");
}
