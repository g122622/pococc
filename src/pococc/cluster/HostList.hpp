#pragma once
#include "pococc/core/Result.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace pococc::cluster {
struct Host {
    std::string specification;
    std::string hostname;
    int port;
    int slots;
};

/** @brief 管理显式 TCP 编译节点，不依赖广播或共享文件系统。 */
class HostList {
public:
    /** @brief 解析 hostname[:port][/slots][,lzo]，IPv6 地址需要方括号。 */
    [[nodiscard]] static Result<std::vector<Host>> parse(std::string_view hosts);
    /** @brief 生成带本地槽位限制的 distcc host 配置。 */
    [[nodiscard]] static std::string serialize(const std::vector<Host>& hosts, int localJobs, int preprocessingJobs);
    /** @brief 验证范围内的正整数，不接受尾随内容。 */
    [[nodiscard]] static Result<int> positiveInteger(std::string_view value, int maximum);
};
} // namespace pococc::cluster
