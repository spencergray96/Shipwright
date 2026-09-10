#include "RsPrefs.h"

#include <cassert>
#include <cstdint>
#include <spdlog/spdlog.h>

#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

// The store. A project-owned global, NOT a gSaveContext member, for the reasons WorldFlags.cpp and
// QuestStore.cpp record. The same accepted costs follow: savestates do not capture it, and the
// threaded save reads it live from this global rather than from the gSaveContext snapshot. It is
// one byte written only from the game thread, so there is nothing here to tear.
static uint8_t sFloorConvention = RS_FLOOR_CONVENTION_DEFAULT;

// Not serialized - it describes where the live value CAME FROM, which is a fact about this session
// and not about the file. See RsPrefs_FloorConventionIsLoaded.
static bool sFloorConventionLoaded = false;

static bool CheckConvention(int32_t convention, const char* op) {
    if (convention < 0 || convention >= RS_FLOOR_CONVENTION_COUNT) {
        SPDLOG_ERROR("RsPrefs: {} out-of-range floor convention {} (max {})", op, convention,
                     RS_FLOOR_CONVENTION_COUNT - 1);
        assert(false && "floor convention out of range");
        return false;
    }
    return true;
}

extern "C" int32_t RsPrefs_GetFloorConvention(void) {
    return sFloorConvention;
}

extern "C" void RsPrefs_SetFloorConvention(int32_t convention) {
    if (!CheckConvention(convention, "set")) {
        return;
    }
    sFloorConvention = static_cast<uint8_t>(convention);
}

extern "C" const char* RsPrefs_FloorConventionName(int32_t convention) {
    switch (convention) {
        case RS_FLOOR_CONVENTION_UK:
            return "uk";
        case RS_FLOOR_CONVENTION_US:
            return "us";
        default:
            return "?";
    }
}

extern "C" int32_t RsPrefs_FloorConventionIsLoaded(void) {
    return sFloorConventionLoaded ? 1 : 0;
}

// --- SaveManager section ---------------------------------------------------------------------
//
// JSON shape, version 1:
//   "rsPrefs": { "floorConvention": 0 }
//
// Adding a FIELD here is not a layout change in the sense QuestStore.cpp warns about - a key the
// file does not carry loads through the default handed to SaveManager::LoadData - so a new
// preference can join this section at version 1. What DOES need version 2 is changing the meaning
// or the type of a key that already shipped, and the serialized VALUES of RS_FLOOR_CONVENTION_*
// can never be renumbered at all.
//
// LoadData's default is `T{}` unless one is passed, i.e. ZERO. Pass it explicitly anyway: the two
// coincide today only because UK happens to be 0, and a silently-correct line that stops being
// correct the moment someone reconsiders the default is exactly the kind this project writes out.
//
// A save written before this landed has no section, so it reads all-default. An older build
// reading a newer save warns and skips the unknown section. Neither is a save break.

// Runs on every new game AND at the top of every LoadFile (SaveManager.cpp LoadFile -> InitFile),
// before the JSON is parsed. That is what makes switching slots unable to leak the previous file's
// setting: a file with no `rsPrefs` section reads the default rather than whatever was live.
// Also runs for the debug save (isDebug), which is what the agent-test auto-boot lands on.
static void RsPrefsInitFile(bool isDebug) {
    sFloorConvention = RS_FLOOR_CONVENTION_DEFAULT;
    sFloorConventionLoaded = false;
}

// Reads OUR global, not the SaveContext snapshot the threaded save hands us; `saveContext`,
// `sectionID` and `fullSave` are part of the SaveFunc signature and unused.
static void SaveRsPrefs(SaveContext* saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveData("floorConvention", sFloorConvention);
}

static void LoadRsPrefsV1() {
    // Read through a WIDE temp, for the reason QuestStore.cpp's loader gives: nlohmann's get_to
    // narrows with a bare static_cast, so reading straight into the u8 would turn a hand-edited
    // 256 into 0 before any range check could see it.
    //
    // A bad value is logged and REPAIRED to the default, which is where this store deliberately
    // differs from QuestStore twice over. QuestStore keeps a nonsense status as evidence and
    // asserts, because a quest status is GAME STATE and losing it loses the bug. This is a display
    // preference read on every composed line: an out-of-range convention would turn every label
    // into an empty string, so repairing it is the only sane outcome and the log is the evidence.
    //
    // AND THERE IS NO ASSERT. A Debug assert here fires inside LoadFile - a path the agent test
    // loop walks on every `agenttest loadsave` - and a tripped assert hangs the loop with no window
    // to read. The value that would trip it comes from a FILE, i.e. from outside the program, which
    // makes it an outcome to handle rather than a bug class to shout about. Same rule the console
    // layer follows by pre-validating before it calls the setter.
    int32_t convention = RS_FLOOR_CONVENTION_DEFAULT;
    SaveManager::Instance->LoadData("floorConvention", convention,
                                    static_cast<int32_t>(RS_FLOOR_CONVENTION_DEFAULT));
    if (convention < 0 || convention >= RS_FLOOR_CONVENTION_COUNT) {
        SPDLOG_ERROR("RsPrefs: save file carries out-of-range floor convention {} (max {}); using the default",
                     convention, RS_FLOOR_CONVENTION_COUNT - 1);
        convention = RS_FLOOR_CONVENTION_DEFAULT;
    }
    sFloorConvention = static_cast<uint8_t>(convention);
    sFloorConventionLoaded = true;
}

static void RegisterRsPrefs() {
    // ShipInit "*" registrations re-run on preset apply (Presets.cpp -> ShipInit::InitAll) and on
    // config-file drop (OTRGlobals.cpp SoH_HandleConfigDrop -> ShipInit::Init("*")).
    // AddSaveFunction asserts on a duplicate name, so register exactly once.
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    SaveManager::Instance->AddInitFunction(RsPrefsInitFile);
    SaveManager::Instance->AddSaveFunction("rsPrefs", 1, SaveRsPrefs, true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction("rsPrefs", 1, LoadRsPrefsV1);
}

static RegisterShipInitFunc rsPrefsInitFunc(RegisterRsPrefs);
