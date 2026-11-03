// The inventory's two tables and the icons' paths (sturdy-bassoon#286, RsItems.h). The store is RsItems.cpp's.

#include "RsItems.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <spdlog/spdlog.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>

#include "align_asset_macro.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

namespace {

// COUNTED ITEMS. The caps are the owner's starting number (999, 2026-11-02), one per row so an item can differ.
// RS's own names; `plural` is how a sentence names several ("You can't carry any more oak planks.").
const RsItemDef kItems[RS_ITEM_COUNT] = {
    { RS_ITEM_LOGS, "logs", "Logs", "logs", 999 },
    { RS_ITEM_OAK_LOGS, "oak_logs", "Oak logs", "oak logs", 999 },
    { RS_ITEM_PLANK, "plank", "Plank", "planks", 999 },
    { RS_ITEM_OAK_PLANK, "oak_plank", "Oak plank", "oak planks", 999 },
    { RS_ITEM_STEEL_NAILS, "steel_nails", "Steel nails", "steel nails", 999 },
    { RS_ITEM_DRAGON_NAILS, "dragon_nails", "Dragon nails", "dragon nails", 999 },
};

// KEY ITEMS. The flag is the save's; this row is how it is shown.
const RsKeyItemDef kKeys[RS_KEY_ITEM_COUNT] = {
    { RS_KEY_ITEM_HAMMER, "hammer", "Hammer", RS_KEY_CAT_TOOLS, WORLD_FLAG_KEY_HAMMER,
      "You got a hammer!&Good for nails, and for&fixing a boat." },
    { RS_KEY_ITEM_SAW, "saw", "Saw", RS_KEY_CAT_TOOLS, WORLD_FLAG_KEY_SAW,
      "You got a saw!&Now logs can become planks." },
    { RS_KEY_ITEM_SKIFF_SCHEMATIC, "skiff_schematic", "Skiff schematic", RS_KEY_CAT_SCHEMATICS,
      WORLD_FLAG_KEY_SKIFF_SCHEMATIC, "You found a skiff schematic!&A shipwright could build&a boat from this." },
    { RS_KEY_ITEM_DEBUG, "debug_key", "Debug key item", RS_KEY_CAT_KEEPSAKES, WORLD_FLAG_DEBUG_KEY_ITEM, nullptr },
};

const char* const kCategories[RS_KEY_CAT_COUNT] = { "tools", "schematics", "keepsakes", "quest" };

// THE ICON PATHS, built once from the tokens. A path handed to gDPLoadTextureBlock must sit at an EVEN address, or
// the renderer takes it for raw pixels (RsItemArt.h, "ALIGN_ASSET(2) IS LOAD-BEARING"); a string literal promises
// no alignment, so they live in aligned rows of their own.
constexpr size_t kPathLen = 64;
struct alignas(2) IconPath {
    char path[kPathLen];
};
// [0] the inventory icon, [1] the ground icon.
IconPath sItemPaths[2][RS_ITEM_COUNT];
IconPath sKeyPaths[2][RS_KEY_ITEM_COUNT];
bool sItemFound[2][RS_ITEM_COUNT];
bool sKeyFound[2][RS_KEY_ITEM_COUNT];

bool Problem(const char* what, int32_t index) {
    SPDLOG_ERROR("RsItems: catalogue {}[{}] is wrong", what, index);
    assert(false && "inventory catalogue is wrong");
    return false;
}

bool TokenClean(const char* token) {
    if (token == nullptr || token[0] == '\0' || std::strlen(token) > 40) {
        return false;
    }
    for (const char* c = token; *c != '\0'; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) {
            return false;
        }
    }
    return true;
}

// The archive lookup. A path that does not resolve is not an error the player sees: the item draws its placeholder.
// It IS one the renderer would see - an unresolved texture skips the command after it and draws garbage
// (RsItemArt.cpp) - which is why every draw asks RsItems_IconFound first, and why this runs at boot, once.
bool Resolve(IconPath& out, const char* token, int32_t ground) {
    std::snprintf(out.path, kPathLen, ground ? "__OTR__rs/items/ground/%s" : "__OTR__rs/items/%s", token);
    return Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(out.path) != nullptr;
}

