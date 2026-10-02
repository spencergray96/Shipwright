/*
 * Archive prop lists (sturdy-bassoon#171 slice A): the contract is in ArchivePropLists.h.
 *
 * WHY A LIST IS HELD, not re-resolved: the static bake is keyed by display-list address, and an
 * archive list's address is its resource's instruction vector - stable only while the resource is
 * alive and is the one the cache hands out. Holding the shared_ptr for as long as the bake group
 * holds its entry (#157) pins both: the resource manager can neither free it nor swap it under the
 * key. The #160 proof re-resolved on every draw instead, which leaves a stale entry behind if the
 * cache ever hands out a new copy.
 *
 * WHY loadExact: ResourceMgr_LoadGfxByName - the route an "__OTR__" string takes - calls
 * ResourceMgr_UnloadOriginalWhenAltExists, which evicts the original whenever an alt/ twin exists.
 * Loading the exact path never looks at alt/, so an alt/ twin of an archive prop list is ignored, with
 * a warning. (Its sub-resources still resolve through alt/ when alt assets are on - the toggle sends
 * every bake back to be recorded, so a bake follows them.)
 */

#include "ArchivePropLists.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>

#include <fast/StaticMeshCache.h>
#include <fast/TextureMips.h>
#include <fast/resource/type/DisplayList.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/console/ConsoleSink.h"

namespace {

using RoomKey = std::pair<s32, s32>; // scene, room

// What each compiled-in room declared. Facts about the scene's C, so never cleared: a declaration
// for a scene Link has left is simply never looked up.
std::map<RoomKey, std::vector<const char*>> sDeclared;

enum class Resolved {
    Missing,        // in no mounted archive
    NotDisplayList, // the path names some other resource type
    Empty,          // no commands: neither drawn nor offered
    Listed,         // a display list with something in it: drawn and offered to the bake
};

// One list the registry holds, for as long as the bake group does (ReleaseHeld). A missing or empty
// list is held too, so its log line is written once per group rather than once per room load.
struct Held {
    Resolved state = Resolved::Missing;
    std::shared_ptr<Fast::DisplayList> resource;
    Gfx* key = nullptr;
    RoomKey room = { -1, -1 }; // the last room that offered it, for `staticbake props`
};
std::map<std::string, Held> sHeld;

// The lists each room draws, rebuilt every time the room is offered.
std::map<RoomKey, std::vector<Gfx*>> sDrawn;

bool IsEmptyList(const Fast::DisplayList& dl) {
    return dl.Instructions.empty() || (uint8_t)(dl.Instructions[0].words.w0 >> 24) == (uint8_t)G_ENDDL;
}

Held Resolve(const char* path) {
    Held h;
    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    auto archives = resources->GetArchiveManager();
    if (!archives->HasFile(path)) {
        SPDLOG_INFO("[staticbake] archive prop list {} is in no mounted archive: the room draws without it", path);
        return h;
    }
    if (archives->HasFile(std::string("alt/") + path)) {
        SPDLOG_WARN("[staticbake] archive prop list {} has an alt/ twin, which is ignored: the list is held as "
                    "loaded from its own path (sturdy-bassoon#171)",
                    path);
    }
    h.resource = std::dynamic_pointer_cast<Fast::DisplayList>(resources->LoadResource(path, true));
    if (h.resource == nullptr) {
        h.state = Resolved::NotDisplayList;
        SPDLOG_WARN("[staticbake] archive prop list {} is not a display list: the room draws without it", path);
        return h;
    }
    if (IsEmptyList(*h.resource)) {
        h.state = Resolved::Empty;
        return h;
    }
    h.state = Resolved::Listed;
    h.key = (Gfx*)h.resource->Instructions.data();
    return h;
}

const char* StateName(const Held& h, const Fast::StaticBakeEntryInfo& entry) {
    switch (h.state) {
        case Resolved::Missing:
            return "missing";
        case Resolved::NotDisplayList:
            return "not_displaylist";
        case Resolved::Empty:
            return "empty";
        case Resolved::Listed:
            break;
    }
    switch (entry.state) {
        case Fast::StaticBakeEntryState::Unbaked:
            return "unbaked";
        case Fast::StaticBakeEntryState::Baked:
            return "baked";
        case Fast::StaticBakeEntryState::Rejected:
            return "rejected";
        default:
            return "unregistered";
    }
}

} // namespace

