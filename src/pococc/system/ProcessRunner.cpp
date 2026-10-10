#include "pococc/system/ProcessRunner.hpp"
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {
volatile sig_atomic_t _childProcess = 0;
void _forwardSignal(int signal)
{
    const auto child = _childProcess;
    if (child > 0) ::kill(-child, signal);
}
std::vector<char*> _makeArguments(const std::vector<std::string>& arguments)
{
    std::vector<char*> result;
    result.reserve(arguments.size() + 1);
    for (const auto& argument : arguments)
        result.push_back(const_cast<char*>(argument.c_str()));
    result.push_back(nullptr);
    return result;
}
std::string _systemError(std::string_view operation, int code)
{
    return std::string(operation) + ": " + std::strerror(code);
}
} // namespace

namespace pococc::system {
Result<int> ProcessRunner::run(const std::vector<std::string>& arguments)
{
    if (arguments.empty() || arguments.front().empty()) return Result<int>::fail("Cannot execute an empty command");
    // 在子进程组创建完成前阻塞信号，避免取消信号落入创建窗口。
    const std::array signals{SIGINT, SIGTERM, SIGHUP};
    sigset_t blocked;
    sigset_t oldMask;
    ::sigemptyset(&blocked);
    for (int signal : signals)
        ::sigaddset(&blocked, signal);
    if (::sigprocmask(SIG_BLOCK, &blocked, &oldMask) != 0) {
        return Result<int>::fail(_systemError("sigprocmask", errno));
    }
    std::array<struct sigaction, 3> oldActions{};
    struct sigaction action{};
    action.sa_handler = _forwardSignal;
    ::sigemptyset(&action.sa_mask);
    for (std::size_t i = 0; i < signals.size(); ++i)
        ::sigaction(signals[i], &action, &oldActions[i]);
    posix_spawnattr_t attributes;
    int code = ::posix_spawnattr_init(&attributes);
    pid_t child = 0;
    if (code == 0) {
        code = ::posix_spawnattr_setflags(
            &attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
        if (code == 0) code = ::posix_spawnattr_setpgroup(&attributes, 0);
        if (code == 0) code = ::posix_spawnattr_setsigmask(&attributes, &oldMask);
        if (code == 0) code = ::posix_spawnattr_setsigdefault(&attributes, &blocked);
        auto argv = _makeArguments(arguments);
        if (code == 0) code = ::posix_spawnp(&child, argv.front(), nullptr, &attributes, argv.data(), environ);
        ::posix_spawnattr_destroy(&attributes);
    }
    _childProcess = child;
    ::sigprocmask(SIG_SETMASK, &oldMask, nullptr);
    int status = 0;
    int waitError = 0;
    if (code == 0) {
        while (::waitpid(child, &status, 0) < 0) {
            if (errno != EINTR) {
                waitError = errno;
                break;
            }
        }
    }
    ::sigprocmask(SIG_BLOCK, &blocked, nullptr);
    _childProcess = 0;
    for (std::size_t i = 0; i < signals.size(); ++i)
        ::sigaction(signals[i], &oldActions[i], nullptr);
    ::sigprocmask(SIG_SETMASK, &oldMask, nullptr);
    if (code != 0) return Result<int>::fail(_systemError("Cannot execute " + arguments.front(), code));
    if (waitError != 0) return Result<int>::fail(_systemError("waitpid", waitError));
    if (WIFEXITED(status)) return Result<int>::ok(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return Result<int>::ok(128 + WTERMSIG(status));
    return Result<int>::fail("Child process did not report an exit status");
}
Result<int> ProcessRunner::replace(const std::vector<std::string>& arguments)
{
    if (arguments.empty() || arguments.front().empty()) return Result<int>::fail("Cannot execute an empty command");
    auto argv = _makeArguments(arguments);
    ::execvp(argv.front(), argv.data());
    return Result<int>::fail(_systemError("Cannot execute " + arguments.front(), errno));
}
Result<std::string> ProcessRunner::resolve(std::string_view executable)
{
    if (executable.empty()) return Result<std::string>::fail("Compiler executable is empty");
    const auto check = [](const std::filesystem::path& path) -> Result<std::string> {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
        if (!error && ::access(absolute.c_str(), X_OK) == 0 && !std::filesystem::is_directory(absolute, error)) {
            return Result<std::string>::ok(absolute.string());
        }
        return Result<std::string>::fail("Executable is unavailable: " + path.string());
    };
    if (executable.find('/') != std::string_view::npos) return check(std::string(executable));
    const auto searchPath = environment("PATH");
    std::size_t begin = 0;
    while (begin <= searchPath.size()) {
        const auto end = searchPath.find(':', begin);
        const auto component = searchPath.substr(begin, end == std::string::npos ? end : end - begin);
        auto candidate = check(std::filesystem::path(component.empty() ? "." : component) / executable);
        if (candidate.success()) return candidate;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return Result<std::string>::fail("Executable not found in PATH: " + std::string(executable));
}
Result<int> ProcessRunner::setEnvironment(std::string_view name, std::string_view value)
{
    const std::string key(name);
    const std::string content(value);
    if (::setenv(key.c_str(), content.c_str(), 1) != 0) {
        return Result<int>::fail(_systemError("Cannot set " + key, errno));
    }
    return Result<int>::ok(0);
}
std::string ProcessRunner::environment(std::string_view name)
{
    const std::string key(name);
    const char* value = std::getenv(key.c_str());
    return value == nullptr ? std::string{} : std::string(value);
}
} // namespace pococc::system
