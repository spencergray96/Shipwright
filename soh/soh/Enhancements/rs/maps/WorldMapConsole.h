#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The `worldmap` console command (sturdy-bassoon#298): the world map image's state, and its switches.
// One renderer behind two sinks: the human `worldmap <sub>`, and `agenttest worldmap <sub>`, which writes
// each line as an `rs_worldmap <line>` marker.
//
//   worldmap [status]
//       op=status scene=0x<id> map=1 size=<w>x<h> px_per_tile=<n> tiles=<w>x<d> storeys=<n> layers=<n> layer=<k>
//         storey_mode=<ground|all|current> link_storey=<k> link_px=<x>,<y> link_rs=<x>,<y> blocks=<w>x<h>
//         bytes=<n>
//       or, with no map for this scene:
//       op=status scene=0x<id> map=0 reason=<no_scene|no_map|bad_file|stale_archive> storey_mode=<mode>
//   worldmap storey ground|all|current
//       sets the storey CVar (saved), live: the next frame draws the new layer. Then the status line.
//   worldmap debug off|whole|link
//       the on-screen debug view: the whole image in the top-right corner, or a window around Link with a
//       red dot on his pixel. op=debug view=<v> blocks=<n> words=<n> (blocks and Gfx words the view's last
//       frame took).
//   worldmap block <w> <h>
//       the TMEM block size, for measuring: even w, (w + 4) * (h + 2) <= 4096. op=block w=<w> h=<h>
//   worldmap pixel <x> <z>
//       where an OoT world position falls on the image: op=pixel x=<x> z=<z> px=<x> py=<y> rs=<x>,<y>
//
// rc: 0 for every read (a scene with no map is an answer, not a failure) and every write that took; 1 for
// an unknown subcommand or a refused argument, and for `pixel` with no map.
int32_t RsWorldMapConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);
