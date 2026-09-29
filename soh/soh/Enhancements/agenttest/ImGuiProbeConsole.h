#ifndef SOH_IMGUI_PROBE_CONSOLE_H
#define SOH_IMGUI_PROBE_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind both console surfaces for the ImGui probe (sturdy-bassoon#163,
// SohGui/ImGuiProbe.h): the human `imgui ...` command, and `agenttest imgui ...`, which writes each
// line as its own `[agenttest] imgui <line>` marker. The first call of either arms the probe, so the
// first `dump` after arming can come back empty - `status` first, then give it a frame.
//
// `args[0]` is the subcommand:
//   status               op=status result=ok armed=1 frame=<n> items=<n> menu=<0|1> pending=<n>
//                        frame= is the ImGui frame the recorded items came from; menu= whether the
//                        Esc menu is open; pending= injection steps still waiting for a frame
//   dump [kind]          op=dump result=ok frame=<n> items=<shown> of=<recorded> menu=<0|1>, then one
//                        `item kind=<k> x=<cx> y=<cy> w=<w> h=<h> value=<v> disabled=<0|1>
//                        visible=<0|1> label="<label>"` per widget the last frame drew, optionally
//                        only one kind. x/y is the centre, where a click lands. Kinds: header,
//                        sidebar (the menu's navigation), checkbox, combo, option (a row of an open
//                        combo; value= is the key the combo takes if it is chosen), slider, dec/inc
//                        (a slider's -/+ buttons), button
//   click [kind:]<label> queue a click on the widget of that label in the last frame. The label is the
//                        rest of the line, compared after collapsing whitespace. A `kind:` prefix
//                        (`header:Settings`, `option:Original (4:3)`) narrows it when the label alone
//                        is ambiguous. On success: op=click result=ok kind= x= y= value=<before>
//                        queued=<n> label="..."; the injection then writes `op=delivered what=click`
//                        and the widget, if it acted, `op=edited kind= value= label=`
//   clickat <x> <y>      queue a click at ImGui coordinates in the main viewport. The escape hatch
//                        for a widget the helpers do not record
//   key <name>           queue a press and release of an ImGui key by ImGui's name for it (Escape,
//                        Enter, Tab, F1). `key Escape` is how the Esc menu opens: Gui::DrawMenu reads
//                        it as an ImGui key, so the synthetic key walks the same path a real one does
//
// Refusals, each rc=1, none echoing the typed text: click `error=not_found|ambiguous matches=<n>|
// disabled|clipped|not_clickable|no_label`; clickat `error=bad_argument`; key `error=bad_argument|
// unknown_key`; dump `error=unknown_kind`; click/clickat/key `error=not_armed` when there was no ImGui
// context to hook (nothing would ever deliver them); any other subcommand `error=unknown_subcommand`.
// rc=0 is success or a read.
int32_t ImGuiProbeConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_IMGUI_PROBE_CONSOLE_H
