#include "pococc/cluster/HostList.hpp"
#include "pococc/compiler/arguments/InvocationAnalyzer.hpp"
#include "pococc/compiler/arguments/ResponseFile.hpp"
#include "pococc/core/Result.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
int _failures = 0;
void _expect(bool condition, const std::string& message)
{
    if (!condition) {
        ++_failures;
        std::cerr << message << '\n';
    }
}
void _classification()
{
    using pococc::compiler::InvocationAnalyzer;
    const auto plain = InvocationAnalyzer::analyze({"clang++",
        "-c",
        "hello.cpp",
        "-o",
        "hello.o",
        "-std=c++20",
        "-D",
        "VERSION=2",
        "-isystem",
        "includes",
        "-MD",
        "-MF",
        "hello.d",
        "-Xclang",
        "-fno-pch-timestamp"});
    _expect(plain.distributable && plain.source == "hello.cpp", "ordinary Clang compile must be distributable");
    _expect(InvocationAnalyzer::analyze({"gcc", "-c", "hello.c"}).distributable, "C compile");
    _expect(
        InvocationAnalyzer::analyze({"clang++-22", "-c", "hello.cc", "-flto=thin"}).distributable, "ThinLTO compile");
    const std::vector<std::vector<std::string>> local{{},
        {"clang++", "hello.o", "-o", "hello"},
        {"clang++", "-E", "hello.cpp"},
        {"clang++", "-c", "hello.cpp", "-Xclang", "-include-pch", "-Xclang", "common.pch"},
        {"clang++", "-c", "hello.cpp", "-fmodules"},
        {"clang++", "-c", "hello.cpp", "-march=native"},
        {"clang++", "-c", "hello.cpp", "-gsplit-dwarf"},
        {"clang++", "-c", "hello.cpp", "-ftime-trace"},
        {"clang++", "-c", "hello.cpp", "-fplugin=plugin.so"},
        {"clang++", "-c", "hello.cpp", "--coverage"},
        {"clang++", "-c", "hello.cpp", "-Xclang", "-load"},
        {"clang++", "-c", "hello.cpp", "-include", "forced.hpp"},
        {"clang++", "-c", "one.cpp", "two.cpp"},
        {"clang++", "-c", "-"},
        {"clang++", "-c", "hello.cpp", "-o"},
        {"clang++", "-c", "hello.cpp", "-o", "-"},
        {"clang++", "-c", "hello.cpp", "-x", "c++-header"},
        {"clang++", "-c", "hello.cpp", "-MF"},
        {"custom-driver", "-c", "hello.cpp"},
        {"clang++", "-c", "hello.cpp", "@unexpanded"},
        {"clang++", "-c", "source.S"},
        {"clang++", "-c", "hello.cpp", "-fpass-plugin=plugin.so"},
        {"clang++", "-c", "hello.cpp", "-M"},
        {"clang++", "-c", "hello.cpp", "-o-"},
        {"clang-cl", "-c", "hello.cpp"},
        {"clang++", "-c", "hello.cpp", "-Wp,-include,header.hpp"}};
    for (std::size_t i = 0; i < local.size(); ++i) {
        _expect(!InvocationAnalyzer::analyze(local[i]).distributable, "unsafe invocation " + std::to_string(i));
    }
}
void _responses()
{
    using pococc::compiler::ResponseFile;
    auto parsed = ResponseFile::tokenize("-D'NAME=hello world' \"source file.cpp\" '' escaped\\ space");
    _expect(parsed.success(), "GNU response quoting");
    if (parsed.success())
        _expect(
            parsed.value() == std::vector<std::string>{"-DNAME=hello world", "source file.cpp", "", "escaped space"},
            "response argument boundaries");
    _expect(!ResponseFile::tokenize("'missing").success(), "unclosed quote");
    _expect(!ResponseFile::tokenize("trailing\\").success(), "trailing escape");
    _expect(!ResponseFile::tokenize(std::string("a\0b", 3)).success(), "NUL byte");
    const auto root = std::filesystem::path("/tmp") / ("pococc-unit-" + std::to_string(::getpid()));
    std::filesystem::create_directory(root);
    const auto inner = root / "inner.rsp";
    const auto outer = root / "outer.rsp";
    {
        std::ofstream stream(inner);
        stream << "-c 'source file.cpp'";
    }
    {
        std::ofstream stream(outer);
        stream << "@" << inner.string() << " -o out.o";
    }
    auto expanded = ResponseFile::expand({"clang++", "@" + outer.string()});
    _expect(expanded.success() &&
            expanded.value() == std::vector<std::string>{"clang++", "-c", "source file.cpp", "-o", "out.o"},
        "nested response expansion");
    {
        std::ofstream stream(inner);
        stream << "@" << outer.string();
    }
    _expect(!ResponseFile::expand({"@" + outer.string()}).success(), "response cycle must stop");
    _expect(!ResponseFile::expand({"@" + (root / "missing").string()}).success(), "missing response file");
    std::filesystem::remove_all(root);
}
void _hosts()
{
    using pococc::cluster::HostList;
    auto hosts = HostList::parse("127.0.0.1:12345/2,lzo [::1]:12346/4");
    _expect(hosts.success(), "TCP host parsing");
    if (hosts.success()) {
        _expect(hosts.value().size() == 2 && hosts.value()[0].slots == 2 && hosts.value()[1].hostname == "::1",
            "host fields");
        _expect(HostList::serialize(hosts.value(), 2, 3).starts_with("--localslots=2 --localslots_cpp=3"),
            "local slot settings");
    }
    for (const auto& text : {"",
             "localhost",
             "user@host",
             "host/0",
             "host:65536",
             "host:abc",
             "host:1/2,cpp",
             "--randomize host",
             "[::1",
             "host:",
             "[]:1",
             "host/1/2",
             "host:1:2",
             "[::1]bad"}) {
        _expect(!HostList::parse(text).success(), std::string("invalid host: ") + text);
    }
    _expect(HostList::parse("worker.example/8").success(), "DNS host");
    _expect(!HostList::positiveInteger("-1", 256).success(), "negative slot count");
    _expect(!HostList::positiveInteger("2x", 256).success(), "partial numeric input");
    auto value = pococc::Result<int>::ok(7);
    _expect(value.success() && value.value() == 7, "successful result");
    auto error = pococc::Result<int>::fail("expected");
    _expect(!error.success() && error.error().message == "expected", "failed result");
}
} // namespace

int main()
{
    _classification();
    _responses();
    _hosts();
    return _failures == 0 ? 0 : 1;
}
