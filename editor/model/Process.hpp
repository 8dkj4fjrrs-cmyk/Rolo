#pragma once

// Running another program and reading what it prints.
//
// The editor needs this for Android exports, which are built by Google's
// tools (Java, Gradle, keytool; ADR 0028). The program is started directly,
// never through a shell: its arguments are given one by one, so spaces and
// quotes in file names need no care and nothing typed by the user is ever
// read as a command.
//
// Operating-system differences live in Process.cpp and nowhere else.

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rolo::editor {

struct ProcessOptions {
    std::filesystem::path program;        // the program file itself (not looked up in PATH)
    std::vector<std::string> arguments;   // UTF-8, one entry per argument
    std::filesystem::path workingFolder;  // empty: this program's own
    /// Environment variables for the program, added to this program's own
    /// (or replacing those of the same name). The way to hand over a
    /// password: it is on no command line and in no file.
    std::vector<std::pair<std::string, std::string>> environment;
};

struct ProcessResult {
    bool started = false;    // false: the program could not be started (see `error`)
    bool cancelled = false;  // stopped because `cancel` became true
    int exitCode = -1;       // what the program returned (0 means it succeeded)
    std::string error;       // why it could not be started
    bool ok() const noexcept { return started && !cancelled && exitCode == 0; }
};

/// Runs the program and waits for it to end. Everything it prints (its
/// output and its error messages, as one stream) is passed to `onLine` a
/// line at a time, without the line ends, on the calling thread. If `cancel`
/// is given and becomes true, the program and the programs it started are
/// stopped. Blocks: call it from a worker thread when a window must stay alive.
ProcessResult runProcess(const ProcessOptions& options, const std::function<void(std::string_view)>& onLine = {},
                         const std::atomic<bool>* cancel = nullptr);

} // namespace rolo::editor
