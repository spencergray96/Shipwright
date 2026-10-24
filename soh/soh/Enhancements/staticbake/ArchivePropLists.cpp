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
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <fast/FlipBook.h>
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

// Actor-drawn copies (ArchivePropLists.h, ACTOR-DRAWN COPIES). A declared path's `.anim` table, read once a
// bake group; each copy's lists once it is asked for; both released with the rest in ReleaseHeld.
struct AnimLine {
    std::string id;
    s32 check = 0;
    s32 frameStep = 0;
    s32 startPose = 0;
    s32 startCycle = 0;
    std::vector<std::pair<s32, s32>> poses; // (frame, cycles)
};
struct AnimTable {
    bool read = false;
    bool ok = false;
    std::vector<AnimLine> lines;
};
std::map<std::string, AnimTable> sAnimTables;                            // by declared path
// A held copy: the record the actor reads, and the arrays it points into, sized to the copy's own poses (#204,
// in-between poses; ArchivePropLists.h, SIZED PER COPY). Moving one keeps the arrays where they are, so `c`'s
// pointers stay good in the map; they go when the map clears (ReleaseHeld), after the copy's flips.
struct HeldAnimCopy {
    ArchivePropsAnimCopy c = {};
    std::vector<s32> frames;
    std::vector<s32> cycles;
    std::unique_ptr<Gfx* [][ARCHIVE_PROPS_PASSES]> lists;
};
std::map<std::pair<std::string, s32>, HeldAnimCopy> sAnimCopies; // by (declared path, copy)
std::map<std::string, ArchiveList> sAnimLists;                           // every list a copy holds, by path
u32 sGeneration = 1;
constexpr const char* ANIM_SUFFIX = ".anim";

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

// The scroll files beside a list, by the tile they register (sturdy-bassoon#231): `.scroll` is tile 0,
// TEXEL0, and `.scroll1` tile 1, TEXEL1, the second layer of a two-layer material. The second is a
// sidecar of the same form rather than a field on `.scroll`, because a build from before #231 refuses a
// scroll file with a line it cannot read, whole: it never opens `.scroll1`, so there tile 0 still scrolls.
constexpr const char* SCROLL_SUFFIXES[Fast::STATIC_BAKE_SCROLL_TILES] = { ".scroll", ".scroll1" };

// Registers the textures `<path>.scroll` names (and `<path>.scroll1`, for tile 1), before the list is
// first offered to the bake: a rate is read when a draw is recorded (libultraship's "Texture scroll"), so
// it must be in the registry before the list's first draw. Idempotent, as StaticBakeSetTextureScroll is,
// so a list resolved again (a new bake group) registers the same rates again and changes nothing. Each
// file stands alone: one that does not read registers nothing, and the other is registered as it reads.
// Returns how many lines registered, both files together.
uint32_t RegisterScrolls(const char* path) {
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    uint32_t registered = 0;
    for (int tile = 0; tile < Fast::STATIC_BAKE_SCROLL_TILES; tile++) {
        const std::string scrollPath = std::string(path) + SCROLL_SUFFIXES[tile];
        if (!archives->HasFile(scrollPath)) {
            continue;
        }
        std::shared_ptr<Ship::File> file = archives->LoadFile(scrollPath);
        std::vector<ScrollLine> lines;
        if (file == nullptr || file->Buffer == nullptr || !ParseScrollFile(*file->Buffer, file->BufferOffset, lines)) {
            SPDLOG_WARN("[staticbake] archive prop list {}: its scroll file {} does not read, so none of its textures "
                        "scroll on tile {} (sturdy-bassoon#187 A2, #231)",
                        path, scrollPath, tile);
            continue;
        }
        uint32_t changed = 0;
        for (const ScrollLine& s : lines) {
            changed += Fast::StaticBakeSetTextureScroll(s.path.c_str(), tile, s.du, s.dv) ? 1 : 0;
        }
        SPDLOG_INFO("[staticbake] archive prop list {}: {} scrolling texture(s) on tile {} registered before its first "
                    "draw, {} changed (sturdy-bassoon#187 A2, #231)",
                    path, lines.size(), tile, changed);
        registered += (uint32_t)lines.size();
    }
    return registered;
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
    // scroll file (and one `.scroll1`, #231) for the two: the registry binds a rate to an image path's
    // tile, whichever list draws it. A path with actor-drawn copies (`.anim`, #204) registers them too,
    // even when its own lists are empty: the copies' textures are the same image paths.
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    if (h.lists[ARCHIVE_PROPS_PASS_OPA].state == Resolved::Listed ||
        h.lists[ARCHIVE_PROPS_PASS_XLU].state == Resolved::Listed ||
        archives->HasFile(std::string(path) + ANIM_SUFFIX)) {
        h.scrolls = RegisterScrolls(path);
    }
    return h;
}

