// RsItemArt.cpp - which texture a quest item wears (sturdy-bassoon#58 P6 / #82, D19).
//
// Two things live here, and the second is the interesting one.
//
// THE TABLE is a flat (quest, step) -> texture list. It is deliberately not a field on QuestDef:
// a quest definition is serialized-adjacent data that P4 froze, and what an item LOOKS like is not
// part of what a quest MEANS. Keeping art out of the definition also means adding a sprite touches
// no file any save format depends on.
//
// THE VALIDATOR exists because of how this renderer fails. When a texture path does not resolve,
// gfx_set_timg_handler_rdp advances the command pointer and returns
// (libultraship/src/fast/interpreter.cpp) - and the dispatch loop then advances it AGAIN, so the
// NEXT command is skipped as well. Inside gDPLoadTextureBlock the next command is the LOADTILE
// gDPSetTile, so the following gDPLoadBlock runs against whatever tile state was left over and the
// quad draws garbage. There is no log line, no fallback to the old art, and no crash - the failure
// mode is a wrong picture. The overwhelmingly likely cause is a soh.o2r that was regenerated but
// never copied next to soh.exe, which nothing in the build does for you.
//
// So every art path is resolved ONCE at boot, a miss is a loud log line, and the result is
// READABLE AFTERWARDS through `quest itemart` on both console sinks. The report rather than a boot
// marker is deliberate: agent mode is not decided until the first console-logo tick
// (AgentTest.cpp), which is long after ShipInit, so a marker written here would never reach
// agent-log.txt - only the engine log (sturdy-bassoon#97), where no run waits on it. That is the
// exact failure class this file exists to remove. A pull-based report is also the
// better shape, because it can be asserted at any point in a run instead of only at boot.

#include "RsItemArt.h"

#include <spdlog/spdlog.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>

#include "soh/Enhancements/rs/quest/QuestIds.h"
#include "soh/ShipInit.hpp"

namespace {

struct RsItemArtEntry {
    int32_t questId;
    int32_t step;
    const char* tex;
    const char* name; // a token for the log line and `quest itemart`; never shown to a player
};

// THE TABLE. Adding item N+1 is one row here plus one PNG - see the recipe in
// docs/reference/ASSET_PIPELINE.md. A quest absent from this list keeps the shared fallback model,
// which is what every debug fixture still does.
const RsItemArtEntry sArt[] = {
    { QUEST_COOKS_ASSISTANT, 0, gRsItemEggTex, "egg" },
    { QUEST_COOKS_ASSISTANT, 1, gRsItemMilkTex, "milk" },
    { QUEST_COOKS_ASSISTANT, 2, gRsItemFlourTex, "flour" },
};

constexpr int32_t kCount = static_cast<int32_t>(sizeof(sArt) / sizeof(sArt[0]));

bool sResolved[kCount] = {};
bool sChecked = false;
int32_t sMissing = 0;

// Resolve every path once, at boot. ShipInit::InitAll runs after OTRGlobals::Initialize
// (OTRGlobals.cpp), so the resource manager and every archive are up by the time this runs - which
// is the whole reason the check can live here rather than in a draw call.
void ValidateItemArt() {
    sMissing = 0;

    for (int32_t i = 0; i < kCount; i++) {
        // LoadResourceProcess, not LoadResource: it strips the "__OTR__" prefix itself and always
        // reads from the archive rather than answering from the cache, so this proves the bytes are
        // really there and really decode - which is the entire point of a validator.
        auto resource = Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(sArt[i].tex);
        sResolved[i] = resource != nullptr;

        if (!sResolved[i]) {
            sMissing++;
            SPDLOG_ERROR("RsItemArt: quest {} step {} ({}) has no resource at {} - regenerate "
                         "soh.o2r AND copy it next to soh.exe",
                         sArt[i].questId, sArt[i].step, sArt[i].name, sArt[i].tex);
        }
    }

    sChecked = true;
    if (sMissing == 0) {
        SPDLOG_INFO("RsItemArt: {} item textures resolved", kCount);
    }
}

// ShipInit "*" functions re-run on preset apply and config load, which is harmless here: the check
// is a pure read and re-running it after an archive swap is if anything the right answer.
RegisterShipInitFunc rsItemArtInitFunc(ValidateItemArt);

} // namespace

extern "C" const char* RsItemArt_Texture(int32_t questId, int32_t step) {
    for (const RsItemArtEntry& entry : sArt) {
        if (entry.questId == questId && entry.step == step) {
            return entry.tex;
        }
    }
    return nullptr;
}

extern "C" int32_t RsItemArt_Count(void) {
    return kCount;
}

extern "C" int32_t RsItemArt_Get(int32_t index, RsItemArtInfo* out) {
    if (out == nullptr || index < 0 || index >= kCount) {
        return 0;
    }
    out->questId = sArt[index].questId;
    out->step = sArt[index].step;
    out->name = sArt[index].name;
    out->tex = sArt[index].tex;
    out->resolved = sResolved[index] ? 1 : 0;
    return 1;
}

extern "C" int32_t RsItemArt_MissingCount(void) {
    return sMissing;
}

extern "C" int32_t RsItemArt_Checked(void) {
    return sChecked ? 1 : 0;
}
