#pragma once
#include "pococc/core/Result.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace pococc::compiler {
/** @brief 按 GCC/Clang 的 GNU 风格解析响应文件，不使用 shell 展开。 */
class ResponseFile {
public:
    /** @brief 解析引号、反斜杠、空参数，错误引号直接报错。 */
    [[nodiscard]] static Result<std::vector<std::string>> tokenize(std::string_view content);
    /** @brief 递归展开相对于当前工作目录的响应文件，限制深度与总大小。 */
    [[nodiscard]] static Result<std::vector<std::string>> expand(const std::vector<std::string>& arguments);
};
} // namespace pococc::compiler
