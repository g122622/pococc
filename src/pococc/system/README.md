# 系统进程

## 目录结构

```text
system/
├── ProcessRunner.hpp   # 进程和环境门面
└── ProcessRunner.cpp   # POSIX spawn、exec、PATH 和信号转发
```

## 模块关系与外部依赖

application 与 compiler 使用本门面，依赖 Linux/POSIX 接口。真实编译器继承 stdin/stdout/stderr 和工作目录。

## 容易踩的坑

- 独立进程组接收 SIGINT、SIGTERM、SIGHUP；取消时要终止整个构建子进程组。
- 命令以 argv 传递，不通过 shell；带空格和特殊符号的路径保持一个参数。
- TODO: 当前不提供原生 Windows 进程执行后端。
