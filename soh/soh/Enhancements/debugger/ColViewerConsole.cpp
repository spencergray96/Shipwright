#include "ColViewerConsole.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/debug/Console.h>

#include "colViewer.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/cvar_prefixes.h"

namespace {

using ConsoleSink::Addf;

// What `climb on` found and switched, for `climb off` to put back: the viewer's switch and its Scene
// layer. -1 = nothing remembered. Session only, like the agent loop that drives it.
int32_t sRestoreEnabled = -1;
int32_t sRestoreScene = -1;

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
        const bool climbing =
            CVarGetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.ColorMode"), COLVIEW_COLOR_CLASS) == COLVIEW_COLOR_CLIMB;
        if (args[1] == "on") {
            // Remember what this switches, once, so `climb off` can put it back. A second `on` must
            // not overwrite it with the values the first one set.
            if (!climbing) {
                sRestoreEnabled = CVarGetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Enabled"), 0);
                sRestoreScene = CVarGetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Scene"), COLVIEW_DISABLED);
            }
            CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Enabled"), 1);
            // The colours draw on the Scene and Bg Actors layers. Leave a layer the user already
            // chose (solid or transparent) as it is; switch the Scene layer on only if it was off.
            if (CVarGetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Scene"), COLVIEW_DISABLED) == COLVIEW_DISABLED) {
                CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Scene"), COLVIEW_SOLID);
            }
            CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.ColorMode"), COLVIEW_COLOR_CLIMB);
        } else {
            CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.ColorMode"), COLVIEW_COLOR_CLASS);
            // Undo exactly what `climb on` switched. With nothing remembered (the colours came on from
            // the menu, or before a restart) the viewer is left as it is, in class colours.
            if (sRestoreEnabled >= 0) {
                CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Enabled"), sRestoreEnabled);
                CVarSetInteger(CVAR_DEVELOPER_TOOLS("ColViewer.Scene"), sRestoreScene);
                sRestoreEnabled = -1;
                sRestoreScene = -1;
            }
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
// `colview` collides with nothing in Enhancements/debugconsole.cpp's CMD_REGISTER list.

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
