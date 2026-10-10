#include "pococc/compiler/arguments/ResponseFile.hpp"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
constexpr std::size_t _MAX_RESPONSE_BYTES = 8 * 1024 * 1024;
constexpr std::size_t _MAX_ARGUMENTS = 65536;
constexpr std::size_t _MAX_DEPTH = 16;
pococc::Result<int> _expand(
    const std::vector<std::string>& input, std::vector<std::string>& output, std::size_t depth, std::size_t& totalBytes)
{
    if (depth > _MAX_DEPTH) return pococc::Result<int>::fail("Response file recursion limit exceeded");
    for (const auto& argument : input) {
        if (argument.starts_with('@')) {
            const std::filesystem::path path(argument.substr(1));
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error) return pococc::Result<int>::fail("Cannot read response file: " + path.string());
            if (size > _MAX_RESPONSE_BYTES - totalBytes) {
                return pococc::Result<int>::fail("Response file size limit exceeded");
            }
            totalBytes += static_cast<std::size_t>(size);
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return pococc::Result<int>::fail("Cannot open response file: " + path.string());
            const std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            if (stream.bad()) return pococc::Result<int>::fail("Cannot read response file: " + path.string());
            auto tokens = pococc::compiler::ResponseFile::tokenize(content);
            if (!tokens.success()) return pococc::Result<int>::fail(tokens.error().message);
            auto result = _expand(tokens.value(), output, depth + 1, totalBytes);
            if (!result.success()) return result;
        } else {
            if (output.size() >= _MAX_ARGUMENTS) return pococc::Result<int>::fail("Compiler argument limit exceeded");
            output.push_back(argument);
        }
    }
    return pococc::Result<int>::ok(0);
}
} // namespace

namespace pococc::compiler {
Result<std::vector<std::string>> ResponseFile::tokenize(std::string_view content)
{
    std::vector<std::string> result;
    std::string token;
    char quote = '\0';
    bool escaped = false;
    bool active = false;
    for (char character : content) {
        if (character == '\0') return Result<std::vector<std::string>>::fail("Response file contains a NUL byte");
        if (escaped) {
            token.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
            active = true;
        } else if (quote != '\0') {
            if (character == quote)
                quote = '\0';
            else
                token.push_back(character);
        } else if (character == '\'' || character == '"') {
            quote = character;
            active = true;
        } else if (std::isspace(static_cast<unsigned char>(character))) {
            if (active) {
                result.push_back(std::move(token));
                token.clear();
                active = false;
            }
        } else {
            token.push_back(character);
            active = true;
        }
    }
    if (quote != '\0' || escaped) return Result<std::vector<std::string>>::fail("Unterminated response file quoting");
    if (active) result.push_back(std::move(token));
    return Result<std::vector<std::string>>::ok(std::move(result));
}
Result<std::vector<std::string>> ResponseFile::expand(const std::vector<std::string>& arguments)
{
    std::vector<std::string> output;
    std::size_t totalBytes = 0;
    auto result = _expand(arguments, output, 0, totalBytes);
    if (!result.success()) return Result<std::vector<std::string>>::fail(result.error().message);
    return Result<std::vector<std::string>>::ok(std::move(output));
}
} // namespace pococc::compiler
