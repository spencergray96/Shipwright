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
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <utility>

#include <fast/StaticMeshCache.h>
#include <fast/TextureMips.h>
#include <fast/resource/type/DisplayList.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/resource/File.h>
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

// One resolved archive display list: a declared path's own, or its `.xlu`.
struct ArchiveList {
    Resolved state = Resolved::Missing;
    std::shared_ptr<Fast::DisplayList> resource;
    Gfx* key = nullptr;
};

// What the registry holds of one declared path, for as long as the bake group does (ReleaseHeld): the
// list itself, drawn in the room's opaque pass, and `<path>.xlu`, its translucent pairs, drawn in the
// translucent pass (sturdy-bassoon#216 T2). A missing or empty list is held too, so its log line is
// written once per group rather than once per room load.
struct Held {
    ArchiveList lists[ARCHIVE_PROPS_PASSES]; // ARCHIVE_PROPS_PASS_OPA, ARCHIVE_PROPS_PASS_XLU
    RoomKey room = { -1, -1 };               // the last room that offered it, for `staticbake props`
    uint32_t scrolls = 0;                    // textures its scroll file registered (sturdy-bassoon#187 A2)
};
std::map<std::string, Held> sHeld;

// The lists each room draws, one vector per pass, rebuilt every time the room is offered.
std::map<RoomKey, std::vector<Gfx*>> sDrawn[ARCHIVE_PROPS_PASSES];

// `<path>.xlu`: where the generator writes a list's translucent pairs (generate_props.py, "THE
// TRANSLUCENT LIST"). Found by this convention so a scene's C declares one path per list, as before.
constexpr const char* XLU_SUFFIX = ".xlu";

// The scroll file's lines, or false when one does not read: `<path> <du> <dv>`, '#' lines skipped.
struct ScrollLine {
    std::string path;
    float du = 0.0f;
    float dv = 0.0f;
};
bool ParseScrollFile(const std::vector<char>& bytes, size_t offset, std::vector<ScrollLine>& out) {
    std::istringstream in(std::string(bytes.begin() + (std::ptrdiff_t)std::min(offset, bytes.size()), bytes.end()));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        ScrollLine s;
        std::string rest;
        if (!(fields >> s.path >> s.du >> s.dv) || (fields >> rest) || !std::isfinite(s.du) || !std::isfinite(s.dv)) {
            return false;
        }
        out.push_back(std::move(s));
    }
    return true;
}

// Registers the textures `<path>.scroll` names, before the list is first offered to the bake: a rate is
// read when a draw is recorded (libultraship's "Texture scroll"), so it must be in the registry before
// the list's first draw. Idempotent, as StaticBakeSetTextureScroll is, so a list resolved again (a new
// bake group) registers the same rates again and changes nothing. Returns how many lines registered.
uint32_t RegisterScrolls(const char* path) {
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    const std::string scrollPath = std::string(path) + ".scroll";
    if (!archives->HasFile(scrollPath)) {
        return 0;
    }
    std::shared_ptr<Ship::File> file = archives->LoadFile(scrollPath);
    std::vector<ScrollLine> lines;
    if (file == nullptr || file->Buffer == nullptr || !ParseScrollFile(*file->Buffer, file->BufferOffset, lines)) {
        SPDLOG_WARN("[staticbake] archive prop list {}: its scroll file {} does not read, so none of its textures "
                    "scroll (sturdy-bassoon#187 A2)",
                    path, scrollPath);
        return 0;
    }
    uint32_t changed = 0;
    for (const ScrollLine& s : lines) {
        changed += Fast::StaticBakeSetTextureScroll(s.path.c_str(), s.du, s.dv) ? 1 : 0;
    }
    SPDLOG_INFO("[staticbake] archive prop list {}: {} scrolling texture(s) registered before its first draw, {} "
                "changed (sturdy-bassoon#187 A2)",
                path, lines.size(), changed);
    return (uint32_t)lines.size();
}

bool IsEmptyList(const Fast::DisplayList& dl) {
    return dl.Instructions.empty() || (uint8_t)(dl.Instructions[0].words.w0 >> 24) == (uint8_t)G_ENDDL;
}

