/*
 * The shared mechanical half of every console command surface in the mod - the human command
 * (sturdy-bassoon#112) and the agenttest marker channel (sturdy-bassoon#113). See ConsoleSink.h
 * for what belongs here and what deliberately does not.
 */

#include "ConsoleSink.h"

#include <cstdarg>
#include <cstdio>
#include <memory>

#include <ship/Context.h>

namespace ConsoleSink {

namespace {

// The '%' doubling, in the one place it now exists. ConsoleWindow hands a handler's output to
// vsnprintf AS THE FORMAT STRING, so a bare '%' is read as a conversion and reads off varargs that
// were never passed. Both sinks append through here.
void AppendEscaped(const std::string& line, std::string* output) {
    for (char c : line) {
        *output += c;
        if (c == '%') {
            *output += '%';
        }
    }
}

} // namespace

void Addf(std::vector<std::string>& lines, const char* fmt, ...) {
    char buf[512];
    // vsnprintf returns negative on an encoding error and writes nothing, so buf would otherwise be
    // read uninitialised. No format string here can provoke it; the cost of ruling it out is a byte.
    buf[0] = '\0';
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lines.emplace_back(buf);
}

void WriteOutput(const std::vector<std::string>& lines, std::string* output) {
    if (output == nullptr) {
        return;
    }
    for (size_t i = 0; i < lines.size(); i++) {
        if (i > 0) {
            *output += "\n";
        }
        AppendEscaped(lines[i], output);
    }
}

int32_t RunToMarkers(const RunFn& run, const std::vector<std::string>& args, const std::string& markerPrefix,
                     std::string* output, MarkerFn writeMarker) {
    std::vector<std::string> lines;
    const int32_t rc = run(args, lines);
    for (const std::string& line : lines) {
        writeMarker(markerPrefix + line);
        if (output != nullptr) {
            // The test is "is `output` empty", not "is this the first line": a caller that has
            // already written something gets a separator before the first line too. Carried over
            // from the four hand-copied blocks unchanged, including its one oddity - an EMPTY line
            // leaves `output` empty, so the line after it gets no separator either and the empty
            // line itself vanishes from `out=`. No renderer emits one; this is a record of the
            // behaviour, not an endorsement of it.
            if (!output->empty()) {
                *output += " | ";
            }
            AppendEscaped(line, output);
        }
    }
    return rc;
}

Command::Command(std::string name, RunFn run, std::string description, std::vector<Ship::CommandArgument> arguments)
    : mInitFunc([name, run, description, arguments]() {
          auto console = Ship::Context::GetRawInstance()->GetConsole();
          if (console->HasCommand(name)) {
              return;
          }
          console->AddCommand(
              name, { [run](std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string* output) {
                         // Console::Run always puts the command name at index 0, so the slice is
                         // safe; the empty check is here because a shared sink cannot assume every
                         // future caller of AddCommand is Console::Run.
                         std::vector<std::string> sub(args.empty() ? args.begin() : args.begin() + 1, args.end());
                         std::vector<std::string> lines;
                         const int32_t rc = run(sub, lines);
                         WriteOutput(lines, output);
                         return rc;
                     },
                      description, arguments });
      }) {
}

} // namespace ConsoleSink
