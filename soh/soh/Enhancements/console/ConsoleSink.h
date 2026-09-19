#ifndef SOH_CONSOLE_SINK_H
#define SOH_CONSOLE_SINK_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <ship/debug/Console.h>

#include "soh/ShipInit.hpp"

// The mechanical half of a console command surface, shared by every `<feature>Console.cpp` in the
// mod - BOTH destinations a renderer's lines go to:
//
//   - the human `<name> <sub>` command (sturdy-bassoon#112): Addf, the '%'-safe join into the
//     console's output string, and the registration - HasCommand guard, handler lambda,
//     RegisterShipInitFunc. That is `Command` plus `WriteOutput`.
//   - the `agenttest <name> <sub>` marker channel (sturdy-bassoon#113): one marker per line plus
//     the same lines joined into `out=`. That is `RunToMarkers`.
//
// Between them those were nine hand-copied blocks; the '%' doubling alone was five, and is now one
// function (AppendEscaped in the .cpp) that both destinations append through.
//
// It owns NONE of the parsing. A feature's `<Feature>Console_Run(args, lines)` stays exactly where
// it is and keeps its own header - the one-parser-two-sinks arrangement documented in
// MusicConsole.h is untouched by this, and a renderer must go on being callable from both.
//
// Adding a console command is a parser, one file-scope Command object and one RunToMarkers call;
// the recipe is the `soh-add-console-command` skill.
namespace ConsoleSink {

// A feature's renderer. `args` is the subcommand words with the command name already sliced off;
// `lines` is appended to. The return value is what the console reports as the command's rc, so 0
// means the operation succeeded (or was read-only) and non-zero means it did not.
using RunFn = std::function<int32_t(const std::vector<std::string>& args, std::vector<std::string>& lines)>;

// Formats one line and appends it. Lines are single-line and greppable by convention - `key=value`
// fields, no spaces inside a value - because the same text reaches the agent loop's log through
// the marker channel. Truncates at 511 characters plus a NUL, which no line has come near.
void Addf(std::vector<std::string>& lines, const char* fmt, ...);

// Joins `lines` with newlines onto the end of `*output`, doubling every '%' on the way.
//
// The doubling is the reason this function exists. ConsoleWindow hands a handler's output string
// to vsnprintf AS THE FORMAT STRING, so a bare '%' is read as a conversion and reads off the
// (empty) varargs. `region expand` echoes a transformation of arbitrary typed text, so this is
// load-bearing rather than belt-and-braces for at least one caller - and which caller that is has
// to stop being something each new console remembers for itself.
//
// A null `output` is a no-op: the renderer has already run and its side effects stand.
//
// Public although Command is its only caller today: a console that cannot use Command for some
// reason must still be able to reach this rather than write the loop again, which is the whole
// point of the extraction.
void WriteOutput(const std::vector<std::string>& lines, std::string* output);

// The marker sink's line writer (sturdy-bassoon#113). This is a parameter rather than a direct
// call because AgentTest.cpp's WriteMarker is file-local there - and deliberately so: the public
// AgentTest_WriteMarker stands down when the agent-test switch is off, which is right for gameplay
// code and wrong for a command the user typed on purpose.
// A plain function pointer rather than a std::function (which is what RunFn is): no caller needs
// to capture, and refusing a capturing lambda at compile time is free.
using MarkerFn = void (*)(const std::string&);

// Runs `run` and feeds the two things an `agenttest <sub>` produces: one `<prefix><line>` marker
// per line, written VERBATIM because a marker is never a format string; and the same lines joined
// onto `*output` with " | " and '%'-doubled, because a typed `agenttest <sub>` reaches
// ConsoleWindow's vsnprintf like any other command. The differing escaping is the point.
//
// The caller keeps its own arity guard. `agenttest music` admits an empty subcommand list where
// the others do not, and that difference is the caller's to state rather than this function's to
// flatten.
int32_t RunToMarkers(const RunFn& run, const std::vector<std::string>& args, const std::string& markerPrefix,
                     std::string* output, MarkerFn writeMarker);

// One console command - sturdy-bassoon#112's `RegisterConsoleCommand(name, runFn, helpText,
// argSpec)` in object form, which is what lets the RegisterShipInitFunc live inside it. Declare it
// at file scope, next to the parser it wraps:
//
//     const ConsoleSink::Command questCommand("quest", QuestConsole_Run, "Quest system ...",
//                                             { { "list|dump|...", Ship::ArgumentType::TEXT },
//                                               { "quest id", Ship::ArgumentType::TEXT, true } });
//
// Nothing runs at construction beyond registering the init function: the command reaches the
// console on boot, and again on every preset apply and config load, because ShipInit "*" functions
// re-run then. AddCommand only warns on a duplicate, but the guard keeps the log clean.
//
// The object is inert once constructed: everything it was given has been copied into the init
// function by then, and nothing reads it again. Copying one would therefore be meaningless rather
// than harmful, which is exactly why it is worth refusing.
class Command {
  public:
    Command(std::string name, RunFn run, std::string description, std::vector<Ship::CommandArgument> arguments);

    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;

  private:
    RegisterShipInitFunc mInitFunc;
};

} // namespace ConsoleSink

#endif // SOH_CONSOLE_SINK_H
