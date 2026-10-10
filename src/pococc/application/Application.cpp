#include "pococc/application/Application.hpp"
#include "pococc/cluster/HostList.hpp"
#include "pococc/compiler/CompilationManager.hpp"
#include "pococc/core/Logger.hpp"
#include "pococc/system/ProcessRunner.hpp"
#include "pococc/telemetry/BuildJournal.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <arpa/inet.h>
#include <unistd.h>

namespace {
using pococc::Result;
using pococc::cluster::HostList;
using pococc::compiler::Backend;
using pococc::compiler::BuildConfiguration;
using pococc::compiler::CompilationManager;
using pococc::system::ProcessRunner;

struct _BuildOptions {
    BuildConfiguration configuration;
    std::vector<std::string> command;
    std::string compiler;
};

Result<_BuildOptions> _parseBuildOptions(const std::vector<std::string>& arguments)
{
    auto configuration = CompilationManager::configurationFromEnvironment();
    if (!configuration.success()) return Result<_BuildOptions>::fail(configuration.error().message);
    _BuildOptions result{std::move(configuration.value()), {}, "clang++"};
    for (std::size_t i = 2; i < arguments.size(); ++i) {
        const auto& option = arguments[i];
        if (option == "--") {
            result.command.assign(arguments.begin() + static_cast<std::ptrdiff_t>(i + 1), arguments.end());
            break;
        }
        if (option == "--strict") {
            result.configuration.strict = true;
            continue;
        }
        if (option == "--fallback") {
            result.configuration.strict = false;
            continue;
        }
        if (i + 1 >= arguments.size()) return Result<_BuildOptions>::fail("Missing value for " + option);
        const auto& value = arguments[++i];
        if (option == "--hosts")
            result.configuration.hosts = value;
        else if (option == "--journal")
            result.configuration.journalPath = value;
        else if (option == "--distcc")
            result.configuration.distccExecutable = value;
        else if (option == "--compiler")
            result.compiler = value;
        else if (option == "--backend") {
            if (value != "local" && value != "distcc") {
                return Result<_BuildOptions>::fail("Backend must be local or distcc");
            }
            result.configuration.backend = value == "local" ? Backend::Local : Backend::Distcc;
        } else if (option == "--local-jobs" || option == "--preprocess-jobs") {
            auto number = HostList::positiveInteger(value, 256);
            if (!number.success()) return Result<_BuildOptions>::fail(number.error().message);
            if (option == "--local-jobs")
                result.configuration.localJobs = number.value();
            else
                result.configuration.preprocessingJobs = number.value();
        } else {
            return Result<_BuildOptions>::fail("Unknown option: " + option);
        }
    }
    if (result.configuration.backend == Backend::Distcc) {
        auto hosts = HostList::parse(result.configuration.hosts);
        if (!hosts.success()) return Result<_BuildOptions>::fail(hosts.error().message);
        auto backend = ProcessRunner::resolve(result.configuration.distccExecutable);
        if (!backend.success()) return Result<_BuildOptions>::fail(backend.error().message);
    }
    if (!result.configuration.journalPath.empty()) {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(result.configuration.journalPath, error);
        if (error) return Result<_BuildOptions>::fail("Cannot resolve journal path: " + error.message());
        result.configuration.journalPath = absolute.lexically_normal().string();
    }
    return Result<_BuildOptions>::ok(std::move(result));
}

bool _validAddress(std::string_view address)
{
    std::array<unsigned char, 16> bytes{};
    const std::string text(address);
    return ::inet_pton(AF_INET, text.c_str(), bytes.data()) == 1 ||
        ::inet_pton(AF_INET6, text.c_str(), bytes.data()) == 1;
}

bool _validNetwork(std::string_view network)
{
    const auto slash = network.find('/');
    if (!_validAddress(network.substr(0, slash))) return false;
    if (slash == std::string_view::npos) return true;
    int prefix = 0;
    const auto text = network.substr(slash + 1);
    if (text == "0") return true;
    auto parsed =
        HostList::positiveInteger(text, network.substr(0, slash).find(':') == std::string_view::npos ? 32 : 128);
    if (!parsed.success()) return false;
    prefix = parsed.value();
    return prefix > 0;
}

Result<int> _worker(const std::vector<std::string>& arguments)
{
    std::string listen = "127.0.0.1";
    int port = 3632;
    int jobs = 4;
    std::string compiler = "clang++";
    std::string daemon = "distccd";
    std::vector<std::string> allowed;
    for (std::size_t i = 2; i < arguments.size(); ++i) {
        const auto& option = arguments[i];
        if (++i >= arguments.size()) return Result<int>::fail("Missing value for " + option);
        const auto& value = arguments[i];
        if (option == "--listen")
            listen = value;
        else if (option == "--compiler")
            compiler = value;
        else if (option == "--distccd")
            daemon = value;
        else if (option == "--allow") {
            if (!_validNetwork(value)) return Result<int>::fail("Invalid allowed IP network: " + value);
            allowed.push_back(value);
        } else if (option == "--port" || option == "--jobs") {
            auto parsed = HostList::positiveInteger(value, option == "--port" ? 65535 : 256);
            if (!parsed.success()) return Result<int>::fail(parsed.error().message);
            if (option == "--port")
                port = parsed.value();
            else
                jobs = parsed.value();
        } else {
            return Result<int>::fail("Unknown worker option: " + option);
        }
    }
    if (!_validAddress(listen)) return Result<int>::fail("--listen must be a numeric IP address");
    if (allowed.empty()) return Result<int>::fail("Worker requires at least one explicit --allow IP or CIDR");
    auto resolvedCompiler = ProcessRunner::resolve(compiler);
    if (!resolvedCompiler.success()) return Result<int>::fail(resolvedCompiler.error().message);
    auto resolvedDaemon = ProcessRunner::resolve(daemon);
    if (!resolvedDaemon.success()) return Result<int>::fail(resolvedDaemon.error().message);
    const auto directory = std::filesystem::path(resolvedCompiler.value()).parent_path().string();
    auto environment = ProcessRunner::setEnvironment("PATH", directory + ":" + ProcessRunner::environment("PATH"));
    if (!environment.success()) return environment;
    std::vector<std::string> command{resolvedDaemon.value(),
        "--daemon",
        "--no-detach",
        "--listen",
        listen,
        "--port",
        std::to_string(port),
        "--jobs",
        std::to_string(jobs),
        "--log-stderr",
        "--verbose"};
    for (const auto& network : allowed) {
        command.push_back("--allow");
        command.push_back(network);
    }
    // TODO: 节点注册、工具链指纹和自动退出由后续环境适配器负责；当前显式配置并前台运行。
    pococc::Logger::info(
        "Starting worker on " + listen + ":" + std::to_string(port) + " with compiler " + resolvedCompiler.value());
    return ProcessRunner::replace(command);
}

class _ProbeDirectory {
public:
    explicit _ProbeDirectory(std::filesystem::path path)
        : m_path(std::move(path))
    {}
    ~_ProbeDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
        if (error) pococc::Logger::warning("Cannot remove probe directory: " + error.message());
    }
    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

Result<int> _doctor(const std::vector<std::string>& arguments)
{
    auto options = _parseBuildOptions(arguments);
    if (!options.success()) return Result<int>::fail(options.error().message);
    if (!options.value().command.empty()) return Result<int>::fail("doctor does not accept a build command");
    auto configuration = options.value().configuration;
    configuration.backend = Backend::Distcc;
    configuration.strict = true;
    auto hosts = HostList::parse(configuration.hosts);
    if (!hosts.success()) return Result<int>::fail(hosts.error().message);
    auto compiler = ProcessRunner::resolve(options.value().compiler);
    if (!compiler.success()) return Result<int>::fail(compiler.error().message);
    const std::string pattern = "/tmp/pococc-probe-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    char* directory = ::mkdtemp(buffer.data());
    if (directory == nullptr) return Result<int>::fail("Cannot create temporary probe directory");
    const _ProbeDirectory temporary{std::filesystem::path(directory)};
    const auto source = temporary.path() / "probe.cpp";
    {
        std::ofstream stream(source);
        stream << "extern \"C\" int pococc_probe() { return 42; }\n";
        if (!stream) return Result<int>::fail("Cannot write probe source");
    }
    std::size_t index = 0;
    for (const auto& host : hosts.value()) {
        configuration.hosts = host.specification;
        const auto object = temporary.path() / ("probe-" + std::to_string(index++) + ".o");
        auto result = CompilationManager::compile(
            {compiler.value(), "-c", source.string(), "-o", object.string()}, configuration);
        if (!result.success()) return result;
        if (result.value() != 0) return result;
        std::error_code error;
        if (!std::filesystem::is_regular_file(object, error) || std::filesystem::file_size(object, error) == 0 ||
            error) {
            return Result<int>::fail("Worker returned no usable object: " + host.specification);
        }
        std::cout << "worker=" << host.specification << " probe=passed slots=" << host.slots << '\n';
    }
    std::cout << "All workers completed strict compile probes.\n";
    return Result<int>::ok(0);
}

void _help()
{
    std::cout << "pococc 0.1.0 - Linux distributed compiler prototype\n"
                 "  pococc worker --allow IP[/CIDR] [--listen IP] [--port N] [--jobs N] [--compiler PATH]\n"
                 "  pococc doctor --hosts 'HOST:PORT/SLOTS,lzo ...' [--compiler PATH]\n"
                 "  pococc run --hosts 'HOST:PORT/SLOTS,lzo ...' [--journal FILE] [--fallback] -- BUILD COMMAND\n"
                 "  pococc run --backend local [--journal FILE] -- BUILD COMMAND\n"
                 "  pococc compile -- COMPILER ARGUMENTS...\n"
                 "  pococc stats FILE\n"
                 "  pococc-cc COMPILER ARGUMENTS...\n"
                 "Configuration: POCOCC_HOSTS, POCOCC_BACKEND, POCOCC_DISTCC, POCOCC_STRICT,\n"
                 "POCOCC_JOURNAL, POCOCC_LOCAL_JOBS, POCOCC_PREPROCESS_JOBS.\n"
                 "Strict mode is enabled unless explicitly disabled. PCH/modules compile locally.\n";
}
} // namespace