// `<path>.anim`'s lines, or false when one does not read (ArchivePropLists.h's form). '#' lines skipped.
bool ParseAnimTable(const std::vector<char>& bytes, size_t offset, std::vector<AnimLine>& out) {
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
        AnimLine a;
        if (!(fields >> a.id >> a.check >> a.frameStep >> a.startPose >> a.startCycle)) {
            return false;
        }
        std::string pose;
        while (fields >> pose) {
            const size_t colon = pose.find(':');
            if (colon == std::string::npos) {
                return false;
            }
            char* end = nullptr;
            const long frame = std::strtol(pose.c_str(), &end, 10);
            if (end != pose.c_str() + colon || colon == 0) {
                return false;
            }
            const long cycles = std::strtol(pose.c_str() + colon + 1, &end, 10);
            if (frame < 0 || cycles < 1 || *end != '\0') {
                return false;
            }
            a.poses.emplace_back((s32)frame, (s32)cycles);
        }
        if (a.poses.size() < 2 || a.poses.size() > ARCHIVE_PROPS_ANIM_POSES_MAX || a.startPose < 0 ||
            a.startPose >= (s32)a.poses.size() || a.startCycle < 0 || a.startCycle >= a.poses[a.startPose].second) {
            return false;
        }
        out.push_back(std::move(a));
    }
    return true;
}

const AnimTable& TableFor(const std::string& path) {
    AnimTable& t = sAnimTables[path];
    if (t.read) {
        return t;
    }
    t.read = true;
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    const std::string tablePath = path + ANIM_SUFFIX;
    if (!archives->HasFile(tablePath)) {
        return t;
    }
    std::shared_ptr<Ship::File> file = archives->LoadFile(tablePath);
    if (file == nullptr || file->Buffer == nullptr || !ParseAnimTable(*file->Buffer, file->BufferOffset, t.lines)) {
        SPDLOG_WARN(
            "[staticbake] archive prop list {}: its flipbook table {} does not read, so none of its copies draw "
            "(sturdy-bassoon#204)",
            path, tablePath);
        t.lines.clear();
        return t;
    }
    t.ok = true;
    SPDLOG_INFO("[staticbake] archive prop list {}: {} flipbook copies in its table (sturdy-bassoon#204)", path,
                t.lines.size());
    return t;
}

// One of a copy's lists, held for the bake group and offered to the bake and the mips as a room's are. NULL
// for a list with nothing to submit; `missing` set when an opaque one is not in the archive at all.
Gfx* HoldAnimList(const std::string& path, bool optional, bool* missing) {
    auto it = sAnimLists.find(path);
    if (it == sAnimLists.end()) {
        it = sAnimLists.emplace(path, ResolveList(path, optional)).first;
        if (it->second.state == Resolved::Listed) {
            Fast::StaticBakeRegister(it->second.key);
            Fast::TextureMipsRegisterDisplayList(it->second.key);
        }
    }
    const ArchiveList& l = it->second;
    if (!optional && (l.state == Resolved::Missing || l.state == Resolved::NotDisplayList)) {
        *missing = true;
    }
    return l.state == Resolved::Listed ? l.key : nullptr;
}

