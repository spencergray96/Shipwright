// The boat prototype's RS model (sturdy-bassoon#256): the Sailing skiff's hull, loc 59503, as one archive prop list
// with a single copy at the origin, turn 0 - which is the kind in its own model space (the lit export sits on the
// origin rule; only the generator's turn and move put copies into world space). Rs_Boat draws it under its own
// matrix, so it follows the hull's position, yaw, bob and rock.
//
// The archive is `rs-props-rs_boat_skiff.o2r` in mods/, made by generate_props.py from a one-line placement list
// (docs/notes/2026-10-24-sailing-boat-prototype.md says how). Not in mods/: the boat draws its placeholder box.
//
// Held, not re-resolved, as ArchivePropLists.cpp holds a room's lists: the list's address is its resource's
// instruction vector, stable only while the resource lives. And loaded by its exact path, which never looks at alt/.

#include "RsBoat.h"

#include <memory>
#include <string>

#include <fast/resource/type/DisplayList.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <spdlog/spdlog.h>

namespace {

const char* const kSkiffList = "objects/rs_props/rs_boat_skiff/props";

std::shared_ptr<Fast::DisplayList> sSkiff;
bool sLooked = false;

} // namespace

extern "C" Gfx* RsBoat_ModelList(void) {
    if (!sLooked) {
        sLooked = true;
        auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
        if (!resources->GetArchiveManager()->HasFile(kSkiffList)) {
            SPDLOG_INFO("[rs_boat] {} is in no mounted archive: the boat draws its placeholder box", kSkiffList);
            return nullptr;
        }
        sSkiff = std::dynamic_pointer_cast<Fast::DisplayList>(resources->LoadResource(kSkiffList, true));
        if (sSkiff == nullptr || sSkiff->Instructions.empty()) {
            SPDLOG_WARN("[rs_boat] {} is not a display list: the boat draws its placeholder box", kSkiffList);
            sSkiff = nullptr;
            return nullptr;
        }
        SPDLOG_INFO("[rs_boat] drawing the skiff from {}", kSkiffList);
    }
    return sSkiff != nullptr ? (Gfx*)sSkiff->Instructions.data() : nullptr;
}
