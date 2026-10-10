# 命令入口

## 目录结构

```text
application/
├── Application.hpp   # CLI 门面
└── Application.cpp   # worker、run、doctor、stats 与编译器入口
```

## 模块关系与外部依赖

主程序进入本门面，配置编译子系统并调用进程执行。worker 用前台 distccd 替换进程；doctor 对每个配置节点执行严格编译探测。

## 容易踩的坑

- worker 必须显式声明允许的客户端 IP/CIDR；listen 为数字地址。
- run 与 worker 要同时存活，节点清理由外部 CI 生命周期控制。
- doctor 验证实际对象生成，不仅检测端口开放；TODO: 增加完整工具链指纹交换。