// The tables' own consistency and the icons, at startup. A slip in a table is a bug in this file; a missing icon
// is the normal state of a tree that has not packed mods/rs-items.o2r (sturdy-bassoon tools/items/), logged once.
void InitCatalogue() {
    for (int32_t i = 0; i < RS_ITEM_COUNT; i++) {
        const RsItemDef& d = kItems[i];
        if (d.id != i || !TokenClean(d.token) || d.name == nullptr || d.plural == nullptr || d.cap < 1 ||
            d.cap > RS_ITEM_COUNT_MAX) {
            Problem("item", i);
        }
    }
    for (int32_t i = 0; i < RS_KEY_ITEM_COUNT; i++) {
        const RsKeyItemDef& d = kKeys[i];
        if (d.id != i || !TokenClean(d.token) || d.name == nullptr || d.category < 0 ||
            d.category >= RS_KEY_CAT_COUNT || d.worldFlag < 0 || d.worldFlag >= WORLD_FLAG_MAX ||
            WORLD_FLAG_IS_PLACEMENT(d.worldFlag)) {
            Problem("key item", i);
        }
        for (int32_t j = 0; j < i; j++) {
            if (kKeys[j].worldFlag == d.worldFlag) {
                Problem("key item (a flag two share)", i);
            }
        }
    }
    // One token space: the console's `items give <token>` and the icon archive take either kind.
    for (int32_t i = 0; i < RS_ITEM_COUNT + RS_KEY_ITEM_COUNT; i++) {
        const char* a = i < RS_ITEM_COUNT ? kItems[i].token : kKeys[i - RS_ITEM_COUNT].token;
        for (int32_t j = 0; j < i; j++) {
            const char* b = j < RS_ITEM_COUNT ? kItems[j].token : kKeys[j - RS_ITEM_COUNT].token;
            if (std::strcmp(a, b) == 0) {
                Problem("token (two share it)", i);
            }
        }
    }

    int32_t missing = 0;
    for (int32_t ground = 0; ground < 2; ground++) {
        for (int32_t i = 0; i < RS_ITEM_COUNT; i++) {
            sItemFound[ground][i] = Resolve(sItemPaths[ground][i], kItems[i].token, ground);
            missing += sItemFound[ground][i] ? 0 : 1;
        }
        for (int32_t i = 0; i < RS_KEY_ITEM_COUNT; i++) {
            sKeyFound[ground][i] = Resolve(sKeyPaths[ground][i], kKeys[i].token, ground);
            missing += sKeyFound[ground][i] ? 0 : 1;
        }
    }
    if (missing > 0) {
        SPDLOG_WARN("RsItems: {} of {} item icons missing (mods/rs-items.o2r not packed?); they draw a placeholder",
                    missing, 2 * (RS_ITEM_COUNT + RS_KEY_ITEM_COUNT));
    } else {
        SPDLOG_INFO("RsItems: every item icon resolved");
    }
}

// ShipInit "*" functions re-run on preset apply and config load; the check is a pure read, harmless twice.
RegisterShipInitFunc rsItemCatalogueInitFunc(InitCatalogue);

} // namespace

extern "C" const RsItemDef* RsItems_Def(int32_t item) {
    return item >= 0 && item < RS_ITEM_COUNT ? &kItems[item] : nullptr;
}

extern "C" int32_t RsItems_ByToken(const char* token) {
    for (int32_t i = 0; token != nullptr && i < RS_ITEM_COUNT; i++) {
        if (std::strcmp(kItems[i].token, token) == 0) {
            return i;
        }
    }
    return -1;
}

extern "C" const RsKeyItemDef* RsKeyItems_Def(int32_t key) {
    return key >= 0 && key < RS_KEY_ITEM_COUNT ? &kKeys[key] : nullptr;
}

extern "C" int32_t RsKeyItems_ByToken(const char* token) {
    for (int32_t i = 0; token != nullptr && i < RS_KEY_ITEM_COUNT; i++) {
        if (std::strcmp(kKeys[i].token, token) == 0) {
            return i;
        }
    }
    return -1;
}

extern "C" int32_t RsKeyItems_ByFlag(int32_t worldFlag) {
    for (int32_t i = 0; i < RS_KEY_ITEM_COUNT; i++) {
        if (kKeys[i].worldFlag == worldFlag) {
            return i;
        }
    }
    return -1;
}

extern "C" const char* RsKeyItems_CategoryName(int32_t category) {
    return category >= 0 && category < RS_KEY_CAT_COUNT ? kCategories[category] : "?";
}

extern "C" const char* RsItems_IconPath(int32_t item, int32_t ground) {
    return item >= 0 && item < RS_ITEM_COUNT ? sItemPaths[ground ? 1 : 0][item].path : nullptr;
}

extern "C" const char* RsKeyItems_IconPath(int32_t key, int32_t ground) {
    return key >= 0 && key < RS_KEY_ITEM_COUNT ? sKeyPaths[ground ? 1 : 0][key].path : nullptr;
}

extern "C" int32_t RsItems_IconFound(int32_t item, int32_t ground) {
    return item >= 0 && item < RS_ITEM_COUNT && sItemFound[ground ? 1 : 0][item] ? 1 : 0;
}

extern "C" int32_t RsKeyItems_IconFound(int32_t key, int32_t ground) {
    return key >= 0 && key < RS_KEY_ITEM_COUNT && sKeyFound[ground ? 1 : 0][key] ? 1 : 0;
}
