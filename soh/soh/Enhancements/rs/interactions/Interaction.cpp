// The interaction registry (sturdy-bassoon#183). See Interaction.h.
#include "Interaction.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <spdlog/spdlog.h>
#include "soh/Enhancements/rs/dialogue/NpcDialogue.h"
#include "soh/Enhancements/rs/quest/Quest.h"

namespace {

// Ordered, so a listing comes out in id order. Read at a placement's Init and when a box opens,
// never per tick, so a map is fine; 15 bits of id space rules out a flat array of pointers.
std::map<int32_t, const RsInteractionDef*>& Defs() {
    static std::map<int32_t, const RsInteractionDef*> defs;
    return defs;
}

bool Problem(char* buf, size_t len, const char* what, int32_t value) {
    if (buf != nullptr && len > 0) {
        std::snprintf(buf, len, what, value);
    }
    return false;
}

bool Validate(const RsInteractionDef* def, char* buf, size_t len) {
    if (def == nullptr) {
        return Problem(buf, len, "NULL definition", 0); // an unused argument is harmless to snprintf
    }
    if (!RS_INTERACTION_ID_IS_VALID(def->id)) {
        return Problem(buf, len, "interaction id %d is outside 1..0x7FFF", def->id);
    }
    if (def->tier != RS_INTERACTION_ID_TIER(def->id)) {
        return Problem(buf, len, "interaction %d: tier does not match the id's band", def->id);
    }
    // The screens, flags, trees: held to exactly what a character is held to.
    return RsDialogue_BodyProblem(def, buf, len) == 0;
}

} // namespace

extern "C" int32_t RsInteraction_DefProblem(const RsInteractionDef* def, char* buf, size_t len) {
    if (buf != nullptr && len > 0) {
        buf[0] = '\0';
    }
    return Validate(def, buf, len) ? 0 : 1;
}

extern "C" int32_t RsInteraction_Register(const RsInteractionDef* def) {
    char problem[192];
    if (!Validate(def, problem, sizeof(problem))) {
        SPDLOG_ERROR("RsInteraction: register {} ({}): {}", def != nullptr ? def->id : -1,
                     (def != nullptr && def->name != nullptr) ? def->name : "<null>", problem);
        assert(false && "interaction definition failed validation");
        return RS_INTERACTION_ERR_BAD_DEF;
    }
    auto& defs = Defs();
    auto found = defs.find(def->id);
    if (found != defs.end()) {
        if (found->second == def) {
            return RS_INTERACTION_OK; // ShipInit re-run; already ours
        }
        SPDLOG_ERROR("RsInteraction: register {} ({}): id already owned by '{}'", def->id, def->name,
                     found->second->name);
        assert(false && "duplicate interaction id");
        return RS_INTERACTION_ERR_DUPLICATE;
    }
    defs[def->id] = def;
    SPDLOG_INFO("RsInteraction: registered I-{:04} '{}' tier={} rules={} nodes={}", def->id, def->name,
                Quest_TierName(def->tier), def->ruleCount, def->nodeCount);
    return RS_INTERACTION_OK;
}

extern "C" const RsInteractionDef* RsInteraction_GetDef(int32_t id) {
    if (!RS_INTERACTION_ID_IS_VALID(id)) {
        return nullptr;
    }
    const auto& defs = Defs();
    auto found = defs.find(id);
    return found != defs.end() ? found->second : nullptr;
}

extern "C" int32_t RsInteraction_RegisteredCount(void) {
    return static_cast<int32_t>(Defs().size());
}

extern "C" const RsInteractionDef* RsInteraction_DefAt(int32_t n) {
    if (n < 0) {
        return nullptr;
    }
    for (const auto& [id, def] : Defs()) {
        if (n-- == 0) {
            return def;
        }
    }
    return nullptr;
}