// A held copy's flips (ArchivePropLists.h, FLIP HEADS): one a pass with a list in some pose, on the copy's own
// clock - a loop from the clock's zero plus its random start, as Rs_AnimProp counts it; a play-once one from
// the origin its actor sets.
void RegisterFlips(ArchivePropsAnimCopy& c, const std::string& path, s32 copy) {
    const bool loops = c.frameStep >= 1 && c.frameStep <= c.poseCount;
    Fast::FlipBookDesc d;
    d.poseCount = c.poseCount;
    d.frameStep = c.frameStep;
    d.cycles.assign(c.cycles, c.cycles + c.poseCount);
    d.poses.resize((size_t)c.poseCount + 1);
    if (loops) {
        for (s32 k = 0; k < c.startPose && k < c.poseCount; k++) {
            d.cycleOffset += c.cycles[k];
        }
        d.cycleOffset += c.startCycle;
    }
    for (s32 pass = 0; pass < ARCHIVE_PROPS_PASSES; pass++) {
        bool any = false;
        for (s32 k = 0; k <= c.poseCount; k++) {
            d.poses[k] = c.lists[k][pass];
            any = any || d.poses[k] != nullptr;
        }
        c.flipSerial[pass] = any ? Fast::FlipBookRegister(&c.lists[0][pass], d) : 0;
        if (any && c.flipSerial[pass] == 0) {
            // Its actor writes this pass's list per tick instead (RsAnimProp.c), so it still draws.
            SPDLOG_WARN("[staticbake] archive prop list {}: copy {}'s {} flip was refused (its clock does not read), "
                        "so it is picked per tick (sturdy-bassoon#204 C4b)",
                        path, copy, pass == ARCHIVE_PROPS_PASS_OPA ? "opaque" : "translucent");
        }
    }
}

