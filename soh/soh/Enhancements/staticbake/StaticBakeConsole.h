#ifndef SOH_STATIC_BAKE_CONSOLE_H
#define SOH_STATIC_BAKE_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for the static bake's runtime switch
// (sturdy-bassoon#142), the arrangement RegionConsole.h describes:
//
//   - the human `staticbake ...` command, registered in StaticBakeConsole.cpp, and
//   - `agenttest staticbake ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] staticbake <line>` marker in agent-log.txt.
//
// Why it exists: a baked/interpreted picture A/B across two sessions does not line up - a session
// at single-digit fps lands its camera 3-15 units away from one at full speed. Flipping the switch
// inside one session keeps the camera, the time and everything else still, and turning it back on
// replays the existing bakes rather than re-recording them.
//
// Every successful line is `op=<sub> result=ok` and the same eleven fields: `active=` whether the bake
// runs (StaticBake_IsActive: the switch, and 0 on a backend that cannot bake), `setting=` the saved
// setting (sturdy-bassoon#153; it differs from active= when SOH_STATIC_BAKE decided the session, after
// an agent-loop switch, or on a backend that cannot bake), `registered=` display lists offered by the
// loaded scene's rooms, `baked=` those with a GPU buffer, `rejected=` those the recorder refused
// (interpreted for good), `supported=` whether this rendering backend can bake at all (DX11 only),
// `sort=` whether recordings are ordered by material (sturdy-bassoon#158), `group=` the bake group the registry holds
// (0x<smallest scene id in it>, or `none`), `scenes=` how many of its scenes have registered rooms
// since the last reset and `links=` how many `link`s this session added (sturdy-bassoon#157), and
// `scrolls=` how many textures are registered to scroll (sturdy-bassoon#187 A1).
// registered/baked/rejected count the WHOLE group: a return to a scene visited earlier in the group
// finds its entries still baked. baked + rejected <
// registered means some have not been drawn since they were registered or invalidated - or, in a
// group, that some belong to a scene Link is not in. An unknown subcommand prints
// `op=unknown result=error error=unknown_subcommand usage=...` without echoing the word.
//
// `args[0]` is the subcommand; none is `status`:
//   status    read the switch and the registry
//   on        switch the bake on. Registered rooms record on their next draw, or replay if baked
//   off       switch it off. Every room is interpreted; bakes and registrations are kept
//   From the human command, on/off also save setting= (sturdy-bassoon#153), so a restart starts
//   there. From `agenttest staticbake` they switch for the session only - StaticBakeConsole_RunSession
//   - so an agent's A/B, or a run that dies halfway through one, never leaves the owner's bake off.
//   rebake    send every baked room back to be recorded again on its next draw, releasing its
//             buffer and textures - what a shader-cache clear, the alt-assets toggle and a filter
//             change do. For re-recording under a condition (cache pressure, a time of day)
//   reset     free every bake and registration the group holds, other scenes' included, and
//             register the current room again so it records on its next draw (sturdy-bassoon#157).
//             For timing a cold load - reset, then enter the scene - and for suspecting a kept bake.
//             Changes no setting, so it is the same from both sinks
//   link <scene> <scene>  join two scenes' bake groups for the rest of the session, as if a step warp
//             ran between them (0x96 or 150). For measuring a kept return on content no warp reaches
//             yet; a link takes effect at the next room load, and only a restart removes it.
//             Session only from both sinks
//   sort on|off  order each recording by material (the default), or keep list order, and send every
//             baked room back to be recorded that way (sturdy-bassoon#158). Session only from both
//             sinks: for comparing the two orders, and the way back if content depends on list order
//   props     the status line, then `op=props result=ok lists=<n>` and one line per archive prop list the group
//             holds (sturdy-bassoon#171): `op=props list=<path> scene=0x<id> room=<n> state=<s> key=<p>
//             draws=<n> tris=<n> reason=<rest of line>`. state= is missing (in no mounted archive),
//             not_displaylist, empty (offered to nothing), or the bake's own: unbaked, baked,
//             rejected, unregistered. draws= and tris= are the baked entry's, 0 otherwise; reason= is
//             why the recorder refused it, `none` otherwise. Then `op=props result=ok scroll_lists=<n>`
//             and one line per baked list with a scrolling draw (sturdy-bassoon#187 A1), archive or
//             not: `op=props scroll_key=<p> draws=<n> tris=<n> scroll_draws=<n> scroll_tris=<n>`,
//             keyed as the list lines' key= so the two join; a list with no such line has none.
//             scroll_draws counts the draws whose TEXEL0 moves. Read-only, the same from both sinks
//
// Texture scroll (sturdy-bassoon#187 A1): libultraship's registry (fast/StaticMeshCache.h, "Texture
// scroll"), which belongs to the process - a scene change, `reset`, `rebake` and a texture-cache clear
// all keep it. Session only from both sinks: nothing here is saved.
//   scroll [list]  the status line, then one line per registered texture:
//             `op=scroll du=<f> dv=<f> bound=<0|1> path=<archive path>`, the path last, as the rest of
//             the line. bound=1 once an archive list has named the path since it was registered; until
//             then nothing draws it scrolling
//   scroll <path> <du> <dv>  register, change or (0 0) remove one texture's scroll, in texture widths
//             and heights a second; the status line, then `op=scroll du=<f> dv=<f> changed=<0|1>
//             set=<path>`. changed=0 is the idempotent case. The rate is read when a list is
//             RECORDED: a change to a texture an existing bake holds shows on baked draws after `rebake`
//   scroll clear  remove every registration; the status line, then `op=scroll cleared=<n>`
//   clock [<seconds>|run]  the clock every scroll reads: pin it at <seconds> (0-1000000) for
//             same-picture comparisons, or let it run. Bare, it reports. The status line, then
//             `op=clock pinned=<0|1> t=<seconds>`, the value the next draw reads (bare and running:
//             the last frame's sample)
//   texclear  clear the interpreter's texture cache, as an ocarina textbox does: bakes hold their own
//             textures and scroll registrations stay. For proving both. The status line only
//
// `sort` without on or off prints `op=sort result=error error=bad_argument usage=sort(on|off)`; `link`
// without two scene ids, `op=link result=error error=bad_argument usage=link(<scene>,<scene>)`; a
// `scroll` that is not list, clear or a path with two finite rates (-1000 to 1000),
// `op=scroll result=error error=bad_argument usage=scroll(list)|scroll(clear)|scroll(<path>,<du>,<dv>)`;
// a `clock` that is neither run nor a number in range,
// `op=clock result=error error=bad_argument usage=clock|clock(<seconds>)|clock(run)`. No error line
// echoes what was typed.
//
// Returns 0 for all eleven, 1 for an unknown subcommand or a bad sort, link, scroll or clock argument -
// so `rc=` on the agent loop's cmd marker is the pass/fail bit.
int32_t StaticBakeConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);
int32_t StaticBakeConsole_RunSession(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_STATIC_BAKE_CONSOLE_H
