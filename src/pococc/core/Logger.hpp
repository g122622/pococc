#pragma once
#include <cstdio>
#include <string_view>

namespace pococc {
/** @brief 向标准错误输出带级别的日志，不污染编译器标准输出。 */
class Logger {
public:
    /** @brief 输出正常操作记录。 */
    static void info(std::string_view message) { _write("info", message); }
    /** @brief 输出本地路由、回退或受限能力记录。 */
    static void warning(std::string_view message) { _write("warn", message); }
    /** @brief 输出操作失败原因。 */
    static void error(std::string_view message) { _write("error", message); }

private:
    static void _write(const char* level, std::string_view message)
    {
        std::fprintf(stderr, "[pococc] %s: %.*s\n", level, static_cast<int>(message.size()), message.data());
    }
};
} // namespace pococc
