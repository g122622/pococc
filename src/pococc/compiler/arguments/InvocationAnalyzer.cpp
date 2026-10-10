#include "pococc/compiler/arguments/InvocationAnalyzer.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>

namespace {
bool _isCompiler(std::string_view name)
{
    if (name.starts_with("clang-cl")) return false;
    const std::array<std::string_view, 6> names{"clang", "clang++", "gcc", "g++", "cc", "c++"};
    return std::any_of(names.begin(), names.end(), [name](std::string_view compiler) {
        return name == compiler ||
            (name.starts_with(compiler) && name.size() > compiler.size() && name[compiler.size()] == '-');
    });
}
bool _isSource(const std::string& path)
{
    const auto extension = std::filesystem::path(path).extension().string();
    const std::array<std::string_view, 9> extensions{".c", ".cc", ".cpp", ".cxx", ".cp", ".c++", ".C", ".i", ".ii"};
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}
bool _takesOperand(std::string_view option)
{
    const std::array<std::string_view, 19> options{"-o",
        "-I",
        "-D",
        "-U",
        "-isystem",
        "-iquote",
        "-idirafter",
        "-include",
        "-imacros",
        "-MF",
        "-MT",
        "-MQ",
        "-target",
        "--target",
        "-x",
        "-isysroot",
        "--sysroot",
        "-resource-dir",
        "-Xclang"};
    return std::find(options.begin(), options.end(), option) != options.end();
}
bool _knownOption(std::string_view option)
{
    const std::array<std::string_view, 29> prefixes{"-O",
        "-g",
        "-std=",
        "-stdlib=",
        "-D",
        "-U",
        "-I",
        "-isystem",
        "-iquote",
        "-idirafter",
        "-MF",
        "-MT",
        "-MQ",
        "-o",
        "--target=",
        "--sysroot=",
        "-march=",
        "-mtune=",
        "-mcpu=",
        "-mavx",
        "-msse",
        "-mno-",
        "-mfpu=",
        "-mfloat-abi=",
        "-fsanitize=",
        "-fno-sanitize=",
        "-fno-sanitize-recover=",
        "-fsanitize-recover=",
        "-fdiagnostics-color="};
    if (std::any_of(prefixes.begin(), prefixes.end(), [option](auto prefix) { return option.starts_with(prefix); })) {
        return true;
    }
    if (option.starts_with("-W") && !option.starts_with("-Wp,") && !option.starts_with("-Wl,")) return true;
    const std::array<std::string_view, 43> options{"-c",
        "-MD",
        "-MMD",
        "-MP",
        "-MG",
        "-pipe",
        "-pthread",
        "-m32",
        "-m64",
        "-fPIC",
        "-fpic",
        "-fPIE",
        "-fpie",
        "-fexceptions",
        "-fno-exceptions",
        "-frtti",
        "-fno-rtti",
        "-fvisibility-inlines-hidden",
        "-ffast-math",
        "-fno-finite-math-only",
        "-fno-trapping-math",
        "-fno-stack-protector",
        "-fno-stack-check",
        "-fno-omit-frame-pointer",
        "-fno-common",
        "-fno-strict-aliasing",
        "-fno-math-errno",
        "-flto",
        "-flto=thin",
        "-fcolor-diagnostics",
        "-fno-color-diagnostics",
        "-fno-pch-timestamp",
        "-pedantic",
        "-pedantic-errors",
        "-w",
        "-nostdinc",
        "-nostdinc++",
        "-fno-pie",
        "-fno-pic",
        "-fno-PIE",
        "-fno-PIC",
        "-fomit-frame-pointer",
        "-fno-fast-math"};
    return std::find(options.begin(), options.end(), option) != options.end() || option.starts_with("-fvisibility=") ||
        option.starts_with("-ffile-prefix-map=") || option.starts_with("-fdebug-prefix-map=") ||
        option.starts_with("-fmacro-prefix-map=");
}
} // namespace

