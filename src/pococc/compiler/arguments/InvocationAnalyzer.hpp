#pragma once
#include <string>
#include <vector>

namespace pococc::compiler {
struct Invocation {
    bool distributable;
    std::string reason;
    std::string source;
};
/** @brief 仅分发普通单输入编译；依赖额外文件或输出的任务交回真实编译器。 */
class InvocationAnalyzer {
public:
    /** @brief 检查已经展开的参数，不修改原始编译语义。 */
    [[nodiscard]] static Invocation analyze(const std::vector<std::string>& arguments);
};
} // namespace pococc::compiler
