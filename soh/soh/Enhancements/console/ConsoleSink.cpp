/*
 * The shared mechanical half of every console command surface in the mod - the human command
 * (sturdy-bassoon#112) and the agenttest marker channel (sturdy-bassoon#113). See ConsoleSink.h
 * for what belongs here and what deliberately does not.
 */

#include "ConsoleSink.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
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

namespace {

struct HintNode {
    std::string word;
    // Shown after the word: the values it takes.
    std::string args;
    // `=<command>`: the options are that command's.
    std::string alias;
    std::string text;
    std::vector<HintNode> children;
};

// Function-local so a Hints in another file's static initialisation finds it already constructed.
std::map<std::string, HintNode>& HintTrees() {
    static std::map<std::string, HintNode> trees;
    return trees;
}

std::vector<std::string> SplitWords(const std::string& s) {
    std::vector<std::string> words;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(' ', start);
        if (end == std::string::npos) {
            end = s.size();
        }
        if (end > start) {
            words.push_back(s.substr(start, end - start));
        }
        start = end + 1;
    }
    return words;
}

bool IsPlaceholder(const std::string& word) {
    return !word.empty() && (word[0] == '<' || word[0] == '[');
}

const std::vector<HintNode>& Options(const HintNode& node) {
    if (!node.alias.empty()) {
        auto it = HintTrees().find(node.alias);
        if (it != HintTrees().end()) {
            return it->second.children;
        }
    }
    return node.children;
}

std::string Left(const HintNode& node) {
    return node.args.empty() ? node.word : node.word + " " + node.args;
}

// Appends one line per row, its explanation lined up in a column unless one left side is long
// enough to push the column too far out.
void AppendRows(std::string& out, const std::vector<std::pair<std::string, std::string>>& rows) {
    size_t width = 0;
    for (const auto& [left, text] : rows) {
        width = std::max(width, left.size());
    }
    width = std::min<size_t>(width, 24);
    for (const auto& [left, text] : rows) {
        out += "\n  " + left;
        if (!text.empty()) {
            out += std::string(left.size() < width ? width - left.size() : 0, ' ') + "  " + text;
        }
    }
}

// The opening words of a command's help text, as its one-line explanation: up to the first colon,
// bracket or sentence end ("Staircases - menu-driven storey moves (sturdy-bassoon#147): ...").
std::string Summary(const std::string& description) {
    size_t end = description.size();
    for (const char* stop : { ": ", " (", ". " }) {
        end = std::min(end, description.find(stop));
    }
    std::string summary = description.substr(0, end);
    if (!summary.empty() && summary.back() == '.') {
        summary.pop_back();
    }
    return summary.size() > 60 ? summary.substr(0, 57) + "..." : summary;
}

bool BuildTreeHint(const std::string& input, const std::vector<std::string>& words, const HintNode& root,
                   std::string* hint);

// The command name itself is still being typed: the commands it could become, vanilla ones included.
// Down to one with a tree, that command's options instead, so the hint looks the same before the
// name is finished as after.
bool CommandList(const std::string& partial, std::string* hint) {
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto& [name, entry] : Ship::Context::GetRawInstance()->GetConsole()->GetCommands()) {
        if (name.compare(0, partial.size(), partial) == 0) {
            rows.emplace_back(name, Summary(entry.Description));
        }
    }
    if (rows.empty()) {
        return false;
    }
    if (rows.size() == 1) {
        auto tree = HintTrees().find(rows[0].first);
        // One vanilla command left: its own usage line.
        if (tree == HintTrees().end()) {
            return false;
        }
        return BuildTreeHint(rows[0].first, { rows[0].first }, tree->second, hint);
    }
    constexpr size_t kMaxRows = 15;
    const size_t more = rows.size() > kMaxRows ? rows.size() - kMaxRows : 0;
    rows.resize(rows.size() - more);
    std::string out = "commands starting with " + partial;
    AppendRows(out, rows);
    if (more > 0) {
        out += "\n  ... and " + std::to_string(more) + " more";
    }
    *hint = out;
    return true;
}

} // namespace

