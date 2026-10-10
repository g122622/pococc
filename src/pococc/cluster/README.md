# 节点配置

## 目录结构

```text
cluster/
├── HostList.hpp   # TCP 节点配置门面
└── HostList.cpp   # 主机、端口、编译槽位验证
```

## 模块关系与外部依赖

application 和 compiler 使用本门面，依赖标准库与 Result。输出普通 distcc 主机列表。

## 容易踩的坑

- localhost 在 distcc 中表示本地编译，不是 TCP worker；环回测试使用 127.0.0.1。
- 地址只支持 hostname[:port][/slots][,lzo]；IPv6 地址必须用方括号。
- 不接受 pump、SSH 或自动发现参数；TODO: 通过环境适配器实现节点发现。