namespace pococc::compiler {
Invocation InvocationAnalyzer::analyze(const std::vector<std::string>& arguments)
{
    if (arguments.empty()) return {false, "empty-command", {}};
    if (!_isCompiler(std::filesystem::path(arguments.front()).filename().string())) {
        return {false, "unsupported-compiler-driver", {}};
    }
    bool compile = false;
    std::size_t inputs = 0;
    std::string source;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument(arguments[i]);
        if (argument == "-c") compile = true;
        if (argument == "-E" || argument == "-S" || argument == "-fsyntax-only" || argument == "--version" ||
            argument == "-M" || argument == "-MM" || argument.starts_with("-dump")) {
            return {false, "non-object-action", source};
        }
        // TODO: 分发 PCH/modules 需要完整输入闭包和远端路径映射，目前明确保留本地语义。
        if (argument.find("pch") != std::string_view::npos && argument != "-fno-pch-timestamp" &&
            argument != "-Winvalid-pch")
            return {false, "precompiled-header", source};
        if (argument.starts_with("-fmodule") || argument.starts_with("-fcxx-module") || argument == "-emit-module" ||
            argument == "-fprebuilt-module-path" || argument.starts_with("-fprebuilt-module-path=")) {
            return {false, "compiler-modules", source};
        }
        if (argument == "-march=native" || argument == "-mtune=native" || argument == "-mcpu=native") {
            return {false, "native-cpu-selection", source};
        }
        // TODO: 插件、分析和伴随产物需要显式传输声明，不能仅返回一个对象文件。
        if (argument.starts_with("-fplugin") || argument.starts_with("-fprofile") ||
            argument.starts_with("-fauto-profile") || argument == "--coverage" || argument == "-ftest-coverage" ||
            argument.starts_with("-save-temps") || argument.starts_with("-gsplit-dwarf") ||
            argument.starts_with("-ftime") || argument.starts_with("-fdump") ||
            argument.starts_with("-fsanitize-blacklist") || argument.starts_with("-fsanitize-ignorelist") ||
            argument.starts_with("-serialize-diagnostics") || argument.starts_with("-MJ") || argument == "--analyze" ||
            argument.starts_with("-Wa,") || argument == "-Xassembler" || argument == "-Xpreprocessor" ||
            argument == "-ivfsoverlay" || argument.starts_with("--config") || argument == "-frepo") {
            return {false, "additional-input-or-output", source};
        }
        if (argument == "-" || argument == "--" || argument.empty() || argument.starts_with('@')) {
            return {false, "unsupported-input-form", source};
        }
        if (argument == "-o-") return {false, "stdout-output", source};
        if (argument == "-Xclang") {
            if (i + 1 >= arguments.size()) return {false, "missing-option-operand", source};
            // 时间戳开关不要求远端读取额外文件，可以按原参数传递。
            if (arguments[i + 1] != "-fno-pch-timestamp") return {false, "unsupported-clang-frontend-option", source};
        }
        if (_takesOperand(argument)) {
            if (++i >= arguments.size()) return {false, "missing-option-operand", source};
            if (argument == "-x" && arguments[i] != "c" && arguments[i] != "c++") {
                return {false, "unsupported-language", source};
            }
            if (argument == "-o" && arguments[i] == "-") return {false, "stdout-output", source};
            if (argument == "-include" || argument == "-imacros") {
                // GCC 会隐式加载同路径 .gch；先保留所有强制头输入，避免漏掉 PCH。
                return {false, "forced-header-input", source};
            }
            continue;
        }
        // TODO: 扩大经过验证的参数能力表；未知选项保持本地执行，不猜测其输入输出语义。
        if (argument.starts_with('-') && !_knownOption(argument)) return {false, "unrecognized-option", source};
        if (!argument.starts_with('-')) {
            if (!_isSource(arguments[i])) return {false, "unknown-positional-input", source};
            ++inputs;
            source = arguments[i];
        }
    }
    if (!compile) return {false, "link-or-probe", source};
    if (inputs != 1) return {false, "requires-single-source", source};
    return {true, "plain-object-compile", std::move(source)};
}
} // namespace pococc::compiler