Hints::Hints(std::string command, const char* spec) {
    HintNode root;
    root.word = command;
    // stack[d] is the node a line indented to depth d belongs to. A sibling's push_back may move the
    // vector the previous sibling lives in, so the stack is cut back to the parent before it.
    std::vector<HintNode*> stack{ &root };
    const std::string text(spec);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t indent = line.find_first_not_of(' ');
        if (indent == std::string::npos) {
            continue;
        }
        const size_t depth = std::min(indent / 2, stack.size() - 1);
        line = line.substr(indent);

        HintNode node;
        const size_t gap = line.find("  ");
        const std::string left = line.substr(0, gap);
        if (gap != std::string::npos) {
            const size_t explanation = line.find_first_not_of(' ', gap);
            node.text = explanation == std::string::npos ? "" : line.substr(explanation);
        }
        const size_t space = left.find(' ');
        node.word = left.substr(0, space);
        if (space != std::string::npos) {
            node.args = left.substr(space + 1);
            if (node.args[0] == '=') {
                node.alias = node.args.substr(1);
                node.args.clear();
            }
        }

        stack.resize(depth + 1);
        HintNode* parent = stack[depth];
        parent->children.push_back(std::move(node));
        stack.push_back(&parent->children.back());
    }
    HintTrees()[command] = std::move(root);
}

bool BuildHint(const std::string& input, std::string* hint) {
    const std::vector<std::string> words = SplitWords(input);
    if (words.empty()) {
        return false;
    }
    auto tree = HintTrees().find(words[0]);
    if (tree != HintTrees().end()) {
        return BuildTreeHint(input, words, tree->second, hint);
    }
    if (words.size() == 1 && input.back() != ' ') {
        return CommandList(words[0], hint);
    }
    return false;
}

namespace {

bool BuildTreeHint(const std::string& input, const std::vector<std::string>& words, const HintNode& root,
                   std::string* hint) {
    // The last word is still being typed until a space follows it: it narrows the options rather
    // than choosing one.
    const bool typing = input.back() != ' ';
    const size_t chosen = std::max<size_t>(1, typing ? words.size() - 1 : words.size());
    const HintNode* node = &root;
    std::string path = words[0];
    size_t i = 1;
    for (; i < chosen; i++) {
        const HintNode* next = nullptr;
        for (const HintNode& option : Options(*node)) {
            if (option.word == words[i]) {
                next = &option;
                break;
            }
        }
        if (next == nullptr) {
            for (const HintNode& option : Options(*node)) {
                if (IsPlaceholder(option.word)) {
                    next = &option;
                    break;
                }
            }
        }
        // A word with no option of its own (a value for a subcommand's arguments, or a typo) stops
        // the walk where it is.
        if (next == nullptr) {
            break;
        }
        node = next;
        path += " " + words[i];
    }

    const std::string partial = (typing && i == chosen && words.size() > 1) ? words.back() : "";
    std::vector<const HintNode*> shown;
    for (const HintNode& option : Options(*node)) {
        if (option.word.compare(0, partial.size(), partial) == 0) {
            shown.push_back(&option);
        }
    }
    // Nothing starts with what is typed: list every option rather than none.
    if (shown.empty()) {
        for (const HintNode& option : Options(*node)) {
            shown.push_back(&option);
        }
    }

    std::string out = path;
    if (!node->args.empty()) {
        out += " " + node->args;
    }
    if (node != &root && !node->text.empty()) {
        out += " - " + node->text;
    }
    std::vector<std::pair<std::string, std::string>> rows;
    for (const HintNode* option : shown) {
        rows.emplace_back(Left(*option), option->text);
    }
    AppendRows(out, rows);
    *hint = out;
    return true;
}

} // namespace

} // namespace ConsoleSink
