// The boat's RS models (sturdy-bassoon#256, #262): each kind as one archive prop list in the boat's own model space -
// its hull at the origin, turn 0 (the lit export sits on the origin rule; only the generator's turn and move put
// copies into world space), and the skiff's Sailing parts (trim, keel, helm, sails) beside it at the template boats'
// offsets, welded onto the hull where RS shares their vertices. Rs_Boat draws it under its own matrix, so it follows
// the hull's position, yaw, bob and rock.
//
// Each archive is `rs-props-rs_boat_<kind>.o2r` in mods/, made by generate_props.py from the kind's committed
// placement list, soh/soh/custom/rs_boats/rs_boat_<kind>_props.json (docs/notes/2026-10-24-sailing-boat-prototype.md
// says how). Not in mods/: that kind draws its placeholder box.
//
// Held, not re-resolved, as ArchivePropLists.cpp holds a room's lists: the list's address is its resource's
// instruction vector, stable only while the resource lives. And loaded by its exact path, which never looks at alt/.


#include "RsBoat.h"

#include <map>
#include <memory>
#include <string>

#include <fast/resource/type/DisplayList.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <spdlog/spdlog.h>

namespace {

// By list path, once each: a null entry is a path already looked for and not found.
std::map<std::string, std::shared_ptr<Fast::DisplayList>> sModels;

std::shared_ptr<Fast::DisplayList> Load(const char* path) {
    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!resources->GetArchiveManager()->HasFile(path)) {
        SPDLOG_INFO("[rs_boat] {} is in no mounted archive: that boat draws its placeholder box", path);
        return nullptr;
    }
    auto list = std::dynamic_pointer_cast<Fast::DisplayList>(resources->LoadResource(path, true));
    if (list == nullptr || list->Instructions.empty()) {
        SPDLOG_WARN("[rs_boat] {} is not a display list: that boat draws its placeholder box", path);
        return nullptr;
    }
    SPDLOG_INFO("[rs_boat] drawing a boat from {}", path);
    return list;
}

} // namespace


extern "C" Gfx* RsBoat_ModelList(const char* listPath) {
    auto found = sModels.find(listPath);
    if (found == sModels.end()) {
        found = sModels.emplace(listPath, Load(listPath)).first;
    }
    return found->second != nullptr ? (Gfx*)found->second->Instructions.data() : nullptr;
}
