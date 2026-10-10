# 编译门面

## 目录结构

```text
compiler/
├── CompilationManager.hpp   # 配置与编译路由门面
├── CompilationManager.cpp   # 后端执行、本地槽位锁与统计
└── arguments/               # GNU 响应文件和编译能力分析
```

## 模块关系与外部依赖

application 调用门面；门面使用 arguments、cluster、system 和 telemetry。远程后端为外部 distcc，普通模式在客户端预处理。

## 容易踩的坑

- 不删除 PCH 参数；不支持的调用交给原编译器并保持原响应文件。
- 默认禁用 distcc 通信失败回退和远程编译错误本地重试；fallback 模式显式开启。
- 本地编译使用进程间 flock 槽位；默认目录按用户共享，可用 POCOCC_STATE_DIR 隔离。
- journal 写入失败会告警，编译退出码保持原值。
