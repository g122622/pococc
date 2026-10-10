# 核心子系统

## 目录结构

```text
pococc/
├── application/   # CLI 与构建生命周期门面
├── compiler/      # 编译路由门面及参数能力分析
├── cluster/       # 显式节点地址与槽位配置
├── system/        # 程序执行与信号转发
├── telemetry/     # 并行编译记录与汇总
└── core/          # 结果类型与英文日志
```

## 模块关系与外部依赖

application 调用 compiler、cluster、system 和 telemetry；compiler 使用参数分析、节点配置、进程执行和统计。core 无业务依赖。distcc 作为外部程序调用，未修改上游源码。

## 容易踩的坑

- 同一源文件在不同构建目标中的命令不能合并；保持构建系统提供的每次调用。
- 编译器符号链接名称可能决定 C++ 驱动行为，解析路径不能自动替换成最终链接目标。
