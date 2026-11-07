#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The `minimap` console command (sturdy-bassoon#306): the round HUD minimap in RS scenes, and its settings.
// One renderer behind two sinks: the human `minimap <sub>`, and `agenttest minimap <sub>`, which writes each
// line as an `rs_minimap <line>` marker.
//
//   minimap [status]
//       op=status scene=0x<id> handled=1 drawn=<0|1> mode=<north|rotate> radius=<tiles> link_px=<x>,<y>
//         rot_deg=<deg> blocks=<n> words=<n> arrows=<n> alpha=<minimapAlpha> minimap_off=<R_MINIMAP_DISABLED>
//         centre=<x>,<y> storey_mode=<mode> link_storey=<k>
//       drawn is last frame's: the map was visible (not toggled off with L, hidden, paused or faded to 0).
//       blocks, words and arrows are what that frame drew: world map blocks, the Gfx words of the minimap's heap
//       list (OVERLAY_DISP itself takes one), and arrows including Link's. centre is the circle's centre in the
//       HUD ortho's centre-origin, y-up N64 pixels; rot_deg the map's turn (0 north-up).
//       or, in a scene without one (vanilla's minimap draws there, or none):
//       op=status scene=0x<id> handled=0 reason=<no_play|no_scene|no_map|bad_file|stale_archive> mode=<m>
//         radius=<tiles>
//   minimap mode north|rotate
//       sets the mode (saved), live. Then the status line.
//   minimap radius <tiles>|default
//       sets the radius in RS tiles (saved, 8-160; default 45), live. Then the status line.
//
// rc: 0 for every read and every write that took; 1 for an unknown subcommand or a refused argument.
int32_t RsMinimapConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);
