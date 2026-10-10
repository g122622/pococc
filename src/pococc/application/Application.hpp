#pragma once
#include "pococc/core/Result.hpp"
#include <string>
#include <vector>

namespace pococc::application {
/** @brief CLI 门面，编排构建、worker 和真实远程探测。 */
class Application {
public:
    /** @brief 根据程序名与子命令执行，编译器入口不解析构建参数。 */
    [[nodiscard]] static Result<int> execute(const std::vector<std::string>& arguments);
};
} // namespace pococc::application
