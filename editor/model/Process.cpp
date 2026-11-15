#include "Process.hpp"

#include <chrono>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;   // this program's environment (POSIX)
#endif

namespace rolo::editor {

namespace {

// Collects what the program prints and hands it on line by line.
class LineSplitter {
public:
    explicit LineSplitter(const std::function<void(std::string_view)>& onLine)
        : m_onLine(onLine)
    {
    }

    void add(const char* bytes, std::size_t count)
    {
        m_pending.append(bytes, count);
        std::size_t start = 0;
        for (std::size_t end = m_pending.find('\n', start); end != std::string::npos;
             end = m_pending.find('\n', start)) {
            emit(std::string_view(m_pending).substr(start, end - start));
            start = end + 1;
        }
        m_pending.erase(0, start);
    }

    void finish()
    {
        if (!m_pending.empty()) {
            emit(m_pending);
            m_pending.clear();
        }
    }

private:
    void emit(std::string_view line)
    {
        if (!line.empty() && line.back() == '\r') {   // Windows line ends
            line.remove_suffix(1);
        }
        if (m_onLine) {
            m_onLine(line);
        }
    }

    const std::function<void(std::string_view)>& m_onLine;
    std::string m_pending;
};

bool isCancelled(const std::atomic<bool>* cancel)
{
    return cancel != nullptr && cancel->load();
}

} // namespace

#if defined(_WIN32)

namespace {

std::wstring widen(std::string_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

// One argument as Windows programs expect it on their command line (the
// rules of the C runtime's own parser): quoted if it has spaces, with
// backslashes doubled only where they come before a quote.
std::wstring quoted(const std::wstring& argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += c;
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            out += c;
            backslashes = 0;
        }
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

// This program's environment with `changes` applied, in the form
// CreateProcess wants: NAME=value strings, each ending in a zero, and one
// more zero at the end.
std::wstring environmentBlock(const std::vector<std::pair<std::string, std::string>>& changes)
{
    // Windows treats names without regard to case; keep one entry per name.
    struct NoCase {
        bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; }
    };
    std::map<std::wstring, std::wstring, NoCase> variables;
    if (wchar_t* own = GetEnvironmentStringsW()) {
        for (const wchar_t* entry = own; *entry != L'\0'; entry += wcslen(entry) + 1) {
            const std::wstring text(entry);
            const std::size_t equals = text.find(L'=', 1);   // (names of hidden entries start with '=')
            if (equals != std::wstring::npos) {
                variables[text.substr(0, equals)] = text.substr(equals + 1);
            }
        }
        FreeEnvironmentStringsW(own);
    }
    for (const auto& [name, value] : changes) {
        variables[widen(name)] = widen(value);
    }
    std::wstring block;
    for (const auto& [name, value] : variables) {
        block += name + L"=" + value;
        block += L'\0';
    }
    block += L'\0';
    return block;
}

} // namespace

ProcessResult runProcess(const ProcessOptions& options, const std::function<void(std::string_view)>& onLine,
                         const std::atomic<bool>* cancel)
{
    ProcessResult result;

    // A pipe: the program writes into one end, this function reads the other.
    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (CreatePipe(&readEnd, &writeEnd, &inherit, 0) == 0) {
        result.error = "Windows could not make a pipe to read the program's output.";
        return result;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);   // our end stays ours
    // The program gets no keyboard input: an empty input instead.
    HANDLE noInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

    std::wstring commandLine = quoted(options.program.wstring());
    for (const std::string& argument : options.arguments) {
        commandLine += L' ';
        commandLine += quoted(widen(argument));
    }
    std::wstring environment = environmentBlock(options.environment);
    const std::wstring folder = options.workingFolder.wstring();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = noInput != INVALID_HANDLE_VALUE ? noInput : nullptr;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    PROCESS_INFORMATION process{};

    // A "job" holds the program and every program it starts (Gradle starts
    // more Java programs), so that all of them can be stopped together.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }

    const BOOL created = CreateProcessW(options.program.wstring().c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, environment.data(),
                                        folder.empty() ? nullptr : folder.c_str(), &startup, &process);
    CloseHandle(writeEnd);   // the program has its own copy; ours would keep the pipe open for ever
    if (noInput != INVALID_HANDLE_VALUE) {
        CloseHandle(noInput);
    }
    if (created == 0) {
        const DWORD code = GetLastError();
        result.error = options.program.string() + " could not be started (Windows error " + std::to_string(code) + ").";
        CloseHandle(readEnd);
        if (job != nullptr) {
            CloseHandle(job);
        }
        return result;
    }
    if (job != nullptr) {
        AssignProcessToJobObject(job, process.hProcess);
    }
    ResumeThread(process.hThread);   // started held back, so that it was in the job before it ran
    result.started = true;

    LineSplitter lines(onLine);
    char buffer[4096];
    for (;;) {
        if (isCancelled(cancel)) {
            result.cancelled = true;
            if (job != nullptr) {
                TerminateJobObject(job, 1);
            } else {
                TerminateProcess(process.hProcess, 1);
            }
            break;
        }
        DWORD waiting = 0;
        if (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &waiting, nullptr) == 0) {
            break;   // the pipe is closed: the program (and its children) ended
        }
        if (waiting == 0) {
            Sleep(30);
            continue;
        }
        DWORD got = 0;
        if (ReadFile(readEnd, buffer, sizeof(buffer), &got, nullptr) == 0 || got == 0) {
            break;
        }
        lines.add(buffer, got);
    }
    lines.finish();
    WaitForSingleObject(process.hProcess, result.cancelled ? 5000 : INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    result.exitCode = static_cast<int>(code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readEnd);
    if (job != nullptr) {
        CloseHandle(job);
    }
    return result;
}

#else // POSIX: macOS and Linux

ProcessResult runProcess(const ProcessOptions& options, const std::function<void(std::string_view)>& onLine,
                         const std::atomic<bool>* cancel)
{
    ProcessResult result;

    // Everything the new program needs is prepared before it is split off:
    // between fork() and exec() only a few simple system calls are safe in
    // a program with several threads (the editor has some).
    const std::string program = options.program.string();
    std::vector<std::string> argumentTexts;
    argumentTexts.push_back(program);
    argumentTexts.insert(argumentTexts.end(), options.arguments.begin(), options.arguments.end());
    std::vector<char*> argv;
    for (std::string& text : argumentTexts) {
        argv.push_back(text.data());
    }
    argv.push_back(nullptr);

    std::map<std::string, std::string> variables;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string_view text(*entry);
        const std::size_t equals = text.find('=');
        if (equals != std::string_view::npos) {
            variables[std::string(text.substr(0, equals))] = std::string(text.substr(equals + 1));
        }
    }
    for (const auto& [name, value] : options.environment) {
        variables[name] = value;
    }
    std::vector<std::string> environmentTexts;
    for (const auto& [name, value] : variables) {
        environmentTexts.push_back(name + "=" + value);
    }
    std::vector<char*> envp;
    for (std::string& text : environmentTexts) {
        envp.push_back(text.data());
    }
    envp.push_back(nullptr);
    const std::string folder = options.workingFolder.string();