// One list. `optional`: a list that is in no mounted archive is no list at all, silently - the
// translucent one, which only a map with translucent props has, and which an archive generated before
// sturdy-bassoon#216 T2 never has. Most chunks of a stitched scene have none, so no line per chunk.
ArchiveList ResolveList(const std::string& path, bool optional) {
    ArchiveList l;
    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    auto archives = resources->GetArchiveManager();
    if (!archives->HasFile(path)) {
        if (!optional) {
            SPDLOG_INFO("[staticbake] archive prop list {} is in no mounted archive: the room draws without it", path);
        }
        return l;
    }
    if (archives->HasFile("alt/" + path)) {
        SPDLOG_WARN("[staticbake] archive prop list {} has an alt/ twin, which is ignored: the list is held as "
                    "loaded from its own path (sturdy-bassoon#171)",
                    path);
    }
    l.resource = std::dynamic_pointer_cast<Fast::DisplayList>(resources->LoadResource(path, true));
    if (l.resource == nullptr) {
        l.state = Resolved::NotDisplayList;
        SPDLOG_WARN("[staticbake] archive prop list {} is not a display list: the room draws without it", path);
        return l;
    }
    if (IsEmptyList(*l.resource)) {
        l.state = Resolved::Empty;
        return l;
    }
    l.state = Resolved::Listed;
    l.key = (Gfx*)l.resource->Instructions.data();
    return l;
}

// A declared path: the list and its translucent twin.
Held Resolve(const char* path) {
    Held h;
    h.lists[ARCHIVE_PROPS_PASS_OPA] = ResolveList(path, false);
    h.lists[ARCHIVE_PROPS_PASS_XLU] = ResolveList(std::string(path) + XLU_SUFFIX, true);
    // Before OfferRoom hands either list to the bake (StaticBakeRegister), so before its first draw. One
    // scroll file for the two: the registry binds a rate to an image path, whichever list draws it.
    if (h.lists[ARCHIVE_PROPS_PASS_OPA].state == Resolved::Listed ||
        h.lists[ARCHIVE_PROPS_PASS_XLU].state == Resolved::Listed) {
        h.scrolls = RegisterScrolls(path);
    }
    return h;
}