extern "C" void ArchiveProps_DeclareRoom(PlayState* play, RoomContext* roomCtx, const char* const* paths,
                                         s32 count) {
    if (play == nullptr || roomCtx == nullptr) {
        return;
    }
    std::vector<const char*>& declared = sDeclared[{ play->sceneNum, roomCtx->curRoom.num }];
    declared.clear();
    for (s32 i = 0; paths != nullptr && i < count; i++) {
        if (paths[i] != nullptr && paths[i][0] != '\0') {
            declared.push_back(paths[i]);
        }
    }
}

extern "C" Gfx* const* ArchiveProps_RoomLists(PlayState* play, s32 roomNum, s32* count) {
    *count = 0;
    if (play == nullptr || sDrawn.empty()) {
        return nullptr;
    }
    auto it = sDrawn.find({ play->sceneNum, roomNum });
    if (it == sDrawn.end() || it->second.empty()) {
        return nullptr;
    }
    *count = (s32)it->second.size();
    return it->second.data();
}

namespace ArchiveProps {

uint32_t OfferRoom(s32 sceneNum, s32 roomNum) {
    const RoomKey room = { sceneNum, roomNum };
    auto declared = sDeclared.find(room);
    if (declared == sDeclared.end()) {
        sDrawn.erase(room);
        return 0;
    }
    std::vector<Gfx*>& drawn = sDrawn[room];
    drawn.clear();
    uint32_t offered = 0;
    for (const char* path : declared->second) {
        auto held = sHeld.find(path);
        if (held == sHeld.end()) {
            held = sHeld.emplace(path, Resolve(path)).first;
        }
        held->second.room = room;
        if (held->second.state != Resolved::Listed ||
            std::find(drawn.begin(), drawn.end(), held->second.key) != drawn.end()) {
            continue;
        }
        Fast::StaticBakeRegister(held->second.key);
        // Its textures are the mod's own, mipmapped like the room's (#146): named here, withdrawn in
        // ReleaseHeld, so a list is in scope exactly while its resource is held.
        Fast::TextureMipsRegisterDisplayList(held->second.key);
        drawn.push_back(held->second.key);
        offered++;
    }
    return offered;
}

void ReleaseHeld() {
    // Before the resources go: a list later allocated at a freed key's address must not be in scope.
    for (const auto& kv : sHeld) {
        if (kv.second.key != nullptr) {
            Fast::TextureMipsUnregisterDisplayList(kv.second.key);
        }
    }
    sDrawn.clear();
    sHeld.clear();
}

void Describe(std::vector<std::string>& lines) {
    ConsoleSink::Addf(lines, "op=props result=ok lists=%u", (unsigned)sHeld.size());
    for (const auto& kv : sHeld) {
        const Held& h = kv.second;
        const Fast::StaticBakeEntryInfo entry =
            h.state == Resolved::Listed ? Fast::StaticBakeGetEntry(h.key) : Fast::StaticBakeEntryInfo{};
        const char* state = StateName(h, entry);
        // reason= last: it is free text, the rest of the line.
        ConsoleSink::Addf(lines, "op=props list=%s scene=0x%X room=%d state=%s key=%p draws=%u tris=%u reason=%s",
                          kv.first.c_str(), (unsigned)h.room.first, (int)h.room.second, state, (void*)h.key,
                          entry.draws, entry.tris, entry.rejectReason != nullptr ? entry.rejectReason : "none");
    }
}

} // namespace ArchiveProps
