# 自举环境适配器

## 目录结构

    ci/
    └── Bootstrap.py   # GitHub artifact 会合、SSH 隧道与远程编译证据核验

## 模块关系与外部依赖

bootstrap workflow 先用普通 Clang 构建 seed，再由本适配器连接两个独立 worker runner。核心编译入口仍为 seed/pococc-cc。依赖 Python 标准库、OpenSSH、cloudflared 与 GitHub Actions artifact API。

## 容易踩的坑

- coordinator 与 worker 都依赖 seed，不能互相通过 needs 串行等待。
- 私钥仅保存在 coordinator 临时目录，artifact 只传公钥和 worker SSH 主机公钥；隧道端口需通过 SSH 认证。
- 客户端和 worker 工具链版本完全相同，distccd 仅监听环回地址。
- distccd 命令清单只允许配置的编译器及同目录的 C 驱动，支持 LLVM 专用目录下的绝对路径。
- 同时校验严格 journal 与 worker 的 COMPILE_OK 记录，避免把提交数误判为实际远端编译。
- 临时 Quick Tunnel 依赖外部服务，仅用于原型验证。TODO: 提供持久覆盖网络适配器。
- 外部 workflow 可配置 POCOCC_CI_BINARY_DIR、ARTIFACT_PREFIX、POLL_INTERVAL、HOLD_TIMEOUT、
  RENDEZVOUS_TIMEOUT 和 WORKER_SLOTS（均使用 POCOCC_CI_ 前缀），支持独立命名和长时间构建。
