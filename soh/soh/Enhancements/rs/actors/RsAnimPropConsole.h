#ifndef SOH_RS_ANIM_PROP_CONSOLE_H
#define SOH_RS_ANIM_PROP_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one renderer behind both console surfaces for Rs_AnimProp (sturdy-bassoon#204 C4a), RegionConsole's
// arrangement: the human `animprop ...` command joins `lines` into the ImGui console, and `agenttest animprop
// ...` (agenttest/AgentTest.cpp) writes each as its own `[agenttest] rs_animprop <line>` marker. What it shows
// is what a capture cannot: which copies found their lists, which pose each drew last, how often poses change,
// and whether the bake holds them.
//
// `args[0]` is the subcommand:
//   status          `op=status result=ok copies=<n> ok=<n> bad=<n> awake=<n> drawn=<n> culled=<n> changes=<n>
//                   draws=<n> clock=<seconds> pinned=0|1 forced=clock|rest|<k> lists=<n> baked=<n> unbaked=<n>
//                   rejected=<n> instance=<bytes> copy_bytes=<bytes> tex_evict=<n> streak=<n> tripped=0|1`.
//                   copies: live Rs_AnimProp; ok/bad: found their lists or not; awake: inside the cull volume;
//                   drawn: the last frame drew it, culled = copies - drawn; changes and draws: pose changes and
//                   ticks drawn, summed over the copies since spawn or `reset`, so two reads a known clock apart
//                   give pose changes a second; lists: the lists ArchiveProps holds for copies; baked..rejected:
//                   the live copies' lists by the bake's state; instance: sizeof(RsAnimProp), copy_bytes:
//                   sizeof(ArchivePropsAnimCopy), the two per-copy structs; tex_evict: texture-cache evictions
//                   counted on frames a copy drew, streak: consecutive such frames with the camera still, and
//                   tripped: the #172 tripwire fired (a log warning, once a scene)
//   list            one `copy idx=<i> list=<n> copy=<n> check=<n>
//                   state=<ok|no_list|no_table|no_line|check|no_pose|stale> poses=<n> frames=<f>,<f>
//                   cycles=<c>,<c> step=<n> start=<pose>:<cycle> pose=<k|rest|-> changes=<n> draws=<n> awake=0|1
//                   drawn=0|1 x= y= z=` line per live copy, in the actor list's order, then `op=list result=ok
//                   count=<n>`. `stale`: its copy was released (a new bake group, `staticbake reset`) and
//                   is fetched again when it next draws; until then it is reported, never read
//   pose <k|rest|clock>
//                   forces every copy to pose k (its index, clamped to the copy's last), or rest, or back to the
//                   clock, for this session only; `op=pose result=ok forced=<k|rest|clock>`. For the planted
//                   controls: rest is the baked still copy's own triangles
//   reset           zeroes every copy's changes and draws: `op=reset result=ok copies=<n>`
//
//   pick <tick|frame|default>
//                   where the pose is picked (RsAnimProp.h, WHERE THE POSE IS PICKED): `op=pick result=ok
//                   pick=<tick|frame> saved=0|1`. From the human command it is saved (CVAR_RS_ANIMPROP_PICK;
//                   default clears it); from `agenttest animprop` it holds for this session only, as `staticbake
//                   xlu` does. status also gains `pick=<tick|frame> flips=<n> flip_resolves=<n> flip_changes=<n>
//                   flip_stale=<n> flip_unheld=<n> frames=<n>`: the flips registered (one a copy's pass), flip
//                   heads resolved and how many picked a new pose (both passes counted: not the actor's
//                   `changes`), commands that found no flip (drawn as nothing: a release since they were built),
//                   in a Debug build resolves to a list the bake does not know (a pointer that outlived its
//                   resource; 0 in Release), and rendered frames. list's lines gain `flip=<k|rest|->
//                   flip_changes=<n> flip_resolves=<n>`, of the copy's translucent flip (else its opaque one)
//   trace <idx> <frames>
//                   traces copy idx (list's order) for that many rendered frames it draws in: `op=trace result=ok
//                   started=1`. `trace` alone reads it: one `trace frame=<n> clock=<s> pose=<k> via=head|dl` line a
//                   frame, then `op=trace result=ok rows= running=0|1 heads= changes= span_s= frame_span= fps=
//                   changes_per_s= holds_ms=<ms>:<count>,...`, the holds being the clock between pose changes
//   selftest [plant]
//                   the actor's own pick against each copy's registered flip at every millisecond over two plays:
//                   `op=selftest result=ok|fail plant=0|1 copies= skipped= checked= mismatched=`. plant lengthens the
//                   actor side's first pose a client cycle, which must fail
//   plant [off|unheld]
//                   the first copy with a translucent flip also draws a bake-registered list holding its flip head:
//                   `op=plant result=ok planted=1 state=<not_registered|unbaked|baked|rejected> reason="<the
//                   recorder's>"`. Must read rejected, "a flip head inside a recorded list". Again after a reset
//                   re-plants it. unheld instead draws a flip whose pose the bake never registered: `flip_unheld`
//                   must climb (Debug). off stops either
//
// Returns 0 on success (and for the reads), 1 for a refused operation: no scene, an unknown subcommand, an
// extra argument or a bad value (`op=<sub> result=error error=bad_value`).
//
// RsAnimPropConsole_Run is the human `animprop` command (pick is saved); RsAnimPropConsole_RunSession is the
// agent loop's (pick holds for the session only).
int32_t RsAnimPropConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);
int32_t RsAnimPropConsole_RunSession(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_ANIM_PROP_CONSOLE_H
