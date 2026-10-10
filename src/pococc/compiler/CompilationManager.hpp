#pragma once
#include "pococc/core/Result.hpp"
#include <string>
#include <vector>

namespace pococc::compiler {
enum class Backend { Local, Distcc };
struct BuildConfiguration {
    Backend backend;
    std::string hosts;
    std::string distccExecutable;
    std::string journalPath;
    bool strict;
    int localJobs;
    int preprocessingJobs;
};

/** @brief 编译子系统唯一门面，封装参数分析、后端路由和状态记录。 */
class CompilationManager {
public:
    /** @brief 从环境显式构造配置；无节点的独立编译器调用保留本地执行。 */
    [[nodiscard]] static Result<BuildConfiguration> configurationFromEnvironment();
    /** @brief 导出供构建进程及其编译器子进程继承的配置。 */
    [[nodiscard]] static Result<int> exportConfiguration(const BuildConfiguration& configuration);
    /** @brief 分析并执行编译，返回真实退出码；不吞掉远程编译错误。 */
    [[nodiscard]] static Result<int> compile(
        const std::vector<std::string>& arguments, const BuildConfiguration& configuration);
};
} // namespace pococc::compiler
