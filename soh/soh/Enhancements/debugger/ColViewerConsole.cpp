#include "ColViewerConsole.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/debug/Console.h>

#include "colViewer.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/cvar_prefixes.h"

namespace {

using ConsoleSink::Addf;

const char* LayerName(int32_t setting) {
    switch (setting) {
        case COLVIEW_DISABLED:
            return "disabled";
        case COLVIEW_SOLID:
            return "solid";
        case COLVIEW_TRANSPARENT:
            return "transparent";
        default:
            return "unknown";
    }
}

void Describe(const char* op, std::vector<std::string>& lines) {
    const ColViewerClimbStats stats = ColViewer_GetClimbStats();
    const int32_t mode = CVarGetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.ColorMode"), COLVIEW_COLOR_CLASS);
    Addf(lines,
         "op=%s result=ok enabled=%d scene=%s bgactors=%s mode=%s vanilla=%u noclimb=%u hands=%u other=%u "
         "nonwall=%u frames=%u",
         op, CVarGetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Enabled"), 0),
         LayerName(CVarGetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Scene"), COLVIEW_DISABLED)),
         LayerName(CVarGetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.BGActors"), COLVIEW_DISABLED)),
         mode == COLVIEW_COLOR_CLIMB ? "climb" : "class", stats.vanilla, stats.noClimb, stats.hands, stats.otherWall,
         stats.nonWall, stats.frames);
}

} // namespace

int32_t ColViewerConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? "status" : args[0];

    if (sub == "status") {
        Describe("status", lines);
        return 0;
    }
    if (sub == "climb") {
        if (args.size() < 2 || (args[1] != "on" && args[1] != "off")) {
            lines.push_back("op=climb result=error error=bad_argument usage=climb(on|off)");
            return 1;
        }
        if (args[1] == "on") {
            CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Enabled"), 1);
            // The colours draw on the Scene and Bg Actors layers. Leave a layer the user already
            // chose (solid or transparent) as it is; switch the Scene layer on only if it was off.
            if (CVarGetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Scene"), COLVIEW_DISABLED) == COLVIEW_DISABLED) {
                CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Scene"), COLVIEW_SOLID);
            }
            CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.ColorMode"), COLVIEW_COLOR_CLIMB);
        } else {
            CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.ColorMode"), COLVIEW_COLOR_CLASS);
            CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Enabled"), 0);
        }
        CVarSave();
        Describe("climb", lines);
        return 0;
    }
    // The typed word is not echoed: it is free text, and this line is parsed field by field.
    lines.push_back("op=unknown result=error error=unknown_subcommand usage=status|climb(on|off)");
    return 1;
}

// --- the human sink: the `colview` console command ----------------------------------------------
//
// `colview` collides with nothing in debugger/debugconsole.cpp's CMD_REGISTER list.

namespace {

const ConsoleSink::Command colViewerCommand(
    "colview", ColViewerConsole_Run,
    "The Collision Viewer's climb colours (sturdy-bassoon#196): status | climb on | climb off. "
    "Colours every wall by what Link's ledge climb does at it - amber vanilla, red no-climb, green "
    "hands-climb - so a placed prop's climb setting shows in any map, with no actors and no reload. "
    "Faces are shaded by facing, so a prop's collision mode reads from its shape: a few boxes is "
    "auto, one box is bounds, the mesh is native. Also in the menu: Dev Tools > Collision Viewer, "
    "Popout Collision Viewer, Colour by.",
    { { "status|climb", Ship::ArgumentType::TEXT, true }, { "on|off", Ship::ArgumentType::TEXT, true } });

} // namespace