namespace pococc::application {
Result<int> Application::execute(const std::vector<std::string>& arguments)
{
    if (arguments.empty()) return Result<int>::fail("Missing executable name");
    if (std::filesystem::path(arguments.front()).filename() == "pococc-cc") {
        auto configuration = CompilationManager::configurationFromEnvironment();
        if (!configuration.success()) return Result<int>::fail(configuration.error().message);
        return CompilationManager::compile(
            std::vector<std::string>(arguments.begin() + 1, arguments.end()), configuration.value());
    }
    if (arguments.size() == 1 || arguments[1] == "--help" || arguments[1] == "help") {
        _help();
        return Result<int>::ok(0);
    }
    if (arguments[1] == "--version") {
        std::cout << "pococc 0.1.0\n";
        return Result<int>::ok(0);
    }
    if (arguments[1] == "worker") return _worker(arguments);
    if (arguments[1] == "doctor") return _doctor(arguments);
    if (arguments[1] == "stats") {
        if (arguments.size() != 3) return Result<int>::fail("Expected exactly one journal path");
        return telemetry::BuildJournal::summarize(arguments[2]);
    }
    if (arguments[1] == "compile") {
        if (arguments.size() < 4 || arguments[2] != "--") return Result<int>::fail("Expected compile -- COMPILER ARGS");
        auto configuration = CompilationManager::configurationFromEnvironment();
        if (!configuration.success()) return Result<int>::fail(configuration.error().message);
        return CompilationManager::compile(
            std::vector<std::string>(arguments.begin() + 3, arguments.end()), configuration.value());
    }
    if (arguments[1] == "run") {
        auto options = _parseBuildOptions(arguments);
        if (!options.success()) return Result<int>::fail(options.error().message);
        if (options.value().command.empty()) return Result<int>::fail("Expected -- followed by a build command");
        auto exported = CompilationManager::exportConfiguration(options.value().configuration);
        if (!exported.success()) return exported;
        // TODO: 自动发现和伸缩尚未实现；编译节点地址由外部环境显式提供。
        Logger::info("Running build with " + std::string(options.value().configuration.strict ? "strict" : "fallback") +
            " backend policy");
        return ProcessRunner::run(options.value().command);
    }
    return Result<int>::fail("Unknown command: " + arguments[1]);
}
} // namespace pococc::application
