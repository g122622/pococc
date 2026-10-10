# 项目规范

- 使用 C++20；文件、类型用 PascalCase，成员用 m_ 前缀，私有方法和匿名命名空间符号用 _ 前缀。
- 采用命名空间隔离和单一子系统门面，禁止 using namespace std、裸 new/delete、原始数组和 C 风格转换。
- 函数参数和配置结构体不设置默认值；配置由入口显式构造。
- 新代码通过 Result 传播错误，不用异常控制流程。边界输入校验，内部前置条件使用断言。
- Doxygen 说明放在头文件；注释使用简体中文，运行日志和错误消息使用英文，只使用 info/warn/error 级别。
- 未实现或受限能力必须有 TODO。禁止把具体业务项目的路径、目标、宏和依赖写入核心。
- 各源码目录 README 保留目录树、模块关系、外部依赖和容易踩的坑；使用指南放在 docs/USAGE.md。
- 使用仓库 .clang-format；编译使用 -Wall -Wextra -Wpedantic -Werror。
- Linux 构建：cmake --preset linux-debug，然后 cmake --build --preset linux-debug；验证使用 ctest --preset linux-debug。
- 在 Windows 上通过 Ubuntu WSL 构建。不得启动 GitHub workflow；不得启动子代理。
- 修改保留用户工作区内容；不自动提交或推送。
