#include "pococc/cluster/HostList.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>

namespace pococc::cluster {
Result<int> HostList::positiveInteger(std::string_view value, int maximum)
{
    int number = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || number <= 0 || number > maximum) {
        return Result<int>::fail("Invalid positive integer: " + std::string(value));
    }
    return Result<int>::ok(number);
}

Result<std::vector<Host>> HostList::parse(std::string_view hosts)
{
    std::istringstream stream{std::string(hosts)};
    std::vector<Host> result;
    std::string token;
    while (stream >> token) {
        std::string specification = token;
        if (token.ends_with(",lzo")) token.resize(token.size() - 4);
        if (token.empty() || token.find(',') != std::string::npos || token.find('@') != std::string::npos ||
            token.starts_with('-')) {
            return Result<std::vector<Host>>::fail(
                "Only explicit TCP hosts with optional ,lzo are supported: " + specification);
        }
        int slots = 4;
        const auto slash = token.find('/');
        if (slash != std::string::npos) {
            auto parsed = positiveInteger(std::string_view(token).substr(slash + 1), 256);
            if (!parsed.success()) return Result<std::vector<Host>>::fail(parsed.error().message);
            slots = parsed.value();
            token.resize(slash);
        }
        std::string hostname;
        std::string portText;
        if (token.starts_with('[')) {
            const auto end = token.find(']');
            if (end == std::string::npos) return Result<std::vector<Host>>::fail("Unterminated IPv6 host: " + token);
            hostname = token.substr(1, end - 1);
            if (end + 1 < token.size()) {
                if (token[end + 1] != ':') return Result<std::vector<Host>>::fail("Invalid host suffix: " + token);
                portText = token.substr(end + 2);
                if (portText.empty()) return Result<std::vector<Host>>::fail("Missing host port: " + token);
            }
        } else {
            const auto colon = token.find(':');
            hostname = token.substr(0, colon);
            if (colon != std::string::npos) {
                portText = token.substr(colon + 1);
                if (portText.empty()) return Result<std::vector<Host>>::fail("Missing host port: " + token);
            }
        }
        if (hostname.empty() || hostname == "localhost" ||
            !std::all_of(hostname.begin(), hostname.end(), [](unsigned char character) {
                return std::isalnum(character) || character == '.' || character == '-' || character == ':' ||
                    character == '%' || character == '_';
            })) {
            return Result<std::vector<Host>>::fail(
                "Invalid TCP hostname (use 127.0.0.1 for a loopback worker): " + hostname);
        }
        int port = 3632;
        if (!portText.empty()) {
            auto parsed = positiveInteger(portText, 65535);
            if (!parsed.success()) return Result<std::vector<Host>>::fail(parsed.error().message);
            port = parsed.value();
        }
        result.push_back({std::move(specification), std::move(hostname), port, slots});
    }
    if (result.empty()) return Result<std::vector<Host>>::fail("The worker host list is empty");
    return Result<std::vector<Host>>::ok(std::move(result));
}

std::string HostList::serialize(const std::vector<Host>& hosts, int localJobs, int preprocessingJobs)
{
    std::string result =
        "--localslots=" + std::to_string(localJobs) + " --localslots_cpp=" + std::to_string(preprocessingJobs);
    for (const auto& host : hosts)
        result += " " + host.specification;
    return result;
}
} // namespace pococc::cluster
