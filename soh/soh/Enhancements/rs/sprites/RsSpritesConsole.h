#ifndef SOH_RS_SPRITES_CONSOLE_H
#define SOH_RS_SPRITES_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for RS interface sprites (sturdy-bassoon#297):
//
//   - the human `rssprite ...` command, registered here, and
//   - `agenttest rssprite ...` (agenttest/AgentTest.cpp), one `[agenttest] rs_sprite <line>` marker a line.
//
// A DEBUG DRAW, not a feature: it puts packed sprites on screen by token so a screenshot can show what
// mods/rs-sprites.o2r holds and how its edges draw. The scroll (#300), the slot rows (#301) and the map icons (#307)
// call RsSprites_EmitRect themselves; nothing here is meant to ship as a look.
//
// Screen positions are the HUD's 320 x 240 frame (square units, centred 4:3 in a wide window), origin top-left, y down;
// sizes are in the same units.
//
// `args[0]` is the subcommand:
//   status                      op=status shown=<n> filter=<bilerp|point> blend=<xlu|cutout> panel=<on|off>
//   info <token>                op=info result=ok token=<t> width=<px> height=<px> cols=<n> rows=<n> rs=<w>x<h>, or
//                               (width/height the packed texels; rs the picture's size in RS pixels, which is what
//                               anything sized like the RS client uses - RsSpriteInfo.rsWidth, #307), or
//                               result=error error=missing (rc=1): the token is in no mounted archive
//   show <token> <x> <y> [<w> [<h>]]
//                               adds the token to the debug draw, top-left at (x, y). <w> defaults to the
//                               picture's width in texels capped at 300; <h> keeps the aspect. Prints
//                               op=show result=ok token= x= y= w= h= shown=<n>. Refuses (rc=1)
//                               error=missing, error=bad_number, error=full (32 at once)
//   layout                      the #297 proof: every approved slider piece, an assembled track, the three
//                               map icons and the quest-complete parchment, at fixed places. One show line each
//   filter bilerp|point         how the debug draw samples (the edge check compares the two)
//   blend xlu|cutout            G_RM_XLU_SURF (blended edge) or G_RM_TEX_EDGE (alpha-tested edge)
//   panel on|off                the flat grey panel behind the layout (layout switches it on), so an A/B of two
//                               edges sits on one colour
//   clear                       removes everything and the panel; op=clear result=ok shown=0
//
// Returns 0 when the operation succeeded (or for a read), 1 when it was refused, so `rc=` is the pass/fail bit.
int32_t RsSpritesConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_SPRITES_CONSOLE_H
