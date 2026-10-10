#pragma once
#include "pococc/core/Result.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace pococc::system {
/** @brief 不经过 shell 执行程序并保留编译器输入输出。 */
class ProcessRunner {
public:
    /** @brief 在独立进程组中执行并等待，转发终止信号，返回子进程退出码。 */
    [[nodiscard]] static Result<int> run(const std::vector<std::string>& arguments);
    /** @brief 用目标程序替换当前进程，适合前台 worker 生命周期。 */
    [[nodiscard]] static Result<int> replace(const std::vector<std::string>& arguments);
    /** @brief 解析 PATH 中的程序位置，保留 clang++ 等驱动符号链接名称。 */
    [[nodiscard]] static Result<std::string> resolve(std::string_view executable);
    /** @brief 修改当前进程环境，检查系统错误。 */
    [[nodiscard]] static Result<int> setEnvironment(std::string_view name, std::string_view value);
    /** @brief 读取环境变量，未设置时返回空字符串。 */
    [[nodiscard]] static std::string environment(std::string_view name);
};
} // namespace pococc::system
