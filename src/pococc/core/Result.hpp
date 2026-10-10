#pragma once
#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace pococc {
struct Error {
    std::string message;
};

/** @brief 使用显式成功或错误结果传递边界操作状态。 */
template <typename T>
class Result {
public:
    /** @brief 构造成功结果。 */
    [[nodiscard]] static Result ok(T value) { return Result(std::move(value)); }
    /** @brief 构造包含英文消息的失败结果。 */
    [[nodiscard]] static Result fail(std::string message) { return Result(Error{std::move(message)}); }
    /** @brief 判断结果是否成功。 */
    [[nodiscard]] bool success() const { return std::holds_alternative<T>(m_value); }
    /** @brief 获取成功值，调用前必须确认 success。 */
    [[nodiscard]] T& value()
    {
        assert(success());
        return std::get<T>(m_value);
    }
    /** @brief 获取成功值，调用前必须确认 success。 */
    [[nodiscard]] const T& value() const
    {
        assert(success());
        return std::get<T>(m_value);
    }
    /** @brief 获取错误，调用前必须确认失败。 */
    [[nodiscard]] const Error& error() const
    {
        assert(!success());
        return std::get<Error>(m_value);
    }

private:
    explicit Result(T value)
        : m_value(std::move(value))
    {}
    explicit Result(Error error)
        : m_value(std::move(error))
    {}
    std::variant<T, Error> m_value;
};
} // namespace pococc
