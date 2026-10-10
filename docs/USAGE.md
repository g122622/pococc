# 使用指南

原型运行于 Linux。Windows 开发环境可用 Ubuntu WSL，仓库路径对应 /mnt/e/dev/pococc。

## 构建与验证

```bash
sudo apt-get install -y clang cmake ninja-build distcc ccache python3
cd /mnt/e/dev/pococc
cmake --preset linux-debug
cmake --build --preset linux-debug -j4
ctest --preset linux-debug
```

产物为 build/pococc 和 build/pococc-cc。测试会自动启动两个独立 TCP distccd worker，通过真实编译确认分发，并在结束时清理进程。C++ 实现使用标准库和 POSIX 接口，无运行时 Python 依赖；Python 只用于集成验证。

安装可用 cmake --install build --prefix /目标目录。

## 本机双节点演示

在同一台 Linux 主机的两个终端分别启动 worker。每个命令前台运行，结束演示后 Ctrl+C：

```bash
./build/pococc worker --listen 127.0.0.1 --allow 127.0.0.1 --port 43632 --jobs 2 --compiler clang++
```

```bash
./build/pococc worker --listen 127.0.0.1 --allow 127.0.0.1 --port 43633 --jobs 2 --compiler clang++
```

在第三个终端确认两个节点真实完成编译：

```bash
./build/pococc doctor --hosts '127.0.0.1:43632/2,lzo 127.0.0.1:43633/2,lzo' --compiler clang++
```

生成示例构建，再传递本次集群配置：

```bash
cmake -S src/examples/hello -B build-hello -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_COMPILER_LAUNCHER="$PWD/build/pococc-cc"

./build/pococc run \
  --hosts '127.0.0.1:43632/2,lzo 127.0.0.1:43633/2,lzo' \
  --journal "$PWD/build-hello/compile.tsv" \
  -- cmake --build build-hello -j4

./build-hello/hello
./build/pococc stats build-hello/compile.tsv
```

示例输出 42。这是同机多 worker 的协议和路由验证，不是多机器性能测试。

## 连接多个实际 runner

为每个 worker 安装兼容工具链，并通过可互通的 TCP 或覆盖网络连接。worker 上使用 --listen 指定本机网络地址，--allow 指定协调节点的地址或明确的网络范围。协调节点用 --hosts 提供这些地址。

worker 和协调构建必须同时运行；结束、失败或取消后由外部环境终止 worker。仓库的 bootstrap workflow 提供 GitHub Actions 双 worker 自举适配器；核心 CLI 仍使用显式节点，不自动启动 GitHub job。TODO: 增加通用集群管理和覆盖网络适配器。

普通 distcc 不需要 worker checkout 业务源码，但编译器名称、路径、版本与目标必须兼容。显式传入 --compiler 只检查 worker 程序并调整 PATH，尚不自动交换客户端与 worker 的版本指纹。

## 任意 CMake 项目接入

将 CMAKE_C_COMPILER_LAUNCHER 和 CMAKE_CXX_COMPILER_LAUNCHER 指向 pococc-cc。已有工程如果在 CMake 中强制覆盖 launcher，可采用下述 ccache prefix。

pococc run 设置后代进程继承的环境，不会自动改写目标项目配置。构建工具并行度需要根据 worker 总槽位设置，本地编译与预处理各有独立限制。

直接调用也支持：

```bash
POCOCC_HOSTS='10.0.0.2:3632/4,lzo' \
  ./build/pococc-cc clang++ -c hello.cpp -o hello.o

POCOCC_HOSTS='10.0.0.2:3632/4,lzo' \
  ./build/pococc compile -- clang++ -c hello.cpp -o hello.o
```

## ccache 接入

ccache 位于 pococc 外层，命中时直接返回缓存对象：

```bash
export CCACHE_PREFIX="$PWD/build/pococc-cc"
export POCOCC_HOSTS='10.0.0.2:3632/4,lzo'
ccache clang++ -c hello.cpp -o hello.o
```

项目配置 compiler launcher 为 ccache 时，同样设置 CCACHE_PREFIX。不要再把 pococc-cc 同时设置为该目标的额外 launcher，避免重复包装。

## 参数与环境

| 配置 | 命令行 | 环境变量 | 未设置时 |
| --- | --- | --- | --- |
| 后端 | run --backend | POCOCC_BACKEND | distcc；独立调用无节点时本地执行 |
| 节点列表 | run/doctor --hosts | POCOCC_HOSTS | run/doctor 要求显式列表 |
| distcc 程序 | --distcc | POCOCC_DISTCC | distcc |
| 严格模式 | --strict / --fallback | POCOCC_STRICT | 1 |
| 日志路径 | --journal | POCOCC_JOURNAL | 不记录 |
| 本地编译槽位 | --local-jobs | POCOCC_LOCAL_JOBS | 2 |
| 本地预处理槽位 | --preprocess-jobs | POCOCC_PREPROCESS_JOBS | 2 |
| 本地锁目录 | 无 | POCOCC_STATE_DIR | /tmp/pococc-local-用户UID |

节点格式为 hostname[:port][/slots][,lzo]，默认端口 3632、槽位 4。IPv6 使用 [地址]:端口/槽位。当前不接受 distcc 的 localhost 特殊本地项、SSH、pump 或 zeroconf。

strict 关闭 distcc 通信错误回退，并关闭远程编译错误的本地重试。--fallback 仅允许 distcc 自己的通信失败回退。PCH、modules、native CPU、未知选项等本地路由与严格模式无冲突，日志会给出原因。

## 行为与限制

- PCH 创建和使用保留本地；支持 GNU 风格嵌套响应文件，参数不经过 shell。
- 普通单源 .c/.cpp/.cc 编译可分发；链接、生成代码和最终 ThinLTO 由原构建系统负责。
- 不支持的调用保留原始响应文件执行，普通远程调用显式展开响应文件。
- 输入诊断和退出码传播给构建系统，SIGINT/SIGTERM/SIGHUP 转发到构建子进程组。
- 本地编译有跨进程槽位锁。不同构建共用默认目录时共享该限制，可用 POCOCC_STATE_DIR 隔离。
- stats 汇总 local 和 distcc_submissions、失败与原因；累计任务时间不是墙钟时间。
- journal 采用 flock 防止并行记录交错。journal 失败会告警，不覆盖编译器退出码。
- TODO: 远程 PCH、modules、输入缓存、动态调度、Icecream 后端与通用集群管理尚未实现。

## GitHub Actions 分布式自举

Distributed Bootstrap workflow 由 push、PR 或手动触发。seed job 用普通 Clang 18 构建 stage 0；随后 coordinator 与两个 worker job 同时启动。

coordinator 在当前 run 生成 SSH 密钥，只上传公钥。worker 运行仅监听环回地址的 distccd，并通过临时 Quick Tunnel 提供 SSH 入口。coordinator 校验 worker 主机公钥、比较工具链版本，然后建立经过认证的端口转发。

stage 1 在独立 build-bootstrap 目录中构建，compiler launcher 指向 stage 0 的 pococc-cc。CI 强制全部编译单元经严格 distcc 执行，核对 journal 与两个 worker 的实际 COMPILE_OK 记录，并运行 stage 1 的完整测试。

产物包含自举统计、worker 报告和日志。该方案验证多 runner 自举，不承诺加速；临时隧道依赖外部服务。TODO: 替换为持久覆盖网络并扩大节点数量。