    // Two pipes: one for what the program prints, and one that tells us if
    // it could not be started at all (it closes by itself when exec works).
    int output[2] = {-1, -1};
    int failure[2] = {-1, -1};
    if (pipe(output) != 0 || pipe(failure) != 0) {
        result.error = "The system could not make a pipe to read the program's output.";
        return result;
    }
    fcntl(failure[1], F_SETFD, FD_CLOEXEC);

    const pid_t child = fork();
    if (child < 0) {
        result.error = "The system could not start another program (fork failed).";
        for (const int fd : {output[0], output[1], failure[0], failure[1]}) {
            close(fd);
        }
        return result;
    }
    if (child == 0) {
        // In the new program-to-be. Its own group, so that it and whatever
        // it starts can be stopped together.
        setpgid(0, 0);
        const int nothing = open("/dev/null", O_RDONLY);
        if (nothing >= 0) {
            dup2(nothing, STDIN_FILENO);
        }
        dup2(output[1], STDOUT_FILENO);
        dup2(output[1], STDERR_FILENO);
        close(output[0]);
        close(output[1]);
        close(failure[0]);
        if (!folder.empty() && chdir(folder.c_str()) != 0) {
            const int code = errno;
            static_cast<void>(!write(failure[1], &code, sizeof(code)));
            _exit(127);
        }
        execve(program.c_str(), argv.data(), envp.data());
        const int code = errno;   // only reached if it could not be started
        static_cast<void>(!write(failure[1], &code, sizeof(code)));
        _exit(127);
    }
    close(output[1]);
    close(failure[1]);

    int startError = 0;
    ssize_t told = 0;
    do {
        told = read(failure[0], &startError, sizeof(startError));
    } while (told < 0 && errno == EINTR);
    close(failure[0]);
    if (told > 0) {
        int status = 0;
        waitpid(child, &status, 0);
        close(output[0]);
        result.error = program + " could not be started (" + std::strerror(startError) + ").";
        return result;
    }
    result.started = true;

    LineSplitter lines(onLine);
    char buffer[4096];
    for (;;) {
        if (isCancelled(cancel) && !result.cancelled) {
            result.cancelled = true;
            kill(-child, SIGTERM);   // the whole group
        }
        pollfd waiting{output[0], POLLIN, 0};
        const int ready = poll(&waiting, 1, 100);
        if (ready < 0 && errno != EINTR) {
            break;
        }
        if (ready <= 0) {
            continue;
        }
        const ssize_t got = read(output[0], buffer, sizeof(buffer));
        if (got > 0) {
            lines.add(buffer, static_cast<std::size_t>(got));
        } else if (got == 0 || errno != EINTR) {
            break;   // the program, and everything it started, closed its output
        }
    }
    lines.finish();
    close(output[0]);

    int status = 0;
    if (result.cancelled) {
        // Give it a moment to end by itself, then insist.
        for (int tries = 0; tries < 50 && waitpid(child, &status, WNOHANG) == 0; ++tries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (tries == 30) {
                kill(-child, SIGKILL);
            }
        }
        result.exitCode = 1;
        return result;
    }
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return result;
}

#endif

} // namespace rolo::editor