const char* StateName(const ArchiveList& list, const Fast::StaticBakeEntryInfo& entry) {
    switch (list.state) {
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

extern "C" Gfx* const* ArchiveProps_RoomLists(PlayState* play, s32 roomNum, s32 pass, s32* count) {
    *count = 0;
    if (play == nullptr || pass < 0 || pass >= ARCHIVE_PROPS_PASSES || sDrawn[pass].empty()) {
        return nullptr;
    }
    // The comparison switch (sturdy-bassoon#216 T2, `staticbake xlu`): read at every draw, so it takes
    // effect on the next frame with no rebake. Off draws a map as it drew before T2, less its translucent
    // pairs - the opaque list never held them.
    if (pass == ARCHIVE_PROPS_PASS_XLU && !ArchiveProps::XluSubmitted()) {
        return nullptr;
    }
    auto it = sDrawn[pass].find({ play->sceneNum, roomNum });
    if (it == sDrawn[pass].end() || it->second.empty()) {
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
        for (auto& drawn : sDrawn) {
            drawn.erase(room);
        }
        return 0;
    }
    for (auto& drawn : sDrawn) {
        drawn[room].clear();
    }
    uint32_t offered = 0;
    for (const char* path : declared->second) {
        auto held = sHeld.find(path);
        if (held == sHeld.end()) {
            held = sHeld.emplace(path, Resolve(path)).first;
        }
        held->second.room = room;
        for (s32 pass = 0; pass < ARCHIVE_PROPS_PASSES; pass++) {
            const ArchiveList& list = held->second.lists[pass];
            std::vector<Gfx*>& drawn = sDrawn[pass][room];
            if (list.state != Resolved::Listed || std::find(drawn.begin(), drawn.end(), list.key) != drawn.end()) {
                continue;
            }
            // Keyed like any list: the bake replays a key wherever it is submitted, so the translucent
            // list bakes as the opaque one does, and its batches keep their order (AfterOpaque).
            Fast::StaticBakeRegister(list.key);
            // Its textures are the mod's own, mipmapped like the room's (#146): named here, withdrawn in
            // ReleaseHeld, so a list is in scope exactly while its resource is held.
            Fast::TextureMipsRegisterDisplayList(list.key);
            drawn.push_back(list.key);
            offered++;
        }
    }
    return offered;
}

void ReleaseHeld() {
    // Before the resources go: a list later allocated at a freed key's address must not be in scope.
    for (const auto& kv : sHeld) {
        for (const ArchiveList& list : kv.second.lists) {
            if (list.key != nullptr) {
                Fast::TextureMipsUnregisterDisplayList(list.key);
            }
        }
    }
    for (auto& drawn : sDrawn) {
        drawn.clear();
    }
    sHeld.clear();
}

namespace {
// The agent loop's switch, this session only (-1: none, the saved setting decides), as
// StaticBake_SetActive is to StaticBake_SetSetting: it never reaches the CVar store, so no later save
// can carry an agent's `xlu off` into the owner's config.
int32_t sXluSession = -1;
} // namespace

bool XluSubmitted() {
    if (sXluSession >= 0) {
        return sXluSession != 0;
    }
    return CVarGetInteger(CVAR_STATIC_BAKE_PROPS_XLU, ARCHIVE_PROPS_XLU_DEFAULT) != 0;
}

void SetXluSubmitted(bool on, bool save) {
    if (save) {
        CVarSetInteger(CVAR_STATIC_BAKE_PROPS_XLU, on ? 1 : 0);
        CVarSave();
        sXluSession = -1;
    } else {
        sXluSession = on ? 1 : 0;
    }
}

uint32_t XluLists() {
    uint32_t n = 0;
    for (const auto& kv : sHeld) {
        n += kv.second.lists[ARCHIVE_PROPS_PASS_XLU].state == Resolved::Listed ? 1 : 0;
    }
    return n;
}

void Describe(std::vector<std::string>& lines) {
    // One line per list held: a declared path's own, and its `.xlu` when the archive has one. A missing
    // `.xlu` is no list (Resolve), so a map generated before T2 prints what it always did.
    std::vector<std::pair<const std::pair<const std::string, Held>*, s32>> shown;
    for (const auto& kv : sHeld) {
        shown.emplace_back(&kv, ARCHIVE_PROPS_PASS_OPA);
        if (kv.second.lists[ARCHIVE_PROPS_PASS_XLU].state != Resolved::Missing) {
            shown.emplace_back(&kv, ARCHIVE_PROPS_PASS_XLU);
        }
    }
    ConsoleSink::Addf(lines, "op=props result=ok lists=%u", (unsigned)shown.size());
    for (const auto& [kv, pass] : shown) {
        const std::string& path = kv->first;
        const Held& h = kv->second;
        const ArchiveList& list = h.lists[pass];
        const Fast::StaticBakeEntryInfo entry =
            list.state == Resolved::Listed ? Fast::StaticBakeGetEntry(list.key) : Fast::StaticBakeEntryInfo{};
        const char* state = StateName(list, entry);
        const bool xlu = pass == ARCHIVE_PROPS_PASS_XLU;
        // pass= (#216 T2) after the fields older run scripts parse; reason= last: it is free text, the
        // rest of the line. A path's scroll file is counted on its opaque line.
        ConsoleSink::Addf(
            lines,
            "op=props list=%s%s scene=0x%X room=%d state=%s key=%p draws=%u tris=%u scrolls=%u pass=%s reason=%s",
            path.c_str(), xlu ? XLU_SUFFIX : "", (unsigned)h.room.first, (int)h.room.second, state, (void*)list.key,
            entry.draws, entry.tris, xlu ? 0u : h.scrolls, xlu ? "xlu" : "opa",
            entry.rejectReason != nullptr ? entry.rejectReason : "none");
    }
}

} // namespace ArchiveProps
