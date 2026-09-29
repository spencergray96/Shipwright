#include "DisplayDefaults.h"

#include <climits>
#include <cstdint>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/config/Config.h>

#include "soh/ShipInit.hpp"

// See DisplayDefaults.h for why these two, and why they are defaults rather than a lock.

namespace {

constexpr int32_t kFirstWindowWidth = 1280;
constexpr int32_t kFirstWindowHeight = 720;

// A sentinel default rather than Config::Contains, which answers true for a missing nested key:
// Config::Nested skips every path segment it cannot find and returns the object it stopped at, which is
// never null - so Contains("Window.Width") is true on a config with no Window block at all. GetInt
// returns the default unless the value there is an integer.
constexpr int32_t kUnset = INT32_MIN;

// Enabled at 16:9. AspectRatioX/Y are the Resolution editor's Widescreen preset, which is also its
// default combo row (UIComboItem.AspectRatio 3), so the editor shows these as chosen. Registered, not
// set: RegisterInteger/RegisterFloat leave a CVar the config already holds alone.
void RegisterRsDisplayDefaults() {
    CVarRegisterInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled", 1);
    CVarRegisterFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioX", 16.0f);
    CVarRegisterFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioY", 9.0f);
}

RegisterShipInitFunc displayDefaultsInitFunc(RegisterRsDisplayDefaults);

} // namespace

void RsDisplay_ApplyWindowDefaults() {
    auto config = Ship::Context::GetRawInstance()->GetConfig();
    if (config == nullptr) {
        return;
    }
    // Either one saved means a window the player (or the game, on exit) has already sized.
    if (config->GetInt("Window.Width", kUnset) != kUnset || config->GetInt("Window.Height", kUnset) != kUnset) {
        return;
    }
    // Not logged: this runs before OTRGlobals::Initialize sets the logger up. `agenttest display` reports
    // the window that resulted.
    config->SetInt("Window.Width", kFirstWindowWidth);
    config->SetInt("Window.Height", kFirstWindowHeight);
}