void UnregisterFlips(const ArchivePropsAnimCopy& c) {
    for (s32 pass = 0; pass < ARCHIVE_PROPS_PASSES; pass++) {
        if (c.flipSerial[pass] != 0) {
            Fast::FlipBookUnregister(&c.lists[0][pass]);
        }
    }
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

extern "C" const ArchivePropsAnimCopy* ArchiveProps_AnimCopy(PlayState* play, s32 roomNum, s32 list, s32 copy,
                                                             s32 check, s32* miss) {
    *miss = ARCHIVE_PROPS_ANIM_NO_LIST;
    if (play == nullptr) {
        return nullptr;
    }
    auto declared = sDeclared.find({ play->sceneNum, roomNum });
    if (declared == sDeclared.end() || list < 0 || list >= (s32)declared->second.size()) {
        return nullptr;
    }
    const std::string path = declared->second[list];
    auto held = sAnimCopies.find({ path, copy });
    if (held != sAnimCopies.end()) {
        *miss = ARCHIVE_PROPS_ANIM_OK;
        return &held->second.c;
    }
    // The path's own lists and scrolls first, as its room would: an actor may ask before the room offers.
    if (sHeld.find(path) == sHeld.end()) {
        sHeld.emplace(path, Resolve(path.c_str()));
    }
    const AnimTable& table = TableFor(path);
    if (!table.ok) {
        *miss = ARCHIVE_PROPS_ANIM_NO_TABLE;
        return nullptr;
    }
    if (copy < 0 || copy >= (s32)table.lines.size()) {
        *miss = ARCHIVE_PROPS_ANIM_NO_LINE;
        return nullptr;
    }
    const AnimLine& line = table.lines[copy];
    if (line.check != check) {
        *miss = ARCHIVE_PROPS_ANIM_CHECK;
        SPDLOG_WARN("[staticbake] archive prop list {}: copy {} ({}) has check {}, its row {}: the scene and the "
                    "archive are from two exports, so it draws nothing (sturdy-bassoon#204)",
                    path, copy, line.id, line.check, check);
        return nullptr;
    }
    // Its arrays, as long as its own poses (SIZED PER COPY): built here, moved into the map whole on success.
    HeldAnimCopy h;
    const s32 poses = (s32)line.poses.size();
    h.frames.resize(poses);
    h.cycles.resize(poses);
    h.lists.reset(new Gfx* [(size_t)poses + 1][ARCHIVE_PROPS_PASSES]());
    ArchivePropsAnimCopy& c = h.c;
    c.poseCount = poses;
    c.frameStep = line.frameStep;
    c.startPose = line.startPose;
    c.startCycle = line.startCycle;
    c.frames = h.frames.data();
    c.cycles = h.cycles.data();
    c.lists = h.lists.get();
    const std::string dir = path.substr(0, path.rfind('/')) + "/copies/" + line.id + "/";
    bool missing = false;
    for (s32 k = 0; k <= c.poseCount; k++) {
        const std::string name = k < c.poseCount ? "f" + std::to_string(line.poses[k].first) : std::string("rest");
        if (k < c.poseCount) {
            h.frames[k] = line.poses[k].first;
            h.cycles[k] = line.poses[k].second;
        }
        c.lists[k][ARCHIVE_PROPS_PASS_OPA] = HoldAnimList(dir + name, false, &missing);
        c.lists[k][ARCHIVE_PROPS_PASS_XLU] = HoldAnimList(dir + name + XLU_SUFFIX, true, &missing);
    }
    if (missing) {
        *miss = ARCHIVE_PROPS_ANIM_NO_POSE;
        SPDLOG_WARN("[staticbake] archive prop list {}: copy {} ({}) is missing a pose list under {}, so it draws "
                    "nothing (sturdy-bassoon#204)",
                    path, copy, line.id, dir);
        return nullptr;
    }
    *miss = ARCHIVE_PROPS_ANIM_OK;
    ArchivePropsAnimCopy& stored = sAnimCopies.emplace(std::make_pair(path, copy), std::move(h)).first->second.c;
    RegisterFlips(stored, path, copy);
    return &stored;
}

extern "C" void ArchiveProps_AnimSetOrigin(const ArchivePropsAnimCopy* copy, double origin) {
    if (copy == nullptr) {
        return;
    }
    for (s32 pass = 0; pass < ARCHIVE_PROPS_PASSES; pass++) {
        if (copy->flipSerial[pass] != 0) {
            Fast::FlipBookSetOrigin(&copy->lists[0][pass], origin);
        }
    }
}

extern "C" u32 ArchiveProps_Generation(void) {
    return sGeneration;
}

extern "C" s32 ArchiveProps_XluSubmitted(void) {
    return ArchiveProps::XluSubmitted() ? 1 : 0;
}

extern "C" const char* ArchiveProps_AnimMissName(s32 miss) {
    switch (miss) {
        case ARCHIVE_PROPS_ANIM_OK:
            return "ok";
        case ARCHIVE_PROPS_ANIM_NO_LIST:
            return "no_list";
        case ARCHIVE_PROPS_ANIM_NO_TABLE:
            return "no_table";
        case ARCHIVE_PROPS_ANIM_NO_LINE:
            return "no_line";
        case ARCHIVE_PROPS_ANIM_CHECK:
            return "check";
        case ARCHIVE_PROPS_ANIM_NO_POSE:
            return "no_pose";
        default:
            return "unknown";
    }
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
    for (const auto& kv : sAnimLists) {
        if (kv.second.key != nullptr) {
            Fast::TextureMipsUnregisterDisplayList(kv.second.key);
        }
    }
    for (auto& drawn : sDrawn) {
        drawn.clear();
    }
    // The flips name the copies' lists (#204 C4b): out of the registry before the lists go. Nothing else
    // registers a flip today, so this empties it; anything left is cleared too, with a line, since its lists may
    // be about to go. (What checks a pointer at use is the registry's Debug `unheld` count.)
    for (const auto& kv : sAnimCopies) {
        UnregisterFlips(kv.second.c);
    }
    const uint32_t leftover = Fast::FlipBookGetStats().registered;
    if (leftover != 0) {
        SPDLOG_ERROR("[staticbake] {} flip heads were still registered after the copies' release: clearing them "
                     "(sturdy-bassoon#204 C4b)",
                     leftover);
    }
    Fast::FlipBookClear();
    sHeld.clear();
    // The actor-drawn copies' too (#204): a live Rs_AnimProp sees the generation move and asks again.
    sAnimCopies.clear();
    sAnimLists.clear();
    sAnimTables.clear();
    sGeneration++;
}

uint32_t AnimLists() {
    uint32_t n = 0;
    for (const auto& kv : sAnimLists) {
        n += kv.second.state == Resolved::Listed ? 1 : 0;
    }
    return n;
}

void AnimEntryStates(const ArchivePropsAnimCopy& copy, uint32_t* baked, uint32_t* rejected, uint32_t* unbaked) {
    for (s32 k = 0; k <= copy.poseCount; k++) {
        for (s32 pass = 0; pass < ARCHIVE_PROPS_PASSES; pass++) {
            if (copy.lists[k][pass] == nullptr) {
                continue;
            }
            switch (Fast::StaticBakeGetEntry(copy.lists[k][pass]).state) {
                case Fast::StaticBakeEntryState::Baked:
                    (*baked)++;
                    break;
                case Fast::StaticBakeEntryState::Rejected:
                    (*rejected)++;
                    break;
                default:
                    (*unbaked)++;
                    break;
            }
        }
    }
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
