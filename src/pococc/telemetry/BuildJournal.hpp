#pragma once
#include "pococc/core/Result.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace pococc::telemetry {
struct CompilationEvent {
    std::string route;
    int exitCode;
    std::int64_t elapsedMicroseconds;
    std::string source;
    std::string reason;
    bool strict;
};

/** @brief 使用进程间锁记录每次编译路由，避免并行 Ninja 作业交错写入。 */
class BuildJournal {
public:
    /** @brief 追加七列 TSV 记录；空路径表示显式关闭记录。 */
    [[nodiscard]] static Result<int> append(std::string_view path, const CompilationEvent& event);
    /** @brief 汇总提交次数、失败和累计任务时间，不将后端提交误报为远端完成。 */
    [[nodiscard]] static Result<int> summarize(std::string_view path);
};
} // namespace pococc::telemetry
