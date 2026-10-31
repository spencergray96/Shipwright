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
#include "soh/Enhancements/rs/boats/RsBoatFit.h"

#include <map>
#include <memory>
#include <sstream>
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

// By list path, once each: a null entry is a list with no recolour file, or one that would not read.
std::map<std::string, std::unique_ptr<RsBoatRecolour>> sRecolours;

// generate_props.py's THE RECOLOUR FILE: `segment`, `locs <part> <prop> <loc>...`, `slot <k> <part> <look> <n> <rgb>...`.
// A slot out of order, a part or look the build does not know, or more slots or tiers than fit: the whole file is
// refused, logged, and an empty table returned, so every slot the list calls is a no-op and it draws as its archive
// has it. Only a list with no file at all (an archive from before tiers) gets none.
std::unique_ptr<RsBoatRecolour> ReadRecolour(s32 kind, const std::string& path) {
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    if (!archives->HasFile(path)) {
        return nullptr;
    }
    auto file = archives->LoadFile(path);
    if (file == nullptr || file->Buffer == nullptr) {
        SPDLOG_WARN("[rs_boat] {} would not load: its list draws as its archive has it", path);
        return std::make_unique<RsBoatRecolour>();
    }
    auto out = std::make_unique<RsBoatRecolour>();
    out->count = 0;
    std::istringstream text(std::string(file->Buffer->begin() + file->BufferOffset, file->Buffer->end()));
    std::string line;
    std::map<std::string, bool> checked;
    while (std::getline(text, line)) {
        std::istringstream w(line);
        std::string what;
        w >> what;
        if (what == "segment") {
            // The list calls this segment at this stride: RsBoat_Draw fills RS_BOAT_LOOK_SEGMENT at its own.
            int32_t segment = -1, stride = -1;
            std::string strideWord;
            w >> segment >> strideWord >> stride;
            if (segment != RS_BOAT_LOOK_SEGMENT || strideWord != "stride" || stride != RS_BOAT_RECOLOUR_STRIDE) {
                SPDLOG_ERROR("[rs_boat] {}: '{}' is not segment {} stride {}: its list draws as its archive has it", path,
                             line, RS_BOAT_LOOK_SEGMENT, RS_BOAT_RECOLOUR_STRIDE);
                return std::make_unique<RsBoatRecolour>();
            }
        } else if (what == "locs") {
            // The first placement of each part is its own (a trim's follow the hull): its locs are the catalogue's.
            std::string part, prop;
            w >> part >> prop;
            const int32_t slot = RsBoatFit_SlotByName(part.c_str());
            if (checked[part]) {
                continue;
            }
            checked[part] = true;
            int32_t loc;
            for (int32_t tier = 0; w >> loc; tier++) {
                const RsBoatTier* row = RsBoatFit_Tier(kind, slot, tier);
                if (row == nullptr || row->loc != loc) {
                    SPDLOG_WARN("[rs_boat] {}: {} tier {} draws loc {}, and the catalogue says {}", path, part, tier, loc,
                                row == nullptr ? -1 : row->loc);
                }
            }
        } else if (what == "slot") {
            int32_t k = -1, n = -1;
            std::string part, look;
            w >> k >> part >> look >> n;
            RsBoatRecolourSlot s = {};
            s.part = part == "-" ? -1 : RsBoatFit_SlotByName(part.c_str());
            s.look = look == "-" ? -1 : RsBoatFit_LookByName(look.c_str());
            s.tierCount = n;
            bool ok = !w.fail() && k == out->count && k < RS_BOAT_RECOLOUR_MAX_SLOTS && n >= 0 &&
                      n <= RS_BOAT_RECOLOUR_MAX_TIERS && (part == "-" || s.part >= 0) && (look == "-" || s.look >= 0);
            for (int32_t t = 0; ok && t < n; t++) {
                int r = -1, g = -1, b = -1;
                ok = static_cast<bool>(w >> r >> g >> b) && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255;
                s.rgb[t][0] = r, s.rgb[t][1] = g, s.rgb[t][2] = b;
            }
            if (!ok) {
                SPDLOG_ERROR("[rs_boat] {}: slot line '{}' does not read: its list draws as its archive has it", path, line);
                return std::make_unique<RsBoatRecolour>();
            }
            out->slots[out->count++] = s;
        }
    }
    SPDLOG_INFO("[rs_boat] {}: {} recolour slots", path, out->count);
    return out;
}

} // namespace

extern "C" const RsBoatRecolour* RsBoat_Recolour(s32 kind, const char* listPath) {
    auto found = sRecolours.find(listPath);
    if (found == sRecolours.end()) {
        found = sRecolours.emplace(listPath, ReadRecolour(kind, std::string(listPath) + ".recolour")).first;
    }
    return found->second.get();
}

extern "C" Gfx* RsBoat_ModelList(const char* listPath) {
    auto found = sModels.find(listPath);
    if (found == sModels.end()) {
        found = sModels.emplace(listPath, Load(listPath)).first;
    }
    return found->second != nullptr ? (Gfx*)found->second->Instructions.data() : nullptr;
}
