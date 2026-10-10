#include "pococc/compiler/CompilationManager.hpp"
#include "pococc/cluster/HostList.hpp"
#include "pococc/compiler/arguments/InvocationAnalyzer.hpp"
#include "pococc/compiler/arguments/ResponseFile.hpp"
#include "pococc/core/Logger.hpp"
#include "pococc/system/ProcessRunner.hpp"
#include "pococc/telemetry/BuildJournal.hpp"
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>
#include <utility>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace {
class _LocalSlot {
public:
    explicit _LocalSlot(int descriptor)
        : m_descriptor(descriptor)
    {}
    ~_LocalSlot()
    {
        if (m_descriptor >= 0) ::close(m_descriptor);
    }
    _LocalSlot(const _LocalSlot&) = delete;
    _LocalSlot& operator=(const _LocalSlot&) = delete;
    [[nodiscard]] static pococc::Result<int> acquire(int slots)
    {
        const auto configured = pococc::system::ProcessRunner::environment("POCOCC_STATE_DIR");
        const auto directory =
            std::filesystem::path(configured.empty() ? "/tmp/pococc-local-" + std::to_string(::getuid()) : configured);
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) return pococc::Result<int>::fail("Cannot create local slot directory: " + error.message());
        while (true) {
            for (int slot = 0; slot < slots; ++slot) {
                const auto path = directory / ("local-" + std::to_string(slot) + ".lock");
                const int descriptor = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
                if (descriptor < 0) {
                    return pococc::Result<int>::fail("Cannot open local slot: " + std::string(std::strerror(errno)));
                }
                if (::flock(descriptor, LOCK_EX | LOCK_NB) == 0) return pococc::Result<int>::ok(descriptor);
                const int code = errno;
                ::close(descriptor);
                if (code != EWOULDBLOCK && code != EAGAIN) {
                    return pococc::Result<int>::fail("Cannot acquire local slot: " + std::string(std::strerror(code)));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

private:
    int m_descriptor;
};
} // namespace

namespace pococc::compiler {
Result<BuildConfiguration> CompilationManager::configurationFromEnvironment()
{
    using system::ProcessRunner;
    const auto backendText = ProcessRunner::environment("POCOCC_BACKEND");
    if (!backendText.empty() && backendText != "local" && backendText != "distcc") {
        return Result<BuildConfiguration>::fail("POCOCC_BACKEND must be local or distcc");
    }
    const auto strictText = ProcessRunner::environment("POCOCC_STRICT");
    if (!strictText.empty() && strictText != "0" && strictText != "1") {
        return Result<BuildConfiguration>::fail("POCOCC_STRICT must be 0 or 1");
    }
    std::string hosts = ProcessRunner::environment("POCOCC_HOSTS");
    const auto distcc = ProcessRunner::environment("POCOCC_DISTCC");
    int localJobs = 2;
    int preprocessingJobs = 2;
    const std::array<std::pair<std::string_view, int*>, 2> limits{
        {{"POCOCC_LOCAL_JOBS", &localJobs}, {"POCOCC_PREPROCESS_JOBS", &preprocessingJobs}}};
    for (const auto& [name, destination] : limits) {
        const auto text = ProcessRunner::environment(name);
        if (text.empty()) continue;
        auto parsed = cluster::HostList::positiveInteger(text, 256);
        if (!parsed.success()) return Result<BuildConfiguration>::fail(parsed.error().message);
        *destination = parsed.value();
    }
    return Result<BuildConfiguration>::ok({backendText == "local" ? Backend::Local : Backend::Distcc,
        std::move(hosts),
        distcc.empty() ? "distcc" : distcc,
        ProcessRunner::environment("POCOCC_JOURNAL"),
        strictText != "0",
        localJobs,
        preprocessingJobs});
}

Result<int> CompilationManager::exportConfiguration(const BuildConfiguration& configuration)
{
    const std::array<std::pair<std::string, std::string>, 7> values{{
        {"POCOCC_BACKEND", configuration.backend == Backend::Local ? "local" : "distcc"},
        {"POCOCC_HOSTS", configuration.hosts},
        {"POCOCC_DISTCC", configuration.distccExecutable},
        {"POCOCC_JOURNAL", configuration.journalPath},
        {"POCOCC_STRICT", configuration.strict ? "1" : "0"},
        {"POCOCC_LOCAL_JOBS", std::to_string(configuration.localJobs)},
        {"POCOCC_PREPROCESS_JOBS", std::to_string(configuration.preprocessingJobs)},
    }};
    for (const auto& [name, value] : values) {
        auto set = system::ProcessRunner::setEnvironment(name, value);
        if (!set.success()) return set;
    }
    return Result<int>::ok(0);
}

Result<int> CompilationManager::compile(
    const std::vector<std::string>& arguments, const BuildConfiguration& configuration)
{
    using system::ProcessRunner;
    if (arguments.empty()) return Result<int>::fail("Expected a compiler followed by its arguments");
    auto compiler = ProcessRunner::resolve(arguments.front());
    if (!compiler.success()) return Result<int>::fail(compiler.error().message);
    std::error_code identityError;
    if (std::filesystem::equivalent(compiler.value(), "/proc/self/exe", identityError)) {
        return Result<int>::fail("The compiler resolves to pococc itself");
    }
    const auto name = std::filesystem::path(compiler.value()).filename().string();
    if (name == "pococc" || name == "pococc-cc" || name == "distcc" || name == "ccache" || name == "icecc") {
        return Result<int>::fail("A real compiler is required; refusing recursive compiler wrappers");
    }
    auto expanded = ResponseFile::expand(arguments);
    if (!expanded.success()) return Result<int>::fail(expanded.error().message);
    auto invocation = InvocationAnalyzer::analyze(expanded.value());
    std::string route = "local";
    std::string reason = invocation.reason;
    // 本地执行保留原响应文件，让编译器处理自身支持的完整参数语义。
    std::vector<std::string> command = arguments;
    command.front() = compiler.value();
    int localDescriptor = -1;
    if (configuration.backend == Backend::Distcc && invocation.distributable && !configuration.hosts.empty()) {
        auto hosts = cluster::HostList::parse(configuration.hosts);
        if (!hosts.success()) return Result<int>::fail(hosts.error().message);
        auto backend = ProcessRunner::resolve(configuration.distccExecutable);
        if (!backend.success()) return Result<int>::fail(backend.error().message);
        identityError.clear();
        if (std::filesystem::equivalent(backend.value(), "/proc/self/exe", identityError)) {
            return Result<int>::fail("The backend resolves to pococc itself");
        }
        const std::array<std::pair<std::string, std::string>, 3> settings{{
            {"DISTCC_HOSTS",
                cluster::HostList::serialize(hosts.value(), configuration.localJobs, configuration.preprocessingJobs)},
            {"DISTCC_FALLBACK", configuration.strict ? "0" : "1"},
            {"DISTCC_SKIP_LOCAL_RETRY", "1"},
        }};
        for (const auto& [key, value] : settings) {
            auto set = ProcessRunner::setEnvironment(key, value);
            if (!set.success()) return set;
        }
        command = expanded.value();
        command.front() = compiler.value();
        command.insert(command.begin(), backend.value());
        route = "distcc";
        Logger::info("Submitting " + invocation.source + " to distcc");
    } else {
        if (configuration.backend == Backend::Local)
            reason = "local-backend";
        else if (invocation.distributable)
            reason = "no-workers-configured";
        Logger::warning("Executing locally: " + reason);
        auto slot = _LocalSlot::acquire(configuration.localJobs);
        if (!slot.success()) return Result<int>::fail(slot.error().message);
        localDescriptor = slot.value();
    }
    const _LocalSlot localSlot(localDescriptor);
    const auto start = std::chrono::steady_clock::now();
    auto result = ProcessRunner::run(command);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    auto recorded = telemetry::BuildJournal::append(configuration.journalPath,
        {route, result.success() ? result.value() : 127, elapsed, invocation.source, reason, configuration.strict});
    if (!recorded.success()) Logger::warning(recorded.error().message);
    if (result.success() && result.value() != 0 && route == "distcc") {
        Logger::error("distcc returned exit code " + std::to_string(result.value()) +
            (configuration.strict ? "; local retry is disabled" : "; fallback policy was enabled"));
    }
    return result;
}
} // namespace pococc::compiler
