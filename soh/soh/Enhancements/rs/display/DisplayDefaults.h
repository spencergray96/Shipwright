#ifndef SOH_RS_DISPLAY_DEFAULTS_H
#define SOH_RS_DISPLAY_DEFAULTS_H

// ============================================================================================
//  16:9 BY DEFAULT, NOT BY LOCK  (sturdy-bassoon#139)
// ============================================================================================
//
// The mod's own UI - the scroll, the page stepper, the HUD raise, the name panel and its L/R icons - is
// laid out and measured at 16:9 only; at 4:3 the L/R icons sit entirely off-screen. Vanilla SoH opens a
// fresh install in a 640x480 window with Advanced Resolution off, which renders at whatever shape the
// window is: 4:3.
//
// Two defaults, both of which a player's saved config overrides, and both still in the Settings menu:
//
//   1. ADVANCED RESOLUTION ON, AT 16:9 - registered from a ShipInit function (DisplayDefaults.cpp), the
//      way RegisterRsHudDefaults registers the HUD raise: CVarRegister* writes only a CVar that does not
//      exist yet. With it on, resizing the window adds bars and never changes the game's shape.
//   2. A 1280x720 FIRST WINDOW - RsDisplay_ApplyWindowDefaults below, so a fresh launch has no bars
//      either. Window.Width/Height are libultraship CONFIG keys, not CVars, read once when the window is
//      created, so this has to run between InitConfiguration and InitWindow (OTRGlobals.cpp).
//
// N64 Mode (gSettings.LowResMode 1) still forces 4:3 over all of it. `agenttest display` reports what
// is in force.

// Writes a 1280x720 window size into the config when it has neither Window.Width nor Window.Height - a
// fresh install, or a config the game has never saved a window into. Call after the config is loaded
// and before the window is created.
void RsDisplay_ApplyWindowDefaults();

#endif
