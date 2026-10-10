#include "pococc/telemetry/BuildJournal.hpp"
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace {
std::string _escape(std::string_view text)
{
    std::string result;
    for (char character : text) {
        switch (character) {
            case '\\':
                result += "\\\\";
                break;
            case '\t':
                result += "\\t";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\n':
                result += "\\n";
                break;
            default:
                result.push_back(character);
                break;
        }
    }
    return result;
}
template <typename T>
bool _integer(std::string_view text, T& value)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
} // namespace

namespace pococc::telemetry {
Result<int> BuildJournal::append(std::string_view path, const CompilationEvent& event)
{
    if (path.empty()) return Result<int>::ok(0);
    const std::string filename(path);
    const int descriptor = ::open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (descriptor < 0) return Result<int>::fail("Cannot open journal: " + std::string(std::strerror(errno)));
    if (::flock(descriptor, LOCK_EX) != 0) {
        const std::string message = std::strerror(errno);
        ::close(descriptor);
        return Result<int>::fail("Cannot lock journal: " + message);
    }
    const std::string line = std::to_string(::getpid()) + "\t" + event.route + "\t" + std::to_string(event.exitCode) +
        "\t" + std::to_string(event.elapsedMicroseconds) + "\t" + _escape(event.source) + "\t" + _escape(event.reason) +
        "\t" + (event.strict ? "strict" : "fallback-allowed") + "\n";
    std::size_t offset = 0;
    int error = 0;
    while (offset < line.size()) {
        const auto written = ::write(descriptor, line.data() + offset, line.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            error = written == 0 ? EIO : errno;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    ::flock(descriptor, LOCK_UN);
    ::close(descriptor);
    if (error != 0) return Result<int>::fail("Cannot append journal: " + std::string(std::strerror(error)));
    return Result<int>::ok(0);
}

Result<int> BuildJournal::summarize(std::string_view path)
{
    std::ifstream stream{std::string(path)};
    if (!stream) return Result<int>::fail("Cannot read journal: " + std::string(path));
    std::size_t count = 0;
    std::size_t failures = 0;
    std::int64_t microseconds = 0;
    std::map<std::string, std::size_t> routes;
    std::map<std::string, std::size_t> reasons;
    std::string line;
    while (std::getline(stream, line)) {
        std::vector<std::string> fields;
        std::istringstream row(line);
        std::string field;
        while (std::getline(row, field, '\t'))
            fields.push_back(field);
        int exitCode = 0;
        std::int64_t duration = 0;
        if (fields.size() != 7 || !_integer(fields[2], exitCode) || !_integer(fields[3], duration) || duration < 0 ||
            (fields[1] != "local" && fields[1] != "distcc")) {
            return Result<int>::fail("Malformed journal record at line " + std::to_string(count + 1));
        }
        ++count;
        failures += exitCode != 0 ? 1 : 0;
        microseconds += duration;
        ++routes[fields[1]];
        ++reasons[fields[5]];
    }
    if (stream.bad()) return Result<int>::fail("Cannot read journal: " + std::string(path));
    std::cout << "invocations=" << count << "\nlocal=" << routes["local"] << "\ndistcc_submissions=" << routes["distcc"]
              << "\nfailures=" << failures << "\nsummed_task_seconds=" << static_cast<double>(microseconds) / 1000000.0
              << '\n';
    for (const auto& [reason, total] : reasons)
        std::cout << "reason." << reason << '=' << total << '\n';
    std::cout
        << "Remote execution is verified with strict mode and worker logs; submissions are not completion counts.\n";
    return Result<int>::ok(0);
}
} // namespace pococc::telemetry
