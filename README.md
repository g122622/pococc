# pococc

项目无关的 C++20 分布式编译入口，使用 Linux 上的 distcc 后端。

## 目录结构

```text
pococc/
├── src/                    # 实现源码和头文件
│   ├── main.cpp            # 两个程序共享的入口
│   ├── pococc/             # 编译、系统进程、集群配置与统计子系统
│   ├── ci/                 # GitHub Actions 自举与临时隧道适配器
│   └── examples/hello/     # 可独立构建的 CMake 示例源码
├── tests/                  # 参数契约与真实双 worker 集成验证
├── docs/USAGE.md            # 构建、worker、launcher 与 ccache 使用指南
├── CMakeLists.txt           # C++20 构建、安装与 CTest
└── CMakePresets.json        # Linux Clang/Ninja 预设
```

## 模块关系

Ninja/Make 继续管理依赖和最终链接，pococc-cc 接收真实编译器及参数。编译门面展开响应文件、判断能力、执行本地编译器或 distcc，并写入可选统计记录。pococc 提供构建环境传递、前台 worker、真实编译探测和统计入口。

## 外部依赖

运行依赖 Linux、编译器和 distcc；构建依赖 CMake、Ninja 与 C++20 编译器。集成验证使用 Python 3、ccache 和真实 distccd。网络地址由调用者提供，工具不包含业务项目路径或构建目标。

## 容易踩的坑

- Windows 使用 WSL，产生 Linux 可执行文件；具体命令见 docs/USAGE.md。
- 节点必须互通并安装兼容工具链。严格模式默认开启；正常本地任务仍允许执行，远端失败不会被隐藏重试。
- PCH、modules、native CPU、插件及未验证参数保持本地执行。TODO: 扩大经过验证的远程输入能力，增加 Icecream 后端。
- 日志中的 distcc_submissions 是提交数；需要严格模式、doctor 和 worker 日志确认实际远端执行。
- GitHub Actions 自举通过临时 SSH 隧道连接两个独立 worker，无需预配置 Secret；源码中的 CI 适配器与核心分离。
- TODO: 通用动态集群注册、持久覆盖网络和自动伸缩尚未实现。
