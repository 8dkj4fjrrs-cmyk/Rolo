// Running other programs from the editor (editor/model/Process.hpp), with
// programs every system has.

#include "support/TempFolder.hpp"
#include "Process.hpp"

#include "rolo/core/Files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace rolo;
using namespace rolo::editor;
namespace fs = std::filesystem;

namespace {

// A command for the system's own shell: the one program every computer has
// that can print, read variables and wait.
ProcessOptions shell(const std::string& command)
{
    ProcessOptions options;
#if defined(_WIN32)
    const auto root = environmentVariable("SystemRoot");
    options.program = fs::path(root ? *root : "C:\\Windows") / "System32" / "cmd.exe";
    options.arguments = {"/d", "/c", command};
#else
    options.program = "/bin/sh";
    options.arguments = {"-c", command};
#endif
    return options;
}

std::vector<std::string> linesOf(const ProcessOptions& options, ProcessResult* result = nullptr)
{
    std::vector<std::string> lines;
    const ProcessResult r = runProcess(options, [&](std::string_view line) { lines.emplace_back(line); });
    if (result != nullptr) {
        *result = r;
    }
    return lines;
}

} // namespace

TEST_CASE("A program's output is read line by line, and its exit code kept", "[process]")
{
    ProcessResult result;
    const auto lines = linesOf(shell("echo one&& echo two&& exit 3"), &result);
    REQUIRE(result.started);
    CHECK(result.exitCode == 3);
    CHECK_FALSE(result.ok());
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "one");
    CHECK(lines[1] == "two");

    CHECK(runProcess(shell("exit 0")).ok());
}

TEST_CASE("Error messages come through the same stream", "[process]")
{
    const auto lines = linesOf(shell("echo problem 1>&2"));
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("problem") == 0);
}

TEST_CASE("A program gets the variables it is given, and starts in the folder asked for", "[process]")
{
    const fs::path folder = test::uniqueTempFolder("process");
    fs::create_directories(folder / "inside here");
#if defined(_WIN32)
    ProcessOptions options = shell("echo %ROLO_TEST_SECRET%&& cd");
#else
    ProcessOptions options = shell("echo \"$ROLO_TEST_SECRET\" && pwd");
#endif
    options.environment = {{"ROLO_TEST_SECRET", "p4ss word"}};
    options.workingFolder = folder / "inside here";
    const auto lines = linesOf(options);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "p4ss word");
    CHECK(fs::path(lines[1]).filename() == "inside here");
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}

TEST_CASE("Arguments with spaces and quotes arrive as they were given", "[process]")
{
#if defined(_WIN32)
    // cmd's "echo" prints its text as it is; the test is that the program starts and the text survives.
    ProcessOptions options = shell("echo a b");
#else
    ProcessOptions options;
    options.program = "/bin/sh";
    options.arguments = {"-c", "printf '%s\\n' \"$1\" \"$2\"", "sh", "two words", "it's \"quoted\""};
#endif
    const auto lines = linesOf(options);
#if defined(_WIN32)
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "a b");
#else
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "two words");
    CHECK(lines[1] == "it's \"quoted\"");
#endif
}

TEST_CASE("A program that does not exist is reported, not run", "[process]")
{
    ProcessOptions options;
    options.program = test::uniqueTempFolder("process") / "no-such-program";
    const ProcessResult result = runProcess(options);
    CHECK_FALSE(result.started);
    CHECK_FALSE(result.ok());
    CHECK(result.error.find("could not be started") != std::string::npos);
}

TEST_CASE("A running program can be cancelled", "[process]")
{
#if defined(_WIN32)
    const ProcessOptions options = shell("ping -n 30 127.0.0.1 > NUL");
#else
    const ProcessOptions options = shell("sleep 30");
#endif
    std::atomic<bool> cancel{false};
    std::thread later([&cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        cancel = true;
    });
    const auto start = std::chrono::steady_clock::now();
    const ProcessResult result = runProcess(options, {}, &cancel);
    later.join();
    CHECK(result.started);
    CHECK(result.cancelled);
    CHECK_FALSE(result.ok());
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(15));
}
